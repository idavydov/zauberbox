#pragma once

#include <Arduino.h>

class ButtonController {
  public:
    void begin();
    bool waitForFactoryResetRequest(uint32_t holdMs) const;
    [[noreturn]] void factoryResetAndReboot() const;

  private:
    void configureKey1Input() const;
    bool isKey1Pressed() const;
};
