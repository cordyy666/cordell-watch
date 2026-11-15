// Cordell Watch passive awareness engine.
// PASSIVE ONLY: periodic WiFi scans (beacons/probe responses the ESP32 already receives)
// and BLE advertisement scans. Classifies by SSID/name patterns, MAC OUI and BLE
// service/manufacturer IDs. These are FINGERPRINT MATCHES ("likely"), not proof.
#include "watch.h"
#include <WiFi.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "attack.h"

ApRec g_aps[MAX_APS]; int g_ap_n = 0; volatile uint32_t g_scan_gen = 0;
BleRec g_ble[MAX_BLE]; int g_ble_n = 0; volatile uint32_t g_ble_gen = 0;
bool g_watch_on = true;
uint32_t g_seen_total = 0, g_cams_seen = 0;
String g_log[LOG_N]; int g_log_head = 0; volatile uint32_t g_log_gen = 0;

enum { T_CAM, T_FLOCK, T_DRONE, T_HOTSPOT, T_PRINTER, T_TV, T_ROUTER, T_IOT,
       T_CAR, T_OPEN, T_HIDDEN, T_WPA3, T_TAG, T_AUDIO, T_WEAR, T_CLOSE };
const char *TILE_NAMES[N_TILES] = {"Wireless camera", "Flock / ALPR-style", "Drone / Remote ID", "Phone hotspot",
    "Printer", "Smart TV / streamer", "Router / mesh", "IoT gadget", "Car / infotainment", "Open WiFi",
    "Hidden SSID", "WPA3 network", "Tracker tag", "BT audio", "Wearable", "Point blank (>-35dBm)"};
const char *TILE_SHORT[N_TILES] = {"CAM", "FLOCK", "DRONE", "HOT\nSPOT", "PRINT", "TV", "ROUTER", "IoT",
    "CAR", "OPEN", "HIDDEN", "WPA3", "TAG", "AUDIO", "WEAR", "POINT\nBLANK"};

static watch_evt_cb_t evcb;
static Preferences prefs;
static uint16_t tiles = 0, achieved = 0;
static uint32_t next_scan = 0, pause_until = 0;
static bool scanning = false;

// ---------------------------------------------------------------- fingerprints
struct Pat { const char *s; uint8_t prefix; };  // prefix=1: must start with s
static bool match(const String &lname, const Pat *p, int n, const char **hit) {
    for (int i = 0; i < n; i++) {
        String s = p[i].s; s.toLowerCase();
        bool m = p[i].prefix ? lname.startsWith(s) : (lname.indexOf(s) >= 0);
        if (m) { if (hit) *hit = p[i].s; return true; }
    }
    return false;
}
#define NP(a) (int)(sizeof(a) / sizeof(a[0]))
static const Pat P_FLOCK[] = {{"flock",0},{"Penguin",1},{"Pigvision",0},{"FS Ext Battery",0}};
static const Pat P_CAM[] = {{"Ring Setup",0},{"Ring-",1},{"Arlo",0},{"VMB4",1},{"VMB5",1},{"Wyze",0},{"NestCam",0},
    {"Nest Cam",0},{"Reolink",0},{"eufy",0},{"Blink",1},{"Hikvision",0},{"HIK-",1},{"Dahua",0},{"Amcrest",0},
    {"Lorex",0},{"Swann",0},{"Tapo_Cam",0},{"TAPO_C",1},{"YI-",1},{"Kami",1},{"Imou",0},{"EZVIZ",0},{"Foscam",0},
    {"Vivint",0},{"SimpliCam",0},{"Canary",1},{"IPC",1},{"IPCAM",0},{"IP-CAM",0},{"IP_CAM",0},{"camera",0},
    {"_cam",0},{"-cam",0},{"cam_",1},{"cam-",1},{"Axis-",1},{"Dashcam",0},{"Vantrue",0},{"VIOFO",0}};
static const Pat P_DRONE[] = {{"DJI",0},{"TELLO",0},{"Mavic",0},{"Spark-",1},{"Skydio",0},{"Parrot",0},{"Bebop",0},
    {"Anafi",0},{"Autel",0},{"Avata",0},{"FPV",0},{"Holy Stone",0},{"HS-",1},{"Potensic",0},{"DRONE",0}};
static const Pat P_HOT[] = {{"iPhone",0},{"Android",0},{"Galaxy",0},{"Pixel",0},{"OnePlus",0},{"moto",1},{"Redmi",0},
    {"Xiaomi",0},{"Hotspot",0},{"'s iPad",0},{"Verizon-MiFi",0},{"Jetpack",0},{"Nighthawk M",0}};
static const Pat P_PRINT[] = {{"DIRECT-",1},{"HP-",1},{"HP ",1},{"EPSON",0},{"Canon",0},{"Brother",0},{"OfficeJet",0},
    {"LaserJet",0},{"ENVY",0},{"DeskJet",0},{"Xerox",0}};
static const Pat P_TV[] = {{"Roku",0},{"[TV]",0},{"Samsung",0},{"[LG]",0},{"webOS",0},{"FireTV",0},{"Fire TV",0},
    {"Chromecast",0},{"VIZIO",0},{"BRAVIA",0},{"Android TV",0},{"AppleTV",0},{"Apple TV",0},{"TCL",1},{"Hisense",0}};
static const Pat P_ROUTER[] = {{"eero",0},{"Orbi",0},{"NETGEAR",0},{"Linksys",0},{"TP-Link",0},{"Deco",1},{"ASUS",0},
    {"Xfinity",0},{"Spectrum",0},{"ATT",1},{"Google Wifi",0},{"Nest Wifi",0},{"Verizon",0},{"FiOS",0},
    {"CenturyLink",0},{"Starlink",0},{"UniFi",0},{"Ubiquiti",0},{"MikroTik",0},{"T-Mobile",0},{"CoxWiFi",0}};
static const Pat P_IOT[] = {{"ESP_",1},{"ESP-",1},{"ESP32",0},{"Tuya",0},{"SmartLife",0},{"Smart Life",0},{"Shelly",0},
    {"Sonoff",0},{"Kasa",0},{"Govee",0},{"Meross",0},{"Wemo",0},{"Hue",1},{"ecobee",0},{"tasmota",0},{"myQ",0},
    {"LIFX",0},{"Wiz",1},{"Echo",1},{"Sonos",0},{"Rachio",0},{"Tile",1},{"Roomba",0},{"iRobot",0},{"Midea",0}};
static const Pat P_CAR[] = {{"myChevrolet",0},{"Tesla",0},{"Ford",0},{"SYNC",1},{"Toyota",0},{"Hyundai",0},{"Kia",1},
    {"Subaru",0},{"VW_",1},{"Audi",0},{"BMW",0},{"Honda",0},{"Jeep",0},{"Uconnect",0},{"CarPlay",0},{"GMC",1},
    {"Mazda",0},{"Nissan",0},{"Lexus",0},{"Rivian",0},{"OnStar",0}};
static const Pat P_TAG[] = {{"AirTag",0},{"Tile",1},{"SmartTag",0},{"Chipolo",0},{"Pebblebee",0}};
static const Pat P_AUDIO[] = {{"AirPods",0},{"Buds",0},{"Bose",0},{"Beats",0},{"JBL",0},{"WH-",1},{"WF-",1},
    {"Soundcore",0},{"Sonos",0},{"Skullcandy",0},{"Jabra",0},{"Powerbeats",0},{"Speaker",0},{"Headphone",0},{"LE-",1}};
static const Pat P_WEAR[] = {{"Watch",0},{"Fitbit",0},{"Garmin",0},{"Band",0},{"WHOOP",0},{"Oura",0},{"Charge",1},
    {"Versa",1},{"Amazfit",0},{"Forerunner",0},{"fenix",0},{"Venu",1},{"Polar",0},{"Mi Smart",0}};

// OUI fingerprints (first 3 bytes). Community-sourced; treat as "likely", not proof.
static const uint8_t OUI_FLOCK[][3] = {{0x58,0x8E,0x81},{0xCC,0xCC,0xCC},{0xEC,0x1B,0xBD},{0x90,0x35,0xEA},{0x04,0x0D,0x84},
    {0xF0,0x82,0xC0},{0x1C,0x34,0xF1},{0x38,0x5B,0x44},{0x94,0x34,0x69},{0xB4,0xE3,0xF9},{0x70,0xC9,0x4E},{0x3C,0x91,0x80},
    {0xD8,0xF3,0xBC},{0x80,0x30,0x49},{0x14,0x5A,0xFC},{0x74,0x4C,0xA1},{0x08,0x3A,0x88},{0x9C,0x2F,0x9D},{0x94,0x08,0x53},{0xE4,0xAA,0xEA}};
struct OuiCam { uint8_t o[3]; const char *v; };
static const OuiCam OUI_CAM[] = {{{0x44,0x19,0xB6},"Hikvision"},{{0xBC,0xAD,0x28},"Hikvision"},{{0xC0,0x56,0xE3},"Hikvision"},
    {{0x4C,0xBD,0x8F},"Hikvision"},{{0x28,0x57,0xBE},"Hikvision"},{{0x54,0xC4,0x15},"Hikvision"},
    {{0x3C,0xEF,0x8C},"Dahua"},{{0x90,0x02,0xA9},"Dahua"},{{0xBC,0x32,0x5F},"Dahua"},{{0x14,0xA7,0x8B},"Dahua"},
    {{0x2C,0xAA,0x8E},"Wyze"},{{0x7C,0x78,0xB2},"Wyze"},{{0xD0,0x3F,0x27},"Wyze"},
    {{0xEC,0x71,0xDB},"Reolink"},{{0x34,0x3E,0xA4},"Ring"},{{0x9C,0x76,0x13},"Ring"},{{0x54,0xE0,0x19},"Ring"},
    {{0x18,0x7F,0x88},"Ring"},{{0x64,0x9A,0x63},"Ring"},{{0xA4,0x11,0x62},"Arlo"},{{0x9C,0x8E,0xCD},"Amcrest"},
    {{0x00,0x62,0x6E},"Foscam"},{{0xC4,0x3C,0xB0},"Foscam"},{{0x00,0x40,0x8C},"Axis"},{{0xAC,0xCC,0x8E},"Axis"},{{0xB8,0xA4,0x4F},"Axis"}};

static bool oui_in(const uint8_t *mac, const uint8_t (*list)[3], int n) {
    for (int i = 0; i < n; i++) if (!memcmp(mac, list[i], 3)) return true;
    return false;
}
static const char *oui_cam(const uint8_t *mac) {
    for (auto &c : OUI_CAM) if (!memcmp(mac, c.o, 3)) return c.v;
    return nullptr;
}

// classify returns tile bitmask; alert: 0 none, 1 camera, 2 flock. why = human reason
static uint16_t classify(const String &name, const uint8_t *mac, int rssi, uint8_t &alert, String &why) {
    String l = name; l.toLowerCase();
    uint16_t t = 0; const char *hit = nullptr; alert = 0;
    if (name.length() && match(l, P_FLOCK, NP(P_FLOCK), &hit)) { t |= 1 << T_FLOCK; alert = 2; why = String("name ~ \"") + hit + "\""; }
    else if (mac && oui_in(mac, OUI_FLOCK, NP(OUI_FLOCK))) { t |= 1 << T_FLOCK; alert = 2; why = "MAC OUI on Flock list"; }
    if (!alert) {
        const char *v = mac ? oui_cam(mac) : nullptr;
        if (name.length() && match(l, P_CAM, NP(P_CAM), &hit)) { t |= 1 << T_CAM; alert = 1; why = String("name ~ \"") + hit + "\""; }
        else if (v) { t |= 1 << T_CAM; alert = 1; why = String("MAC OUI: ") + v; }
    }
    if (name.length()) {
        if (match(l, P_DRONE, NP(P_DRONE), nullptr)) t |= 1 << T_DRONE;
        if (match(l, P_HOT, NP(P_HOT), nullptr)) t |= 1 << T_HOTSPOT;
        if (match(l, P_PRINT, NP(P_PRINT), nullptr)) t |= 1 << T_PRINTER;
        if (match(l, P_TV, NP(P_TV), nullptr)) t |= 1 << T_TV;
        if (match(l, P_ROUTER, NP(P_ROUTER), nullptr)) t |= 1 << T_ROUTER;
        if (match(l, P_IOT, NP(P_IOT), nullptr)) t |= 1 << T_IOT;
        if (match(l, P_CAR, NP(P_CAR), nullptr)) t |= 1 << T_CAR;
    }
    if (rssi >= -35) t |= 1 << T_CLOSE;
    return t;
}

// ---------------------------------------------------------------- seen-set (dedupe)
#define SEEN_N 512
static uint32_t seen[SEEN_N]; static int seen_cnt = 0;
static bool seen_add(const uint8_t *mac) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) { h ^= mac[i]; h *= 16777619u; }
    if (!h) h = 1;
    for (int i = 0; i < seen_cnt; i++) if (seen[i] == h) return false;
    if (seen_cnt < SEEN_N) seen[seen_cnt++] = h; else seen[random(SEEN_N)] = h;
    g_seen_total++;
    return true;
}

static void logln(const String &s) {
    g_log[g_log_head] = s; g_log_head = (g_log_head + 1) % LOG_N; g_log_gen++;
    Serial.println("[watch] " + s);
}

// ---------------------------------------------------------------- bingo
static bool line_bingo(uint16_t m) {
    for (int r = 0; r < 4; r++) if (((m >> (r * 4)) & 0xF) == 0xF) return true;
    for (int c = 0; c < 4; c++) { bool ok = true; for (int r = 0; r < 4; r++) if (!(m & (1 << (r * 4 + c)))) ok = false; if (ok) return true; }
    if ((m & 0x8421) == 0x8421 || (m & 0x1248) == 0x1248) return true;
    return false;
}
int watch_tile_count() { return __builtin_popcount(tiles); }
uint16_t watch_tiles() { return tiles; }
struct Ach { uint8_t bit; int need; const char *name; };
static const Ach ACH[] = {{0,4,"Rookie Spotter"},{1,8,"Signal Hunter"},{2,12,"RF Ranger"},{3,16,"BLACKOUT! Full card"}};

static void add_tiles(uint16_t t, const String &src) {
    uint16_t nw = t & ~tiles;
    if (!nw) return;
    tiles |= nw;
    for (int i = 0; i < N_TILES; i++) if (nw & (1 << i)) {
        logln(String("TILE: ") + TILE_NAMES[i] + " (" + src + ")");
        if (evcb) evcb(EV_TILE, TILE_NAMES[i], src);
    }
    int n = watch_tile_count();
    if (!(achieved & 0x10) && line_bingo(tiles)) {
        achieved |= 0x10;
        if (evcb) evcb(EV_ACHIEVE, "BINGO!", "You completed a line on the card.");
        logln("ACHIEVEMENT: BINGO line");
    }
    for (auto &a : ACH) if (n >= a.need && !(achieved & (1 << a.bit))) {
        achieved |= 1 << a.bit;
        if (evcb) evcb(EV_ACHIEVE, a.name, String(n) + "/16 device types discovered");
        logln(String("ACHIEVEMENT: ") + a.name);
    }
    prefs.putUShort("tiles", tiles); prefs.putUChar("ach", achieved);
}
void watch_reset_bingo() { tiles = 0; achieved = 0; prefs.putUShort("tiles", 0); prefs.putUChar("ach", 0); logln("Bingo card reset"); }

// ---------------------------------------------------------------- WiFi
static void process_wifi(int n) {
    int newc = 0; String first;
    g_ap_n = 0;
    for (int i = 0; i < n && g_ap_n < MAX_APS; i++) {
        ApRec &a = g_aps[g_ap_n++];
        String ssid = WiFi.SSID(i);
        strlcpy(a.ssid, ssid.c_str(), sizeof(a.ssid));
        memcpy(a.bssid, WiFi.BSSID(i), 6);
        a.rssi = WiFi.RSSI(i); a.ch = WiFi.channel(i); a.auth = WiFi.encryptionType(i);
        String why; uint8_t alert;
        uint16_t t = classify(ssid, a.bssid, a.rssi, alert, why);
        if (a.auth == WIFI_AUTH_OPEN) t |= 1 << T_OPEN;
        if (ssid.length() == 0) t |= 1 << T_HIDDEN;
        if (a.auth == WIFI_AUTH_WPA3_PSK || a.auth == WIFI_AUTH_WPA2_WPA3_PSK) t |= 1 << T_WPA3;
        a.tiles = t; a.alert = alert;
        bool isnew = seen_add(a.bssid);
        String nm = ssid.length() ? ssid : String("<hidden>");
        if (isnew) {
            newc++; if (!first.length()) first = nm;
            if (alert) {
                g_cams_seen++;
                String det = nm + "  " + String(a.rssi) + "dBm ch" + String(a.ch) + "\n" + WiFi.BSSIDstr(i) +
                             "\nLikely (fingerprint): " + why;
                logln(String(alert == 2 ? "FLOCK? " : "CAM? ") + nm + " " + String(a.rssi) + "dBm");
                if (evcb) evcb(alert == 2 ? EV_FLOCK : EV_CAMERA, alert == 2 ? "Flock/ALPR-style nearby" : "Wireless camera nearby", det);
            }
        }
        if (t) add_tiles(t, "WiFi " + nm);
    }
    g_scan_gen++;
    if (newc && evcb) evcb(EV_NEW_DEVICES, String("+") + newc + " WiFi", first);
}

void watch_scan_now() { next_scan = 0; }
void watch_pause_wifi(uint32_t ms) { pause_until = millis() + ms; }

// ---------------------------------------------------------------- BLE (own task)
struct BlePend { char name[24]; char mac[18]; uint8_t addr[6]; int8_t rssi; uint8_t flags; };
// flags: 1=FindMy(Apple 0x12) 2=Tile/SmartTag svc 4=RemoteID(FFFA) 8=Flock mfg 0x09C8 16=Apple
static BlePend pend[MAX_BLE]; static volatile int pend_n = 0; static volatile bool pend_ready = false;
static SemaphoreHandle_t ble_mx;
static BLEScan *s_ble_scan = nullptr;

// True while the scanner task owns the radio in a blocking scan.
static volatile bool g_ble_scanning = false;

// Stop the passive BLE scanner and wait until the radio is free.
//
// NimBLE cannot scan and advertise at the same time: ble_gap_disc_cancel()
// has to complete before ble_gap_adv_start() will succeed. The scanner task
// blocks in BLEScan::start(4, false) for up to 4 seconds, so it only
// notices the "stand down" flag between scans - an attack that starts
// mid-scan would otherwise fail to advertise.
//
// BLEScan::stop() cancels the running scan and releases the task blocked in
// start(), so this returns promptly.
void watch_ble_stand_down() {
    if (s_ble_scan) s_ble_scan->stop();
    // Wait for the scanner task to actually leave its scan.
    for (int i = 0; i < 100 && g_ble_scanning; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void ble_task(void *) {
    BLEDevice::init("");
    BLEScan *s = BLEDevice::getScan();
    s_ble_scan = s;
    s->setActiveScan(false);   // passive: listen only
    s->setInterval(100); s->setWindow(90);
    for (;;) {
        // An attack owns the radio: stand down so we don't fight it for the
        // WiFi/BLE stack (AP mode vs scan, advertiser vs scanner).
        if (!g_watch_on || pend_ready || g_attack_ble_busy || g_attack_active) {
            vTaskDelay(pdMS_TO_TICKS(500)); continue;
        }
        g_ble_scanning = true;
        BLEScanResults *res = s->start(4, false);
        g_ble_scanning = false;
        if (g_attack_ble_busy || g_attack_active) {
            // Cancelled by an attack starting up: drop the results.
            s->clearResults();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int n = res ? res->getCount() : 0;
        xSemaphoreTake(ble_mx, portMAX_DELAY);
        pend_n = 0;
        for (int i = 0; i < n && pend_n < MAX_BLE; i++) {
            BLEAdvertisedDevice d = res->getDevice(i);
            BlePend &p = pend[pend_n++];
            memset(&p, 0, sizeof(p));
            if (d.haveName()) strlcpy(p.name, d.getName().c_str(), sizeof(p.name));
            strlcpy(p.mac, d.getAddress().toString().c_str(), sizeof(p.mac));
            { BLEAddress ad = d.getAddress(); memcpy(p.addr, ad.getNative(), 6); }
            p.rssi = d.getRSSI();
            if (d.haveManufacturerData()) {
                String md = d.getManufacturerData();
                if (md.length() >= 2) {
                    uint16_t cid = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
                    if (cid == 0x004C) { p.flags |= 16; if (md.length() >= 3 && (uint8_t)md[2] == 0x12) p.flags |= 1; }
                    if (cid == 0x09C8) p.flags |= 8;
                }
            }
            for (int k = 0; k < d.getServiceUUIDCount(); k++) {
                String u = d.getServiceUUID(k).toString().c_str(); u.toLowerCase();
                if (u.indexOf("feed") >= 0 || u.indexOf("fd84") >= 0 || u.indexOf("fd5a") >= 0) p.flags |= 2;
                if (u.indexOf("fffa") >= 0) p.flags |= 4;
            }
        }
        pend_ready = true;
        xSemaphoreGive(ble_mx);
        s->clearResults();
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

static void process_ble() {
    if (!pend_ready) return;
    xSemaphoreTake(ble_mx, portMAX_DELAY);
    int newc = 0; String first;
    g_ble_n = 0;
    for (int i = 0; i < pend_n; i++) {
        BlePend &p = pend[i]; BleRec &b = g_ble[g_ble_n++];
        strlcpy(b.name, p.name, sizeof(b.name)); strlcpy(b.mac, p.mac, sizeof(b.mac)); b.rssi = p.rssi;
        String nm = p.name; String why; uint8_t alert;
        uint16_t t = classify(nm, nullptr, p.rssi, alert, why);   // BLE MACs mostly random: name-only
        String l = nm; l.toLowerCase();
        strlcpy(b.kind, "", sizeof(b.kind));
        if (p.flags & 8) { t |= 1 << T_FLOCK; alert = 2; why = "BLE mfg ID 0x09C8"; }
        if (p.flags & 4) { t |= 1 << T_DRONE; strlcpy(b.kind, "RemoteID", sizeof(b.kind)); }
        if ((p.flags & 1) || (p.flags & 2) || match(l, P_TAG, NP(P_TAG), nullptr)) { t |= 1 << T_TAG; strlcpy(b.kind, "Tracker", sizeof(b.kind)); }
        if (nm.length() && match(l, P_AUDIO, NP(P_AUDIO), nullptr)) { t |= 1 << T_AUDIO; strlcpy(b.kind, "Audio", sizeof(b.kind)); }
        if (nm.length() && match(l, P_WEAR, NP(P_WEAR), nullptr)) { t |= 1 << T_WEAR; strlcpy(b.kind, "Wearable", sizeof(b.kind)); }
        if (!b.kind[0] && (p.flags & 16)) strlcpy(b.kind, "Apple", sizeof(b.kind));
        if (alert == 1) strlcpy(b.kind, "Camera?", sizeof(b.kind));
        if (alert == 2) strlcpy(b.kind, "Flock?", sizeof(b.kind));
        b.tiles = t; b.alert = alert;
        bool isnew = seen_add(p.addr);
        if (isnew) {
            newc++; if (!first.length()) first = nm.length() ? nm : String(p.mac);
            if (alert) {
                g_cams_seen++;
                String det = (nm.length() ? nm : String("<no name>")) + "  " + String(p.rssi) + "dBm\nBLE " + p.mac +
                             "\nLikely (fingerprint): " + why;
                logln(String(alert == 2 ? "FLOCK? BLE " : "CAM? BLE ") + (nm.length() ? nm : String(p.mac)));
                if (evcb) evcb(alert == 2 ? EV_FLOCK : EV_CAMERA, alert == 2 ? "Flock/ALPR-style nearby" : "Camera-like BLE device", det);
            }
        }
        if (t) add_tiles(t, "BLE " + (nm.length() ? nm : String(p.mac)));
    }
    pend_ready = false;
    xSemaphoreGive(ble_mx);
    g_ble_gen++;
    if (newc && evcb) evcb(EV_NEW_DEVICES, String("+") + newc + " BLE", first);
}

// ---------------------------------------------------------------- tick
void watch_tick() {
    uint32_t now = millis();
    // An attack owns the radio in AP/promiscuous mode; scanning would fight it.
    if (g_attack_active) { process_ble(); return; }
    if (scanning) {
        int n = WiFi.scanComplete();
        if (n != WIFI_SCAN_RUNNING) {
            scanning = false;
            if (n >= 0) process_wifi(n);
            WiFi.scanDelete();
            next_scan = now + 12000;
        }
    } else if ((g_watch_on || next_scan == 0) && (int32_t)(now - next_scan) >= 0 && (int32_t)(now - pause_until) >= 0) {
        wl_status_t st = WiFi.status();
        if (st != WL_IDLE_STATUS || WiFi.isConnected() || true) {
            if (WiFi.scanNetworks(true, true) == WIFI_SCAN_RUNNING || WiFi.scanComplete() == WIFI_SCAN_RUNNING) scanning = true;
            else next_scan = now + 3000;
        }
    }
    process_ble();
}

void watch_init(watch_evt_cb_t cb) {
    evcb = cb;
    prefs.begin("cwatch", false);
    tiles = prefs.getUShort("tiles", 0); achieved = prefs.getUChar("ach", 0);
    ble_mx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(ble_task, "ble", 6144, NULL, 1, NULL, 0);
    logln("Cordell Watch online. Passive scan only.");
}
