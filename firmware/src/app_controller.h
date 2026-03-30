#pragma once

#include <Arduino.h>

#include "button_controller.h"
#include "led_controller.h"
#include "media_service.h"
#include "qr_service.h"
#include "web_server_service.h"
#include "wifi_service.h"

class AppController {
  public:
    void begin();
    void update();

  private:
    void handlePendingButtonEvents();
    void handleWifiConnected();
    void handleWifiConnectionFailed(bool reopenPortal);
    void handlePendingWifiPortalResume();
    void handleScanAudioState();
    void handlePendingQuietUiSound();
    void handleQuietStateAudioOutput();
    void handlePendingQrAlbumStart();
    void handleButtonEvent(const ButtonEvent &event);
    bool handleQrAlbumScanned(const String &albumId);
    void noteUiSoundQueued(uint32_t holdMs = 250);
    bool queueScanUiSound(UiSound sound);
    bool queueQuietUiSound(UiSound sound);

    ButtonController buttonController_;
    LedController ledController_;
    MediaService mediaService_;
    QrService qrService_;
    WebServerService webServerService_;
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
    bool pendingScanUiSound_ = false;
    UiSound pendingScanUiSoundType_ = UiSound::Button;
    uint32_t pendingScanUiSoundReadyAtMs_ = 0;
    bool pendingQuietUiSound_ = false;
    UiSound pendingQuietUiSoundType_ = UiSound::Button;
    uint32_t pendingQuietUiSoundReadyAtMs_ = 0;
    bool resumeWifiPortalAfterError_ = false;
    bool wifiFailureSoundRunningSeen_ = false;
    uint32_t wifiPortalResumeFallbackAtMs_ = 0;
    bool quietStateSpeakerMuted_ = false;
    uint32_t uiSoundMuteBlockUntilMs_ = 0;
};
