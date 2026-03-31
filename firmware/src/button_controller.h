#pragma once

#include <Arduino.h>
#include <freertos/queue.h>

enum class ButtonId : uint8_t {
    Boot,
    Key1,
    Key2,
    Key3,
};

enum class ButtonPressKind : uint8_t {
    PressDown,
    ShortPress,
    LongPress,
    Repeat,
};

struct ButtonEvent {
    ButtonId buttonId;
    ButtonPressKind pressKind;
};

class ButtonController {
  public:
    void begin();
    bool pollEvent(ButtonEvent *event) const;
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
        uint32_t lastRepeatAtMs;
    };

    static void taskEntry(void *context);
    void runTask();
    void dispatchEvent(const ButtonEvent &event) const;
    void configureInputs() const;
    bool isButtonPressed(uint8_t pin) const;
    bool isBootButtonPressed() const;

    QueueHandle_t eventQueue_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
};
