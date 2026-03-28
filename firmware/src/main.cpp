#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include "app_state.h"
#include "audio_driver.h"
#include "button_controller.h"
#include "io_expander.h"
#include "led_controller.h"
#include "wifi_service.h"

constexpr uint32_t kBootSoundDelayMs = 900;
constexpr uint8_t kI2cScl = 10;
constexpr uint8_t kI2cSda = 11;

ButtonController gButtonController;
LedController gLedController;
WifiService gWifiService;

void bootSoundTask(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(kBootSoundDelayMs));
    audioPlayBootSound();
    vTaskDelete(nullptr);
}

void setup() {
    Serial.begin(115200);
    delay(500); // Give serial some time
    Serial.println("Initializing Zauberbox...");

    appStateStore().init();

    Wire.begin(kI2cSda, kI2cScl);
    delay(100); // Wait for I2C to stabilize

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed!");
    }

    if (!ioExpanderInit()) {
        Serial.println("I/O expander init failed.");
    }
    if (!audioInit()) {
        Serial.println("Audio init failed.");
    }

    gButtonController.begin();
    gLedController.begin();
    gWifiService.begin(audioPlayWifiConnectedSound);

    xTaskCreatePinnedToCore(bootSoundTask, "BootSound_Task", 2048, nullptr, 1, nullptr, 1);

    appStateStore().completeBoot();

    Serial.println("Starting WiFi Config...");
    gWifiService.runStartup();
}

void loop() {
    gWifiService.update();
    vTaskDelay(pdMS_TO_TICKS(10));
}
