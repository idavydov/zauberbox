#pragma once

#include <Arduino.h>
#include "button_controller.h"
#include "led_controller.h"
#include "wifi_service.h"

class AppController {
  public:
    void begin();
    void update();

  private:
    static void bootSoundTaskEntry(void *context);
    void runBootSoundTask();

    ButtonController buttonController_;
    LedController ledController_;
    WifiService wifiService_;
    TaskHandle_t bootSoundTaskHandle_ = nullptr;
};
