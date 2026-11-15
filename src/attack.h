// Cordell Watch — Attack Lab API.
//
// NOTE ON DEAUTH: the ESP32-S3 WiFi driver rejects deauthentication and
// disassociation frames outright (esp_wifi_80211_tx returns
// ESP_ERR_INVALID_ARG and the driver logs "unsupport frame type: 0c0").
// The S3 only accepts beacon / probe-request / probe-response / action /
// non-QoS-data frames. The deauth role is therefore covered by KARMA-style
// probe-response injection (ATK_KARMA), which pulls clients onto our own AP
// using frame types the hardware actually permits.
#pragma once
#include <Arduino.h>

enum AttackMode {
    ATK_NONE = 0,
    ATK_PORTAL,    // evil portal: rogue AP + captive DNS + credential capture
    ATK_BEACON,    // beacon flood with random SSIDs
    ATK_PROBE,     // probe-request flood
    ATK_KARMA,     // probe-response injection (deauth substitute)
    ATK_BLE,       // BLE advertisement spam with rotating random MAC
    ATK_CLIENTS,   // promiscuous client sniffer
};

// Set while an attack owns the WiFi radio (passive scanner must stand down).
extern volatile bool g_attack_active;
// Set while BLE spam owns the BLE stack (passive BLE scanner must stand down).
extern volatile bool g_attack_ble_busy;

void attack_init();
bool attack_start(AttackMode m);
void attack_stop();
bool attack_running();
AttackMode attack_mode();
const char *attack_mode_name();
String attack_status_line();

uint32_t attack_tx_count();   // 802.11 frames transmitted
uint32_t attack_rx_count();   // management frames seen (sniffer/karma)
uint32_t attack_ble_count();  // BLE advertisements sent
uint32_t attack_client_count();// unique client MACs seen

// Portal helpers
int attack_cred_count();
String attack_cred_line(int i);   // "net | user | pass"
void attack_clear_creds();
String attack_ap_ssid();
void attack_set_ap_ssid(const char *ssid);
uint8_t attack_ap_channel();
void attack_set_ap_channel(uint8_t ch);
