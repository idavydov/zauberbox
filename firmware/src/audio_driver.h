#pragma once

#include <Arduino.h>

enum class AudioStorage : uint8_t {
    LittleFs,
    SdCard,
};

enum class AudioPlaybackEvent : uint8_t {
    Finished,
    Failed,
};

using AudioPlaybackFinishedCallback = void (*)(AudioPlaybackEvent event);

bool audioInit();
bool audioDisableOutputForCameraScan();
bool audioQueueFile(AudioStorage storage, const char *path);
bool audioStartFile(AudioStorage storage, const char *path);
bool audioStartFileAtTime(AudioStorage storage, const char *path, uint32_t startTimeSeconds);
bool audioStopPlayback();
bool audioTogglePause();
bool audioIsRunning();
bool audioSetVolume(uint8_t volume);
uint8_t audioVolume();
uint32_t audioCurrentTimeSeconds();
uint32_t audioCurrentFilePosition();
bool audioSeekToFilePosition(uint32_t filePosition);
void audioSetPlaybackFinishedCallback(AudioPlaybackFinishedCallback callback);
