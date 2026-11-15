// Retro 8-bit SFX on FNK0104B onboard audio: ES8311 codec (I2C 0x18 on SDA16/SCL15,
// shared with FT6336U touch) -> I2S (MCLK4 BCLK5 WS7 DOUT8 DIN6) -> amp (enable GPIO1, active LOW).
// Pins from Freenove Sketch_07.1_Music (FNK0104AB branch). Square/pulse-wave synth.
#include "sfx.h"
#include <Wire.h>
#include "ESP_I2S.h"

#define I2S_MCK 4
#define I2S_BCK 5
#define I2S_DIN 6
#define I2S_DOUT 8
#define I2S_WS 7
#define AMP_EN 1
#define ES_ADDR 0x18
#define SR 16000

static I2SClass i2s;
static QueueHandle_t q;
static volatile bool muted = false;
static bool ok = false;

static bool wr(uint8_t r, uint8_t v) {
    Wire.beginTransmission(ES_ADDR); Wire.write(r); Wire.write(v);
    return Wire.endTransmission() == 0;
}

static bool es8311_setup() {
    // 16kHz, MCLK = 256*fs = 4.096MHz from MCLK pin (coeff row {4096000,16000,...})
    if (!wr(0x00, 0x1F)) return false;
    delay(20);
    wr(0x00, 0x00); wr(0x00, 0x80);        // power on, slave
    wr(0x01, 0x3F);                         // clocks on, MCLK from pin
    wr(0x02, 0x00); wr(0x03, 0x10); wr(0x04, 0x10); wr(0x05, 0x00);
    wr(0x06, 0x03); wr(0x07, 0x00); wr(0x08, 0xFF);
    wr(0x09, 0x0C); wr(0x0A, 0x0C);         // I2S, 16-bit
    wr(0x0D, 0x01); wr(0x0E, 0x02); wr(0x12, 0x00); wr(0x13, 0x10);
    wr(0x1C, 0x6A); wr(0x37, 0x08);
    wr(0x32, 0xBF);                         // DAC volume (~75%)
    wr(0x31, 0x00);                         // unmute
    return true;
}

struct Note { uint16_t hz; uint16_t ms; uint8_t duty; }; // hz 0 = rest
#define N(h,m) {h,m,2}
static const Note s_boot[]    = {N(523,70),N(659,70),N(784,70),N(1047,90),{0,40,2},N(784,70),{1047,220,1},{0,0,0}};
static const Note s_ping[]    = {{1760,30,1},{2637,55,2},{0,0,0}};
static const Note s_camera[]  = {{988,90,2},{659,90,2},{988,90,2},{659,90,2},{988,90,2},{659,90,2},{220,180,1},{0,0,0}};
static const Note s_tile[]    = {N(659,60),N(784,60),N(1319,60),N(1047,60),N(1175,60),{1568,140,1},{0,0,0}};
static const Note s_achieve[] = {N(523,70),N(659,70),N(784,70),N(1047,70),N(1319,70),N(1568,70),{0,60,2},
                                 {1568,90,1},{0,30,2},{1568,90,1},{0,30,2},{2093,380,1},{0,0,0}};
static const Note s_click[]   = {{3136,12,2},{0,0,0}};
static const Note s_connect[] = {N(784,70),N(1175,70),{1568,160,1},{0,0,0}};
static const Note s_fail[]    = {N(392,120),N(330,120),{262,260,1},{0,0,0}};
static const Note *tunes[] = {s_boot, s_ping, s_camera, s_tile, s_achieve, s_click, s_connect, s_fail};

static void play(const Note *n) {
    static int16_t buf[256 * 2];
    const int16_t amp = 5000;
    for (; n->ms; n++) {
        uint32_t total = (uint32_t)SR * n->ms / 1000, done = 0;
        uint32_t period = n->hz ? SR / n->hz : 0;
        uint32_t high = period ? (period * (n->duty == 1 ? 1 : 2)) / 4 : 0; // 25% or 50% pulse
        uint32_t ph = 0;
        while (done < total) {
            int cnt = min<uint32_t>(256, total - done);
            for (int i = 0; i < cnt; i++) {
                int16_t v = 0;
                if (period) {
                    v = (ph < high) ? amp : -amp;
                    uint32_t rem = total - done - i;          // short fade-out to avoid clicks
                    if (rem < 64) v = v * (int32_t)rem / 64;
                    if (++ph >= period) ph = 0;
                }
                buf[2 * i] = v; buf[2 * i + 1] = v;
            }
            i2s.write((uint8_t *)buf, cnt * 4);
            done += cnt;
        }
    }
    memset(buf, 0, sizeof(buf));
    i2s.write((uint8_t *)buf, sizeof(buf));
}

static void sfx_task(void *) {
    uint8_t id;
    for (;;) {
        if (xQueueReceive(q, &id, portMAX_DELAY) == pdTRUE) {
            if (!muted && id < sizeof(tunes) / sizeof(tunes[0])) play(tunes[id]);
        }
    }
}

bool sfx_init() {
    pinMode(AMP_EN, OUTPUT);
    digitalWrite(AMP_EN, LOW);  // amp on (active LOW per Freenove)
    i2s.setPins(I2S_BCK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCK);
    if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("[sfx] I2S init failed"); return false;
    }
    delay(10);
    ok = es8311_setup();
    Serial.printf("[sfx] ES8311 %s\n", ok ? "ok" : "NOT FOUND");
    q = xQueueCreate(8, 1);
    xTaskCreatePinnedToCore(sfx_task, "sfx", 4096, NULL, 2, NULL, 0);
    return ok;
}
void sfx_play(SfxId id) { if (q) { uint8_t v = id; xQueueSend(q, &v, 0); } }
void sfx_set_muted(bool m) { muted = m; }
bool sfx_muted() { return muted; }
