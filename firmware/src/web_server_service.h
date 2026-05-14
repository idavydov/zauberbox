#pragma once

#include <FS.h>
#include <functional>
#include <time.h>
#include <WebServer.h>

#include "album_input_service.h"
#include "media_service.h"

class WebServerService {
  public:
    using PlayAlbumCallback = std::function<bool(const String &albumId)>;
    using PlaybackActionCallback = std::function<bool()>;

    void begin(MediaService *mediaService,
               AlbumInputService *albumInputService,
               PlayAlbumCallback onPlayAlbum,
               PlaybackActionCallback onPreviousTrack,
               PlaybackActionCallback onNextTrack,
               PlaybackActionCallback onTogglePause);
    void update();
    void stop();

  private:
    void startIfNeeded();
    void stopIfNeeded();
    void registerRoutes();
    bool ensureAuthorized();
    bool ensureStorageMounted();
    static bool isSafePathSegment(const String &value);
    static bool removePathRecursive(const String &path);
    static String sanitizeUploadFileName(const String &value);
    static String baseNameForPath(const char *value);
    static String joinStoragePath(const String &directory, const String &fileName = "");
    static String buildFileEtag(const String &fileName, size_t fileSize, time_t lastWrite);
    static bool requestIfNoneMatchMatches(const String &requestHeader, const String &etag);
    static bool parseClientLastModifiedMs(const String &value, time_t *outSeconds);
    static bool applyFileTimestamp(const String &path, time_t lastWrite);
    static const char *mimeTypeForPath(const String &path);
    void sendJsonError(int code, const char *message);
    void sendFirstLoginPage(const char *errorMessage = nullptr);
    void handleUpdatePassword();
    void handleIndex();
    void handleStaticAsset();
    void handleListAlbums();
    void handleListFiles();
    void handleGetFile();
    void handleMakeDirectory();
    void handleRemoveDirectory();
    void handleDeleteFile();
    void handleRenameFile();
    void handleUploadStart();
    void handleUploadData();
    void handleDebugCameraPreviewStart();
    void handleDebugCameraPreviewStop();
    void handleDebugCameraFrame();
    void handleDebugLogs();
    void handleDebugBatteryOverride();
    void handleBeginTagWrite();
    void handleTagWriteStatus();
    void handleCancelTagWrite();
    void handleStatus();
    void handlePlayAlbum();
    void handlePreviousTrack();
    void handleNextTrack();
    void handleTogglePause();
    bool supportsDebugCameraPreview() const;
    AlbumInputBackend currentBackend() const;

    WebServer server_{80};
    MediaService *mediaService_ = nullptr;
    AlbumInputService *albumInputService_ = nullptr;
    PlayAlbumCallback onPlayAlbum_;
    PlaybackActionCallback onPreviousTrack_;
    PlaybackActionCallback onNextTrack_;
    PlaybackActionCallback onTogglePause_;
    bool routesRegistered_ = false;
    bool running_ = false;
    File uploadFile_;
    String uploadTargetPath_;
    String uploadTempPath_;
    String uploadBackupPath_;
    bool uploadFailed_ = false;
    String uploadError_;
    bool uploadTargetHasClientTimestamp_ = false;
    time_t uploadTargetLastWrite_ = 0;
};
