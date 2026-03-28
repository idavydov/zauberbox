#pragma once

#include <Arduino.h>

#include <atomic>
#include <vector>

enum class UiSound : uint8_t {
    Boot,
    WifiConnected,
    Error,
    Sleep,
    Button,
};

class MediaService {
  public:
    bool begin();
    void update();

    bool playBootSound();
    bool playWifiConnectedSound();
    bool playUiSound(UiSound sound);

    bool playAlbum(const char *albumId);
    bool restartCurrentAlbum();
    bool nextTrack();
    bool previousTrackOrRestart();
    bool togglePause();
    bool stopAlbum();

    bool isStorageReady() const;

  private:
    static void handlePlaybackFinishedStatic();
    static const char *uiSoundPath(UiSound sound);
    static bool isSupportedAudioFile(const String &path);

    bool mountStorage();
    bool loadAlbumTracks(const char *albumId);
    bool startCurrentTrack();
    void handlePlaybackFinished();

    static MediaService *activeInstance_;

    std::atomic<bool> playbackFinished_ = false;
    std::vector<String> trackPaths_;
    String currentAlbumId_;
    size_t currentTrackIndex_ = 0;
    bool storageReady_ = false;
    bool albumActive_ = false;
    bool paused_ = false;
};
