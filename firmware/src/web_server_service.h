#pragma once

#include <FS.h>
#include <WebServer.h>

#include "media_service.h"

class WebServerService {
  public:
    void begin(MediaService *mediaService);
    void update();

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
    static const char *mimeTypeForPath(const String &path);
    void sendJsonError(int code, const char *message);
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

    WebServer server_{80};
    MediaService *mediaService_ = nullptr;
    bool routesRegistered_ = false;
    bool running_ = false;
    File uploadFile_;
    String uploadTargetPath_;
    bool uploadFailed_ = false;
    String uploadError_;
};
