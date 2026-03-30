#include "button_controller.h"

#include "app_state.h"
#include "config_service.h"
#include "io_expander.h"

namespace {

constexpr uint32_t kKeyHoldMs = 3000;
constexpr uint32_t kPollIntervalMs = 20;
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kButtonEventQueueDepth = 8;

const char *buttonName(ButtonId buttonId) {
    switch (buttonId) {
        case ButtonId::Boot:
            return "BOOT";
        case ButtonId::Key1:
            return "KEY1";
        case ButtonId::Key2:
            return "KEY2";
        case ButtonId::Key3:
            return "KEY3";
    }

    return "UNKNOWN";
}

const char *pressKindName(ButtonPressKind pressKind) {
    switch (pressKind) {
        case ButtonPressKind::PressDown:
            return "down";
        case ButtonPressKind::ShortPress:
            return "short";
        case ButtonPressKind::LongPress:
            return "long";
    }

    return "unknown";
}

} // namespace

void ButtonController::begin() {
    configureInputs();

    if (!eventQueue_) {
        eventQueue_ = xQueueCreate(kButtonEventQueueDepth, sizeof(ButtonEvent));
        if (!eventQueue_) {
            Serial.println("Button controller: failed to create event queue.");
            return;
        }
    }

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

bool ButtonController::pollEvent(ButtonEvent *event) const {
    if (!eventQueue_ || !event) {
        return false;
    }

    return xQueueReceive(eventQueue_, event, 0) == pdPASS;
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
            .buttonId = ButtonId::Boot,
            .pin = 0,
            .rawPressed = false,
            .stablePressed = false,
            .longDispatched = false,
            .lastRawChangeAtMs = 0,
            .pressedAtMs = 0,
        },
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
            const bool rawPressed = button.buttonId == ButtonId::Boot
                                        ? isBootButtonPressed()
                                        : isButtonPressed(button.pin);
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
                    if (button.buttonId == ButtonId::Boot) {
                        dispatchEvent({
                            .buttonId = button.buttonId,
                            .pressKind = ButtonPressKind::PressDown,
                        });
                    }
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
    Serial.printf("Button event: %s %s press\n",
                  buttonName(event.buttonId),
                  pressKindName(event.pressKind));
    if (!eventQueue_) {
        return;
    }
    if (xQueueSend(eventQueue_, &event, 0) != pdPASS) {
        Serial.println("Button controller: event queue full, dropping event.");
    }
}

void ButtonController::configureInputs() const {
    pinMode(0, INPUT_PULLUP);
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

bool ButtonController::isBootButtonPressed() const {
    return digitalRead(0) == LOW;
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
