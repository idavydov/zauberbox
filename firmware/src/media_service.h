#pragma once

#include <Arduino.h>

#include <atomic>
#include <deque>
#include <vector>

#include "audio_driver.h"

enum class UiSound : uint8_t {
    ScanStart,
    WifiConnected,
    Error,
    Button,
    LowBattery,
};

enum class MediaPlaybackMode : uint8_t {
    Stopped,
    Playing,
    Paused,
};

struct MediaPlaybackSnapshot {
    MediaPlaybackMode mode = MediaPlaybackMode::Stopped;
    bool hasAlbum = false;
    String albumId;
    String trackName;
    size_t trackIndex = 0;
    size_t trackCount = 0;
    uint32_t positionSeconds = 0;
    uint32_t durationSeconds = 0;
};

class MediaService {
  public:
    bool begin();
    void update();

    bool playUiSound(UiSound sound);
    bool playTransientUiSoundOverAlbum(UiSound sound);

    bool playAlbum(const char *albumId);
    bool restartCurrentAlbum();
    bool nextTrack();
    bool previousTrackOrRestart();
    bool togglePause();
    bool stopAlbum();
    bool changeVolume(int8_t delta);
    bool ensureStorageMounted();

    bool isStorageReady() const;
    bool hasActiveAlbum() const;
    bool isAlbumPlaying() const;
    MediaPlaybackSnapshot snapshot() const;

  private:
    static void handlePlaybackFinishedStatic(AudioPlaybackEvent event);
    static const char *uiSoundPath(UiSound sound);
    static bool isSupportedAudioFile(const String &path);
    static String baseNameForPath(const String &path);
    bool startQueuedTransientUiSound();

    bool mountStorage();
    bool loadAlbumTracks(const char *albumId);
    bool startCurrentTrack(bool muteUntilRunning = false);
    bool startCurrentTrackAt(uint32_t startTimeSeconds, bool muteUntilRunning = false);
    void clearTransientUiSoundState();
    void resumeAfterTransientUiSound();
    void handlePlaybackFinished(AudioPlaybackEvent event);

    static MediaService *activeInstance_;

    std::atomic<int> playbackFinishedEvent_ = {-1};
    std::vector<String> trackPaths_;
    String currentAlbumId_;
    size_t currentTrackIndex_ = 0;
    bool storageReady_ = false;
    bool albumActive_ = false;
    bool paused_ = false;
    uint32_t pausedAtSeconds_ = 0;
    uint32_t pausedDurationSeconds_ = 0;
    uint32_t pausedFilePosition_ = 0;
    bool transientUiSoundActive_ = false;
    bool transientUiSoundResumePaused_ = false;
    bool transientUiSoundRePausePending_ = false;
    uint32_t transientUiSoundResumeAtSeconds_ = 0;
    uint32_t transientUiSoundResumeFilePosition_ = 0;
    uint32_t transientUiSoundStartReadyAtMs_ = 0;
    uint32_t transientUiSoundResumeReadyAtMs_ = 0;
    std::deque<UiSound> transientUiSoundQueue_;
};
