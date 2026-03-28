#include "button_controller.h"

#include "app_state.h"
#include "config_service.h"
#include "io_expander.h"

namespace {

constexpr uint32_t kKeyHoldMs = 3000;
constexpr uint32_t kPollIntervalMs = 20;
constexpr uint32_t kDebounceMs = 30;

} // namespace

void ButtonController::begin(ButtonEventCallback onEvent) {
    onEvent_ = onEvent;
    configureInputs();

    if (!taskHandle_) {
        xTaskCreatePinnedToCore(taskEntry,
                                "Button_Task",
                                4096,
                                this,
                                2,
                                &taskHandle_,
                                1);
    }
}

bool ButtonController::waitForFactoryResetRequest(uint32_t holdMs) const {
    const uint32_t requiredHoldMs = holdMs == 0 ? kKeyHoldMs : holdMs;
    if (!appStateStore().allowsFactoryReset() || !isButtonPressed(kIoExpanderKey1Pin)) {
        return false;
    }

    const uint32_t pressedAt = millis();
    while (isButtonPressed(kIoExpanderKey1Pin)) {
        if (millis() - pressedAt >= requiredHoldMs) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
    }

    return false;
}

void ButtonController::taskEntry(void *context) {
    static_cast<ButtonController *>(context)->runTask();
}

void ButtonController::runTask() {
    ButtonTracker buttons[] = {
        {
            .buttonId = ButtonId::Key1,
            .pin = kIoExpanderKey1Pin,
            .rawPressed = false,
            .stablePressed = false,
            .longDispatched = false,
            .lastRawChangeAtMs = 0,
            .pressedAtMs = 0,
        },
        {
            .buttonId = ButtonId::Key2,
            .pin = kIoExpanderKey2Pin,
            .rawPressed = false,
            .stablePressed = false,
            .longDispatched = false,
            .lastRawChangeAtMs = 0,
            .pressedAtMs = 0,
        },
        {
            .buttonId = ButtonId::Key3,
            .pin = kIoExpanderKey3Pin,
            .rawPressed = false,
            .stablePressed = false,
            .longDispatched = false,
            .lastRawChangeAtMs = 0,
            .pressedAtMs = 0,
        },
    };

    while (true) {
        const uint32_t now = millis();

        for (ButtonTracker &button : buttons) {
            const bool rawPressed = isButtonPressed(button.pin);
            if (rawPressed != button.rawPressed) {
                button.rawPressed = rawPressed;
                button.lastRawChangeAtMs = now;
            }

            if (now - button.lastRawChangeAtMs < kDebounceMs) {
                continue;
            }

            if (button.stablePressed != button.rawPressed) {
                button.stablePressed = button.rawPressed;
                if (button.stablePressed) {
                    button.pressedAtMs = now;
                    button.longDispatched = false;
                } else if (!button.longDispatched) {
                    dispatchEvent({
                        .buttonId = button.buttonId,
                        .pressKind = ButtonPressKind::ShortPress,
                    });
                }
            }

            if (button.stablePressed &&
                !button.longDispatched &&
                now - button.pressedAtMs >= kKeyHoldMs) {
                button.longDispatched = true;
                dispatchEvent({
                    .buttonId = button.buttonId,
                    .pressKind = ButtonPressKind::LongPress,
                });
            }
        }

        vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
    }
}

void ButtonController::dispatchEvent(const ButtonEvent &event) const {
    if (onEvent_) {
        onEvent_(event);
    }
}

void ButtonController::configureInputs() const {
    if (!ioExpanderPinMode(kIoExpanderKey1Pin, INPUT)) {
        Serial.println("KEY1 config failed.");
    }
    if (!ioExpanderPinMode(kIoExpanderKey2Pin, INPUT)) {
        Serial.println("KEY2 config failed.");
    }
    if (!ioExpanderPinMode(kIoExpanderKey3Pin, INPUT)) {
        Serial.println("KEY3 config failed.");
    }
}

bool ButtonController::isButtonPressed(uint8_t pin) const {
    bool levelHigh = true;
    if (!ioExpanderDigitalRead(pin, &levelHigh)) {
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
