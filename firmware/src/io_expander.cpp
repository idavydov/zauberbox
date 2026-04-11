#include "io_expander.h"

#include "debug_log.h"

#include <TCA9555.h>
#include <Wire.h>
#include "freertos/semphr.h"

namespace {

constexpr uint8_t kIoExpanderAddress = 0x20;

TCA9555 gIoExpander(kIoExpanderAddress, &Wire);
SemaphoreHandle_t gIoExpanderMutex = nullptr;
bool gIoExpanderInitialized = false;

bool lockIoExpander() {
    if (!gIoExpanderMutex) {
        gIoExpanderMutex = xSemaphoreCreateMutex();
    }
    return gIoExpanderMutex && xSemaphoreTake(gIoExpanderMutex, portMAX_DELAY) == pdTRUE;
}

void unlockIoExpander() {
    if (gIoExpanderMutex) {
        xSemaphoreGive(gIoExpanderMutex);
    }
}

bool logLastError(const char *operation) {
    const int error = gIoExpander.lastError();
    if (error == TCA9555_OK) {
        return true;
    }
    Serial.printf("I/O expander %s failed (0x%02X)\n", operation, error);
    return false;
}

} // namespace

bool ioExpanderInit() {
    if (gIoExpanderInitialized) {
        return true;
    }
    if (!lockIoExpander()) {
        Serial.println("I/O expander mutex lock failed.");
        return false;
    }

    if (!gIoExpanderInitialized) {
        if (!gIoExpander.begin(INPUT)) {
            Serial.println("I/O expander begin failed.");
            unlockIoExpander();
            return false;
        }
        if (!logLastError("init")) {
            unlockIoExpander();
            return false;
        }
        gIoExpanderInitialized = true;
    }

    unlockIoExpander();
    return true;
}

bool ioExpanderPinMode(uint8_t pin, uint8_t mode) {
    if (!ioExpanderInit() || !lockIoExpander()) {
        return false;
    }

    const bool ok = gIoExpander.pinMode1(pin, mode);
    const bool errorFree = logLastError("pinMode");
    unlockIoExpander();
    return ok && errorFree;
}

bool ioExpanderDigitalWrite(uint8_t pin, uint8_t value) {
    if (!ioExpanderInit() || !lockIoExpander()) {
        return false;
    }

    const bool ok = gIoExpander.write1(pin, value);
    const bool errorFree = logLastError("write");
    unlockIoExpander();
    return ok && errorFree;
}

bool ioExpanderDigitalRead(uint8_t pin, bool *value) {
    if (!value) {
        return false;
    }
    if (!ioExpanderInit() || !lockIoExpander()) {
        return false;
    }

    const uint8_t rawValue = gIoExpander.read1(pin);
    const bool errorFree = logLastError("read");
    unlockIoExpander();
    if (!errorFree) {
        return false;
    }

    *value = rawValue != 0;
    return true;
}
