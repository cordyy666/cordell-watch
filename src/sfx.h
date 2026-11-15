#pragma once
#include <Arduino.h>
enum SfxId { SFX_BOOT, SFX_PING, SFX_CAMERA, SFX_TILE, SFX_ACHIEVE, SFX_CLICK, SFX_CONNECT, SFX_FAIL };
bool sfx_init();          // call AFTER touch init (shares I2C bus 16/15 via Wire)
void sfx_play(SfxId id);  // non-blocking, queued
void sfx_set_muted(bool m);
bool sfx_muted();
