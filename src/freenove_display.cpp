#include "freenove_display.h"
#include <TFT_eSPI.h>
#include "FT6336U.h"

// FNK0104AB / FNK0104B capacitive-touch pins (from Freenove Sketch_11.1_Touch.ino)
#define I2C_SDA   16
#define I2C_SCL   15
#define RST_N_PIN 18
#define INT_N_PIN 17

// Panel is native 240x320 (portrait). Freenove display.h uses TFT_DIRECTION 0.
static const uint16_t screenWidth  = 240;
static const uint16_t screenHeight = 320;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10];

static TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight);
static FT6336U ft6336u(I2C_SDA, I2C_SCL, RST_N_PIN, INT_N_PIN);

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

static void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
    FT6336U_TouchPointType tp = ft6336u.scan();
    if (tp.touch_count > 0) {
        int16_t x = tp.tp[0].x;
        int16_t y = tp.tp[0].y;
        if (x < 0) x = 0; if (x >= screenWidth)  x = screenWidth - 1;
        if (y < 0) y = 0; if (y >= screenHeight) y = screenHeight - 1;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void freenove_display_init()
{
    lv_init();
    tft.begin();              // turns backlight (BL=45) on, inits ILI9341
    tft.setRotation(0);       // TFT_DIRECTION 0

    ft6336u.begin();          // I2C capacitive touch controller

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = screenWidth;
    disp_drv.ver_res  = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

void freenove_display_routine()
{
    lv_timer_handler();
}
