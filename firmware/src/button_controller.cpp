#include "button_controller.h"

#include "app_state.h"
#include "config_service.h"
#include "io_expander.h"

namespace {

constexpr uint32_t kKeyHoldMs = 3000;

} // namespace

void ButtonController::begin() {
    configureKey1Input();

    if (!resetTaskHandle_) {
        xTaskCreatePinnedToCore(resetTaskEntry,
                                "KEY1_Task",
                                4096,
                                this,
                                2,
                                &resetTaskHandle_,
                                1);
    }
}

void ButtonController::resetTaskEntry(void *context) {
    static_cast<ButtonController *>(context)->runResetTask();
}

void ButtonController::runResetTask() {
    uint32_t pressedAt = 0;

    while (true) {
        if (!appStateStore().allowsFactoryReset()) {
            pressedAt = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (isKey1Pressed()) {
            if (pressedAt == 0) {
                pressedAt = millis();
            } else if (millis() - pressedAt >= kKeyHoldMs) {
                eraseWifiCredentialsAndReboot();
            }
        } else {
            pressedAt = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void ButtonController::configureKey1Input() const {
    if (!ioExpanderPinMode(kIoExpanderKey1Pin, INPUT)) {
        Serial.println("KEY1 config failed.");
    }
}

bool ButtonController::isKey1Pressed() const {
    bool levelHigh = true;
    if (!ioExpanderDigitalRead(kIoExpanderKey1Pin, &levelHigh)) {
        return false;
    }
    return !levelHigh;
}

[[noreturn]] void ButtonController::eraseWifiCredentialsAndReboot() const {
    appStateStore().requestFactoryReset();
    const FactoryResetReport report = configService().eraseFactoryData();
    Serial.printf("Factory reset: removed=%u missing=%u failed=%u\n",
                  report.removedCount,
                  report.missingCount,
                  report.failedCount);

    const uint32_t rebootAt = millis() + 3000;
    while (millis() < rebootAt) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP.restart();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
