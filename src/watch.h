#pragma once
#include <Arduino.h>
#define MAX_APS 48
#define MAX_BLE 40
#define N_TILES 16
struct ApRec { char ssid[33]; uint8_t bssid[6]; int8_t rssi; uint8_t ch; uint8_t auth; uint16_t tiles; uint8_t alert; };
struct BleRec { char name[24]; char mac[18]; int8_t rssi; uint16_t tiles; uint8_t alert; char kind[16]; };
extern ApRec g_aps[MAX_APS]; extern int g_ap_n; extern volatile uint32_t g_scan_gen;
extern BleRec g_ble[MAX_BLE]; extern int g_ble_n; extern volatile uint32_t g_ble_gen;
extern const char *TILE_NAMES[N_TILES];
extern const char *TILE_SHORT[N_TILES];
extern bool g_watch_on;
extern uint32_t g_seen_total, g_cams_seen;

enum WatchEvt { EV_NEW_DEVICES, EV_CAMERA, EV_FLOCK, EV_TILE, EV_ACHIEVE };
typedef void (*watch_evt_cb_t)(WatchEvt type, const String &title, const String &detail);

void watch_init(watch_evt_cb_t cb);
void watch_tick();                 // call from LVGL thread (timer)
void watch_scan_now();             // request an immediate WiFi scan
void watch_pause_wifi(uint32_t ms);// hold off scans (while connecting)
void watch_ble_stand_down();       // cancel the passive BLE scan, free the radio
uint16_t watch_tiles();
int watch_tile_count();
void watch_reset_bingo();
// event log for the feed page
#define LOG_N 30
extern String g_log[LOG_N]; extern int g_log_head; extern volatile uint32_t g_log_gen;
