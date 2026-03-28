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

    buttonController_.begin();
    ledController_.begin();

    if (buttonController_.waitForFactoryResetRequest(kFactoryResetHoldMs)) {
        buttonController_.factoryResetAndReboot();
    }

    (void)mediaService_.begin();

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
}

void AppController::bootSoundTaskEntry(void *context) {
    static_cast<AppController *>(context)->runBootSoundTask();
}

void AppController::runBootSoundTask() {
    vTaskDelay(pdMS_TO_TICKS(kBootSoundDelayMs));
    mediaService_.playBootSound();
    vTaskDelete(nullptr);
}
