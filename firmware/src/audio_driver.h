#pragma once

#include <Arduino.h>

bool audioInit();
void audioPlayBip(float frequency, int durationMs, int16_t amplitude = 4000);
