#include "app_controller.h"

#include <ESPmDNS.h>

#include "app_state.h"
#include "audio_driver.h"
#include "config_service.h"

namespace {

constexpr uint32_t kFactoryResetHoldMs = 3000;
constexpr uint32_t kAudioReadyAfterInitDelayMs = 1000;
constexpr uint32_t kQrErrorResumeDelayMs = 250;
constexpr uint32_t kScanStartSpeakerHoldMs = 1500;
constexpr uint32_t kWifiPortalResumeFallbackMs = 5000;
constexpr char kWifiMdnsHostname[] = "zauberbox";

} // namespace

void AppController::begin() {
    appStateStore().init();
    configService().begin();

    buttonController_.begin();
    ledController_.begin();

    if (buttonController_.waitForFactoryResetRequest(kFactoryResetHoldMs)) {
        buttonController_.factoryResetAndReboot();
    }

    (void)mediaService_.begin();
    webServerService_.begin(&mediaService_);
    (void)qrService_.begin([this](const String &albumId) {
        return handleQrAlbumScanned(albumId);
    });
    wifiService_.begin([this]() {
        handleWifiConnected();
    }, [this]() {
        handleWifiConnectionFailed();
    });

    appStateStore().completeBoot();
}

void AppController::update() {
    handlePendingButtonEvents();
    wifiService_.update();
    webServerService_.update();
    qrService_.update();
    handlePendingWifiPortalResume();
    handleScanAudioState();
    handlePendingQrAlbumStart();
    mediaService_.update();
}

void AppController::handlePendingButtonEvents() {
    ButtonEvent event = {};
    while (buttonController_.pollEvent(&event)) {
        handleButtonEvent(event);
    }
}

void AppController::handleButtonEvent(const ButtonEvent &event) {
    if (event.buttonId == ButtonId::Boot) {
        if (event.pressKind == ButtonPressKind::PressDown) {
            (void)mediaService_.playUiSound(UiSound::Button);
            return;
        }
        if (event.pressKind == ButtonPressKind::ShortPress) {
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
    if (state != AppState::Playing && state != AppState::Paused) {
        return;
    }

    switch (event.buttonId) {
        case ButtonId::Boot:
            break;
        case ButtonId::Key1:
            if (event.pressKind == ButtonPressKind::ShortPress) {
                (void)mediaService_.changeVolume(-1);
            } else {
                (void)mediaService_.previousTrackOrRestart();
            }
            break;
        case ButtonId::Key2:
            if (event.pressKind == ButtonPressKind::ShortPress) {
                (void)mediaService_.togglePause();
            } else {
                (void)mediaService_.stopAlbum();
            }
            break;
        case ButtonId::Key3:
            if (event.pressKind == ButtonPressKind::ShortPress) {
                (void)mediaService_.changeVolume(1);
            } else {
                (void)mediaService_.nextTrack();
            }
            break;
    }
}

void AppController::handleWifiConnected() {
    MDNS.end();
    if (MDNS.begin(kWifiMdnsHostname)) {
        Serial.printf("App controller: mDNS started at http://%s.local\n", kWifiMdnsHostname);
    } else {
        Serial.printf("App controller: mDNS start failed for %s.local\n", kWifiMdnsHostname);
    }
    (void)mediaService_.playWifiConnectedSound();
}

void AppController::handleWifiConnectionFailed() {
    (void)mediaService_.playUiSound(UiSound::Error);
    resumeWifiPortalAfterError_ = true;
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
    (void)wifiService_.enable();
}

bool AppController::handleQrAlbumScanned(const String &albumId) {
    if (albumId.isEmpty()) {
        return false;
    }

    if (!pendingQrAlbumId_.isEmpty()) {
        Serial.printf("App controller: QR album already pending, ignoring %s\n",
                      albumId.c_str());
        return false;
    }

    pendingQrAlbumId_ = albumId;
    pendingQrAlbumStartAtMs_ = 0;
    resumeScanningAfterQrError_ = false;
    resumeScanningReadyAtMs_ = 0;

    if (!appStateStore().transitionTo(AppState::Idle)) {
        pendingQrAlbumId_ = "";
        return false;
    }

    Serial.printf("App controller: queued QR album %s for playback after scan shutdown.\n",
                  albumId.c_str());
    return true;
}

void AppController::handleScanAudioState() {
    const bool scanning = qrService_.isScanning();
    if (scanning != lastScanning_) {
        lastScanning_ = scanning;
        scanStartChimeReadyAtMs_ = scanning ? 1 : 0;
        scanStartChimeMuteReadyAtMs_ = 0;
        scanStartChimeQueued_ = false;
        scanSpeakerMutedForScan_ = false;
    }

    if (!scanning || appStateStore().current() != AppState::QrScan || !pendingQrAlbumId_.isEmpty()) {
        return;
    }

    if (scanSpeakerMutedForScan_) {
        return;
    }

    if (scanStartChimeReadyAtMs_ == 1) {
        if (!audioInit()) {
            Serial.println("App controller: audio init for scan-start chime failed.");
            return;
        }
        scanStartChimeReadyAtMs_ = millis() + kAudioReadyAfterInitDelayMs;
        return;
    }

    if (!scanStartChimeQueued_) {
        if (scanStartChimeReadyAtMs_ == 0 || millis() < scanStartChimeReadyAtMs_) {
            return;
        }
        if (!mediaService_.playUiSound(UiSound::ScanStart)) {
            Serial.println("App controller: failed to queue scan-start chime.");
            scanSpeakerMutedForScan_ = audioDisableOutputForCameraScan();
            return;
        }

        scanStartChimeQueued_ = true;
        scanStartChimeMuteReadyAtMs_ = millis() + kScanStartSpeakerHoldMs;
        return;
    }

    if (scanStartChimeMuteReadyAtMs_ != 0 &&
        millis() >= scanStartChimeMuteReadyAtMs_) {
        scanSpeakerMutedForScan_ = audioDisableOutputForCameraScan();
        scanStartChimeMuteReadyAtMs_ = 0;
    }
}

void AppController::handlePendingQrAlbumStart() {
    if (resumeScanningAfterQrError_) {
        if (millis() >= resumeScanningReadyAtMs_ && !audioIsRunning()) {
            resumeScanningAfterQrError_ = false;
            resumeScanningReadyAtMs_ = 0;
            (void)appStateStore().transitionTo(AppState::QrScan);
        }
    }

    if (pendingQrAlbumId_.isEmpty()) {
        return;
    }

    if (qrService_.isScanning()) {
        return;
    }

    if (pendingQrAlbumStartAtMs_ == 0) {
        if (!audioInit()) {
            Serial.println("App controller: audio init before QR playback failed.");
            pendingQrAlbumId_ = "";
            resumeScanningAfterQrError_ = true;
            resumeScanningReadyAtMs_ = millis() + kQrErrorResumeDelayMs;
            return;
        }

        pendingQrAlbumStartAtMs_ = millis() + kAudioReadyAfterInitDelayMs;
        return;
    }

    if (millis() < pendingQrAlbumStartAtMs_) {
        return;
    }

    const String albumId = pendingQrAlbumId_;
    pendingQrAlbumId_ = "";
    pendingQrAlbumStartAtMs_ = 0;

    if (!mediaService_.playAlbum(albumId.c_str())) {
        resumeScanningAfterQrError_ = true;
        resumeScanningReadyAtMs_ = millis() + kQrErrorResumeDelayMs;
    }
}
