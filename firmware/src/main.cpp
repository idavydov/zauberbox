#include <Arduino.h>
#include <WiFi.h>
#include <AyresWiFiManager.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include "driver/i2s_std.h"

// Pins
#define I2C_SCL 10
#define I2C_SDA 11
#define LED_PIN 38
#define LED_COUNT 7
#define TCA9555_ADDR 0x20

// I2S Pins for Waveshare Board
#define I2S_MCLK 12
#define I2S_SCLK 13
#define I2S_LRCK 14
#define I2S_DOUT 16
#define I2S_DIN 15
#define I2S_NUM  I2S_NUM_0
#define I2S_SAMPLE_RATE 16000

#define ES8311_ADDR 0x18
#define TCA9555_REG_OUTPUT_PORT_1 0x03
#define TCA9555_REG_CONFIG_PORT_1 0x07
#define TCA9555_PA_EN_MASK 0x01

// States for LED Task
enum SystemState {
    STATE_CONNECTING,
    STATE_CONNECTED
};

volatile SystemState currentState = STATE_CONNECTING;
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
AyresWiFiManager awm;
i2s_chan_handle_t tx_handle;

bool writeRegister8(uint8_t deviceAddr, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(deviceAddr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegister8(uint8_t deviceAddr, uint8_t reg, uint8_t *value) {
    Wire.beginTransmission(deviceAddr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom((int)deviceAddr, 1) != 1) {
        return false;
    }
    *value = Wire.read();
    return true;
}

/**
 * Basic ES8311 DAC Initialization via I2C
 */
void initES8311() {
    const struct RegisterValue {
        uint8_t reg;
        uint8_t value;
    } initSequence[] = {
        {0x00, 0x1F}, // reset
        {0x00, 0x00},
        {0x00, 0x80}, // power on
        {0x01, 0x3F}, // enable clocks, use external MCLK pin
        {0x02, 0x00},
        {0x03, 0x10},
        {0x04, 0x10},
        {0x05, 0x00},
        {0x06, 0x03},
        {0x07, 0x00},
        {0x08, 0xFF},
        {0x09, 0x0C}, // 16-bit I2S input
        {0x0A, 0x0C}, // 16-bit I2S output
        {0x0D, 0x01}, // power analog
        {0x0E, 0x02},
        {0x12, 0x00}, // power DAC
        {0x13, 0x10}, // enable headphone/speaker driver path
        {0x1C, 0x6A}, // bypass ADC EQ / DC offset handling
        {0x31, 0x00}, // unmute DAC
        {0x32, 0xC0}, // audible default volume
        {0x37, 0x08}, // bypass DAC EQ
    };

    for (size_t i = 0; i < (sizeof(initSequence) / sizeof(initSequence[0])); ++i) {
        if (!writeRegister8(ES8311_ADDR, initSequence[i].reg, initSequence[i].value)) {
            Serial.printf("ES8311 write failed at reg 0x%02X\n", initSequence[i].reg);
            return;
        }
        if (initSequence[i].reg == 0x00 && initSequence[i].value == 0x1F) {
            delay(20);
        }
    }

    uint8_t chipId = 0;
    if (readRegister8(ES8311_ADDR, 0xFD, &chipId)) {
        Serial.printf("ES8311 initialized, chip ID 0x%02X\n", chipId);
    } else {
        Serial.println("ES8311 initialized, chip ID read failed.");
    }
}

/**
 * Enable Speaker via TCA9555 GPIO Expander
 * Demo firmware uses EXIO8, which is Port 1 Bit 0 on the TCA9555.
 */
void enableSpeaker() {
    uint8_t configPort1 = 0xFF;
    uint8_t outputPort1 = 0x00;

    if (!readRegister8(TCA9555_ADDR, TCA9555_REG_CONFIG_PORT_1, &configPort1)) {
        Serial.println("TCA9555 config read failed.");
        return;
    }
    if (!readRegister8(TCA9555_ADDR, TCA9555_REG_OUTPUT_PORT_1, &outputPort1)) {
        Serial.println("TCA9555 output read failed.");
        return;
    }

    configPort1 &= ~TCA9555_PA_EN_MASK;
    outputPort1 |= TCA9555_PA_EN_MASK;

    if (!writeRegister8(TCA9555_ADDR, TCA9555_REG_CONFIG_PORT_1, configPort1) ||
        !writeRegister8(TCA9555_ADDR, TCA9555_REG_OUTPUT_PORT_1, outputPort1)) {
        Serial.println("Speaker enable via TCA9555 failed.");
        return;
    }

    delay(50);
    Serial.println("Speaker enabled via TCA9555 EXIO8.");
}

void playBip(float frequency, int duration_ms, int16_t amplitude = 4000) {
    if (!tx_handle) {
        return;
    }

    size_t bytes_written;
    int samples = (I2S_SAMPLE_RATE * duration_ms) / 1000;
    int silenceSamples = I2S_SAMPLE_RATE / 50; // 20 ms of silence after each tone
    int totalSamples = samples + silenceSamples;
    int16_t* buf = (int16_t*)calloc(totalSamples * 2, sizeof(int16_t));
    if (!buf) {
        Serial.println("Audio buffer allocation failed.");
        return;
    }

    i2s_channel_enable(tx_handle);
    delay(12);

    int fadeSamples = samples / 4;
    const int maxFadeSamples = I2S_SAMPLE_RATE / 200; // about 5 ms at 16 kHz
    if (fadeSamples > maxFadeSamples) {
        fadeSamples = maxFadeSamples;
    }
    if (fadeSamples < 1) {
        fadeSamples = 1;
    }
    for (int i = 0; i < samples; i++) {
        float envelope = 1.0f;
        if (i < fadeSamples) {
            envelope = (float)i / fadeSamples;
        } else if (i >= samples - fadeSamples) {
            envelope = (float)(samples - 1 - i) / fadeSamples;
        }
        float wave = sinf(2.0f * PI * frequency * i / I2S_SAMPLE_RATE);
        int16_t val = (int16_t)(wave * amplitude * envelope);
        buf[i*2] = val; buf[i*2 + 1] = val;
    }
    i2s_channel_write(tx_handle, buf, totalSamples * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    free(buf);

    int playbackMs = (totalSamples * 1000) / I2S_SAMPLE_RATE;
    delay(playbackMs + 10);
    i2s_channel_disable(tx_handle);
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
    delay(500); // Give serial some time
    Serial.println("Initializing Zauberbox...");

    Wire.begin(I2C_SDA, I2C_SCL);
    delay(100); // Wait for I2C to stabilize
    
    enableSpeaker();
    initES8311();
    
    // I2S Setup with New Driver
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_handle, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)I2S_MCLK,
            .bclk = (gpio_num_t)I2S_SCLK,
            .ws = (gpio_num_t)I2S_LRCK,
            .dout = (gpio_num_t)I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    i2s_channel_init_std_mode(tx_handle, &std_cfg);

    ring.begin();
    ring.setBrightness(50);
    xTaskCreatePinnedToCore(ledTask, "LED_Task", 4096, NULL, 1, NULL, 1);

    delay(80);
    playBip(880, 45, 1800);

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed!");
    }

    awm.setAPCredentials("Zauberbox-Config", "123456789");
    awm.begin();

    Serial.println("Starting WiFi Config...");
    // run() will try to connect or start the portal based on policy
    awm.run();

    if (awm.isConnected()) {
        currentState = STATE_CONNECTED;
        Serial.println("WiFi Connected!");
        playBip(2000, 35, 1400);
        delay(35);
        playBip(2500, 50, 1400);
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
