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
    void handlePendingButtonEvents();
    void handleWifiConnected();
    void handleWifiConnectionFailed();
    void handlePendingWifiPortalResume();
    void handleIdleDiagnostics();
    void handleScanAudioState();
    void handlePendingQrAlbumStart();
    void handleButtonEvent(const ButtonEvent &event);
    bool handleQrAlbumScanned(const String &albumId);

    ButtonController buttonController_;
    LedController ledController_;
    MediaService mediaService_;
    QrService qrService_;
    WifiService wifiService_;
    bool lastScanning_ = false;
    String pendingQrAlbumId_;
    uint32_t pendingQrAlbumStartAtMs_ = 0;
    bool resumeScanningAfterQrError_ = false;
    uint32_t resumeScanningReadyAtMs_ = 0;
    uint32_t scanStartChimeReadyAtMs_ = 0;
    uint32_t scanStartChimeMuteReadyAtMs_ = 0;
    bool scanStartChimeQueued_ = false;
    bool scanSpeakerMutedForScan_ = false;
    bool resumeWifiPortalAfterError_ = false;
    bool wifiFailureSoundRunningSeen_ = false;
    uint32_t wifiPortalResumeFallbackAtMs_ = 0;
    uint32_t nextIdleLogAtMs_ = 0;
};
