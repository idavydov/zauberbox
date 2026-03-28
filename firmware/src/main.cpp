#include <Arduino.h>
#include <WiFi.h>
#include <AyresWiFiManager.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include "app_state.h"
#include "audio_driver.h"
#include "io_expander.h"

// Pins
#define I2C_SCL 10
#define I2C_SDA 11
#define LED_PIN 38
#define LED_COUNT 7

constexpr uint32_t kKeyHoldMs = 3000;
constexpr uint32_t kBootSoundDelayMs = 900;

Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);
AyresWiFiManager awm;

WifiMode detectWifiModeFromManager() {
    if (awm.isPortalActive()) {
        return WifiMode::PortalActive;
    }
    if (awm.isConnected()) {
        return WifiMode::Connected;
    }
    return WifiMode::Connecting;
}

void syncWifiStateFromManager() {
    static WifiMode lastMode = WifiMode::Disabled;

    const WifiMode currentMode = detectWifiModeFromManager();
    if (currentMode == lastMode) {
        return;
    }

    appStateStore().syncWifiMode(currentMode);
    if (currentMode == WifiMode::Connected) {
        audioPlayWifiConnectedSound();
    }
    lastMode = currentMode;
}

void configureKey1Input() {
    if (!ioExpanderPinMode(kIoExpanderKey1Pin, INPUT)) {
        Serial.println("KEY1 config failed.");
    }
}

bool isKey1Pressed() {
    bool levelHigh = true;
    if (!ioExpanderDigitalRead(kIoExpanderKey1Pin, &levelHigh)) {
        return false;
    }
    return !levelHigh;
}

[[noreturn]] void eraseWifiCredentialsAndReboot() {
    appStateStore().requestFactoryReset();
    const bool removed = LittleFS.remove("/wifi.json");
    Serial.printf("KEY1 long press: %s /wifi.json\n", removed ? "removed" : "could not remove");

    const uint32_t rebootAt = millis() + 3000;
    while (millis() < rebootAt) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP.restart();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void key1HoldTask(void *pvParameters) {
    uint32_t pressedAt = 0;

    while (true) {
        if (!appStateStore().allowsFactoryReset()) {
            pressedAt = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (isKey1Pressed()) {
            if (pressedAt == 0) {
                pressedAt = millis();
            } else if (millis() - pressedAt >= kKeyHoldMs) {
                eraseWifiCredentialsAndReboot();
            }
        } else {
            pressedAt = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void bootSoundTask(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(kBootSoundDelayMs));
    audioPlayBootSound();
    vTaskDelete(nullptr);
}

void ledTask(void *pvParameters) {
    uint8_t hue = 0;
    uint8_t scanFrame = 0;
    while (true) {
        const AppState state = appStateStore().current();

        switch (state) {
            case AppState::Boot: {
                const bool on = (millis() / 250) % 2 == 0;
                const uint32_t color = on ? ring.Color(24, 18, 8) : ring.Color(4, 2, 0);
                for (int i = 0; i < LED_COUNT; i++) {
                    ring.setPixelColor(i, color);
                }
                ring.show();
                vTaskDelay(pdMS_TO_TICKS(60));
                break;
            }
            case AppState::WifiPortal: {
                const float breath = (exp(sin(millis() / 500.0 * PI)) - 0.36787944) * 108.0;
                for (int i = 0; i < LED_COUNT; i++) {
                    ring.setPixelColor(i, ring.Color(0, breath, breath * 0.15));
                }
                ring.show();
                vTaskDelay(pdMS_TO_TICKS(10));
                break;
            }
            case AppState::QrScan: {
                const uint8_t head = scanFrame % LED_COUNT;
                for (int i = 0; i < LED_COUNT; i++) {
                    uint8_t level = 0;
                    const uint8_t distance = (i + LED_COUNT - head) % LED_COUNT;
                    if (distance == 0) {
                        level = 180;
                    } else if (distance == 1) {
                        level = 70;
                    } else if (distance == 2) {
                        level = 24;
                    }
                    ring.setPixelColor(i, ring.Color(level, level / 5, 0));
                }
                ring.show();
                scanFrame++;
                vTaskDelay(pdMS_TO_TICKS(70));
                break;
            }
            case AppState::Idle: {
                const float breath = (exp(sin(millis() / 1300.0 * PI)) - 0.36787944) * 22.0;
                for (int i = 0; i < LED_COUNT; i++) {
                    ring.setPixelColor(i, ring.Color(breath * 0.4, breath * 0.25, 0));
                }
                ring.show();
                vTaskDelay(pdMS_TO_TICKS(25));
                break;
            }
            case AppState::Playing: {
                for (int i = 0; i < LED_COUNT; i++) {
                    ring.setPixelColor(i, ring.gamma32(ring.ColorHSV(hue + (i * 65536 / LED_COUNT))));
                }
                ring.show();
                hue += 256;
                vTaskDelay(pdMS_TO_TICKS(20));
                break;
            }
            case AppState::Paused: {
                const bool on = (millis() / 400) % 2 == 0;
                const uint32_t color = on ? ring.Color(0, 18, 24) : ring.Color(0, 4, 6);
                for (int i = 0; i < LED_COUNT; i++) {
                    ring.setPixelColor(i, color);
                }
                ring.show();
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            }
            case AppState::Sleep: {
                ring.clear();
                ring.show();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }
            case AppState::Resetting: {
                const bool on = (millis() / 150) % 2 == 0;
                const uint32_t color = on ? ring.Color(180, 0, 0) : 0;
                for (int i = 0; i < LED_COUNT; i++) {
                    ring.setPixelColor(i, color);
                }
                ring.show();
                vTaskDelay(pdMS_TO_TICKS(30));
                break;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500); // Give serial some time
    Serial.println("Initializing Zauberbox...");
    appStateStore().init();

    Wire.begin(I2C_SDA, I2C_SCL);
    delay(100); // Wait for I2C to stabilize

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed!");
    }

    if (!ioExpanderInit()) {
        Serial.println("I/O expander init failed.");
    }
    configureKey1Input();
    xTaskCreatePinnedToCore(key1HoldTask, "KEY1_Task", 4096, NULL, 2, NULL, 1);

    audioInit();

    awm.setHtmlPathPrefix("/wifimanager");
    awm.setAPCredentials("Zauberbox-Config", "789456123");
    awm.setPortalTimeout(300); // 5 min of inactivity
    awm.setAPClientCheck(true); // don't close if clients connected
    awm.setWebClientCheck(true); // each HTTP request resets the timer
    awm.begin();
    if (awm.tieneCredenciales()) {
        appStateStore().syncWifiMode(WifiMode::Connecting);
    }

    ring.begin();
    ring.setBrightness(50);
    xTaskCreatePinnedToCore(ledTask, "LED_Task", 4096, NULL, 1, NULL, 1);

    xTaskCreatePinnedToCore(bootSoundTask, "BootSound_Task", 2048, NULL, 1, NULL, 1);

    Serial.println("Starting WiFi Config...");
    // run() will try to connect or start the portal based on policy
    awm.run();
    appStateStore().completeBoot();
    syncWifiStateFromManager();
}

void loop() {
    // AWM handles client processing and reconnection internally
    awm.update();
    if (!awm.isConnected()) {
        awm.reintentarConexionSiNecesario();
    }
    syncWifiStateFromManager();
    vTaskDelay(pdMS_TO_TICKS(10));
}
