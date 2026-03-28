#pragma once

#include <Arduino.h>

#include <functional>

enum class ButtonId : uint8_t {
    Key1,
    Key2,
    Key3,
};

enum class ButtonPressKind : uint8_t {
    ShortPress,
    LongPress,
};

struct ButtonEvent {
    ButtonId buttonId;
    ButtonPressKind pressKind;
};

using ButtonEventCallback = std::function<void(const ButtonEvent &)>;

class ButtonController {
  public:
    void begin(ButtonEventCallback onEvent);
    bool waitForFactoryResetRequest(uint32_t holdMs) const;
    [[noreturn]] void factoryResetAndReboot() const;

  private:
    struct ButtonTracker {
        ButtonId buttonId;
        uint8_t pin;
        bool rawPressed;
        bool stablePressed;
        bool longDispatched;
        uint32_t lastRawChangeAtMs;
        uint32_t pressedAtMs;
    };

    static void taskEntry(void *context);
    void runTask();
    void dispatchEvent(const ButtonEvent &event) const;
    void configureInputs() const;
    bool isButtonPressed(uint8_t pin) const;

    ButtonEventCallback onEvent_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
};
