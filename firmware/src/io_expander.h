#pragma once

#include <Arduino.h>

constexpr uint8_t kIoExpanderSpeakerEnablePin = 8;
constexpr uint8_t kIoExpanderKey1Pin = 9;
constexpr uint8_t kIoExpanderKey2Pin = 10;
constexpr uint8_t kIoExpanderKey3Pin = 11;

bool ioExpanderInit();
bool ioExpanderPinMode(uint8_t pin, uint8_t mode);
bool ioExpanderDigitalWrite(uint8_t pin, uint8_t value);
bool ioExpanderDigitalRead(uint8_t pin, bool *value);
