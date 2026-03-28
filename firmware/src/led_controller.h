#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class LedController {
  public:
    LedController();
    void begin();

  private:
    static void taskEntry(void *context);
    void runTask();

    Adafruit_NeoPixel ring_;
    TaskHandle_t taskHandle_ = nullptr;
};
