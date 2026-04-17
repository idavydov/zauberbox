#pragma once

#include <FS.h>
#include <time.h>
#include <WebServer.h>

#include "media_service.h"

class QrService;
class WebServerService {
  public:
    void begin(MediaService *mediaService, QrService *qrService);
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
    void handleStatus();

    WebServer server_{80};
    MediaService *mediaService_ = nullptr;
    QrService *qrService_ = nullptr;
    bool routesRegistered_ = false;
    bool running_ = false;
    File uploadFile_;
    String uploadTargetPath_;
    bool uploadFailed_ = false;
    String uploadError_;
    bool uploadTargetHasClientTimestamp_ = false;
    time_t uploadTargetLastWrite_ = 0;
};
