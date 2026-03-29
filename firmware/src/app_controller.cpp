#include "app_controller.h"

#include "app_state.h"
#include "audio_driver.h"
#include "config_service.h"

namespace {

constexpr uint32_t kFactoryResetHoldMs = 3000;
constexpr uint32_t kAudioReadyAfterInitDelayMs = 1000;
constexpr uint32_t kQrErrorResumeDelayMs = 250;

} // namespace

void AppController::begin() {
    appStateStore().init();
    configService().begin();

    buttonController_.begin([this](const ButtonEvent &event) {
        handleButtonEvent(event);
    });
    ledController_.begin();

    if (buttonController_.waitForFactoryResetRequest(kFactoryResetHoldMs)) {
        buttonController_.factoryResetAndReboot();
    }

    (void)mediaService_.begin();
    (void)qrService_.begin([this](const String &albumId) {
        return handleQrAlbumScanned(albumId);
    });

    appStateStore().completeBoot();
}

void AppController::update() {
    qrService_.update();
    handlePendingQrAlbumStart();
    mediaService_.update();
}

void AppController::handleButtonEvent(const ButtonEvent &event) {
    const AppState state = appStateStore().current();
    if (state != AppState::Playing && state != AppState::Paused) {
        return;
    }

    switch (event.buttonId) {
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
