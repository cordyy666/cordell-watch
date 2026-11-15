#include <Arduino.h>
#include <WiFi.h>
#include "freenove_display.h"
#include "sfx.h"
#include "app.h"
#include "attack.h"

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    freenove_display_init();  // ILI9341 + FT6336U (Wire on SDA16/SCL15) + LVGL
    sfx_init();               // ES8311 on the same I2C bus + I2S
    attack_init();            // Attack Lab (WiFi raw TX / portal / BLE spam)
    app_start();
    Serial.println("Cordell Watch ready");
}
void loop() {
    lv_timer_handler();
    delay(5);
}
