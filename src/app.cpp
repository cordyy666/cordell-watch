// Cordell Watch UI (LVGL 8.4) — Cordell Aerial navy/gold theme.
// Net tools (scan/connect/info/ping/DNS/traceroute/BLE) + passive surveillance-awareness
// popups + discovery bingo + achievements + retro SFX. No attack features.
#include "app.h"
#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "sfx.h"
#include "watch.h"
#include "attack.h"

LV_IMG_DECLARE(cordell_logo);

#define C_NAVY   0x041123
#define C_NAVY2  0x0A1F3C
#define C_SLATE  0x202B2F
#define C_GOLD   0xB6954D
#define C_GOLD2  0xE2C27A
#define C_RED    0xE5484D
#define C_TEXT   0xE8ECF1

static lv_obj_t *scr_home, *home_stat, *watch_btn_lbl, *sound_btn_lbl;
static lv_timer_t *page_timer = NULL;   // one per page, killed on Back
static uint32_t page_gen = 0;

// ---------------------------------------------------------------- page scaffold
static void kill_page_timer() { if (page_timer) { lv_timer_del(page_timer); page_timer = NULL; } }
static void back_event_cb(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    kill_page_timer();
    lv_obj_t *page = (lv_obj_t *)lv_event_get_user_data(e);
    lv_scr_load(scr_home);
    lv_obj_del_async(page);
}
static lv_obj_t *make_page(const char *title, lv_obj_t **page_out) {
    kill_page_timer();
    lv_obj_t *page = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(page, lv_color_hex(C_NAVY), 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bar = lv_obj_create(page);
    lv_obj_set_size(bar, 240, 40);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_NAVY2), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(C_GOLD), 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *btn = lv_btn_create(bar);
    lv_obj_set_size(btn, 62, 30);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(btn, back_event_cb, LV_EVENT_CLICKED, page);
    lv_obj_t *bl = lv_label_create(btn); lv_label_set_text(bl, LV_SYMBOL_LEFT " Back"); lv_obj_center(bl);
    lv_obj_t *tl = lv_label_create(bar);
    lv_obj_set_style_text_color(tl, lv_color_hex(C_GOLD2), 0);
    lv_label_set_text(tl, title);
    lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_t *content = lv_obj_create(page);
    lv_obj_set_size(content, 240, 280);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(C_NAVY), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 6, 0);
    lv_scr_load(page);
    page_gen++;
    if (page_out) *page_out = page;
    return content;
}
static void label_set(lv_obj_t *l, const String &s) { lv_label_set_text(l, s.c_str()); }
static lv_obj_t *mk_label(lv_obj_t *p, int w) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, w);
    return l;
}

// ---------------------------------------------------------------- keyboard helpers (dismiss bug fix)
static void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_t *ta = lv_keyboard_get_textarea(kb);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        lv_indev_reset(NULL, ta);
        if (code == LV_EVENT_READY && ta) lv_event_send(ta, LV_EVENT_READY, NULL);
    }
}
static void ta_focus_cb(lv_event_t *e) {
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
    lv_keyboard_set_textarea(kb, lv_event_get_target(e));
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(kb);
}
static void bg_tap_hide_kb_cb(lv_event_t *e) {   // tap outside a field hides keyboard
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *mk_keyboard(lv_obj_t *page, lv_obj_t *content) {
    lv_obj_t *kb = lv_keyboard_create(page);
    lv_obj_set_size(kb, 240, 150);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(content, bg_tap_hide_kb_cb, LV_EVENT_CLICKED, kb);
    return kb;
}

// ---------------------------------------------------------------- popups & toasts
struct PendPop { String title, detail; bool flock; };
static PendPop pops[4]; static int pop_n = 0;
static lv_obj_t *pop_obj = NULL;
static void show_next_pop();
static void pop_close_cb(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    if (pop_obj) { lv_obj_del(pop_obj); pop_obj = NULL; }
    show_next_pop();
}
static void show_pop(const String &title, const String &detail, uint32_t color, const char *icon) {
    pop_obj = lv_obj_create(lv_layer_top());            // full-screen dim backdrop
    lv_obj_set_size(pop_obj, 240, 320);
    lv_obj_set_style_bg_color(pop_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(pop_obj, LV_OPA_60, 0);
    lv_obj_set_style_border_width(pop_obj, 0, 0);
    lv_obj_set_style_radius(pop_obj, 0, 0);
    lv_obj_add_event_cb(pop_obj, pop_close_cb, LV_EVENT_CLICKED, NULL);  // tap outside = dismiss
    lv_obj_t *card = lv_obj_create(pop_obj);
    lv_obj_set_size(card, 224, 200);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_NAVY2), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(card);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(color), 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_width(t, 196);
    label_set(t, String(icon) + " " + title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *d = mk_label(card, 196);
    lv_obj_set_style_text_color(d, lv_color_hex(C_TEXT), 0);
    label_set(d, detail);
    lv_obj_align(d, LV_ALIGN_TOP_LEFT, 0, 44);
    lv_obj_t *b = lv_btn_create(card);
    lv_obj_set_size(b, 110, 34);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_add_event_cb(b, pop_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(b); lv_label_set_text(bl, LV_SYMBOL_CLOSE " Dismiss"); lv_obj_center(bl);
    lv_obj_set_style_text_color(bl, lv_color_hex(C_NAVY), 0);
}
static void show_next_pop() {
    if (pop_obj || !pop_n) return;
    PendPop p = pops[0];
    for (int i = 1; i < pop_n; i++) pops[i - 1] = pops[i];
    pop_n--;
    if (p.title.startsWith("@ACH:")) show_pop(p.title.substring(5), p.detail, C_GOLD2, LV_SYMBOL_OK " ACHIEVEMENT UNLOCKED\n");
    else show_pop(p.title, p.detail, p.flock ? C_RED : C_GOLD, LV_SYMBOL_EYE_OPEN);
}
static void queue_pop(const String &t, const String &d, bool flock) {
    if (pop_n >= 4) return;
    pops[pop_n++] = {t, d, flock};
    show_next_pop();
}

static lv_obj_t *toast = NULL;
static void toast_del_cb(lv_timer_t *t) { if (toast) { lv_obj_del(toast); toast = NULL; } }
static void show_toast(const String &s, uint32_t color) {
    if (toast) { lv_obj_del(toast); toast = NULL; }
    toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(toast, 228, LV_SIZE_CONTENT);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(toast, lv_color_hex(C_SLATE), 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(toast, 2, 0);
    lv_obj_set_style_pad_all(toast, 6, 0);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = mk_label(toast, 210);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
    label_set(l, s);
    lv_timer_t *tm = lv_timer_create(toast_del_cb, 2500, NULL);
    lv_timer_set_repeat_count(tm, 1);
}

static uint32_t last_ping_ms = 0;
static void on_watch_event(WatchEvt type, const String &title, const String &detail) {
    switch (type) {
    case EV_NEW_DEVICES:
        if (millis() - last_ping_ms > 1500) { sfx_play(SFX_PING); last_ping_ms = millis(); }
        show_toast(String(LV_SYMBOL_WIFI " ping! ") + title + " new: " + detail, C_GOLD);
        break;
    case EV_CAMERA: sfx_play(SFX_CAMERA); queue_pop(title, detail, false); break;
    case EV_FLOCK:  sfx_play(SFX_CAMERA); sfx_play(SFX_CAMERA); queue_pop(title, detail, true); break;
    case EV_TILE:   sfx_play(SFX_TILE); show_toast(String(LV_SYMBOL_OK " Bingo tile: ") + title, C_GOLD2); break;
    case EV_ACHIEVE: sfx_play(SFX_ACHIEVE); queue_pop(String("@ACH:") + title, detail, false); break;
    }
}

// ---------------------------------------------------------------- ICMP (lwip raw)
static uint16_t icmp_cksum(uint16_t *addr, int len) {
    int nleft = len; uint32_t sum = 0; uint16_t *w = addr; uint16_t answer = 0;
    while (nleft > 1) { sum += *w++; nleft -= 2; }
    if (nleft == 1) { *(uint8_t *)(&answer) = *(uint8_t *)w; sum += answer; }
    sum = (sum >> 16) + (sum & 0xffff); sum += (sum >> 16);
    return (uint16_t)(~sum);
}
static int icmp_probe(uint32_t dst_ip, int ttl, int timeout_ms, char *from_ip, int from_len, int *rtt_ms) {
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (s < 0) return -1;
    struct timeval tv; tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
    static uint16_t seq = 0; seq++;
    uint8_t pkt[64]; memset(pkt, 0, sizeof(pkt));
    pkt[0] = 8; uint16_t id = 0xBEEF;
    pkt[4] = id >> 8; pkt[5] = id & 0xff; pkt[6] = seq >> 8; pkt[7] = seq & 0xff;
    for (int i = 8; i < 32; i++) pkt[i] = i;
    int plen = 32;
    uint16_t ck = icmp_cksum((uint16_t *)pkt, plen);
    pkt[2] = ck & 0xff; pkt[3] = ck >> 8;
    struct sockaddr_in to; memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET; to.sin_addr.s_addr = dst_ip;
    uint32_t t0 = millis();
    if (sendto(s, pkt, plen, 0, (struct sockaddr *)&to, sizeof(to)) < 0) { close(s); return -1; }
    uint8_t rbuf[128]; struct sockaddr_in from; socklen_t fl = sizeof(from);
    int rc = -1;
    while (millis() - t0 < (uint32_t)timeout_ms + 50) {
        int n = recvfrom(s, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) break;
        int ihl = (rbuf[0] & 0x0f) * 4;
        if (n < ihl + 8) continue;
        uint8_t type = rbuf[ihl];
        if (from_ip) inet_ntop(AF_INET, &from.sin_addr, from_ip, from_len);
        if (rtt_ms) *rtt_ms = (int)(millis() - t0);
        if (type == 0) { rc = 0; break; }
        if (type == 11) { rc = 1; break; }
    }
    close(s);
    return rc;
}
static bool resolve_host(const char *host, uint32_t *ip, char *ipstr, int ipstr_len) {
    IPAddress addr;
    if (!WiFi.hostByName(host, addr)) return false;
    *ip = (uint32_t)addr;
    if (ipstr) snprintf(ipstr, ipstr_len, "%s", addr.toString().c_str());
    return true;
}
static const char *auth_str(uint8_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN: return "OPEN"; case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA"; case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2"; case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3"; case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        default: return "?";
    }
}

// ---------------------------------------------------------------- Join sheet (tap SSID -> password only)
static char join_ssid[33]; static uint8_t join_auth;
static lv_obj_t *join_ta, *join_status, *join_kb;
static uint32_t join_t0;
static void join_poll_cb(lv_timer_t *t) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        kill_page_timer();
        sfx_play(SFX_CONNECT);
        label_set(join_status, "Connected!\nIP " + WiFi.localIP().toString() + "  GW " + WiFi.gatewayIP().toString() +
                  "\nPing / DNS / Traceroute ready.\nTap Back.");
        watch_pause_wifi(0);
    } else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL || millis() - join_t0 > 20000) {
        kill_page_timer();
        sfx_play(SFX_FAIL);
        label_set(join_status, st == WL_NO_SSID_AVAIL ? "Network not found." : "Connect failed (wrong password?)");
        watch_pause_wifi(0);
    }
}
static void join_do_connect() {
    lv_obj_add_flag(join_kb, LV_OBJ_FLAG_HIDDEN);
    const char *pass = join_auth == WIFI_AUTH_OPEN ? "" : lv_textarea_get_text(join_ta);
    label_set(join_status, String("Connecting to ") + join_ssid + " ...");
    watch_pause_wifi(25000);
    WiFi.scanDelete();
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    if (join_auth == WIFI_AUTH_OPEN) WiFi.begin(join_ssid); else WiFi.begin(join_ssid, pass);
    join_t0 = millis();
    kill_page_timer();
    page_timer = lv_timer_create(join_poll_cb, 400, NULL);
}
static void join_btn_cb(lv_event_t *e) { sfx_play(SFX_CLICK); join_do_connect(); }
static void join_ready_cb(lv_event_t *e) { join_do_connect(); }  // keyboard OK/Enter
static void show_pw_cb(lv_event_t *e) {
    lv_obj_t *cb = lv_event_get_target(e);
    lv_textarea_set_password_mode(join_ta, !lv_obj_has_state(cb, LV_STATE_CHECKED));
}
static void open_join_sheet(const char *ssid, uint8_t auth) {
    strlcpy(join_ssid, ssid, sizeof(join_ssid)); join_auth = auth;
    lv_obj_t *page; lv_obj_t *c = make_page("Join WiFi", &page);
    join_kb = mk_keyboard(page, c);
    lv_obj_t *l = mk_label(c, 224);
    lv_obj_set_style_text_color(l, lv_color_hex(C_GOLD2), 0);
    label_set(l, String(LV_SYMBOL_WIFI " ") + join_ssid + "  (" + auth_str(auth) + ")");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    // Connect button sits at the TOP so the keyboard can never cover it
    lv_obj_t *go = lv_btn_create(c);
    lv_obj_set_size(go, 80, 36);
    lv_obj_align(go, LV_ALIGN_TOP_RIGHT, 0, 26);
    lv_obj_set_style_bg_color(go, lv_color_hex(C_GOLD), 0);
    lv_obj_add_event_cb(go, join_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(go); lv_label_set_text(gl, "Connect"); lv_obj_center(gl);
    lv_obj_set_style_text_color(gl, lv_color_hex(C_NAVY), 0);
    join_ta = lv_textarea_create(c);
    lv_textarea_set_one_line(join_ta, true);
    lv_textarea_set_password_mode(join_ta, true);
    lv_obj_set_width(join_ta, 136);
    lv_obj_align(join_ta, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_textarea_set_placeholder_text(join_ta, "password");
    lv_obj_add_event_cb(join_ta, ta_focus_cb, LV_EVENT_FOCUSED, join_kb);
    lv_obj_add_event_cb(join_ta, ta_focus_cb, LV_EVENT_CLICKED, join_kb);
    lv_obj_add_event_cb(join_ta, join_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_t *cb = lv_checkbox_create(c);
    lv_checkbox_set_text(cb, "show");
    lv_obj_align(cb, LV_ALIGN_TOP_LEFT, 0, 66);
    lv_obj_add_event_cb(cb, show_pw_cb, LV_EVENT_VALUE_CHANGED, NULL);
    join_status = mk_label(c, 224);
    lv_obj_align(join_status, LV_ALIGN_TOP_LEFT, 0, 92);
    if (auth == WIFI_AUTH_OPEN) {
        lv_obj_add_flag(join_ta, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(cb, LV_OBJ_FLAG_HIDDEN);
        label_set(join_status, "Open network - no password.\nTap Connect.");
    } else {
        label_set(join_status, "Type password, then OK on the\nkeyboard (or tap Connect).\nTap empty space to hide keys.");
        lv_keyboard_set_textarea(join_kb, join_ta);
        lv_obj_clear_flag(join_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------- WiFi scan (tap to join)
static lv_obj_t *scan_list, *scan_status; static uint32_t scan_seen_gen;
static void scan_item_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= g_ap_n) return;
    sfx_play(SFX_CLICK);
    if (!g_aps[i].ssid[0]) { show_toast("Hidden SSID - use Manual Join", C_GOLD); return; }
    char ss[33]; strlcpy(ss, g_aps[i].ssid, sizeof(ss)); uint8_t au = g_aps[i].auth;
    open_join_sheet(ss, au);
}
static void scan_fill() {
    lv_obj_clean(scan_list);
    label_set(scan_status, String(g_ap_n) + " nets - tap one to join");
    for (int i = 0; i < g_ap_n; i++) {
        ApRec &a = g_aps[i];
        String line = a.ssid[0] ? String(a.ssid) : String("<hidden>");
        line += "\n" + String(a.rssi) + "dBm ch" + String(a.ch) + " " + auth_str(a.auth);
        if (a.alert == 2) line += "  FLOCK?"; else if (a.alert == 1) line += "  CAM?";
        lv_obj_t *b = lv_list_add_btn(scan_list, a.auth == WIFI_AUTH_OPEN ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_CLOSE, line.c_str());
        if (a.alert) lv_obj_set_style_text_color(b, lv_color_hex(a.alert == 2 ? C_RED : C_GOLD2), 0);
        lv_obj_add_event_cb(b, scan_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}
static void scan_poll_cb(lv_timer_t *t) { if (g_scan_gen != scan_seen_gen) { scan_seen_gen = g_scan_gen; scan_fill(); } }
static void rescan_cb(lv_event_t *e) { sfx_play(SFX_CLICK); label_set(scan_status, "Scanning..."); watch_scan_now(); }
static void page_wifi_scan(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *c = make_page("WiFi Scan", NULL);
    scan_status = mk_label(c, 140);
    lv_obj_align(scan_status, LV_ALIGN_TOP_LEFT, 0, 4);
    lv_obj_t *rb = lv_btn_create(c); lv_obj_set_size(rb, 78, 28); lv_obj_align(rb, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(rb, rescan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(rb); lv_label_set_text(rl, LV_SYMBOL_REFRESH " Scan"); lv_obj_center(rl);
    scan_list = lv_list_create(c);
    lv_obj_set_size(scan_list, 228, 232);
    lv_obj_align(scan_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(scan_list, lv_color_hex(C_NAVY2), 0);
    scan_seen_gen = g_scan_gen;
    if (g_ap_n) scan_fill(); else label_set(scan_status, "Scanning...");
    watch_scan_now();
    page_timer = lv_timer_create(scan_poll_cb, 300, NULL);
}

// ---------------------------------------------------------------- Manual join (hidden SSIDs)
static lv_obj_t *man_ssid, *man_pass;
static void man_go_cb(lv_event_t *e) {
    strlcpy(join_ssid, lv_textarea_get_text(man_ssid), sizeof(join_ssid));
    String p = lv_textarea_get_text(man_pass);
    open_join_sheet(join_ssid, p.length() ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
    if (p.length()) { lv_textarea_set_text(join_ta, p.c_str()); }
}
static void page_manual(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *page; lv_obj_t *c = make_page("Manual Join", &page);
    lv_obj_t *kb = mk_keyboard(page, c);
    lv_obj_t *go = lv_btn_create(c); lv_obj_set_size(go, 70, 34); lv_obj_align(go, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(go, man_go_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(go); lv_label_set_text(gl, "Next"); lv_obj_center(gl);
    man_ssid = lv_textarea_create(c); lv_textarea_set_one_line(man_ssid, true); lv_obj_set_width(man_ssid, 146);
    lv_textarea_set_placeholder_text(man_ssid, "SSID"); lv_obj_align(man_ssid, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(man_ssid, ta_focus_cb, LV_EVENT_FOCUSED, kb);
    man_pass = lv_textarea_create(c); lv_textarea_set_one_line(man_pass, true); lv_obj_set_width(man_pass, 146);
    lv_textarea_set_password_mode(man_pass, true);
    lv_textarea_set_placeholder_text(man_pass, "password (blank=open)"); lv_obj_align(man_pass, LV_ALIGN_TOP_LEFT, 0, 44);
    lv_obj_add_event_cb(man_pass, ta_focus_cb, LV_EVENT_FOCUSED, kb);
}

// ---------------------------------------------------------------- Channel map
static lv_obj_t *chan_lbl; static uint32_t chan_gen;
static void chan_fill() {
    int ch[15] = {0};
    for (int i = 0; i < g_ap_n; i++) { int c = g_aps[i].ch; if (c >= 1 && c <= 14) ch[c]++; }
    String s = "APs per channel:\n";
    for (int c = 1; c <= 13; c++) {
        s += "ch" + String(c) + (c < 10 ? " : " : ": ");
        for (int k = 0; k < ch[c] && k < 20; k++) s += "#";
        s += " " + String(ch[c]) + "\n";
    }
    label_set(chan_lbl, s);
}
static void chan_poll_cb(lv_timer_t *t) { if (g_scan_gen != chan_gen) { chan_gen = g_scan_gen; chan_fill(); } }
static void page_channels(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *c = make_page("Channel Map", NULL);
    chan_lbl = mk_label(c, 224); lv_obj_align(chan_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    chan_gen = g_scan_gen; chan_fill(); watch_scan_now();
    page_timer = lv_timer_create(chan_poll_cb, 500, NULL);
}

// ---------------------------------------------------------------- Net info
static void page_netinfo(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *c = make_page("Net Info", NULL);
    lv_obj_t *l = mk_label(c, 224); lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    String s;
    if (WiFi.status() == WL_CONNECTED) {
        s  = "SSID: " + WiFi.SSID() + "\nRSSI: " + String(WiFi.RSSI()) + " dBm\n";
        s += "IP:   " + WiFi.localIP().toString() + "\nGW:   " + WiFi.gatewayIP().toString() + "\n";
        s += "Mask: " + WiFi.subnetMask().toString() + "\nDNS:  " + WiFi.dnsIP().toString() + "\n";
        s += "MAC:  " + WiFi.macAddress() + "\nChan: " + String(WiFi.channel());
    } else s = "Not connected.\nWiFi Scan -> tap a network\n-> enter password -> Connect.";
    label_set(l, s);
}

// ---------------------------------------------------------------- text tools (ping/dns/trace)
static lv_obj_t *tool_lbl, *tool_ta;
static void mk_text_tool(const char *title, const char *dflt, lv_event_cb_t go_cb, const char *btn_txt) {
    sfx_play(SFX_CLICK);
    lv_obj_t *page; lv_obj_t *c = make_page(title, &page);
    lv_obj_t *kb = mk_keyboard(page, c);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    tool_ta = lv_textarea_create(c);
    lv_textarea_set_one_line(tool_ta, true); lv_textarea_set_text(tool_ta, dflt);
    lv_obj_set_width(tool_ta, 146); lv_obj_align(tool_ta, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(tool_ta, ta_focus_cb, LV_EVENT_FOCUSED, kb);
    lv_obj_add_event_cb(tool_ta, go_cb, LV_EVENT_READY, NULL);
    lv_obj_t *go = lv_btn_create(c); lv_obj_set_size(go, 70, 36); lv_obj_align(go, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(go, go_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(go); lv_label_set_text(gl, btn_txt); lv_obj_center(gl);
    tool_lbl = mk_label(c, 224); lv_obj_align(tool_lbl, LV_ALIGN_TOP_LEFT, 0, 44);
    label_set(tool_lbl, WiFi.status() == WL_CONNECTED ? "Ready. Edit host, press button." : "Not connected - join WiFi first.");
}
static void ping_go_cb(lv_event_t *e) {
    const char *host = lv_textarea_get_text(tool_ta);
    if (WiFi.status() != WL_CONNECTED) { label_set(tool_lbl, "Not connected to WiFi."); return; }
    uint32_t ip; char ipstr[20];
    label_set(tool_lbl, String("Resolving ") + host + " ..."); lv_timer_handler();
    if (!resolve_host(host, &ip, ipstr, sizeof(ipstr))) { label_set(tool_lbl, "DNS failed"); sfx_play(SFX_FAIL); return; }
    String out = String("PING ") + host + " (" + ipstr + ")\n";
    label_set(tool_lbl, out); lv_timer_handler();
    int ok = 0, total = 4; long sum = 0;
    for (int i = 0; i < total; i++) {
        char from[20] = ""; int rtt = 0;
        int r = icmp_probe(ip, 64, 1000, from, sizeof(from), &rtt);
        if (r == 0) { out += "reply seq" + String(i) + " " + String(rtt) + " ms\n"; ok++; sum += rtt; sfx_play(SFX_CLICK); }
        else out += "seq" + String(i) + " timeout\n";
        label_set(tool_lbl, out); lv_timer_handler();
    }
    out += "---\n" + String(ok) + "/" + String(total) + " received";
    if (ok) out += ", avg " + String((int)(sum / ok)) + " ms";
    sfx_play(ok ? SFX_CONNECT : SFX_FAIL);
    label_set(tool_lbl, out);
}
static void page_ping(lv_event_t *e) { mk_text_tool("Ping", "1.1.1.1", ping_go_cb, "Ping"); }
static void dns_go_cb(lv_event_t *e) {
    const char *host = lv_textarea_get_text(tool_ta);
    if (WiFi.status() != WL_CONNECTED) { label_set(tool_lbl, "Not connected to WiFi."); return; }
    IPAddress addr;
    if (WiFi.hostByName(host, addr)) { label_set(tool_lbl, String(host) + "\n -> " + addr.toString()); sfx_play(SFX_CONNECT); }
    else { label_set(tool_lbl, String("Could not resolve\n") + host); sfx_play(SFX_FAIL); }
}
static void page_dns(lv_event_t *e) { mk_text_tool("DNS Lookup", "google.com", dns_go_cb, "Lookup"); }
static void tr_go_cb(lv_event_t *e) {
    const char *host = lv_textarea_get_text(tool_ta);
    if (WiFi.status() != WL_CONNECTED) { label_set(tool_lbl, "Not connected to WiFi."); return; }
    uint32_t ip; char ipstr[20];
    if (!resolve_host(host, &ip, ipstr, sizeof(ipstr))) { label_set(tool_lbl, "DNS failed"); return; }
    String out = String("traceroute ") + ipstr + "\n";
    label_set(tool_lbl, out); lv_timer_handler();
    for (int ttl = 1; ttl <= 15; ttl++) {
        char from[20] = ""; int rtt = 0;
        int r = icmp_probe(ip, ttl, 1000, from, sizeof(from), &rtt);
        if (r < 0) out += String(ttl) + ". *\n"; else out += String(ttl) + ". " + from + " " + String(rtt) + "ms\n";
        label_set(tool_lbl, out); lv_timer_handler();
        if (r == 0) { out += "reached.\n"; label_set(tool_lbl, out); sfx_play(SFX_CONNECT); break; }
    }
}
static void page_traceroute(lv_event_t *e) { mk_text_tool("Traceroute", "1.1.1.1", tr_go_cb, "Go"); }

// ---------------------------------------------------------------- BLE devices (from watch cache)
static lv_obj_t *ble_list, *ble_status; static uint32_t ble_seen;
static void ble_fill() {
    lv_obj_clean(ble_list);
    label_set(ble_status, String(g_ble_n) + " BLE devices (passive)");
    for (int i = 0; i < g_ble_n; i++) {
        BleRec &b = g_ble[i];
        String line = b.name[0] ? String(b.name) : String(b.mac);
        line += "\n" + String(b.rssi) + "dBm"; if (b.kind[0]) line += String("  ") + b.kind;
        lv_obj_t *it = lv_list_add_btn(ble_list, LV_SYMBOL_BLUETOOTH, line.c_str());
        if (b.alert) lv_obj_set_style_text_color(it, lv_color_hex(b.alert == 2 ? C_RED : C_GOLD2), 0);
    }
}
static void ble_poll_cb(lv_timer_t *t) { if (g_ble_gen != ble_seen) { ble_seen = g_ble_gen; ble_fill(); } }
static void page_ble(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *c = make_page("BLE", NULL);
    ble_status = mk_label(c, 224); lv_obj_align(ble_status, LV_ALIGN_TOP_LEFT, 0, 0);
    ble_list = lv_list_create(c); lv_obj_set_size(ble_list, 228, 240); lv_obj_align(ble_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ble_list, lv_color_hex(C_NAVY2), 0);
    ble_seen = g_ble_gen; ble_fill();
    if (!g_watch_on) label_set(ble_status, "Watch is OFF - turn it on to scan BLE");
    page_timer = lv_timer_create(ble_poll_cb, 500, NULL);
}

// ---------------------------------------------------------------- Watch feed
static lv_obj_t *feed_lbl; static uint32_t feed_gen;
static void feed_fill() {
    String s = "Seen: " + String(g_seen_total) + "  Cams/Flock?: " + String(g_cams_seen) + "\n";
    for (int k = 1; k <= LOG_N; k++) {
        int i = (g_log_head - k + LOG_N) % LOG_N;
        if (g_log[i].length()) s += "> " + g_log[i] + "\n";
    }
    label_set(feed_lbl, s);
}
static void feed_poll_cb(lv_timer_t *t) { if (g_log_gen != feed_gen) { feed_gen = g_log_gen; feed_fill(); } }
static void page_feed(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *c = make_page("Watch Feed", NULL);
    feed_lbl = mk_label(c, 222); lv_obj_align(feed_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    feed_gen = g_log_gen; feed_fill();
    page_timer = lv_timer_create(feed_poll_cb, 500, NULL);
}

// ---------------------------------------------------------------- Bingo card
static lv_obj_t *bingo_tiles[N_TILES], *bingo_hdr; static uint16_t bingo_shown;
static void bingo_paint() {
    uint16_t m = watch_tiles();
    for (int i = 0; i < N_TILES; i++) {
        bool on = m & (1 << i);
        lv_obj_set_style_bg_color(bingo_tiles[i], lv_color_hex(on ? C_GOLD : C_SLATE), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(bingo_tiles[i], 0), lv_color_hex(on ? C_NAVY : 0x7D8A99), 0);
    }
    label_set(bingo_hdr, String("Discovered ") + watch_tile_count() + "/16  (4/8/12/16 + line)");
    bingo_shown = m;
}
static void bingo_poll_cb(lv_timer_t *t) { if (watch_tiles() != bingo_shown) bingo_paint(); }
static void tile_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    sfx_play(SFX_CLICK);
    show_toast(String(TILE_NAMES[i]) + ((watch_tiles() & (1 << i)) ? " - FOUND" : " - not yet"), C_GOLD);
}
static void bingo_reset_cb(lv_event_t *e) { watch_reset_bingo(); bingo_paint(); show_toast("Card reset", C_RED); }
static void page_bingo(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *c = make_page("Bingo", NULL);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    bingo_hdr = mk_label(c, 228); lv_obj_align(bingo_hdr, LV_ALIGN_TOP_LEFT, 0, 0);
    for (int i = 0; i < N_TILES; i++) {
        lv_obj_t *b = lv_btn_create(c);
        lv_obj_set_size(b, 53, 50);
        lv_obj_set_pos(b, (i % 4) * 57, 22 + (i / 4) * 54);
        lv_obj_set_style_pad_all(b, 1, 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, TILE_SHORT[i]); lv_obj_center(l);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_event_cb(b, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        bingo_tiles[i] = b;
    }
    lv_obj_t *r = lv_btn_create(c); lv_obj_set_size(r, 120, 26); lv_obj_set_pos(r, 52, 240);
    lv_obj_add_event_cb(r, bingo_reset_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t *rl = lv_label_create(r); lv_label_set_text(rl, "hold: reset"); lv_obj_center(rl);
    bingo_paint();
    page_timer = lv_timer_create(bingo_poll_cb, 500, NULL);
}

// ---------------------------------------------------------------- Attack Lab
static lv_obj_t *atk_status_lbl, *atk_cred_list;
static void atk_status_cb(lv_timer_t *t) {
    label_set(atk_status_lbl, attack_status_line());
}
static void atk_start(AttackMode m) {
    sfx_play(SFX_CLICK);
    if (attack_running()) { attack_stop(); show_toast("Stopped", C_GOLD); return; }
    if (attack_start(m)) show_toast(String("Running: ") + attack_mode_name(), C_RED);
    else show_toast("Could not start", C_RED);
}
static void atk_mode_cb(lv_event_t *e) {
    atk_start((AttackMode)(intptr_t)lv_event_get_user_data(e));
}
static void atk_stop_cb(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    attack_stop();
    show_toast("All attacks stopped", C_GOLD);
}
static void atk_creds_cb(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *c = make_page("Captured Creds", NULL);
    atk_cred_list = lv_list_create(c);
    lv_obj_set_size(atk_cred_list, 228, 252);
    lv_obj_align(atk_cred_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(atk_cred_list, lv_color_hex(C_NAVY2), 0);
    int n = attack_cred_count();
    if (!n) lv_list_add_btn(atk_cred_list, LV_SYMBOL_WARNING, "No credentials captured yet");
    for (int i = 0; i < n; i++)
        lv_list_add_btn(atk_cred_list, LV_SYMBOL_KEYBOARD, attack_cred_line(i).c_str());
}
static void atk_clear_cb(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    attack_clear_creds();
    show_toast("Creds cleared", C_GOLD);
}
static void page_attack(lv_event_t *e) {
    sfx_play(SFX_CLICK);
    lv_obj_t *c = make_page("Attack Lab", NULL);
    atk_status_lbl = mk_label(c, 224);
    lv_obj_align(atk_status_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_color(atk_status_lbl, lv_color_hex(C_GOLD2), 0);
    label_set(atk_status_lbl, attack_status_line());

    lv_obj_t *list = lv_list_create(c);
    lv_obj_set_size(list, 228, 212);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(C_NAVY2), 0);

    struct { const char *ic; const char *txt; AttackMode m; } items[] = {
        {LV_SYMBOL_WIFI,      "Evil Portal",  ATK_PORTAL},
        {LV_SYMBOL_BLUETOOTH, "BLE Spam",     ATK_BLE},
        {LV_SYMBOL_SETTINGS,  "KARMA Lure",   ATK_KARMA},
        {LV_SYMBOL_BARS,      "Beacon Flood", ATK_BEACON},
        {LV_SYMBOL_REFRESH,   "Probe Flood",  ATK_PROBE},
        {LV_SYMBOL_EYE_OPEN,  "Client Scan",  ATK_CLIENTS},
    };
    for (auto &it : items) {
        lv_obj_t *b = lv_list_add_btn(list, it.ic, it.txt);
        lv_obj_add_event_cb(b, atk_mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)it.m);
    }
    lv_obj_t *cb = lv_list_add_btn(list, LV_SYMBOL_KEYBOARD, "Captured Creds");
    lv_obj_add_event_cb(cb, atk_creds_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sb = lv_list_add_btn(list, LV_SYMBOL_STOP, "Stop All");
    lv_obj_add_event_cb(sb, atk_stop_cb, LV_EVENT_CLICKED, NULL);

    page_timer = lv_timer_create(atk_status_cb, 500, NULL);
}

// ---------------------------------------------------------------- toggles + home
static void watch_toggle_cb(lv_event_t *e) {
    g_watch_on = !g_watch_on; sfx_play(SFX_CLICK);
    lv_label_set_text(watch_btn_lbl, g_watch_on ? "Watch: ON" : "Watch: OFF");
    if (g_watch_on) watch_scan_now();
}
static void sound_toggle_cb(lv_event_t *e) {
    sfx_set_muted(!sfx_muted()); sfx_play(SFX_CLICK);
    lv_label_set_text(sound_btn_lbl, sfx_muted() ? "Sound: OFF" : "Sound: ON");
}
static void home_stat_cb(lv_timer_t *t) {
    String s = String(g_watch_on ? LV_SYMBOL_EYE_OPEN " watching" : LV_SYMBOL_EYE_CLOSE " paused") +
               "  seen " + g_seen_total + "  cam " + g_cams_seen + "  bingo " + watch_tile_count() + "/16";
    if (WiFi.status() == WL_CONNECTED) s += "\n" LV_SYMBOL_WIFI " " + WiFi.SSID() + "  " + WiFi.localIP().toString();
    else s += "\n" LV_SYMBOL_WIFI " not connected";
    label_set(home_stat, s);
}
static void watch_timer_cb(lv_timer_t *t) { watch_tick(); }

static void build_home() {
    scr_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_home, lv_color_hex(C_NAVY), 0);
    lv_obj_clear_flag(scr_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *logo = lv_img_create(scr_home);
    lv_img_set_src(logo, &cordell_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 4, 2);
    lv_obj_t *title = lv_label_create(scr_home);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_GOLD), 0);
    lv_label_set_text(title, "CORDELL WATCH");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 64, 8);
    lv_obj_t *sub = lv_label_create(scr_home);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(C_TEXT), 0);
    lv_label_set_text(sub, "Cordell Aerial  eyes up");
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 64, 34);
    home_stat = mk_label(scr_home, 232);
    lv_obj_set_style_text_color(home_stat, lv_color_hex(C_GOLD2), 0);
    lv_obj_align(home_stat, LV_ALIGN_TOP_LEFT, 4, 60);
    lv_obj_t *list = lv_list_create(scr_home);
    lv_obj_set_size(list, 232, 220);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_color(list, lv_color_hex(C_NAVY2), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(C_GOLD), 0);
    struct { const char *ic; const char *txt; lv_event_cb_t cb; } items[] = {
        {LV_SYMBOL_EYE_OPEN, "Watch Feed", page_feed},
        {LV_SYMBOL_LIST, "Bingo Card", page_bingo},
        {LV_SYMBOL_WIFI, "WiFi Scan + Join", page_wifi_scan},
        {LV_SYMBOL_BLUETOOTH, "BLE Devices", page_ble},
        {LV_SYMBOL_BARS, "Channel Map", page_channels},
        {LV_SYMBOL_HOME, "Network Info", page_netinfo},
        {LV_SYMBOL_UPLOAD, "Ping", page_ping},
        {LV_SYMBOL_DIRECTORY, "DNS Lookup", page_dns},
        {LV_SYMBOL_SHUFFLE, "Traceroute", page_traceroute},
        {LV_SYMBOL_KEYBOARD, "Manual Join (hidden)", page_manual},
        {LV_SYMBOL_WARNING, "Attack Lab", page_attack},
    };
    for (auto &it : items) {
        lv_obj_t *b = lv_list_add_btn(list, it.ic, it.txt);
        lv_obj_add_event_cb(b, it.cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *wb = lv_list_add_btn(list, LV_SYMBOL_EYE_OPEN, "Watch: ON");
    watch_btn_lbl = lv_obj_get_child(wb, 1);
    lv_obj_add_event_cb(wb, watch_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sb = lv_list_add_btn(list, LV_SYMBOL_VOLUME_MAX, "Sound: ON");
    sound_btn_lbl = lv_obj_get_child(sb, 1);
    lv_obj_add_event_cb(sb, sound_toggle_cb, LV_EVENT_CLICKED, NULL);
    home_stat_cb(NULL);
    lv_timer_create(home_stat_cb, 1000, NULL);
}

static lv_obj_t *splash;
static void splash_done_cb(lv_timer_t *t) {
    lv_scr_load_anim(scr_home, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, true);
    watch_init(on_watch_event);
    lv_timer_create(watch_timer_cb, 250, NULL);
}
void app_start() {
    lv_disp_t *d = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(d, lv_color_hex(C_GOLD), lv_color_hex(C_NAVY2), true, &lv_font_montserrat_14);
    lv_disp_set_theme(d, th);
    build_home();
    splash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(splash, lv_color_hex(C_NAVY), 0);
    lv_obj_t *lg = lv_img_create(splash); lv_img_set_src(lg, &cordell_logo);
    lv_img_set_zoom(lg, 512);  // 2x
    lv_obj_align(lg, LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *t = lv_label_create(splash);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(C_GOLD), 0);
    lv_label_set_text(t, "CORDELL WATCH");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 50);
    lv_obj_t *s = lv_label_create(splash);
    lv_obj_set_style_text_color(s, lv_color_hex(C_TEXT), 0);
    lv_label_set_text(s, "passive RF awareness  |  Cordell Aerial");
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 78);
    lv_scr_load(splash);
    sfx_play(SFX_BOOT);
    lv_timer_t *tm = lv_timer_create(splash_done_cb, 2000, NULL);
    lv_timer_set_repeat_count(tm, 1);
}
