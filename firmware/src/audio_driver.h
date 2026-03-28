#pragma once

#include <Arduino.h>

enum class AudioStorage : uint8_t {
    LittleFs,
    SdCard,
};

using AudioPlaybackFinishedCallback = void (*)();

bool audioInit();
bool audioQueueFile(AudioStorage storage, const char *path);
bool audioStartFile(AudioStorage storage, const char *path);
bool audioStopPlayback();
bool audioTogglePause();
bool audioIsRunning();
uint32_t audioCurrentTimeSeconds();
void audioSetPlaybackFinishedCallback(AudioPlaybackFinishedCallback callback);
