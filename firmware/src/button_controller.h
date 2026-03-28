#pragma once

#include <Arduino.h>

class ButtonController {
  public:
    void begin();

  private:
    static void resetTaskEntry(void *context);
    void runResetTask();
    void configureKey1Input() const;
    bool isKey1Pressed() const;
    [[noreturn]] void eraseWifiCredentialsAndReboot() const;

    TaskHandle_t resetTaskHandle_ = nullptr;
};
