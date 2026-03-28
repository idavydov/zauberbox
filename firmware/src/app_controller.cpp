#include "app_controller.h"

#include "app_state.h"
#include "audio_driver.h"

namespace {

constexpr uint32_t kBootSoundDelayMs = 900;

} // namespace

void AppController::begin() {
    appStateStore().init();

    buttonController_.begin();
    ledController_.begin();
    wifiService_.begin(audioPlayWifiConnectedSound);

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

    Serial.println("Starting WiFi Config...");
    wifiService_.runStartup();
}

void AppController::update() {
    wifiService_.update();
}

void AppController::bootSoundTaskEntry(void *context) {
    static_cast<AppController *>(context)->runBootSoundTask();
}

void AppController::runBootSoundTask() {
    vTaskDelay(pdMS_TO_TICKS(kBootSoundDelayMs));
    audioPlayBootSound();
    vTaskDelete(nullptr);
}
