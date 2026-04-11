#include "web_server_service.h"

#include <algorithm>
#include <errno.h>
#include <inttypes.h>
#include <utime.h>
#include <vector>

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <WiFi.h>

#include "app_state.h"
#include "config_service.h"
#include "qr_service.h"

namespace {

constexpr char kIndexPath[] = "/index.html";
constexpr char kFirstLoginPath[] = "/first_login.html";

String readJsonString(WebServer &server, const char *key) {
    StaticJsonDocument<256> doc;
    const DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        return "";
    }

    return doc[key].as<String>();
}

String lowerCaseCopy(const String &value) {
    String lower = value;
    lower.toLowerCase();
    return lower;
}

String urlEncode(const String &value) {
    static const char hex[] = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(value.length() * 3);
    for (size_t i = 0; i < value.length(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        const bool safe = (ch >= 'a' && ch <= 'z') ||
                          (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') ||
                          ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe) {
            encoded += static_cast<char>(ch);
        } else {
            encoded += '%';
            encoded += hex[(ch >> 4) & 0x0F];
            encoded += hex[ch & 0x0F];
        }
    }
    return encoded;
}

String asciiFallbackFileName(const String &value) {
    String fallback;
    fallback.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == ' ') {
            fallback += static_cast<char>(ch);
        } else {
            fallback += '_';
        }
    }
    fallback.trim();
    if (fallback.isEmpty()) {
        fallback = "download";
    }
    return fallback;
}

String readLittleFsTextFile(const char *path) {
    if (!LittleFS.exists(path)) {
        return "";
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        return "";
    }

    String body = file.readString();
    file.close();
    return body;
}

uint64_t fnv1a64Begin() {
    return 1469598103934665603ULL;
}

uint64_t fnv1a64UpdateBytes(uint64_t hash, const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t fnv1a64UpdateString(uint64_t hash, const String &value) {
    return fnv1a64UpdateBytes(hash,
                             reinterpret_cast<const uint8_t *>(value.c_str()),
                             value.length());
}

uint64_t fnv1a64UpdateUint64(uint64_t hash, uint64_t value) {
    return fnv1a64UpdateBytes(hash,
                             reinterpret_cast<const uint8_t *>(&value),
                             sizeof(value));
}

} // namespace

void WebServerService::begin(MediaService *mediaService, QrService *qrService) {
    mediaService_ = mediaService;
    qrService_ = qrService;
    if (!routesRegistered_) {
        registerRoutes();
        routesRegistered_ = true;
    }
}

void WebServerService::update() {
    startIfNeeded();
    stopIfNeeded();

    if (running_) {
        server_.handleClient();
    }
}

void WebServerService::startIfNeeded() {
    if (running_) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED || appStateStore().wifiMode() != WifiMode::Connected) {
        return;
    }

    server_.begin();
    running_ = true;
    const WebAuthConfig auth = configService().loadWebAuthConfig();
    Serial.printf("Web server: started on http://%s%s\n",
                  WiFi.localIP().toString().c_str(),
                  auth.isDefault ? " (default auth)" : "");
}

void WebServerService::stopIfNeeded() {
    if (!running_) {
        return;
    }
    if (WiFi.status() == WL_CONNECTED && appStateStore().wifiMode() == WifiMode::Connected) {
        return;
    }

    server_.stop();
    running_ = false;
    Serial.println("Web server: stopped.");
}

void WebServerService::registerRoutes() {
    static const char *kRequestHeaders[] = {"If-None-Match"};
    server_.collectHeaders(kRequestHeaders, 1);

    server_.on("/", HTTP_GET, [this]() {
        handleIndex();
    });
    server_.on("/index.html", HTTP_GET, [this]() {
        handleIndex();
    });

    server_.on("/api/auth/password", HTTP_POST, [this]() {
        handleUpdatePassword();
    });

    for (const char *asset : {"/style.css", "/app.js", "/pico.min.css", "/qrcode.min.js"}) {
        server_.on(asset, HTTP_GET, [this]() {
            handleStaticAsset();
        });
    }

    server_.on("/api/list", HTTP_GET, [this]() {
        handleListAlbums();
    });
    server_.on("/api/files", HTTP_GET, [this]() {
        handleListFiles();
    });
    server_.on("/api/file", HTTP_GET, [this]() {
        handleGetFile();
    });
    server_.on("/api/mkdir", HTTP_POST, [this]() {
        handleMakeDirectory();
    });
    server_.on("/api/rmdir", HTTP_POST, [this]() {
        handleRemoveDirectory();
    });
    server_.on("/api/delete", HTTP_POST, [this]() {
        handleDeleteFile();
    });
    server_.on("/api/rename", HTTP_POST, [this]() {
        handleRenameFile();
    });
    server_.on("/api/upload",
               HTTP_POST,
               [this]() {
                   handleUploadStart();
               },
               [this]() {
                   handleUploadData();
               });
    server_.on("/api/debug/camera-preview/start", HTTP_POST, [this]() {
        handleDebugCameraPreviewStart();
    });
    server_.on("/api/debug/camera-preview/stop", HTTP_POST, [this]() {
        handleDebugCameraPreviewStop();
    });
    server_.on("/api/debug/camera-frame", HTTP_GET, [this]() {
        handleDebugCameraFrame();
    });
}

bool WebServerService::ensureAuthorized() {
    const WebAuthConfig auth = configService().loadWebAuthConfig();
    if (!auth.isValid()) {
        sendJsonError(500, "Web auth config invalid");
        return false;
    }
    if (server_.authenticate(auth.username.c_str(), auth.password.c_str())) {
        return true;
    }

    server_.requestAuthentication(BASIC_AUTH, "Zauberbox", "Authentication required");
    return false;
}

bool WebServerService::ensureStorageMounted() {
    return mediaService_ && mediaService_->ensureStorageMounted();
}

bool WebServerService::isSafePathSegment(const String &value) {
    if (value.isEmpty() || value == "." || value == "..") {
        return false;
    }
    for (size_t i = 0; i < value.length(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch < 0x20 || ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            return false;
        }
    }
    return true;
}

bool WebServerService::removePathRecursive(const String &path) {
    File entry = SD_MMC.open(path.c_str());
    if (!entry) {
        return false;
    }

    if (!entry.isDirectory()) {
        entry.close();
        return SD_MMC.remove(path.c_str());
    }

    for (File child = entry.openNextFile(); child; child = entry.openNextFile()) {
        const String childName = baseNameForPath(child.name());
        const String childPath = path + "/" + childName;
        const bool ok = child.isDirectory()
                            ? removePathRecursive(childPath)
                            : SD_MMC.remove(childPath.c_str());
        child.close();
        if (!ok) {
            entry.close();
            return false;
        }
    }

    entry.close();
    return SD_MMC.rmdir(path.c_str());
}

String WebServerService::sanitizeUploadFileName(const String &value) {
    String name = baseNameForPath(value.c_str());
    name.trim();
    while (name.startsWith(".")) {
        name.remove(0, 1);
    }
    return name;
}

String WebServerService::baseNameForPath(const char *value) {
    String name = value ? value : "";
    name.replace("\\", "/");
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) {
        name = name.substring(slash + 1);
    }
    return name;
}

String WebServerService::joinStoragePath(const String &directory, const String &fileName) {
    String path = "/";
    path += directory;
    if (!fileName.isEmpty()) {
        path += "/";
        path += fileName;
    }
    return path;
}

String WebServerService::buildFileEtag(const String &fileName, size_t fileSize, time_t lastWrite) {
    uint64_t hash = fnv1a64Begin();
    hash = fnv1a64UpdateString(hash, fileName);
    hash = fnv1a64UpdateUint64(hash, static_cast<uint64_t>(fileSize));
    hash = fnv1a64UpdateUint64(hash, static_cast<uint64_t>(lastWrite));

    char buffer[24];
    snprintf(buffer, sizeof(buffer), "\"%016" PRIx64 "\"", hash);
    return String(buffer);
}

bool WebServerService::requestIfNoneMatchMatches(const String &requestHeader, const String &etag) {
    if (requestHeader.isEmpty()) {
        return false;
    }
    if (requestHeader == "*" || requestHeader == etag) {
        return true;
    }

    int start = 0;
    while (start < requestHeader.length()) {
        int comma = requestHeader.indexOf(',', start);
        if (comma < 0) {
            comma = requestHeader.length();
        }

        String candidate = requestHeader.substring(start, comma);
        candidate.trim();
        if (candidate.startsWith("W/")) {
            candidate.remove(0, 2);
            candidate.trim();
        }
        if (candidate == etag) {
            return true;
        }

        start = comma + 1;
    }

    return false;
}

bool WebServerService::parseClientLastModifiedMs(const String &value, time_t *outSeconds) {
    if (!outSeconds || value.isEmpty()) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    const unsigned long long millisSinceEpoch = strtoull(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0') {
        return false;
    }
    if (millisSinceEpoch == 0) {
        return false;
    }

    const time_t secondsSinceEpoch = static_cast<time_t>(millisSinceEpoch / 1000ULL);
    if (secondsSinceEpoch <= 0) {
        return false;
    }

    *outSeconds = secondsSinceEpoch;
    return true;
}

bool WebServerService::applyFileTimestamp(const String &path, time_t lastWrite) {
    if (path.isEmpty() || lastWrite <= 0) {
        return false;
    }

    const String fullSystemPath = String(SD_MMC.mountpoint()) + path;
    struct utimbuf times;
    times.actime = lastWrite;
    times.modtime = lastWrite;
    return ::utime(fullSystemPath.c_str(), &times) == 0;
}

const char *WebServerService::mimeTypeForPath(const String &path) {
    const String lower = path;
    if (lower.endsWith(".html")) return "text/html";
    if (lower.endsWith(".css")) return "text/css";
    if (lower.endsWith(".js")) return "application/javascript";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    if (lower.endsWith(".png")) return "image/png";
    if (lower.endsWith(".mp3")) return "audio/mpeg";
    if (lower.endsWith(".wav")) return "audio/wav";
    if (lower.endsWith(".json")) return "application/json";
    return "application/octet-stream";
}

void WebServerService::sendJsonError(int code, const char *message) {
    StaticJsonDocument<128> doc;
    doc["success"] = false;
    doc["error"] = message;
    String body;
    serializeJson(doc, body);
    server_.send(code, "application/json", body);
}

void WebServerService::sendFirstLoginPage(const char *errorMessage) {
    String body = readLittleFsTextFile(kFirstLoginPath);
    if (body.isEmpty()) {
        server_.send(503, "text/plain", "First-login page missing. Upload LittleFS data.");
        return;
    }

    const WebAuthConfig auth = configService().loadWebAuthConfig();
    body.replace("__USERNAME__", auth.username.isEmpty() ? "admin" : auth.username);

    const String errorBlock = (errorMessage && errorMessage[0] != '\0')
                                  ? String("<div class=\"error\">") + errorMessage + "</div>"
                                  : "";
    body.replace("__ERROR_BLOCK__", errorBlock);
    server_.send(200, "text/html; charset=utf-8", body);
}

void WebServerService::handleUpdatePassword() {
    if (!ensureAuthorized()) {
        return;
    }

    const String password = readJsonString(server_, "password");
    String trimmedPassword = password;
    trimmedPassword.trim();
    if (trimmedPassword.length() < 4) {
        sendJsonError(400, "Password must be at least 4 characters");
        return;
    }

    if (!configService().saveWebAuthPassword(trimmedPassword)) {
        sendJsonError(500, "Failed to save password");
        return;
    }

    server_.send(200, "application/json", "{\"success\":true}");
}

void WebServerService::handleIndex() {
    if (!ensureAuthorized()) {
        return;
    }
    const WebAuthConfig auth = configService().loadWebAuthConfig();
    if (auth.isDefault) {
        sendFirstLoginPage();
        return;
    }
    if (!LittleFS.exists(kIndexPath)) {
        server_.send(503, "text/plain", "Web app assets missing. Upload LittleFS data.");
        return;
    }

    File file = LittleFS.open(kIndexPath, "r");
    server_.streamFile(file, "text/html");
    file.close();
}

void WebServerService::handleStaticAsset() {
    if (!ensureAuthorized()) {
        return;
    }

    const String path = server_.uri();
    if (!LittleFS.exists(path)) {
        server_.send(404, "text/plain", "Not found");
        return;
    }

    File file = LittleFS.open(path, "r");
    server_.streamFile(file, mimeTypeForPath(path));
    file.close();
}

void WebServerService::handleListAlbums() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!ensureStorageMounted()) {
        sendJsonError(503, "SD card unavailable");
        return;
    }

    DynamicJsonDocument doc(2048);
    JsonArray result = doc.to<JsonArray>();
    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory()) {
        sendJsonError(500, "Failed to open storage root");
        if (root) root.close();
        return;
    }

    std::vector<String> albumNames;
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        if (entry.isDirectory()) {
            albumNames.push_back(String(entry.name()));
        }
        entry.close();
    }
    root.close();
    std::sort(albumNames.begin(), albumNames.end(), [](const String &left, const String &right) {
        return strcasecmp(left.c_str(), right.c_str()) < 0;
    });

    for (const String &albumName : albumNames) {
        JsonObject item = result.createNestedObject();
        item["name"] = albumName;

        const String albumPath = joinStoragePath(albumName);
        File albumDir = SD_MMC.open(albumPath.c_str());
        String coverPath;
        String firstAudio;
        if (albumDir && albumDir.isDirectory()) {
            for (File entry = albumDir.openNextFile(); entry; entry = albumDir.openNextFile()) {
                const String entryName = baseNameForPath(entry.name());
                const String lower = lowerCaseCopy(entryName);
                if (!entry.isDirectory()) {
                    if (coverPath.isEmpty() &&
                        (lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png"))) {
                        coverPath = String("/api/file?path=") + urlEncode(albumName) + "&name=" + urlEncode(entryName);
                    }
                    if (firstAudio.isEmpty() &&
                        (lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".flac") ||
                         lower.endsWith(".m4a") || lower.endsWith(".aac") || lower.endsWith(".ogg"))) {
                        firstAudio = entryName;
                    }
                }
                entry.close();
            }
            albumDir.close();
        }

        if (!coverPath.isEmpty()) {
            item["cover"] = coverPath;
        }
        if (!firstAudio.isEmpty()) {
            item["first_mp3"] = firstAudio;
        }
    }

    String body;
    serializeJson(doc, body);
    server_.send(200, "application/json", body);
}

void WebServerService::handleListFiles() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!ensureStorageMounted()) {
        sendJsonError(503, "SD card unavailable");
        return;
    }

    const String directory = server_.arg("path");
    if (!isSafePathSegment(directory)) {
        sendJsonError(400, "Invalid path");
        return;
    }

    const String fullPath = joinStoragePath(directory);
    File dir = SD_MMC.open(fullPath.c_str());
    if (!dir || !dir.isDirectory()) {
        sendJsonError(404, "Directory not found");
        if (dir) dir.close();
        return;
    }

    DynamicJsonDocument doc(2048);
    JsonArray result = doc.to<JsonArray>();
    std::vector<String> names;
    for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        names.push_back(baseNameForPath(entry.name()));
        entry.close();
    }
    dir.close();
    std::sort(names.begin(), names.end(), [](const String &left, const String &right) {
        return strcasecmp(left.c_str(), right.c_str()) < 0;
    });

    for (const String &name : names) {
        JsonObject item = result.createNestedObject();
        item["name"] = name;
        item["type"] = mimeTypeForPath(name);
    }

    String body;
    serializeJson(doc, body);
    server_.send(200, "application/json", body);
}

void WebServerService::handleGetFile() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!ensureStorageMounted()) {
        server_.send(503, "text/plain", "SD card unavailable");
        return;
    }

    const String directory = server_.arg("path");
    const String fileName = server_.arg("name");
    if (!isSafePathSegment(directory) || !isSafePathSegment(fileName)) {
        server_.send(400, "text/plain", "Invalid path");
        return;
    }

    const String fullPath = joinStoragePath(directory, fileName);
    File file = SD_MMC.open(fullPath.c_str(), "r");
    if (!file) {
        server_.send(404, "text/plain", "Not found");
        return;
    }

    const String etag = buildFileEtag(fileName, file.size(), file.getLastWrite());
    server_.sendHeader("ETag", etag);
    server_.sendHeader("Cache-Control", "private, max-age=0, must-revalidate");
    if (requestIfNoneMatchMatches(server_.header("If-None-Match"), etag)) {
        file.close();
        server_.send(304);
        return;
    }

    const String fallbackName = asciiFallbackFileName(fileName);
    const String contentDisposition =
        String("attachment; filename=\"") + fallbackName +
        "\"; filename*=UTF-8''" + urlEncode(fileName);
    server_.sendHeader("Content-Disposition", contentDisposition);
    server_.streamFile(file, mimeTypeForPath(fileName));
    file.close();
}

void WebServerService::handleMakeDirectory() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!ensureStorageMounted()) {
        sendJsonError(503, "SD card unavailable");
        return;
    }

    const String name = readJsonString(server_, "name");
    if (!isSafePathSegment(name)) {
        sendJsonError(400, "Invalid directory name");
        return;
    }

    if (!SD_MMC.mkdir(joinStoragePath(name).c_str()) && !SD_MMC.exists(joinStoragePath(name).c_str())) {
        sendJsonError(500, "Failed to create directory");
        return;
    }

    server_.send(200, "application/json", "{\"success\":true}");
}

void WebServerService::handleRemoveDirectory() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!ensureStorageMounted()) {
        sendJsonError(503, "SD card unavailable");
        return;
    }

    const String name = readJsonString(server_, "name");
    if (!isSafePathSegment(name)) {
        sendJsonError(400, "Invalid directory name");
        return;
    }

    if (!removePathRecursive(joinStoragePath(name))) {
        sendJsonError(500, "Failed to remove directory");
        return;
    }

    server_.send(200, "application/json", "{\"success\":true}");
}

void WebServerService::handleDeleteFile() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!ensureStorageMounted()) {
        sendJsonError(503, "SD card unavailable");
        return;
    }

    const String directory = readJsonString(server_, "path");
    const String fileName = readJsonString(server_, "file_name");
    if (!isSafePathSegment(directory) || !isSafePathSegment(fileName)) {
        sendJsonError(400, "Invalid path");
        return;
    }

    if (!SD_MMC.remove(joinStoragePath(directory, fileName).c_str())) {
        sendJsonError(500, "Failed to delete file");
        return;
    }

    server_.send(200, "application/json", "{\"success\":true}");
}

void WebServerService::handleRenameFile() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!ensureStorageMounted()) {
        sendJsonError(503, "SD card unavailable");
        return;
    }

    const String directory = readJsonString(server_, "path");
    const String oldName = readJsonString(server_, "old_name");
    const String newName = readJsonString(server_, "new_name");
    if (!isSafePathSegment(directory) || !isSafePathSegment(oldName) || !isSafePathSegment(newName)) {
        sendJsonError(400, "Invalid path");
        return;
    }

    if (!SD_MMC.rename(joinStoragePath(directory, oldName).c_str(),
                       joinStoragePath(directory, newName).c_str())) {
        sendJsonError(500, "Failed to rename file");
        return;
    }

    server_.send(200, "application/json", "{\"success\":true}");
}

void WebServerService::handleUploadStart() {
    if (!ensureAuthorized()) {
        return;
    }

    if (uploadFailed_) {
        sendJsonError(400, uploadError_.c_str());
    } else {
        server_.send(200, "application/json", "{\"success\":true}");
    }

    uploadFailed_ = false;
    uploadError_ = "";
}

void WebServerService::handleUploadData() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!ensureStorageMounted()) {
        uploadFailed_ = true;
        uploadError_ = "SD card unavailable";
        return;
    }

    HTTPUpload &upload = server_.upload();

    if (upload.status == UPLOAD_FILE_START) {
        uploadFailed_ = false;
        uploadError_ = "";
        if (uploadFile_) {
            uploadFile_.close();
        }
        uploadTargetPath_ = "";
        uploadTargetHasClientTimestamp_ = false;
        uploadTargetLastWrite_ = 0;

        const String directory = server_.arg("path");
        const String uploadType = server_.arg("type");
        if (!isSafePathSegment(directory)) {
            uploadFailed_ = true;
            uploadError_ = "Invalid path";
            return;
        }

        String filename = sanitizeUploadFileName(upload.filename);
        if (uploadType == "cover") {
            filename = "cover.jpg";
        }
        if (!isSafePathSegment(filename)) {
            uploadFailed_ = true;
            uploadError_ = "Invalid filename";
            return;
        }

        const String targetDir = joinStoragePath(directory);
        SD_MMC.mkdir(targetDir.c_str());
        uploadTargetPath_ = joinStoragePath(directory, filename);
        uploadTargetHasClientTimestamp_ =
            parseClientLastModifiedMs(server_.arg("last_modified_ms"), &uploadTargetLastWrite_);
        SD_MMC.remove(uploadTargetPath_.c_str());
        uploadFile_ = SD_MMC.open(uploadTargetPath_.c_str(), FILE_WRITE);
        if (!uploadFile_) {
            uploadFailed_ = true;
            uploadError_ = "Failed to open destination";
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFailed_) {
            return;
        }
        if (uploadFile_) {
            if (uploadFile_.write(upload.buf, upload.currentSize) != upload.currentSize) {
                uploadFailed_ = true;
                uploadError_ = "Failed to write upload chunk";
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile_) {
            uploadFile_.close();
        }
        if (!uploadFailed_ && uploadTargetHasClientTimestamp_ &&
            !applyFileTimestamp(uploadTargetPath_, uploadTargetLastWrite_)) {
            Serial.printf("Web server: failed to set mtime for %s\n",
                          uploadTargetPath_.c_str());
        }
        if (!uploadFailed_) {
            Serial.printf("Web server: uploaded %s (%u bytes)\n",
                          uploadTargetPath_.c_str(),
                          static_cast<unsigned>(upload.totalSize));
        }
        uploadTargetPath_ = "";
        uploadTargetHasClientTimestamp_ = false;
        uploadTargetLastWrite_ = 0;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        uploadFailed_ = true;
        uploadError_ = "Upload aborted";
        if (uploadFile_) {
            uploadFile_.close();
        }
        if (!uploadTargetPath_.isEmpty()) {
            SD_MMC.remove(uploadTargetPath_.c_str());
        }
        uploadTargetPath_ = "";
        uploadTargetHasClientTimestamp_ = false;
        uploadTargetLastWrite_ = 0;
    }
}

void WebServerService::handleDebugCameraFrame() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!qrService_) {
        sendJsonError(500, "QR service unavailable");
        return;
    }

    std::vector<uint8_t> jpegData;
    String errorMessage;
    if (!qrService_->captureDebugJpeg(&jpegData, &errorMessage)) {
        const bool busy =
            errorMessage.startsWith("Preview unavailable while QR scanning") ||
            errorMessage.startsWith("Preview unavailable while audio");
        sendJsonError(busy ? 409 : 503,
                      errorMessage.isEmpty() ? "Failed to capture camera frame"
                                             : errorMessage.c_str());
        return;
    }

    server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server_.sendHeader("Pragma", "no-cache");
    server_.setContentLength(jpegData.size());
    server_.send(200, "image/jpeg", "");
    server_.client().write(jpegData.data(), jpegData.size());
}

void WebServerService::handleDebugCameraPreviewStart() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!qrService_) {
        sendJsonError(500, "QR service unavailable");
        return;
    }

    String errorMessage;
    if (!qrService_->beginDebugPreview(&errorMessage)) {
        sendJsonError(409,
                      errorMessage.isEmpty() ? "Failed to start camera preview"
                                             : errorMessage.c_str());
        return;
    }

    server_.send(200, "application/json", "{\"success\":true}");
}

void WebServerService::handleDebugCameraPreviewStop() {
    if (!ensureAuthorized()) {
        return;
    }
    if (!qrService_) {
        sendJsonError(500, "QR service unavailable");
        return;
    }

    qrService_->endDebugPreview();
    server_.send(200, "application/json", "{\"success\":true}");
}
