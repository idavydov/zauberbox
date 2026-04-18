#include "audio_driver.h"

#include "debug_log.h"

#include <Audio.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <deque>

#include "io_expander.h"

namespace {

constexpr uint8_t kEs8311Addr = 0x18;

constexpr gpio_num_t kI2SMclk = GPIO_NUM_12;
constexpr gpio_num_t kI2SBclk = GPIO_NUM_13;
constexpr gpio_num_t kI2SWs = GPIO_NUM_14;
constexpr gpio_num_t kI2SDout = GPIO_NUM_16;
constexpr i2s_port_t kI2SPort = I2S_NUM_0;
constexpr uint8_t kMinVolume = 0;
constexpr uint8_t kMaxVolume = 21;
constexpr uint8_t kPlaybackVolume = 14;
constexpr size_t kAudioCommandQueueDepth = 8;
constexpr size_t kMaxAudioPathLength = 192;
constexpr BaseType_t kAudioServiceCore = 1;
constexpr uint16_t kTransientReplaceFadeOutMs = 35;

enum class AudioCommandType : uint8_t {
    Enqueue,
    ReplaceQueue,
    SeekToFilePosition,
    Stop,
    TogglePause,
    SetVolume,
};

struct AudioPlaybackRequest {
    AudioStorage storage;
    int32_t startTimeSeconds;
    bool muteUntilPlaybackStart;
    uint16_t unmuteDelayMs;
    char path[kMaxAudioPathLength];
};

struct AudioCommand {
    AudioCommandType type;
    AudioPlaybackRequest request;
    uint32_t seekFilePosition;
    uint8_t volume;
};

Audio gFilePlayer(kI2SPort);
QueueHandle_t gAudioCommandQueue = nullptr;
TaskHandle_t gAudioServiceTask = nullptr;
AudioPlaybackFinishedCallback gPlaybackFinishedCallback = nullptr;
uint8_t gCurrentVolume = kPlaybackVolume;
bool gAudioInitialized = false;
bool gSpeakerEnabled = false;
bool gPlaybackPaused = false;
bool gAwaitingNaturalPlaybackEnd = false;

fs::FS *filesystemForStorage(AudioStorage storage) {
    switch (storage) {
        case AudioStorage::LittleFs:
            return &LittleFS;
        case AudioStorage::SdCard:
            return &SD_MMC;
    }

    return nullptr;
}

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
    if (gSpeakerEnabled) {
        return true;
    }

    if (!ioExpanderPinMode(kIoExpanderSpeakerEnablePin, OUTPUT) ||
        !ioExpanderDigitalWrite(kIoExpanderSpeakerEnablePin, HIGH)) {
        Serial.println("Speaker enable via I/O expander failed.");
        return false;
    }

    delay(50);
    gSpeakerEnabled = true;
    Serial.printf("[%lu] Speaker enabled via TCA9555 EXIO8.\n",
                  static_cast<unsigned long>(millis()));
    return true;
}

bool disableSpeaker() {
    if (!gSpeakerEnabled) {
        return true;
    }

    if (!ioExpanderPinMode(kIoExpanderSpeakerEnablePin, OUTPUT) ||
        !ioExpanderDigitalWrite(kIoExpanderSpeakerEnablePin, LOW)) {
        Serial.println("Speaker disable via I/O expander failed.");
        return false;
    }

    delay(20);
    gSpeakerEnabled = false;
    Serial.printf("[%lu] Speaker disabled via TCA9555 EXIO8.\n",
                  static_cast<unsigned long>(millis()));
    return true;
}

bool configurePlayer() {
    if (!gFilePlayer.setPinout(kI2SBclk, kI2SWs, kI2SDout, kI2SMclk)) {
        Serial.println("Audio player I2S pin configuration failed.");
        return false;
    }
    gFilePlayer.setVolume(gCurrentVolume);
    return true;
}

bool playRequest(const AudioPlaybackRequest &request) {
    fs::FS *filesystem = filesystemForStorage(request.storage);
    if (!filesystem) {
        Serial.println("Audio playback start failed: unsupported storage.");
        return false;
    }
    if (!filesystem->exists(request.path)) {
        Serial.printf("Audio file not found: %s\n", request.path);
        return false;
    }
    if (!gFilePlayer.connecttoFS(*filesystem, request.path, request.startTimeSeconds)) {
        Serial.printf("Audio playback start failed: %s\n", request.path);
        return false;
    }

    Serial.printf("[%lu] Playing audio: %s\n",
                  static_cast<unsigned long>(millis()),
                  request.path);
    return true;
}

void handlePlaybackEvent(AudioPlaybackEvent event) {
    gAwaitingNaturalPlaybackEnd = false;
    gPlaybackPaused = false;
    if (gPlaybackFinishedCallback) {
        gPlaybackFinishedCallback(event);
    }
}

void handleAudioInfo(Audio::msg_t msg) {
    if (msg.e != Audio::evt_eof) {
        return;
    }

    if (!gAwaitingNaturalPlaybackEnd) {
        return;
    }

    handlePlaybackEvent(AudioPlaybackEvent::Finished);
}

void fillRequest(AudioPlaybackRequest *request,
                 AudioStorage storage,
                 const char *path,
                 int32_t startTimeSeconds = -1,
                 bool muteUntilPlaybackStart = false,
                 uint16_t unmuteDelayMs = 80) {
    if (!request) {
        return;
    }

    request->storage = storage;
    request->startTimeSeconds = startTimeSeconds;
    request->muteUntilPlaybackStart = muteUntilPlaybackStart;
    request->unmuteDelayMs = unmuteDelayMs;
    request->path[0] = '\0';
    if (!path) {
        return;
    }

    snprintf(request->path, sizeof(request->path), "%s", path);
}

bool queueCommand(const AudioCommand &command) {
    if (!gAudioCommandQueue) {
        return false;
    }

    return xQueueSend(gAudioCommandQueue, &command, 0) == pdPASS;
}

void audioServiceTask(void *pvParameters) {
    AudioCommand command = {};
    std::deque<AudioPlaybackRequest> pendingRequests;
    int32_t pendingSeekFilePosition = -1;
    uint32_t pendingSeekDeadlineMs = 0;
    uint32_t pendingSeekNextAttemptAtMs = 0;
    bool pendingAudioUnmuteOnRunning = false;
    uint32_t pendingAudioUnmuteAtMs = 0;
    uint16_t pendingAudioUnmuteDelayMs = 80;
    bool deferredMutedReplaceActive = false;
    AudioPlaybackRequest deferredMutedReplaceRequest = {};
    uint32_t deferredMutedReplaceAtMs = 0;
    auto startRequest = [&](const AudioPlaybackRequest &request) {
        gPlaybackPaused = false;
        if (playRequest(request)) {
            pendingSeekFilePosition = -1;
            pendingSeekDeadlineMs = 0;
            pendingSeekNextAttemptAtMs = 0;
            gAwaitingNaturalPlaybackEnd = true;
            pendingAudioUnmuteOnRunning = request.muteUntilPlaybackStart;
            if (!request.muteUntilPlaybackStart) {
                pendingAudioUnmuteAtMs = 0;
                pendingAudioUnmuteDelayMs = 80;
                gFilePlayer.setMute(false);
            } else {
                pendingAudioUnmuteDelayMs = request.unmuteDelayMs;
                gFilePlayer.setMute(true);
            }
            return true;
        }

        pendingSeekFilePosition = -1;
        pendingSeekDeadlineMs = 0;
        pendingSeekNextAttemptAtMs = 0;
        pendingAudioUnmuteOnRunning = false;
        pendingAudioUnmuteAtMs = 0;
        pendingAudioUnmuteDelayMs = 80;
        gFilePlayer.setMute(false);
        handlePlaybackEvent(AudioPlaybackEvent::Failed);
        return false;
    };

    while (true) {
        if (xQueueReceive(gAudioCommandQueue, &command, pdMS_TO_TICKS(1)) == pdPASS) {
            switch (command.type) {
                case AudioCommandType::Enqueue:
                    pendingRequests.push_back(command.request);
                    break;
                case AudioCommandType::ReplaceQueue:
                    pendingRequests.clear();
                    pendingSeekFilePosition = -1;
                    pendingSeekDeadlineMs = 0;
                    pendingSeekNextAttemptAtMs = 0;
                    pendingAudioUnmuteOnRunning = false;
                    pendingAudioUnmuteAtMs = 0;
                    pendingAudioUnmuteDelayMs = command.request.unmuteDelayMs;
                    deferredMutedReplaceActive = false;
                    deferredMutedReplaceAtMs = 0;
                    if (command.request.muteUntilPlaybackStart) {
                        if (gFilePlayer.isRunning()) {
                            gFilePlayer.setMute(true);
                            deferredMutedReplaceRequest = command.request;
                            deferredMutedReplaceActive = true;
                            deferredMutedReplaceAtMs = millis() + kTransientReplaceFadeOutMs;
                            gAwaitingNaturalPlaybackEnd = false;
                            gPlaybackPaused = false;
                            break;
                        }
                    }
                    (void)startRequest(command.request);
                    break;
                case AudioCommandType::SeekToFilePosition:
                    pendingSeekFilePosition = static_cast<int32_t>(command.seekFilePosition);
                    pendingSeekDeadlineMs = millis() + 5000;
                    pendingSeekNextAttemptAtMs = millis();
                    break;
                case AudioCommandType::Stop:
                    pendingRequests.clear();
                    pendingSeekFilePosition = -1;
                    pendingSeekDeadlineMs = 0;
                    pendingSeekNextAttemptAtMs = 0;
                    pendingAudioUnmuteOnRunning = false;
                    pendingAudioUnmuteAtMs = 0;
                    pendingAudioUnmuteDelayMs = 80;
                    deferredMutedReplaceActive = false;
                    deferredMutedReplaceAtMs = 0;
                    if (gFilePlayer.isRunning()) {
                        gFilePlayer.stopSong();
                    }
                    gFilePlayer.setMute(false);
                    gAwaitingNaturalPlaybackEnd = false;
                    gPlaybackPaused = false;
                    break;
                case AudioCommandType::TogglePause:
                    if (gPlaybackPaused) {
                        if (!enableSpeaker()) {
                            break;
                        }
                        (void)gFilePlayer.pauseResume();
                        gPlaybackPaused = false;
                        break;
                    }

                    if (gFilePlayer.isRunning()) {
                        (void)gFilePlayer.pauseResume();
                        gPlaybackPaused = true;
                        (void)disableSpeaker();
                    }
                    break;
                case AudioCommandType::SetVolume:
                    gCurrentVolume = command.volume;
                    gFilePlayer.setVolume(gCurrentVolume);
                    break;
            }
        }

        const bool wasRunning = gFilePlayer.isRunning();
        if (wasRunning) {
            gFilePlayer.loop();
        }
        const bool isRunning = gFilePlayer.isRunning();

        if (deferredMutedReplaceActive &&
            static_cast<int32_t>(millis() - deferredMutedReplaceAtMs) >= 0) {
            deferredMutedReplaceActive = false;
            (void)startRequest(deferredMutedReplaceRequest);
        }

        if (pendingAudioUnmuteOnRunning && isRunning) {
            pendingAudioUnmuteOnRunning = false;
            pendingAudioUnmuteAtMs = millis() + pendingAudioUnmuteDelayMs;
        }

        if (pendingAudioUnmuteAtMs != 0 &&
            static_cast<int32_t>(millis() - pendingAudioUnmuteAtMs) >= 0) {
            gFilePlayer.setMute(false);
            pendingAudioUnmuteAtMs = 0;
        }

        if (pendingSeekFilePosition >= 0) {
            const uint32_t now = millis();
            if (!isRunning || gPlaybackPaused) {
                if (pendingSeekDeadlineMs != 0 &&
                    static_cast<int32_t>(now - pendingSeekDeadlineMs) >= 0) {
                    pendingSeekFilePosition = -1;
                    pendingSeekDeadlineMs = 0;
                    pendingSeekNextAttemptAtMs = 0;
                }
            } else if (pendingSeekNextAttemptAtMs == 0 ||
                       static_cast<int32_t>(now - pendingSeekNextAttemptAtMs) >= 0) {
                if (gFilePlayer.setAudioFilePosition(static_cast<uint32_t>(pendingSeekFilePosition))) {
                    pendingSeekFilePosition = -1;
                    pendingSeekDeadlineMs = 0;
                    pendingSeekNextAttemptAtMs = 0;
                } else if (pendingSeekDeadlineMs != 0 &&
                           static_cast<int32_t>(now - pendingSeekDeadlineMs) >= 0) {
                    Serial.printf("Audio file-position seek timed out at pos=%ld\n",
                                  static_cast<long>(pendingSeekFilePosition));
                    pendingSeekFilePosition = -1;
                    pendingSeekDeadlineMs = 0;
                    pendingSeekNextAttemptAtMs = 0;
                } else {
                    pendingSeekNextAttemptAtMs = now + 200;
                }
            }
        }

        if (wasRunning && !isRunning) {
            gPlaybackPaused = false;
        }

        if (!isRunning && !gPlaybackPaused && !pendingRequests.empty()) {
            const AudioPlaybackRequest nextRequest = pendingRequests.front();
            pendingRequests.pop_front();
            (void)startRequest(nextRequest);
        }
    }
}

} // namespace

bool audioInit() {
    if (gAudioInitialized) {
        return enableSpeaker();
    }
    if (!enableSpeaker()) {
        return false;
    }
    if (!initEs8311()) {
        return false;
    }
    if (!configurePlayer()) {
        return false;
    }
    Audio::audio_info_callback = handleAudioInfo;
    if (!gAudioCommandQueue) {
        gAudioCommandQueue = xQueueCreate(kAudioCommandQueueDepth, sizeof(AudioCommand));
        if (!gAudioCommandQueue) {
            Serial.println("Audio command queue creation failed.");
            return false;
        }
    }
    if (!gAudioServiceTask) {
        xTaskCreatePinnedToCore(audioServiceTask,
                                "Audio_Service",
                                8192,
                                nullptr,
                                2,
                                &gAudioServiceTask,
                                kAudioServiceCore);
    }
    gAudioInitialized = true;
    return true;
}

bool audioDisableOutputForCameraScan() {
    return disableSpeaker();
}

bool audioQueueFile(AudioStorage storage, const char *path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    if (!audioInit()) {
        return false;
    }

    AudioCommand command = {
        .type = AudioCommandType::Enqueue,
        .request = {},
    };
    fillRequest(&command.request, storage, path);
    return queueCommand(command);
}

bool audioStartFile(AudioStorage storage, const char *path) {
    return audioStartFileAtTime(storage, path, static_cast<uint32_t>(-1));
}

bool audioStartFileMutedUntilRunning(AudioStorage storage, const char *path, uint16_t unmuteDelayMs) {
    if (!path || path[0] == '\0') {
        return false;
    }
    if (!audioInit()) {
        return false;
    }

    AudioCommand command = {
        .type = AudioCommandType::ReplaceQueue,
        .request = {},
    };
    fillRequest(&command.request, storage, path, -1, true, unmuteDelayMs);
    return queueCommand(command);
}

bool audioStartFileAtTime(AudioStorage storage, const char *path, uint32_t startTimeSeconds) {
    if (!path || path[0] == '\0') {
        return false;
    }
    if (!audioInit()) {
        return false;
    }

    AudioCommand command = {
        .type = AudioCommandType::ReplaceQueue,
        .request = {},
    };
    const int32_t fileStartTime = startTimeSeconds == static_cast<uint32_t>(-1)
        ? -1
        : static_cast<int32_t>(startTimeSeconds);
    fillRequest(&command.request, storage, path, fileStartTime, false);
    return queueCommand(command);
}

bool audioSeekToFilePosition(uint32_t filePosition) {
    if (!audioInit()) {
        return false;
    }

    AudioCommand command = {
        .type = AudioCommandType::SeekToFilePosition,
        .request = {},
        .seekFilePosition = filePosition,
    };
    return queueCommand(command);
}

bool audioStopPlayback() {
    AudioCommand command = {
        .type = AudioCommandType::Stop,
        .request = {},
    };
    return queueCommand(command);
}

bool audioTogglePause() {
    if (!audioInit()) {
        return false;
    }

    AudioCommand command = {
        .type = AudioCommandType::TogglePause,
        .request = {},
    };
    return queueCommand(command);
}

bool audioIsRunning() {
    return gFilePlayer.isRunning();
}

bool audioSetVolume(uint8_t volume) {
    if (!audioInit()) {
        return false;
    }

    const uint8_t clampedVolume = std::min<uint8_t>(kMaxVolume, std::max<uint8_t>(kMinVolume, volume));
    AudioCommand command = {
        .type = AudioCommandType::SetVolume,
        .request = {},
        .volume = clampedVolume,
    };
    return queueCommand(command);
}

uint8_t audioVolume() {
    return gCurrentVolume;
}

uint32_t audioCurrentTimeSeconds() {
    return gFilePlayer.getAudioCurrentTime();
}

uint32_t audioCurrentFilePosition() {
    return gFilePlayer.getAudioFilePosition();
}

void audioSetPlaybackFinishedCallback(AudioPlaybackFinishedCallback callback) {
    gPlaybackFinishedCallback = callback;
}
