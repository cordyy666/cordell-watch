// Freenove FNK0104B (2.8" ILI9341 + FT6336U capacitive touch) display + touch
// Pins/driver taken directly from Freenove's official FNK0104AB stack:
//   TFT: ILI9341_2_DRIVER, MISO13 MOSI11 SCLK12 CS10 DC46 RST-1 BL45(HIGH), BGR, INVERSION_ON, HSPI
//   Touch FT6336U: SDA16 SCL15 RST18 INT17  (from Sketch_11.1_Touch.ino)
#ifndef FREENOVE_DISPLAY_H
#define FREENOVE_DISPLAY_H
#include <Arduino.h>
#include <lvgl.h>

void freenove_display_init();   // init TFT + LVGL + FT6336U touch
void freenove_display_routine(); // call in loop(): lv_timer_handler()

#endif
