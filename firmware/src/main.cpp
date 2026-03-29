#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include "app_controller.h"
#include "io_expander.h"

constexpr uint8_t kI2cScl = 10;
constexpr uint8_t kI2cSda = 11;

AppController gAppController;

void setup() {
    Serial.begin(115200);
    delay(500); // Give serial some time
    Serial.println("Initializing Zauberbox...");

    Wire.begin(kI2cSda, kI2cScl);
    delay(100); // Wait for I2C to stabilize

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed!");
    }

    if (!ioExpanderInit()) {
        Serial.println("I/O expander init failed.");
    }

    gAppController.begin();
}

void loop() {
    gAppController.update();
    vTaskDelay(pdMS_TO_TICKS(10));
}
