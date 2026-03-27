#include "audio_driver.h"

#include <Wire.h>
#include "driver/i2s_std.h"

namespace {

constexpr uint8_t kEs8311Addr = 0x18;
constexpr uint8_t kTca9555Addr = 0x20;
constexpr uint8_t kTca9555RegOutputPort1 = 0x03;
constexpr uint8_t kTca9555RegConfigPort1 = 0x07;
constexpr uint8_t kTca9555PaEnableMask = 0x01;

constexpr gpio_num_t kI2SMclk = GPIO_NUM_12;
constexpr gpio_num_t kI2SBclk = GPIO_NUM_13;
constexpr gpio_num_t kI2SWs = GPIO_NUM_14;
constexpr gpio_num_t kI2SDout = GPIO_NUM_16;
constexpr i2s_port_t kI2SPort = I2S_NUM_0;
constexpr uint32_t kI2SSampleRate = 16000;

i2s_chan_handle_t gTxHandle = nullptr;

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
    uint8_t configPort1 = 0xFF;
    uint8_t outputPort1 = 0x00;

    if (!readRegister8(kTca9555Addr, kTca9555RegConfigPort1, &configPort1)) {
        Serial.println("TCA9555 config read failed.");
        return false;
    }
    if (!readRegister8(kTca9555Addr, kTca9555RegOutputPort1, &outputPort1)) {
        Serial.println("TCA9555 output read failed.");
        return false;
    }

    configPort1 &= ~kTca9555PaEnableMask;
    outputPort1 |= kTca9555PaEnableMask;

    if (!writeRegister8(kTca9555Addr, kTca9555RegConfigPort1, configPort1) ||
        !writeRegister8(kTca9555Addr, kTca9555RegOutputPort1, outputPort1)) {
        Serial.println("Speaker enable via TCA9555 failed.");
        return false;
    }

    delay(50);
    Serial.println("Speaker enabled via TCA9555 EXIO8.");
    return true;
}

bool initI2S() {
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(kI2SPort, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chanCfg, &gTxHandle, nullptr) != ESP_OK) {
        Serial.println("I2S channel creation failed.");
        gTxHandle = nullptr;
        return false;
    }

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kI2SSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = kI2SMclk,
            .bclk = kI2SBclk,
            .ws = kI2SWs,
            .dout = kI2SDout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    if (i2s_channel_init_std_mode(gTxHandle, &stdCfg) != ESP_OK) {
        Serial.println("I2S std mode init failed.");
        return false;
    }

    return true;
}

} // namespace

bool audioInit() {
    if (!enableSpeaker()) {
        return false;
    }
    if (!initEs8311()) {
        return false;
    }
    return initI2S();
}

void audioPlayBip(float frequency, int durationMs, int16_t amplitude) {
    if (!gTxHandle) {
        return;
    }

    size_t bytesWritten;
    int samples = (kI2SSampleRate * durationMs) / 1000;
    int silenceSamples = kI2SSampleRate / 50;
    int totalSamples = samples + silenceSamples;
    int16_t *buf = (int16_t *)calloc(totalSamples * 2, sizeof(int16_t));
    if (!buf) {
        Serial.println("Audio buffer allocation failed.");
        return;
    }

    i2s_channel_enable(gTxHandle);
    delay(12);

    int fadeSamples = samples / 4;
    const int maxFadeSamples = kI2SSampleRate / 200;
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
        float wave = sinf(2.0f * PI * frequency * i / kI2SSampleRate);
        int16_t val = (int16_t)(wave * amplitude * envelope);
        buf[i * 2] = val;
        buf[i * 2 + 1] = val;
    }

    i2s_channel_write(gTxHandle, buf, totalSamples * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    free(buf);

    int playbackMs = (totalSamples * 1000) / kI2SSampleRate;
    delay(playbackMs + 10);
    i2s_channel_disable(gTxHandle);
}
