#include <Arduino.h>
#include <WiFi.h>
#include <AyresWiFiManager.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include "driver/i2s.h"

// Pins
#define I2C_SDA 10
#define I2C_SCL 11
#define LED_PIN 38
#define LED_COUNT 7
#define TCA9555_ADDR 0x20

// I2S Pins for Waveshare Board
#define I2S_BCLK 40
#define I2S_LRCK 41
#define I2S_DOUT 2
#define I2S_MCLK 39
#define I2S_NUM  I2S_NUM_0

// States for LED Task
enum SystemState {
    STATE_CONNECTING,
    STATE_CONNECTED
};

volatile SystemState currentState = STATE_CONNECTING;
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
AyresWiFiManager awm;

/**
 * Basic ES8311 DAC Initialization via I2C
 */
void initES8311() {
    auto writeReg = [](uint8_t reg, uint8_t val) {
        Wire.beginTransmission(0x18);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    };
    writeReg(0x00, 0x80); delay(5);
    writeReg(0x00, 0x00);
    writeReg(0x01, 0x30);
    writeReg(0x02, 0x10);
    writeReg(0x03, 0x10);
    writeReg(0x32, 0x80);
    Serial.println("ES8311 DAC Initialized.");
}

void enableSpeaker() {
    Wire.beginTransmission(TCA9555_ADDR);
    Wire.write(0x06); Wire.write(0x00); Wire.endTransmission();
    Wire.beginTransmission(TCA9555_ADDR);
    Wire.write(0x02); Wire.write(0x01); Wire.endTransmission();
}

void playBip(float frequency, int duration_ms) {
    size_t bytes_written;
    int samples = (44100 * duration_ms) / 1000;
    int16_t* buf = (int16_t*)malloc(samples * 2 * sizeof(int16_t));
    for (int i = 0; i < samples; i++) {
        int16_t val = (sin(2 * PI * frequency * i / 44100.0) > 0) ? 4000 : -4000;
        buf[i*2] = val; buf[i*2 + 1] = val;
    }
    i2s_write(I2S_NUM, buf, samples * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    free(buf);
}

void ledTask(void *pvParameters) {
    uint8_t hue = 0;
    while (true) {
        if (currentState == STATE_CONNECTING) {
            float breath = (exp(sin(millis() / 500.0 * PI)) - 0.36787944) * 108.0;
            for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, ring.Color(breath, breath * 0.6, 0)); 
            ring.show();
            vTaskDelay(pdMS_TO_TICKS(10));
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
    Wire.begin(I2C_SDA, I2C_SCL);
    enableSpeaker();
    initES8311();
    
    // I2S Setup
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK, .ws_io_num = I2S_LRCK, .data_out_num = I2S_DOUT, .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM, &pin_config);
    pinMode(I2S_MCLK, OUTPUT);
    gpio_set_direction((gpio_num_t)I2S_MCLK, GPIO_MODE_OUTPUT);

    playBip(1000, 150);
    
    ring.begin();
    ring.setBrightness(50);
    xTaskCreatePinnedToCore(ledTask, "LED_Task", 4096, NULL, 1, NULL, 1);

    awm.setAPCredentials("Zauberbox-Config", "123456789");
    awm.begin();

    Serial.println("Starting WiFi Config...");
    // run() will try to connect or start the portal based on policy
    awm.run();

    if (awm.isConnected()) {
        currentState = STATE_CONNECTED;
        Serial.println("WiFi Connected!");
        playBip(2000, 80); delay(50); playBip(2500, 120);
    }
}

void loop() {
    // AWM handles client processing and reconnection internally
    awm.update();
    if (!awm.isConnected()) {
        awm.reintentarConexionSiNecesario();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}
