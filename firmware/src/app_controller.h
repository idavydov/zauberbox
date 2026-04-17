#pragma once

#include <Arduino.h>

#include "battery_service.h"
#include "button_controller.h"
#include "led_controller.h"
#include "media_service.h"
#include "qr_service.h"
#include "web_server_service.h"
#include "wifi_service.h"

class AppController {
  public:
    enum class SleepTrigger : uint8_t {
        None,
        IdleTimeout,
        CriticalBattery,
    };

    void begin();
    void update();

  private:
    void handlePendingButtonEvents();
    void handleWifiConnected();
    void handleWifiConnectionFailed(bool reopenPortal);
    void handlePendingWifiPortalResume();
    void handleScanAudioState();
    void handlePendingMutedUiSound();
    void handleMutedStateAudioOutput();
    void handlePendingQrAlbumStart();
    void handleBatteryPowerPolicy();
    void handleSleepState();
    void handleButtonEvent(const ButtonEvent &event);
    bool handleQrAlbumScanned(const String &albumId);
    void noteUiSoundQueued(uint32_t holdMs = 250);
    bool queueMutedUiSound(UiSound sound);
    bool shouldMuteOutputInCurrentState() const;
    bool canUseBatteryPolicy(const BatterySnapshot &battery) const;
    bool shouldEnterCriticalBatterySleep(const BatterySnapshot &battery, AppState state) const;
    void requestSleep(SleepTrigger trigger, const BatterySnapshot *battery = nullptr);
    void enterDeepSleep();

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
    uint32_t scanStartChimePlaybackWaitUntilMs_ = 0;
    bool scanStartChimeQueued_ = false;
    bool scanStartChimePlaybackSeen_ = false;
    bool speakerMuted_ = false;
    bool pendingMutedUiSound_ = false;
    UiSound pendingMutedUiSoundType_ = UiSound::Button;
    uint32_t pendingMutedUiSoundReadyAtMs_ = 0;
    bool resumeWifiPortalAfterError_ = false;
    bool wifiFailureSoundRunningSeen_ = false;
    uint32_t wifiPortalResumeFallbackAtMs_ = 0;
    uint32_t uiSoundMuteBlockUntilMs_ = 0;
    AppState lastObservedState_ = AppState::Boot;
    uint32_t idleEnteredAtMs_ = 0;
    SleepTrigger sleepTrigger_ = SleepTrigger::None;
    bool suppressBootWakeButtonCycle_ = false;
    uint32_t bootWakeButtonSuppressionUntilMs_ = 0;
};
