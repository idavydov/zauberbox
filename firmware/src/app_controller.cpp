#include "app_controller.h"

#include "app_state.h"
#include "config_service.h"

namespace {

constexpr uint32_t kBootSoundDelayMs = 900;
constexpr uint32_t kFactoryResetHoldMs = 3000;

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

    if (!bootSoundTaskHandle_) {
        xTaskCreatePinnedToCore(bootSoundTaskEntry,
                                "BootSound_Task",
                                2048,
                                this,
                                1,
                                &bootSoundTaskHandle_,
                                1);
    }

    appStateStore().completeBoot();
}

void AppController::update() {
    mediaService_.update();
    qrService_.update();
}

void AppController::bootSoundTaskEntry(void *context) {
    static_cast<AppController *>(context)->runBootSoundTask();
}

void AppController::runBootSoundTask() {
    vTaskDelay(pdMS_TO_TICKS(kBootSoundDelayMs));
    mediaService_.playBootSound();
    vTaskDelete(nullptr);
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

    return mediaService_.playAlbum(albumId.c_str());
}
