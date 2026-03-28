#include "audio_driver.h"

#include <Audio.h>
#include <LittleFS.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "io_expander.h"

namespace {

constexpr uint8_t kEs8311Addr = 0x18;

constexpr gpio_num_t kI2SMclk = GPIO_NUM_12;
constexpr gpio_num_t kI2SBclk = GPIO_NUM_13;
constexpr gpio_num_t kI2SWs = GPIO_NUM_14;
constexpr gpio_num_t kI2SDout = GPIO_NUM_16;
constexpr i2s_port_t kI2SPort = I2S_NUM_0;
constexpr char kBootSoundPath[] = "/boot.wav";
constexpr char kWifiConnectedSoundPath[] = "/wifi_connected.wav";
constexpr uint8_t kPlaybackVolume = 14;
constexpr size_t kAudioQueueDepth = 6;

Audio gFilePlayer(kI2SPort);
QueueHandle_t gAudioQueue = nullptr;
TaskHandle_t gAudioServiceTask = nullptr;

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

bool initEs8311() {
    const struct RegisterValue {
        uint8_t reg;
        uint8_t value;
    } initSequence[] = {
        {0x00, 0x1F},
        {0x00, 0x00},
        {0x00, 0x80},
        {0x01, 0x3F},
        {0x02, 0x00},
        {0x03, 0x10},
        {0x04, 0x10},
        {0x05, 0x00},
        {0x06, 0x03},
        {0x07, 0x00},
        {0x08, 0xFF},
        {0x09, 0x0C},
        {0x0A, 0x0C},
        {0x0D, 0x01},
        {0x0E, 0x02},
        {0x12, 0x00},
        {0x13, 0x10},
        {0x1C, 0x6A},
        {0x31, 0x00},
        {0x32, 0xC0},
        {0x37, 0x08},
    };

    for (size_t i = 0; i < (sizeof(initSequence) / sizeof(initSequence[0])); ++i) {
        if (!writeRegister8(kEs8311Addr, initSequence[i].reg, initSequence[i].value)) {
            Serial.printf("ES8311 write failed at reg 0x%02X\n", initSequence[i].reg);
            return false;
        }
        if (initSequence[i].reg == 0x00 && initSequence[i].value == 0x1F) {
            delay(20);
        }
    }

    uint8_t chipId = 0;
    if (readRegister8(kEs8311Addr, 0xFD, &chipId)) {
        Serial.printf("ES8311 initialized, chip ID 0x%02X\n", chipId);
    } else {
        Serial.println("ES8311 initialized, chip ID read failed.");
    }
    return true;
}

bool enableSpeaker() {
    if (!ioExpanderPinMode(kIoExpanderSpeakerEnablePin, OUTPUT) ||
        !ioExpanderDigitalWrite(kIoExpanderSpeakerEnablePin, HIGH)) {
        Serial.println("Speaker enable via I/O expander failed.");
        return false;
    }

    delay(50);
    Serial.println("Speaker enabled via TCA9555 EXIO8.");
    return true;
}

bool configurePlayer() {
    if (!gFilePlayer.setPinout(kI2SBclk, kI2SWs, kI2SDout, kI2SMclk)) {
        Serial.println("Audio player I2S pin configuration failed.");
        return false;
    }
    gFilePlayer.setVolume(kPlaybackVolume);
    return true;
}

bool playFile(const char *path) {
    if (!LittleFS.exists(path)) {
        Serial.printf("Audio file not found: %s\n", path);
        return false;
    }
    if (gFilePlayer.isRunning()) {
        gFilePlayer.stopSong();
    }
    if (!gFilePlayer.connecttoFS(LittleFS, path)) {
        Serial.printf("Audio playback start failed: %s\n", path);
        return false;
    }

    Serial.printf("Playing audio: %s\n", path);
    return true;
}

bool enqueuePath(const char *path) {
    if (!gAudioQueue) {
        return false;
    }
    return xQueueSend(gAudioQueue, &path, 0) == pdPASS;
}

void audioServiceTask(void *pvParameters) {
    const char *path = nullptr;

    while (true) {
        if (xQueueReceive(gAudioQueue, &path, pdMS_TO_TICKS(1)) == pdPASS) {
            playFile(path);
        }
        if (gFilePlayer.isRunning()) {
            gFilePlayer.loop();
        }
    }
}

} // namespace

bool audioInit() {
    if (!enableSpeaker()) {
        return false;
    }
    if (!initEs8311()) {
        return false;
    }
    if (!configurePlayer()) {
        return false;
    }
    if (!gAudioQueue) {
        gAudioQueue = xQueueCreate(kAudioQueueDepth, sizeof(const char *));
        if (!gAudioQueue) {
            Serial.println("Audio queue creation failed.");
            return false;
        }
    }
    if (!gAudioServiceTask) {
        xTaskCreatePinnedToCore(audioServiceTask, "Audio_Service", 8192, nullptr, 2, &gAudioServiceTask, 0);
    }
    return true;
}

bool audioPlayBootSound() {
    return enqueuePath(kBootSoundPath);
}

bool audioPlayWifiConnectedSound() {
    return enqueuePath(kWifiConnectedSoundPath);
}
