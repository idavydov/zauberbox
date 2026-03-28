#pragma once

#include <Arduino.h>
#include "button_controller.h"
#include "led_controller.h"
#include "media_service.h"
#include "qr_service.h"
#include "wifi_service.h"

class AppController {
  public:
    void begin();
    void update();

  private:
    static void bootSoundTaskEntry(void *context);
    void runBootSoundTask();
    void handleButtonEvent(const ButtonEvent &event);
    bool handleQrAlbumScanned(const String &albumId);

    ButtonController buttonController_;
    LedController ledController_;
    MediaService mediaService_;
    QrService qrService_;
    WifiService wifiService_;
    TaskHandle_t bootSoundTaskHandle_ = nullptr;
};
