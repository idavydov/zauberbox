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
    void handlePendingQrAlbumStart();
    void handleButtonEvent(const ButtonEvent &event);
    bool handleQrAlbumScanned(const String &albumId);

    ButtonController buttonController_;
    LedController ledController_;
    MediaService mediaService_;
    QrService qrService_;
    WifiService wifiService_;
    String pendingQrAlbumId_;
    uint32_t pendingQrAlbumStartAtMs_ = 0;
    bool resumeScanningAfterQrError_ = false;
    uint32_t resumeScanningReadyAtMs_ = 0;
};
