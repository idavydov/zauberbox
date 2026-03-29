#pragma once

#include <Arduino.h>

enum class AudioStorage : uint8_t {
    LittleFs,
    SdCard,
};

using AudioPlaybackFinishedCallback = void (*)();

bool audioInit();
bool audioDisableOutputForCameraScan();
bool audioQueueFile(AudioStorage storage, const char *path);
bool audioStartFile(AudioStorage storage, const char *path);
bool audioStopPlayback();
bool audioTogglePause();
bool audioIsRunning();
bool audioSetVolume(uint8_t volume);
uint8_t audioVolume();
uint32_t audioCurrentTimeSeconds();
void audioSetPlaybackFinishedCallback(AudioPlaybackFinishedCallback callback);
