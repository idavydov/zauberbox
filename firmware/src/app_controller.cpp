#include "app_controller.h"

#include <ESPmDNS.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "app_state.h"
#include "audio_driver.h"
#include "battery_policy.h"
#include "battery_service.h"
#include "config_service.h"
#include "debug_log.h"

namespace {

constexpr uint32_t kFactoryResetHoldMs = 3000;
constexpr uint32_t kScanStartAudioReadyDelayMs = 500;
constexpr uint32_t kGeneralAudioReadyDelayMs = 50;
constexpr uint32_t kQrErrorResumeDelayMs = 250;
constexpr uint32_t kScanStartSpeakerHoldMs = 1500;
constexpr uint32_t kScanStartPlaybackStartFallbackMs = 3000;
constexpr uint32_t kWifiPortalResumeFallbackMs = 5000;
constexpr uint32_t kBootWakeButtonSuppressionMs = 1500;
constexpr char kWifiMdnsHostname[] = "zauberbox";

const char *sleepTriggerName(AppController::SleepTrigger trigger) {
    switch (trigger) {
        case AppController::SleepTrigger::None:
            return "none";
        case AppController::SleepTrigger::IdleTimeout:
            return "idle_timeout";
        case AppController::SleepTrigger::PausedTimeout:
            return "paused_timeout";
        case AppController::SleepTrigger::CriticalBattery:
            return "critical_battery";
    }

    return "unknown";
}

} // namespace

void AppController::begin() {
    policyStartedAtMs_ = millis();
    const esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    if (wakeupCause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        Serial.printf("App controller: wakeup cause=%d\n", static_cast<int>(wakeupCause));
    }
    suppressBootWakeButtonCycle_ = wakeupCause == ESP_SLEEP_WAKEUP_EXT0;
    bootWakeButtonSuppressionUntilMs_ =
        suppressBootWakeButtonCycle_ ? millis() + kBootWakeButtonSuppressionMs : 0;
    if (suppressBootWakeButtonCycle_) {
        Serial.println("App controller: suppressing initial BOOT button cycle after deep-sleep wake.");
    }

    appStateStore().init();
    configService().begin();
    batteryService().begin();

    buttonController_.begin();
    ledController_.begin();

    if (buttonController_.waitForFactoryResetRequest(kFactoryResetHoldMs)) {
        buttonController_.factoryResetAndReboot();
        return;
    }

    (void)mediaService_.begin();
#if defined(ZAUBERBOX_INPUT_RC522)
    albumInputService_ = &rc522Service_;
    (void)rc522Service_.begin([this](const String &albumId) {
        return handleAlbumSelected(albumId);
    });
    webServerService_.begin(&mediaService_, nullptr);
#else
    albumInputService_ = &qrService_;
    qrService_.setMediaService(&mediaService_);
    (void)qrService_.begin([this](const String &albumId) {
        return handleAlbumSelected(albumId);
    });
    webServerService_.begin(&mediaService_, &qrService_);
#endif
    wifiService_.begin([this]() {
        handleWifiConnected();
    }, [this](bool reopenPortal) {
        handleWifiConnectionFailed(reopenPortal);
    });

    appStateStore().completeBoot();
}

void AppController::update() {
    if (suppressBootWakeButtonCycle_ &&
        bootWakeButtonSuppressionUntilMs_ != 0 &&
        millis() >= bootWakeButtonSuppressionUntilMs_) {
        suppressBootWakeButtonCycle_ = false;
        bootWakeButtonSuppressionUntilMs_ = 0;
        Serial.println("App controller: BOOT wake suppression window expired.");
    }

    handlePendingButtonEvents();
    batteryService().update();
    wifiService_.update();
    webServerService_.update();
    if (albumInputService_) {
        albumInputService_->update();
    }
    handlePendingWifiPortalResume();
    handleScanAudioState();
    handlePendingMutedUiSound();
    handleMutedStateAudioOutput();
    handlePendingAlbumStart();
    mediaService_.update();
    handleLowBatteryPlaybackWarning();
    handleBatteryPowerPolicy();
    handleSleepState();
}

void AppController::handlePendingButtonEvents() {
    ButtonEvent event = {};
    while (buttonController_.pollEvent(&event)) {
        handleButtonEvent(event);
    }
}

void AppController::handleButtonEvent(const ButtonEvent &event) {
    if (event.buttonId == ButtonId::Boot && suppressBootWakeButtonCycle_) {
        if (event.pressKind == ButtonPressKind::ShortPress ||
            event.pressKind == ButtonPressKind::LongPress) {
            suppressBootWakeButtonCycle_ = false;
            bootWakeButtonSuppressionUntilMs_ = 0;
            Serial.println("App controller: BOOT wake button cycle consumed.");
        }
        return;
    }

    if (event.buttonId == ButtonId::Boot) {
        if (event.pressKind == ButtonPressKind::ShortPress) {
            (void)playUiSoundForCurrentState(UiSound::Button, kScanStartSpeakerHoldMs);
            Serial.printf("App controller: BOOT button in state %s, Wi-Fi %s\n",
                          AppStateStore::stateName(appStateStore().current()),
                          wifiService_.isEnabled() ? "enabled" : "disabled");
            if (wifiService_.isEnabled()) {
                wifiService_.disable();
            } else {
                (void)wifiService_.enable();
            }
            return;
        }
        return;
    }

    const AppState state = appStateStore().current();
    if (state == AppState::Idle) {
        if (event.buttonId == ButtonId::Key2 &&
            event.pressKind == ButtonPressKind::LongPress) {
            (void)appStateStore().transitionTo(AppState::QrScan);
        }
        return;
    }

    if (state != AppState::Playing && state != AppState::Paused) {
        return;
    }

    switch (event.buttonId) {
        case ButtonId::Boot:
            break;
        case ButtonId::Key1:
            if (event.pressKind == ButtonPressKind::ShortPress) {
                (void)mediaService_.previousTrackOrRestart();
            } else if (event.pressKind == ButtonPressKind::LongPress ||
                       event.pressKind == ButtonPressKind::Repeat) {
                (void)mediaService_.changeVolume(-1);
            }
            break;
        case ButtonId::Key2:
            if (event.pressKind == ButtonPressKind::ShortPress) {
                (void)mediaService_.togglePause();
            } else if (event.pressKind == ButtonPressKind::LongPress) {
                (void)mediaService_.stopAlbum();
            }
            break;
        case ButtonId::Key3:
            if (event.pressKind == ButtonPressKind::ShortPress) {
                (void)mediaService_.nextTrack();
            } else if (event.pressKind == ButtonPressKind::LongPress ||
                       event.pressKind == ButtonPressKind::Repeat) {
                (void)mediaService_.changeVolume(1);
            }
            break;
    }
}

void AppController::handleWifiConnected() {
    (void)playUiSoundForCurrentState(UiSound::WifiConnected, kScanStartSpeakerHoldMs);
    MDNS.end();
    if (MDNS.begin(kWifiMdnsHostname)) {
        Serial.printf("App controller: mDNS started at http://%s.local\n", kWifiMdnsHostname);
    } else {
        Serial.printf("App controller: mDNS start failed for %s.local\n", kWifiMdnsHostname);
    }
}

void AppController::handleWifiConnectionFailed(bool reopenPortal) {
    (void)playUiSoundForCurrentState(UiSound::Error, kScanStartSpeakerHoldMs);
    resumeWifiPortalAfterError_ = reopenPortal;
    wifiFailureSoundRunningSeen_ = false;
    wifiPortalResumeFallbackAtMs_ = millis() + kWifiPortalResumeFallbackMs;
}

void AppController::handlePendingWifiPortalResume() {
    if (!resumeWifiPortalAfterError_) {
        return;
    }

    if (audioIsRunning()) {
        wifiFailureSoundRunningSeen_ = true;
        return;
    }

    if (!wifiFailureSoundRunningSeen_ && millis() < wifiPortalResumeFallbackAtMs_) {
        return;
    }

    resumeWifiPortalAfterError_ = false;
    wifiFailureSoundRunningSeen_ = false;
    wifiPortalResumeFallbackAtMs_ = 0;
    wifiService_.resumePortalAfterFailure();
}

bool AppController::handleAlbumSelected(const String &albumId) {
    if (albumId.isEmpty()) {
        return false;
    }

    if (!pendingAlbumId_.isEmpty()) {
        Serial.printf("App controller: album already pending, ignoring %s\n",
                      albumId.c_str());
        return false;
    }

    pendingAlbumId_ = albumId;
    pendingAlbumStartAtMs_ = 0;
    pendingAlbumRequiresInputStop_ =
        albumInputService_ != nullptr &&
        albumInputService_->stopsBeforePlayback() &&
        appStateStore().current() == AppState::QrScan;
    resumeSelectionAfterError_ = false;
    resumeSelectionReadyAtMs_ = 0;

    if (pendingAlbumRequiresInputStop_) {
        if (!appStateStore().transitionTo(AppState::Idle)) {
            pendingAlbumId_ = "";
            pendingAlbumRequiresInputStop_ = false;
            return false;
        }

        Serial.printf("App controller: queued album %s for playback after input shutdown.\n",
                      albumId.c_str());
        return true;
    }

    Serial.printf("App controller: queued album %s for immediate playback.\n",
                  albumId.c_str());
    return true;
}

void AppController::noteUiSoundQueued(uint32_t holdMs) {
    uiSoundMuteBlockUntilMs_ = millis() + holdMs;
}

bool AppController::queueMutedUiSound(UiSound sound) {
    if (!shouldMuteOutputInCurrentState() || !speakerMuted_) {
        return false;
    }

    pendingMutedUiSound_ = true;
    pendingMutedUiSoundType_ = sound;
    pendingMutedUiSoundReadyAtMs_ = 1;
    speakerMuted_ = false;
    return true;
}

bool AppController::playUiSoundForCurrentState(UiSound sound, uint32_t mutedHoldMs) {
    if (mediaService_.hasActiveAlbum()) {
        const bool success = mediaService_.playTransientUiSoundOverAlbum(sound);
        if (success && shouldMuteOutputInCurrentState()) {
            noteUiSoundQueued(mutedHoldMs);
        }
        return success;
    }

    if (queueMutedUiSound(sound)) {
        return true;
    }

    if (!mediaService_.playUiSound(sound)) {
        return false;
    }

    if (shouldMuteOutputInCurrentState()) {
        noteUiSoundQueued(mutedHoldMs);
    } else {
        noteUiSoundQueued();
    }
    return true;
}

bool AppController::shouldMuteOutputInCurrentState() const {
    const AppState state = appStateStore().current();
    if (state == AppState::QrScan) {
        return albumInputService_ != nullptr &&
               albumInputService_->stopsBeforePlayback() &&
               albumInputService_->isSelectionActive() &&
               pendingAlbumId_.isEmpty();
    }

    if (state == AppState::WifiPortal) {
        return !audioIsRunning();
    }

    return state == AppState::Idle ||
           state == AppState::DebugCameraPreview ||
           state == AppState::Paused;
}

void AppController::handleScanAudioState() {
#if defined(ZAUBERBOX_INPUT_RC522)
    lastSelectionActive_ = false;
    scanStartChimeReadyAtMs_ = 0;
    scanStartChimeMuteReadyAtMs_ = 0;
    scanStartChimePlaybackWaitUntilMs_ = 0;
    scanStartChimeQueued_ = false;
    scanStartChimePlaybackSeen_ = false;
    return;
#else
    const bool selectionActive =
        albumInputService_ != nullptr && albumInputService_->isSelectionActive();
    if (selectionActive != lastSelectionActive_) {
        lastSelectionActive_ = selectionActive;
        scanStartChimeReadyAtMs_ = selectionActive ? 1 : 0;
        scanStartChimeMuteReadyAtMs_ = 0;
        scanStartChimePlaybackWaitUntilMs_ = 0;
        scanStartChimeQueued_ = false;
        scanStartChimePlaybackSeen_ = false;
    }

    if (!selectionActive || appStateStore().current() != AppState::QrScan || !pendingAlbumId_.isEmpty()) {
        return;
    }

    if (scanStartChimeReadyAtMs_ == 1) {
        if (!audioInit()) {
            Serial.println("App controller: audio init for scan-start chime failed.");
            return;
        }
        scanStartChimeReadyAtMs_ = millis() + kScanStartAudioReadyDelayMs;
        return;
    }

    if (!scanStartChimeQueued_) {
        if (scanStartChimeReadyAtMs_ == 0 || millis() < scanStartChimeReadyAtMs_) {
            return;
        }
        if (!mediaService_.playUiSound(UiSound::ScanStart)) {
            Serial.println("App controller: failed to queue scan-start chime.");
            speakerMuted_ = audioDisableOutputForCameraScan();
            return;
        }

        scanStartChimeQueued_ = true;
        scanStartChimePlaybackSeen_ = false;
        scanStartChimeMuteReadyAtMs_ = 0;
        scanStartChimePlaybackWaitUntilMs_ = millis() + kScanStartPlaybackStartFallbackMs;
        scanStartChimeReadyAtMs_ = 0;
        return;
    }

    if (!scanStartChimePlaybackSeen_) {
        if (audioIsRunning()) {
            scanStartChimePlaybackSeen_ = true;
            scanStartChimePlaybackWaitUntilMs_ = 0;
            scanStartChimeMuteReadyAtMs_ = millis() + kScanStartSpeakerHoldMs;
            Serial.printf("[%lu] App controller: scan_start playback observed; mute scheduled for %lu.\n",
                          static_cast<unsigned long>(millis()),
                          static_cast<unsigned long>(scanStartChimeMuteReadyAtMs_));
            return;
        }

        if (scanStartChimePlaybackWaitUntilMs_ != 0 &&
            millis() >= scanStartChimePlaybackWaitUntilMs_) {
            Serial.printf("[%lu] App controller: scan_start playback was never observed; muting speaker on fallback.\n",
                          static_cast<unsigned long>(millis()));
            speakerMuted_ = audioDisableOutputForCameraScan();
            scanStartChimeQueued_ = false;
            scanStartChimePlaybackSeen_ = false;
            scanStartChimePlaybackWaitUntilMs_ = 0;
        }
        return;
    }

    if (audioIsRunning()) {
        return;
    }

    if (scanStartChimeMuteReadyAtMs_ != 0 &&
        millis() >= scanStartChimeMuteReadyAtMs_) {
        speakerMuted_ = audioDisableOutputForCameraScan();
        scanStartChimeMuteReadyAtMs_ = 0;
        scanStartChimeQueued_ = false;
        scanStartChimePlaybackSeen_ = false;
    }
#endif
}

void AppController::handlePendingMutedUiSound() {
    if (!pendingMutedUiSound_) {
        return;
    }

    if (pendingMutedUiSoundReadyAtMs_ == 1) {
        if (!audioInit()) {
            Serial.println("App controller: audio init for muted UI sound failed.");
            return;
        }
        pendingMutedUiSoundReadyAtMs_ = millis() + kGeneralAudioReadyDelayMs;
        return;
    }

    if (millis() < pendingMutedUiSoundReadyAtMs_) {
        return;
    }

    if (!mediaService_.playUiSound(pendingMutedUiSoundType_)) {
        Serial.println("App controller: failed to queue muted UI sound.");
        pendingMutedUiSound_ = false;
        pendingMutedUiSoundReadyAtMs_ = 0;
        speakerMuted_ = audioDisableOutputForCameraScan();
        return;
    }

    pendingMutedUiSound_ = false;
    pendingMutedUiSoundReadyAtMs_ = 0;
    noteUiSoundQueued(kScanStartSpeakerHoldMs);
}

void AppController::handleMutedStateAudioOutput() {
    if (!shouldMuteOutputInCurrentState()) {
        speakerMuted_ = false;
        pendingMutedUiSound_ = false;
        pendingMutedUiSoundReadyAtMs_ = 0;
        return;
    }

    if (pendingMutedUiSound_ ||
        scanStartChimeReadyAtMs_ != 0 ||
        scanStartChimeQueued_ ||
        scanStartChimeMuteReadyAtMs_ != 0 ||
        scanStartChimePlaybackWaitUntilMs_ != 0) {
        return;
    }

    if (audioIsRunning()) {
        speakerMuted_ = false;
        return;
    }

    if (speakerMuted_) {
        return;
    }

    if (millis() < uiSoundMuteBlockUntilMs_) {
        return;
    }

    speakerMuted_ = audioDisableOutputForCameraScan();
}

void AppController::handlePendingAlbumStart() {
    if (resumeSelectionAfterError_) {
        if (millis() >= resumeSelectionReadyAtMs_ && !audioIsRunning()) {
            resumeSelectionAfterError_ = false;
            resumeSelectionReadyAtMs_ = 0;
            (void)appStateStore().transitionTo(AppState::QrScan);
        }
    }

    if (pendingAlbumId_.isEmpty()) {
        return;
    }

    if (pendingAlbumRequiresInputStop_ &&
        albumInputService_ != nullptr &&
        albumInputService_->isSelectionActive()) {
        return;
    }

    if (pendingAlbumStartAtMs_ == 0) {
        if (!audioInit()) {
            Serial.println("App controller: audio init before album playback failed.");
            pendingAlbumId_ = "";
            if (pendingAlbumRequiresInputStop_) {
                resumeSelectionAfterError_ = true;
                resumeSelectionReadyAtMs_ = millis() + kQrErrorResumeDelayMs;
            }
            pendingAlbumRequiresInputStop_ = false;
            return;
        }

        pendingAlbumStartAtMs_ = millis() + kGeneralAudioReadyDelayMs;
        return;
    }

    if (millis() < pendingAlbumStartAtMs_) {
        return;
    }

    const String albumId = pendingAlbumId_;
    const bool resumeSelectionAfterFailure = pendingAlbumRequiresInputStop_;
    pendingAlbumId_ = "";
    pendingAlbumStartAtMs_ = 0;
    pendingAlbumRequiresInputStop_ = false;

    if (!mediaService_.playAlbum(albumId.c_str())) {
        if (resumeSelectionAfterFailure) {
            resumeSelectionAfterError_ = true;
            resumeSelectionReadyAtMs_ = millis() + kQrErrorResumeDelayMs;
        }
    }
}

void AppController::handleLowBatteryPlaybackWarning() {
    const AppState state = appStateStore().current();
    if (state != AppState::Playing &&
        state != AppState::Paused &&
        state != AppState::QrScan) {
        nextLowBatteryPlaybackBeepAtMs_ = 0;
        return;
    }

    const BatterySnapshot battery = batteryService().snapshot();
    if (!canUseBatteryPolicy(battery) || !battery.low || battery.critical) {
        nextLowBatteryPlaybackBeepAtMs_ = 0;
        return;
    }

    if (nextLowBatteryPlaybackBeepAtMs_ == 0) {
        nextLowBatteryPlaybackBeepAtMs_ = millis() + BatteryPolicy::kLowBatteryPlaybackBeepIntervalMs;
        return;
    }

    if (static_cast<int32_t>(millis() - nextLowBatteryPlaybackBeepAtMs_) < 0) {
        return;
    }

    if (!playUiSoundForCurrentState(UiSound::LowBattery, kScanStartSpeakerHoldMs)) {
        nextLowBatteryPlaybackBeepAtMs_ = millis() + 5000;
        return;
    }

    Serial.printf("App controller: low-battery warning queued in %s at %umV (%u%%).\n",
                  AppStateStore::stateName(state),
                  battery.batteryMilliVolts,
                  battery.percent);
    nextLowBatteryPlaybackBeepAtMs_ = millis() + BatteryPolicy::kLowBatteryPlaybackBeepIntervalMs;
}

void AppController::handleBatteryPowerPolicy() {
    const AppState state = appStateStore().current();
    if (state != AppState::Sleep) {
        sleepTrigger_ = SleepTrigger::None;
    }

    if (state != lastObservedState_) {
        lastObservedState_ = state;
        idleEnteredAtMs_ = state == AppState::Idle ? millis() : 0;
        pausedEnteredAtMs_ = state == AppState::Paused ? millis() : 0;
    }

    if (state == AppState::Sleep || state == AppState::Resetting || state == AppState::Boot) {
        return;
    }

    const BatterySnapshot battery = batteryService().snapshot();
    if (!canUseBatteryPolicy(battery)) {
        return;
    }

    if (shouldEnterCriticalBatterySleep(battery, state)) {
        if (millis() - policyStartedAtMs_ < BatteryPolicy::kCriticalSleepSettleMs) {
            return;
        }
        requestSleep(SleepTrigger::CriticalBattery, &battery);
        return;
    }

    if (state == AppState::Paused) {
        if (pausedEnteredAtMs_ == 0) {
            pausedEnteredAtMs_ = millis();
        }
        if (millis() - pausedEnteredAtMs_ >= BatteryPolicy::kPausedSleepTimeoutMs) {
            requestSleep(SleepTrigger::PausedTimeout, &battery);
        }
        return;
    }

    if (state != AppState::Idle ||
        wifiService_.isEnabled() ||
        !pendingAlbumId_.isEmpty()) {
        return;
    }

    if (idleEnteredAtMs_ == 0) {
        idleEnteredAtMs_ = millis();
    }

    if (millis() - idleEnteredAtMs_ < BatteryPolicy::kIdleSleepTimeoutMs) {
        return;
    }

    requestSleep(SleepTrigger::IdleTimeout, &battery);
}

void AppController::handleSleepState() {
    if (appStateStore().current() != AppState::Sleep) {
        return;
    }

    if (audioIsRunning() ||
        (albumInputService_ != nullptr && albumInputService_->isHardwareActive()) ||
        pendingMutedUiSound_ ||
        scanStartChimeReadyAtMs_ != 0 ||
        scanStartChimeQueued_ ||
        scanStartChimeMuteReadyAtMs_ != 0 ||
        scanStartChimePlaybackWaitUntilMs_ != 0) {
        return;
    }

    enterDeepSleep();
}

bool AppController::canUseBatteryPolicy(const BatterySnapshot &battery) const {
    return battery.initialized &&
           battery.hasReading &&
           battery.readingAvailable &&
           battery.readingStable &&
           battery.availability == BatteryAvailability::Available;
}

bool AppController::shouldEnterCriticalBatterySleep(const BatterySnapshot &battery,
                                                    AppState state) const {
    if (!battery.critical) {
        return false;
    }

    return state == AppState::Idle ||
           state == AppState::Playing ||
           state == AppState::Paused ||
           state == AppState::QrScan ||
           state == AppState::DebugCameraPreview ||
           state == AppState::WifiPortal;
}

void AppController::requestSleep(SleepTrigger trigger, const BatterySnapshot *battery) {
    if (appStateStore().current() == AppState::Sleep) {
        return;
    }

    if (!appStateStore().transitionTo(AppState::Sleep)) {
        return;
    }

    sleepTrigger_ = trigger;

    // Immediately disable heavy components to save power and clear blockers for handleSleepState()
    webServerService_.stop();
    wifiService_.disable();
    (void)audioStopPlayback();
#if !defined(ZAUBERBOX_INPUT_RC522)
    qrService_.endDebugPreview();
#endif

    if (battery) {
        Serial.printf("App controller: sleep requested (%s) at %umV (%u%%).\n",
                      sleepTriggerName(trigger),
                      battery->batteryMilliVolts,
                      battery->percent);
    } else {
        Serial.printf("App controller: sleep requested (%s).\n",
                      sleepTriggerName(trigger));
    }
}

void AppController::enterDeepSleep() {
    Serial.printf("App controller: entering deep sleep (%s).\n",
                  sleepTriggerName(sleepTrigger_));

    wifiService_.disable();
    (void)audioStopPlayback();
    (void)audioDisableOutputForCameraScan();

    pinMode(0, INPUT_PULLUP);
    rtc_gpio_pullup_en(GPIO_NUM_0);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    delay(50);
    esp_deep_sleep_start();
}
