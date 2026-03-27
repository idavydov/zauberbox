#include <Arduino.h>
#include <WiFi.h>
#include <AyresWiFiManager.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include "audio_driver.h"

// Pins
#define I2C_SCL 10
#define I2C_SDA 11
#define LED_PIN 38
#define LED_COUNT 7
// States for LED Task
enum SystemState {
    STATE_WAITING_AP,
    STATE_CONNECTING_WIFI,
    STATE_CONNECTED
};

volatile SystemState currentState = STATE_WAITING_AP;
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);
AyresWiFiManager awm;

SystemState computeSystemState() {
    if (awm.isConnected()) {
        return STATE_CONNECTED;
    }
    if (awm.isPortalActive()) {
        return STATE_WAITING_AP;
    }
    return STATE_CONNECTING_WIFI;
}

void ledTask(void *pvParameters) {
    uint8_t hue = 0;
    uint8_t connectFrame = 0;
    while (true) {
        if (currentState == STATE_WAITING_AP) {
            float breath = (exp(sin(millis() / 500.0 * PI)) - 0.36787944) * 108.0;
            for (int i = 0; i < LED_COUNT; i++) {
                ring.setPixelColor(i, ring.Color(0, breath, breath * 0.15));
            }
            ring.show();
            vTaskDelay(pdMS_TO_TICKS(10));
        } else if (currentState == STATE_CONNECTING_WIFI) {
            const uint8_t head = connectFrame % LED_COUNT;
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
                ring.setPixelColor(i, ring.Color(0, level / 2, level));
            }
            ring.show();
            connectFrame++;
            vTaskDelay(pdMS_TO_TICKS(70));
        } else {
            for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, ring.gamma32(ring.ColorHSV(hue + (i * 65536 / LED_COUNT))));
            ring.show();
            hue += 256;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500); // Give serial some time
    Serial.println("Initializing Zauberbox...");

    Wire.begin(I2C_SDA, I2C_SCL);
    delay(100); // Wait for I2C to stabilize
    audioInit();

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed!");
    }

    awm.setHtmlPathPrefix("/wifimanager");
    awm.setAPCredentials("Zauberbox-Config", "789456123");
    awm.setPortalTimeout(300); // 5 min of inactivity
    awm.setAPClientCheck(true); // don't close if clients connected
    awm.setWebClientCheck(true); // each HTTP request resets the timer
    awm.begin();
    currentState = awm.tieneCredenciales() ? STATE_CONNECTING_WIFI : STATE_WAITING_AP;

    ring.begin();
    ring.setBrightness(50);
    xTaskCreatePinnedToCore(ledTask, "LED_Task", 4096, NULL, 1, NULL, 1);

    delay(80);
    audioPlayBip(880, 45, 1800);

    Serial.println("Starting WiFi Config...");
    // run() will try to connect or start the portal based on policy
    awm.run();
    currentState = computeSystemState();

    if (awm.isConnected()) {
        currentState = STATE_CONNECTED;
        Serial.println("WiFi Connected!");
        audioPlayBip(2000, 35, 1400);
        delay(35);
        audioPlayBip(2500, 50, 1400);
    }
}

void loop() {
    // AWM handles client processing and reconnection internally
    awm.update();
    if (!awm.isConnected()) {
        awm.reintentarConexionSiNecesario();
    }
    currentState = computeSystemState();
    vTaskDelay(pdMS_TO_TICKS(10));
}
