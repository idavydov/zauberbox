#include "media_service.h"

#include <SD_MMC.h>
#include <strings.h>

#include <algorithm>

#include "app_state.h"
#include "audio_driver.h"
#include "io_expander.h"

namespace {

constexpr char kScanStartSoundPath[] = "/scan_start.wav";
constexpr char kWifiConnectedSoundPath[] = "/wifi_connected.wav";
constexpr char kErrorSoundPath[] = "/error.wav";
constexpr char kSleepSoundPath[] = "/sleep.wav";
constexpr char kButtonSoundPath[] = "/button.wav";
constexpr char kSdMountPath[] = "/sdcard";
constexpr uint32_t kPreviousTrackThresholdSeconds = 5;
constexpr int kSdClkPin = 40;
constexpr int kSdCmdPin = 42;
constexpr int kSdD0Pin = 41;

bool endsWithIgnoreCase(const String &value, const char *suffix) {
    const size_t valueLength = value.length();
    const size_t suffixLength = strlen(suffix);
    if (valueLength < suffixLength) {
        return false;
    }

    const char *valueTail = value.c_str() + (valueLength - suffixLength);
    return strcasecmp(valueTail, suffix) == 0;
}

} // namespace

MediaService *MediaService::activeInstance_ = nullptr;

bool MediaService::begin() {
    activeInstance_ = this;
    audioSetPlaybackFinishedCallback(handlePlaybackFinishedStatic);
    return true;
}

void MediaService::update() {
    const int eventValue = playbackFinishedEvent_.exchange(-1);
    if (eventValue < 0) {
        return;
    }

    handlePlaybackFinished(static_cast<AudioPlaybackEvent>(eventValue));
}

bool MediaService::playWifiConnectedSound() {
    return playUiSound(UiSound::WifiConnected);
}

bool MediaService::playUiSound(UiSound sound) {
    const char *path = uiSoundPath(sound);
    if (!path) {
        return false;
    }

    return audioQueueFile(AudioStorage::LittleFs, path);
}

bool MediaService::playAlbum(const char *albumId) {
    if (!loadAlbumTracks(albumId)) {
        playUiSound(UiSound::Error);
        return false;
    }

    currentTrackIndex_ = 0;
    albumActive_ = true;
    paused_ = false;

    if (!startCurrentTrack()) {
        albumActive_ = false;
        playUiSound(UiSound::Error);
        return false;
    }

    appStateStore().transitionTo(AppState::Playing);
    return true;
}

bool MediaService::restartCurrentAlbum() {
    if (!albumActive_ || trackPaths_.empty()) {
        return false;
    }

    currentTrackIndex_ = 0;
    paused_ = false;
    if (!startCurrentTrack()) {
        return false;
    }

    appStateStore().transitionTo(AppState::Playing);
    return true;
}

bool MediaService::nextTrack() {
    if (!albumActive_ || currentTrackIndex_ + 1 >= trackPaths_.size()) {
        return false;
    }

    currentTrackIndex_++;
    paused_ = false;
    if (!startCurrentTrack()) {
        currentTrackIndex_--;
        return false;
    }

    appStateStore().transitionTo(AppState::Playing);
    return true;
}

bool MediaService::previousTrackOrRestart() {
    if (!albumActive_ || trackPaths_.empty()) {
        return false;
    }

    const uint32_t currentTime = audioCurrentTimeSeconds();
    if (currentTime < kPreviousTrackThresholdSeconds && currentTrackIndex_ > 0) {
        currentTrackIndex_--;
    }

    paused_ = false;
    if (!startCurrentTrack()) {
        return false;
    }

    appStateStore().transitionTo(AppState::Playing);
    return true;
}

bool MediaService::togglePause() {
    if (!albumActive_) {
        return false;
    }
    if (!audioTogglePause()) {
        return false;
    }

    paused_ = !paused_;
    appStateStore().transitionTo(paused_ ? AppState::Paused : AppState::Playing);
    return true;
}

bool MediaService::stopAlbum() {
    if (!albumActive_ && !audioIsRunning()) {
        return false;
    }

    albumActive_ = false;
    paused_ = false;
    trackPaths_.clear();
    currentAlbumId_ = "";
    currentTrackIndex_ = 0;
    if (!audioStopPlayback()) {
        return false;
    }

    appStateStore().transitionTo(AppState::QrScan);
    return true;
}

bool MediaService::changeVolume(int8_t delta) {
    const int nextVolume = static_cast<int>(audioVolume()) + delta;
    const int clampedVolume = std::max(0, std::min(21, nextVolume));
    return audioSetVolume(static_cast<uint8_t>(clampedVolume));
}

bool MediaService::ensureStorageMounted() {
    return mountStorage();
}

bool MediaService::isStorageReady() const {
    return storageReady_;
}

void MediaService::handlePlaybackFinishedStatic(AudioPlaybackEvent event) {
    if (!activeInstance_) {
        return;
    }

    activeInstance_->playbackFinishedEvent_.store(static_cast<int>(event));
}

const char *MediaService::uiSoundPath(UiSound sound) {
    switch (sound) {
        case UiSound::ScanStart:
            return kScanStartSoundPath;
        case UiSound::WifiConnected:
            return kWifiConnectedSoundPath;
        case UiSound::Error:
            return kErrorSoundPath;
        case UiSound::Sleep:
            return kSleepSoundPath;
        case UiSound::Button:
            return kButtonSoundPath;
    }

    return nullptr;
}

bool MediaService::isSupportedAudioFile(const String &path) {
    static constexpr const char *kSupportedExtensions[] = {
        ".aac",
        ".flac",
        ".m4a",
        ".mp3",
        ".ogg",
        ".wav",
    };

    for (const char *extension : kSupportedExtensions) {
        if (endsWithIgnoreCase(path, extension)) {
            return true;
        }
    }

    return false;
}

bool MediaService::mountStorage() {
    if (storageReady_) {
        return true;
    }

    if (!ioExpanderPinMode(kIoExpanderSdCardAuxPin, OUTPUT)) {
        Serial.println("Media service: SD card EXIO setup failed.");
        return false;
    }
    if (!ioExpanderDigitalWrite(kIoExpanderSdCardAuxPin, HIGH)) {
        Serial.println("Media service: failed to enable SD card EXIO line.");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    if (!SD_MMC.setPins(kSdClkPin, kSdCmdPin, kSdD0Pin, -1, -1, -1)) {
        Serial.println("Media service: SD_MMC pin configuration failed.");
        return false;
    }

    if (!SD_MMC.begin(kSdMountPath, true, true)) {
        Serial.println("Media service: SD card mount failed.");
        return false;
    }

    storageReady_ = true;
    Serial.println("Media service: SD card mounted.");
    return true;
}

bool MediaService::loadAlbumTracks(const char *albumId) {
    if (!albumId || albumId[0] == '\0') {
        Serial.println("Media service: album id missing.");
        return false;
    }
    if (!mountStorage()) {
        return false;
    }

    const String albumPath = String("/") + albumId;
    File albumDir = SD_MMC.open(albumPath.c_str());
    if (!albumDir) {
        Serial.printf("Media service: album not found: %s\n", albumPath.c_str());
        return false;
    }
    if (!albumDir.isDirectory()) {
        Serial.printf("Media service: album path is not a directory: %s\n", albumPath.c_str());
        albumDir.close();
        return false;
    }

    std::vector<String> discoveredTracks;
    for (File entry = albumDir.openNextFile(); entry; entry = albumDir.openNextFile()) {
        if (!entry.isDirectory()) {
            const String path = entry.path();
            if (isSupportedAudioFile(path)) {
                discoveredTracks.push_back(path);
            }
        }
        entry.close();
    }
    albumDir.close();

    std::sort(discoveredTracks.begin(),
              discoveredTracks.end(),
              [](const String &left, const String &right) {
                  return strcasecmp(left.c_str(), right.c_str()) < 0;
              });

    if (discoveredTracks.empty()) {
        Serial.printf("Media service: no supported audio files in %s\n", albumPath.c_str());
        return false;
    }

    trackPaths_ = std::move(discoveredTracks);
    currentAlbumId_ = albumId;
    Serial.printf("Media service: loaded album %s with %u tracks\n",
                  currentAlbumId_.c_str(),
                  static_cast<unsigned>(trackPaths_.size()));
    return true;
}

bool MediaService::startCurrentTrack() {
    if (!albumActive_ || currentTrackIndex_ >= trackPaths_.size()) {
        return false;
    }

    const String &path = trackPaths_[currentTrackIndex_];
    if (!audioStartFile(AudioStorage::SdCard, path.c_str())) {
        Serial.printf("Media service: failed to start track %s\n", path.c_str());
        return false;
    }

    Serial.printf("Media service: playing album %s track %u/%u: %s\n",
                  currentAlbumId_.c_str(),
                  static_cast<unsigned>(currentTrackIndex_ + 1),
                  static_cast<unsigned>(trackPaths_.size()),
                  path.c_str());
    return true;
}

void MediaService::handlePlaybackFinished(AudioPlaybackEvent event) {
    if (!albumActive_ || paused_) {
        return;
    }

    if (event == AudioPlaybackEvent::Failed) {
        Serial.printf("Media service: track failed in album %s at index %u\n",
                      currentAlbumId_.c_str(),
                      static_cast<unsigned>(currentTrackIndex_ + 1));
    }

    while (currentTrackIndex_ + 1 < trackPaths_.size()) {
        currentTrackIndex_++;
        if (startCurrentTrack()) {
            return;
        }
        Serial.printf("Media service: skipping bad track %u/%u\n",
                      static_cast<unsigned>(currentTrackIndex_ + 1),
                      static_cast<unsigned>(trackPaths_.size()));
    }

    Serial.printf("Media service: album finished: %s\n", currentAlbumId_.c_str());
    albumActive_ = false;
    paused_ = false;
    trackPaths_.clear();
    currentAlbumId_ = "";
    currentTrackIndex_ = 0;
    const bool moved = appStateStore().transitionTo(AppState::Idle);
    if (!moved) {
        Serial.println("Media service: Idle transition rejected.");
    }
}
