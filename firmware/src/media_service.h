#pragma once

#include <Arduino.h>

#include <atomic>
#include <vector>

#include "audio_driver.h"

enum class UiSound : uint8_t {
    ScanStart,
    WifiConnected,
    Error,
    Sleep,
    Button,
};

class MediaService {
  public:
    bool begin();
    void update();

    bool playWifiConnectedSound();
    bool playUiSound(UiSound sound);

    bool playAlbum(const char *albumId);
    bool restartCurrentAlbum();
    bool nextTrack();
    bool previousTrackOrRestart();
    bool togglePause();
    bool stopAlbum();
    bool changeVolume(int8_t delta);
    bool ensureStorageMounted();

    bool isStorageReady() const;

  private:
    static void handlePlaybackFinishedStatic(AudioPlaybackEvent event);
    static const char *uiSoundPath(UiSound sound);
    static bool isSupportedAudioFile(const String &path);

    bool mountStorage();
    bool loadAlbumTracks(const char *albumId);
    bool startCurrentTrack();
    void handlePlaybackFinished(AudioPlaybackEvent event);

    static MediaService *activeInstance_;

    std::atomic<int> playbackFinishedEvent_ = {-1};
    std::vector<String> trackPaths_;
    String currentAlbumId_;
    size_t currentTrackIndex_ = 0;
    bool storageReady_ = false;
    bool albumActive_ = false;
    bool paused_ = false;
};
