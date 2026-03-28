#include "button_controller.h"

#include "app_state.h"
#include "config_service.h"
#include "io_expander.h"

namespace {

constexpr uint32_t kKeyHoldMs = 3000;
constexpr uint32_t kPollIntervalMs = 50;

} // namespace

void ButtonController::begin() {
    configureKey1Input();
}

bool ButtonController::waitForFactoryResetRequest(uint32_t holdMs) const {
    const uint32_t requiredHoldMs = holdMs == 0 ? kKeyHoldMs : holdMs;
    if (!appStateStore().allowsFactoryReset() || !isKey1Pressed()) {
        return false;
    }

    const uint32_t pressedAt = millis();
    while (isKey1Pressed()) {
        if (millis() - pressedAt >= requiredHoldMs) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
    }

    return false;
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

[[noreturn]] void ButtonController::factoryResetAndReboot() const {
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
