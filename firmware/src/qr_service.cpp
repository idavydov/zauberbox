#include "qr_service.h"

#include <SD_MMC.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <img_converters.h>

#include "app_state.h"
#include "audio_driver.h"
#include "debug_log.h"
#include "io_expander.h"
#include "media_service.h"
#include "qr_reader/ESP32QRCodeReader.h"

namespace {

constexpr int kCameraPinPwdn = -1;
constexpr int kCameraPinReset = -1;
constexpr int kCameraPinXclk = 43;
constexpr int kCameraPinSccbSda = -1;
constexpr int kCameraPinSccbScl = -1;
constexpr int kCameraPinD7 = 48;
constexpr int kCameraPinD6 = 47;
constexpr int kCameraPinD5 = 46;
constexpr int kCameraPinD4 = 45;
constexpr int kCameraPinD3 = 39;
constexpr int kCameraPinD2 = 18;
constexpr int kCameraPinD1 = 17;
constexpr int kCameraPinD0 = 2;
constexpr int kCameraPinVsync = 21;
constexpr int kCameraPinHref = 1;
constexpr int kCameraPinPclk = 44;
constexpr uint32_t kCameraRetryDelayMs = 1000;
constexpr BaseType_t kQrDecodeCore = 1;
constexpr uint32_t kQrScanTimeoutMs = 180000;
constexpr uint32_t kDuplicatePayloadWindowMs = 1500;
constexpr framesize_t kQrFrameSize = FRAMESIZE_QVGA;
constexpr uint32_t kRawFrameCaptureEveryNthFrame = 20;
constexpr char kRawFrameCaptureMarkerPath[] = "/collect_qr_frames";
constexpr char kRawFrameCaptureOutputDir[] = "/debug_frames";
constexpr char kRawFrameCaptureTempSuffix[] = ".tmp";

const char *qrFrameSizeName(framesize_t frameSize) {
    switch (frameSize) {
        case FRAMESIZE_QQVGA:
            return "QQVGA";
        case FRAMESIZE_QVGA:
            return "QVGA";
        case FRAMESIZE_VGA:
            return "VGA";
        default:
            return "other";
    }
}

CameraPins makeCameraPins() {
    return {
        .PWDN_GPIO_NUM = kCameraPinPwdn,
        .RESET_GPIO_NUM = kCameraPinReset,
        .XCLK_GPIO_NUM = kCameraPinXclk,
        .SIOD_GPIO_NUM = kCameraPinSccbSda,
        .SIOC_GPIO_NUM = kCameraPinSccbScl,
        .Y9_GPIO_NUM = kCameraPinD7,
        .Y8_GPIO_NUM = kCameraPinD6,
        .Y7_GPIO_NUM = kCameraPinD5,
        .Y6_GPIO_NUM = kCameraPinD4,
        .Y5_GPIO_NUM = kCameraPinD3,
        .Y4_GPIO_NUM = kCameraPinD2,
        .Y3_GPIO_NUM = kCameraPinD1,
        .Y2_GPIO_NUM = kCameraPinD0,
        .VSYNC_GPIO_NUM = kCameraPinVsync,
        .HREF_GPIO_NUM = kCameraPinHref,
        .PCLK_GPIO_NUM = kCameraPinPclk,
    };
}

camera_config_t makeDirectCameraConfig() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = kCameraPinD0;
    config.pin_d1 = kCameraPinD1;
    config.pin_d2 = kCameraPinD2;
    config.pin_d3 = kCameraPinD3;
    config.pin_d4 = kCameraPinD4;
    config.pin_d5 = kCameraPinD5;
    config.pin_d6 = kCameraPinD6;
    config.pin_d7 = kCameraPinD7;
    config.pin_xclk = kCameraPinXclk;
    config.pin_pclk = kCameraPinPclk;
    config.pin_vsync = kCameraPinVsync;
    config.pin_href = kCameraPinHref;
    config.pin_sccb_sda = kCameraPinSccbSda;
    config.pin_sccb_scl = kCameraPinSccbScl;
    config.pin_pwdn = kCameraPinPwdn;
    config.pin_reset = kCameraPinReset;
    config.xclk_freq_hz = 10000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = kQrFrameSize;
    config.jpeg_quality = 15;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    return config;
}

} // namespace

bool QrService::begin(MediaService *mediaService, AlbumScanCallback onAlbumScanned) {
    mediaService_ = mediaService;
    onAlbumScanned_ = onAlbumScanned;

    if (!ioExpanderPinMode(kIoExpanderCameraEnablePin, OUTPUT)) {
        Serial.println("QR service: camera enable pin setup failed.");
        return false;
    }
    if (!ioExpanderPinMode(kIoExpanderCameraRouteSelectPin, OUTPUT)) {
        Serial.println("QR service: camera route pin setup failed.");
        return false;
    }

    disableCameraHardware();
    available_ = true;
    return true;
}

void QrService::handleRawFrameCapturedStatic(void *context,
                                             const uint8_t *buffer,
                                             size_t length,
                                             uint16_t width,
                                             uint16_t height,
                                             uint32_t frameCounter) {
    if (!context) {
        return;
    }

    static_cast<QrService *>(context)->handleRawFrameCaptured(buffer,
                                                              length,
                                                              width,
                                                              height,
                                                              frameCounter);
}

void QrService::update() {
    if (!available_) {
        return;
    }

    AppState state = appStateStore().current();
    if (debugPreviewActive_ && state != AppState::DebugCameraPreview) {
        Serial.printf("QR service: preview session active while app state is %s; stopping preview session.\n",
                      AppStateStore::stateName(state));
        stopDebugPreviewSession();
        state = appStateStore().current();
    }
    if (!debugPreviewActive_ && state == AppState::DebugCameraPreview) {
        const AppState fallbackState = debugPreviewReturnState_ == AppState::DebugCameraPreview
                                           ? AppState::Idle
                                           : debugPreviewReturnState_;
        Serial.printf("QR service: app state stuck in %s without active preview session; restoring %s.\n",
                      AppStateStore::stateName(state),
                      AppStateStore::stateName(fallbackState));
        debugPreviewReturnState_ = AppState::Idle;
        (void)appStateStore().transitionTo(fallbackState);
        state = appStateStore().current();
    }
    observeState(state);

    if (state == AppState::QrScan) {
        if (!scanning_ && millis() >= nextStartAttemptAtMs_) {
            (void)startScanning();
        }
        if (scanning_) {
            pollDecodedQrs();
            if (lastQrActivityAtMs_ != 0 &&
                millis() - lastQrActivityAtMs_ >= kQrScanTimeoutMs) {
                Serial.println("QR service: scan timeout reached, entering Idle.");
                appStateStore().transitionTo(AppState::Idle);
            }
        }
        return;
    }

    if (scanning_) {
        stopScanning();
    }
}

bool QrService::submitDecodedPayload(const char *payload) {
    if (!scanning_) {
        Serial.println("QR service: ignoring payload while scanner is inactive.");
        return false;
    }

    recordDecodedActivity();

    const String payloadString = payload ? String(payload) : String();
    if (isDuplicatePayload(payloadString)) {
        Serial.printf("QR service: suppressed duplicate payload: %s\n",
                      payload ? payload : "(null)");
        return false;
    }

    lastDecodedPayload_ = payloadString;
    lastDecodedPayloadAtMs_ = millis();

    String albumId;
    if (!parseAlbumId(payload, &albumId)) {
        Serial.printf("QR service: ignored unsupported payload: %s\n",
                      payload ? payload : "(null)");
        return false;
    }

    Serial.printf("QR service: decoded album payload: %s\n", albumId.c_str());
    if (onAlbumScanned_) {
        return onAlbumScanned_(albumId);
    }
    return false;
}

bool QrService::isCameraReady() const {
    return cameraInitialized_;
}

bool QrService::isScanning() const {
    return scanning_;
}

bool QrService::beginDebugPreview(String *errorMessage) {
    Serial.printf("QR service: beginDebugPreview requested in state=%s wifi=%s scanning=%d camera=%d preview=%d.\n",
                  AppStateStore::stateName(appStateStore().current()),
                  AppStateStore::wifiModeName(appStateStore().wifiMode()),
                  scanning_,
                  cameraInitialized_,
                  debugPreviewActive_);
    if (!available_) {
        if (errorMessage) {
            *errorMessage = "Camera service unavailable";
        }
        return false;
    }
    if (audioIsRunning()) {
        if (errorMessage) {
            *errorMessage = "Preview unavailable while audio is active";
        }
        return false;
    }

    const AppState state = appStateStore().current();
    if (state == AppState::DebugCameraPreview) {
        Serial.printf("QR service: beginDebugPreview ignored because app state is already %s and preview=%d.\n",
                      AppStateStore::stateName(state),
                      debugPreviewActive_);
        return debugPreviewActive_;
    }
    if (state != AppState::QrScan && state != AppState::Idle) {
        Serial.printf("QR service: beginDebugPreview rejected in state=%s.\n",
                      AppStateStore::stateName(state));
        if (errorMessage) {
            *errorMessage = "Preview unavailable in the current device state";
        }
        return false;
    }

    debugPreviewReturnState_ = state;
    Serial.printf("QR service: debug preview return state set to %s.\n",
                  AppStateStore::stateName(debugPreviewReturnState_));

    if (!appStateStore().transitionTo(AppState::DebugCameraPreview)) {
        Serial.println("QR service: failed to transition into DebugCameraPreview.");
        if (errorMessage) {
            *errorMessage = "Failed to enter preview mode";
        }
        return false;
    }

    if (state == AppState::QrScan) {
        Serial.println("QR service: stopping QR scanning before enabling debug preview.");
        stopScanning();
    }
    observeState(AppState::DebugCameraPreview);

    if (!initCamera(false)) {
        Serial.println("QR service: debug preview camera init failed; restoring previous state.");
        stopDebugPreviewSession();
        (void)appStateStore().transitionTo(debugPreviewReturnState_);
        observeState(appStateStore().current());
        if (errorMessage) {
            *errorMessage = "Failed to initialize camera";
        }
        return false;
    }

    debugPreviewActive_ = true;
    Serial.println("QR service: debug preview active.");
    return true;
}

void QrService::endDebugPreview() {
    Serial.printf("QR service: endDebugPreview requested in state=%s return=%s scanning=%d camera=%d preview=%d.\n",
                  AppStateStore::stateName(appStateStore().current()),
                  AppStateStore::stateName(debugPreviewReturnState_),
                  scanning_,
                  cameraInitialized_,
                  debugPreviewActive_);
    if (!debugPreviewActive_ && appStateStore().current() != AppState::DebugCameraPreview) {
        Serial.println("QR service: endDebugPreview ignored because no preview session is active.");
        return;
    }

    stopDebugPreviewSession();
    const AppState returnState = debugPreviewReturnState_;
    debugPreviewReturnState_ = AppState::Idle;
    if (appStateStore().current() == AppState::DebugCameraPreview) {
        Serial.printf("QR service: leaving DebugCameraPreview and restoring %s.\n",
                      AppStateStore::stateName(returnState));
        (void)appStateStore().transitionTo(returnState);
        observeState(appStateStore().current());
    }
}

void QrService::pollDecodedQrs() {
    if (!reader_) {
        return;
    }

    QRCodeData qrCodeData = {};
    while (reader_->receiveQrCode(&qrCodeData, 0)) {
        if (!qrCodeData.valid) {
            Serial.println("QR service: decoder rejected candidate frame.");
            continue;
        }

        if (submitDecodedPayload(reinterpret_cast<const char *>(qrCodeData.payload))) {
            return;
        }
    }
}

void QrService::handleStateTransition(AppState state) {
    Serial.printf("QR service: handleStateTransition(%s) with scanning=%d camera=%d preview=%d.\n",
                  AppStateStore::stateName(state),
                  scanning_,
                  cameraInitialized_,
                  debugPreviewActive_);
    if (state == AppState::QrScan) {
        startScanSession();
        return;
    }

    stopScanSession();
}

void QrService::observeState(AppState state) {
    if (state == lastObservedState_) {
        return;
    }

    Serial.printf("QR service: observed app state change %s -> %s.\n",
                  AppStateStore::stateName(lastObservedState_),
                  AppStateStore::stateName(state));
    handleStateTransition(state);
    lastObservedState_ = state;
}

void QrService::startScanSession() {
    Serial.println("QR service: scan session started.");
    lastQrActivityAtMs_ = millis();
    lastDecodedPayload_ = "";
    lastDecodedPayloadAtMs_ = 0;
    rawFrameCaptureSavedCount_ = 0;
    refreshRawFrameCaptureState();
}

void QrService::stopScanSession() {
    Serial.println("QR service: scan session stopped.");
    if (rawFrameCaptureEnabled_) {
        Serial.printf("QR service: raw frame capture stopped after saving %lu frames.\n",
                      static_cast<unsigned long>(rawFrameCaptureSavedCount_));
    }
    lastQrActivityAtMs_ = 0;
    lastDecodedPayload_ = "";
    lastDecodedPayloadAtMs_ = 0;
    rawFrameCaptureEnabled_ = false;
    rawFrameCaptureSessionDir_ = "";
}

void QrService::recordDecodedActivity() {
    lastQrActivityAtMs_ = millis();
}

bool QrService::isDuplicatePayload(const String &payload) const {
    if (payload.isEmpty() || lastDecodedPayload_.isEmpty()) {
        return false;
    }

    return payload == lastDecodedPayload_ &&
           millis() - lastDecodedPayloadAtMs_ < kDuplicatePayloadWindowMs;
}

bool QrService::parseAlbumId(const char *payload, String *albumId) {
    if (!payload || !albumId) {
        return false;
    }

    static constexpr char kPrefix[] = "file://";
    const String payloadString(payload);
    if (!payloadString.startsWith(kPrefix)) {
        return false;
    }

    String candidate = payloadString.substring(strlen(kPrefix));
    if (candidate.isEmpty()) {
        return false;
    }

    if (candidate.endsWith("/")) {
        candidate.remove(candidate.length() - 1);
    }
    if (candidate.isEmpty()) {
        return false;
    }

    for (size_t i = 0; i < candidate.length(); i++) {
        if (!isDigit(candidate.charAt(i))) {
            return false;
        }
    }

    *albumId = candidate;
    return true;
}

void QrService::refreshRawFrameCaptureState() {
    rawFrameCaptureEnabled_ = false;
    rawFrameCaptureSessionDir_ = "";

    if (!mediaService_) {
        Serial.println("QR service: raw frame capture unavailable without media service.");
        return;
    }
    if (!mediaService_->ensureStorageMounted()) {
        Serial.println("QR service: raw frame capture unavailable because SD storage is not mounted.");
        return;
    }
    if (!SD_MMC.exists(kRawFrameCaptureMarkerPath)) {
        return;
    }
    if (!SD_MMC.mkdir(kRawFrameCaptureOutputDir) && !SD_MMC.exists(kRawFrameCaptureOutputDir)) {
        Serial.printf("QR service: failed to prepare %s for raw frame capture.\n",
                      kRawFrameCaptureOutputDir);
        return;
    }

    if (!createRawFrameCaptureSessionDir()) {
        return;
    }
    rawFrameCaptureEnabled_ = true;
    Serial.printf("QR service: raw frame capture enabled via %s; saving every %lu frame to %s.\n",
                  kRawFrameCaptureMarkerPath,
                  static_cast<unsigned long>(kRawFrameCaptureEveryNthFrame),
                  rawFrameCaptureSessionDir_.c_str());
}

bool QrService::createRawFrameCaptureSessionDir() {
    const unsigned long sessionMillis = millis();
    const uint32_t sessionRandom = esp_random();

    for (uint32_t attempt = 0; attempt < 8; ++attempt) {
        char sessionDir[96];
        snprintf(sessionDir,
                 sizeof(sessionDir),
                 "%s/session_%08lu_%08lx",
                 kRawFrameCaptureOutputDir,
                 sessionMillis,
                 static_cast<unsigned long>(sessionRandom + attempt));
        if (!SD_MMC.exists(sessionDir)) {
            if (SD_MMC.mkdir(sessionDir)) {
                rawFrameCaptureSessionDir_ = sessionDir;
                return true;
            }
            Serial.printf("QR service: failed to create raw frame capture session dir %s.\n",
                          sessionDir);
            return false;
        }
    }

    Serial.println("QR service: unable to allocate a unique raw frame capture session dir.");
    return false;
}

void QrService::handleRawFrameCaptured(const uint8_t *buffer,
                                       size_t length,
                                       uint16_t width,
                                       uint16_t height,
                                       uint32_t frameCounter) {
    if (!rawFrameCaptureEnabled_) {
        return;
    }
    if (((frameCounter + 1) % kRawFrameCaptureEveryNthFrame) != 0) {
        return;
    }
    if (!SD_MMC.exists(kRawFrameCaptureMarkerPath)) {
        rawFrameCaptureEnabled_ = false;
        Serial.printf("QR service: raw frame capture disabled because %s was removed.\n",
                      kRawFrameCaptureMarkerPath);
        return;
    }

    (void)saveRawFrameCapture(buffer, length, width, height, frameCounter + 1);
}

bool QrService::saveRawFrameCapture(const uint8_t *buffer,
                                    size_t length,
                                    uint16_t width,
                                    uint16_t height,
                                    uint32_t frameCounter) {
    if (!buffer || length == 0) {
        return false;
    }
    if (!mediaService_ || !mediaService_->ensureStorageMounted()) {
        Serial.println("QR service: skipping raw frame capture because SD storage is unavailable.");
        rawFrameCaptureEnabled_ = false;
        return false;
    }
    if (rawFrameCaptureSessionDir_.isEmpty()) {
        Serial.println("QR service: skipping raw frame capture because no session dir is active.");
        rawFrameCaptureEnabled_ = false;
        return false;
    }

    char basePath[96];
    snprintf(basePath,
             sizeof(basePath),
             "%s/frame_%08lu_%ux%u_%u",
             rawFrameCaptureSessionDir_.c_str(),
             static_cast<unsigned long>(frameCounter),
             static_cast<unsigned>(width),
             static_cast<unsigned>(height),
             static_cast<unsigned>(length));

    const String tmpPath = String(basePath) + kRawFrameCaptureTempSuffix;
    const String finalPath = String(basePath) + ".pgm";
    char pgmHeader[32];
    const int headerLength = snprintf(pgmHeader,
                                      sizeof(pgmHeader),
                                      "P5\n%u %u\n255\n",
                                      static_cast<unsigned>(width),
                                      static_cast<unsigned>(height));
    if (headerLength <= 0 || static_cast<size_t>(headerLength) >= sizeof(pgmHeader)) {
        Serial.printf("QR service: failed to build PGM header for frame %lu.\n",
                      static_cast<unsigned long>(frameCounter));
        return false;
    }

    File file = SD_MMC.open(tmpPath.c_str(), FILE_WRITE);
    if (!file) {
        Serial.printf("QR service: failed to open %s for raw frame capture.\n", tmpPath.c_str());
        return false;
    }

    const size_t headerWritten =
        file.write(reinterpret_cast<const uint8_t *>(pgmHeader), static_cast<size_t>(headerLength));
    const size_t payloadWritten = file.write(buffer, length);
    file.flush();
    file.close();
    if (headerWritten != static_cast<size_t>(headerLength) || payloadWritten != length) {
        Serial.printf("QR service: incomplete PGM frame capture write to %s (header=%u/%u payload=%u/%u bytes).\n",
                      tmpPath.c_str(),
                      static_cast<unsigned>(headerWritten),
                      static_cast<unsigned>(headerLength),
                      static_cast<unsigned>(payloadWritten),
                      static_cast<unsigned>(length));
        SD_MMC.remove(tmpPath.c_str());
        return false;
    }

    (void)SD_MMC.remove(finalPath.c_str());
    if (!SD_MMC.rename(tmpPath.c_str(), finalPath.c_str())) {
        Serial.printf("QR service: failed to finalize raw frame capture %s.\n", finalPath.c_str());
        SD_MMC.remove(tmpPath.c_str());
        return false;
    }

    rawFrameCaptureSavedCount_++;
    Serial.printf("QR service: saved PGM frame %lu to %s.\n",
                  static_cast<unsigned long>(frameCounter),
                  finalPath.c_str());
    return true;
}

bool QrService::startScanning() {
    if (scanning_) {
        return true;
    }

    Serial.printf("QR service: startScanning requested in state=%s camera=%d preview=%d nextRetryAt=%lu.\n",
                  AppStateStore::stateName(appStateStore().current()),
                  cameraInitialized_,
                  debugPreviewActive_,
                  static_cast<unsigned long>(nextStartAttemptAtMs_));
    if (!initCamera(true)) {
        nextStartAttemptAtMs_ = millis() + kCameraRetryDelayMs;
        Serial.println("QR service: camera init failed, scan mode unavailable.");
        return false;
    }

    scanning_ = true;
    nextStartAttemptAtMs_ = 0;
    Serial.println("QR service: scanner active.");
    return true;
}

void QrService::stopScanning() {
    if (!scanning_) {
        return;
    }

    Serial.printf("QR service: stopScanning in state=%s camera=%d preview=%d.\n",
                  AppStateStore::stateName(appStateStore().current()),
                  cameraInitialized_,
                  debugPreviewActive_);
    scanning_ = false;
    deinitCamera();
    Serial.println("QR service: scanner inactive.");
}

void QrService::stopDebugPreviewSession() {
    if (!debugPreviewActive_) {
        return;
    }

    Serial.printf("QR service: stopDebugPreviewSession in state=%s scanning=%d camera=%d.\n",
                  AppStateStore::stateName(appStateStore().current()),
                  scanning_,
                  cameraInitialized_);
    debugPreviewActive_ = false;

    if (!scanning_ && cameraInitialized_) {
        deinitCamera();
    }
    releaseDebugPreviewScratch();

    Serial.println("QR service: debug preview inactive.");
}

bool QrService::ensureDebugPreviewScratch(size_t requiredBytes) {
    if (debugPreviewScratch_ && debugPreviewScratchSize_ >= requiredBytes) {
        return true;
    }

    releaseDebugPreviewScratch();

    debugPreviewScratch_ = static_cast<uint8_t *>(
        heap_caps_malloc(requiredBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!debugPreviewScratch_) {
        debugPreviewScratch_ = static_cast<uint8_t *>(
            heap_caps_malloc(requiredBytes, MALLOC_CAP_8BIT));
    }
    if (!debugPreviewScratch_) {
        debugPreviewScratchSize_ = 0;
        return false;
    }

    debugPreviewScratchSize_ = requiredBytes;
    Serial.printf("QR service: allocated preview scratch buffer (%u bytes).\n",
                  static_cast<unsigned int>(requiredBytes));
    return true;
}

void QrService::releaseDebugPreviewScratch() {
    if (!debugPreviewScratch_) {
        return;
    }
    heap_caps_free(debugPreviewScratch_);
    debugPreviewScratch_ = nullptr;
    debugPreviewScratchSize_ = 0;
}

bool QrService::captureDebugJpeg(std::vector<uint8_t> *jpegData, int transformIndex, String *errorMessage) {
    if (!jpegData) {
        if (errorMessage) {
            *errorMessage = "Debug preview buffer missing";
        }
        return false;
    }
    if (!available_) {
        if (errorMessage) {
            *errorMessage = "Camera service unavailable";
        }
        return false;
    }
    if (audioIsRunning()) {
        if (errorMessage) {
            *errorMessage = "Preview unavailable while audio is active";
        }
        return false;
    }
    if (!debugPreviewActive_ || appStateStore().current() != AppState::DebugCameraPreview) {
        if (errorMessage) {
            *errorMessage = "Preview session is not active";
        }
        return false;
    }

    const bool startedTemporaryCamera = !cameraInitialized_;
    if (!initCamera(false)) {
        if (errorMessage) {
            *errorMessage = "Failed to initialize camera";
        }
        return false;
    }

    camera_fb_t *frameBuffer = esp_camera_fb_get();
    if (!frameBuffer) {
        if (startedTemporaryCamera) {
            deinitCamera();
        }
        if (errorMessage) {
            *errorMessage = "Camera capture failed";
        }
        return false;
    }

    bool ok = false;
    uint8_t *jpegBuffer = nullptr;
    size_t jpegSize = 0;
    bool convertedBuffer = false;

    if (frameBuffer->format == PIXFORMAT_JPEG) {
        jpegBuffer = frameBuffer->buf;
        jpegSize = frameBuffer->len;
        ok = true;
    } else if (transformIndex >= 0 && frameBuffer->format == PIXFORMAT_GRAYSCALE) {
        const size_t grayscaleSize =
            static_cast<size_t>(frameBuffer->width) * static_cast<size_t>(frameBuffer->height);
        if (ensureDebugPreviewScratch(grayscaleSize)) {
            struct Transform {
                int cw;
                int div;
                int off;
            };
            static const Transform kTransforms[] = {
                {8, 1, -32},
                {10, 2, -64}
            };
            const int kNumTransforms = sizeof(kTransforms) / sizeof(kTransforms[0]);
            
            if (transformIndex < kNumTransforms) {
                const Transform& t = kTransforms[transformIndex];
                ESP32QRCodeReader::applyCrossKernel(frameBuffer->buf,
                                                     debugPreviewScratch_,
                                                     frameBuffer->width,
                                                     frameBuffer->height,
                                                     t.cw, t.div, t.off);
            } else {
                // Fallback to basic sharpen if index out of bounds but >= 0
                ESP32QRCodeReader::applyCrossSharpen7(frameBuffer->buf,
                                                     debugPreviewScratch_,
                                                     frameBuffer->width,
                                                     frameBuffer->height);
            }

            convertedBuffer = fmt2jpg(debugPreviewScratch_,
                                      grayscaleSize,
                                      frameBuffer->width,
                                      frameBuffer->height,
                                      PIXFORMAT_GRAYSCALE,
                                      80,
                                      &jpegBuffer,
                                      &jpegSize);
            ok = convertedBuffer && jpegBuffer && jpegSize > 0;
            if (!ok) {
                Serial.printf("QR service: transformed preview conversion failed; falling back to raw frame (w=%u h=%u len=%u).\n",
                              frameBuffer->width,
                              frameBuffer->height,
                              frameBuffer->len);
                convertedBuffer = frame2jpg(frameBuffer, 80, &jpegBuffer, &jpegSize);
                ok = convertedBuffer && jpegBuffer && jpegSize > 0;
            }
        } else {
            Serial.printf("QR service: transformed preview allocation failed for %u bytes; falling back to raw frame.\n",
                          static_cast<unsigned int>(grayscaleSize));
            convertedBuffer = frame2jpg(frameBuffer, 80, &jpegBuffer, &jpegSize);
            ok = convertedBuffer && jpegBuffer && jpegSize > 0;
            if (errorMessage) {
                *errorMessage = "Failed to allocate memory for transformed frame";
            }
        }
    } else {
        convertedBuffer = frame2jpg(frameBuffer, 80, &jpegBuffer, &jpegSize);
        ok = convertedBuffer && jpegBuffer && jpegSize > 0;
    }

    if (ok) {
        jpegData->assign(jpegBuffer, jpegBuffer + jpegSize);
    } else if (errorMessage && (!errorMessage || errorMessage->isEmpty())) {
        *errorMessage = "Failed to encode preview frame";
    }

    if (convertedBuffer && jpegBuffer) {
        free(jpegBuffer);
    }
    esp_camera_fb_return(frameBuffer);

    if (startedTemporaryCamera) {
        deinitCamera();
    }

    return ok;
}

bool QrService::initCamera(bool startDecoderTask) {
    Serial.printf("QR service: initCamera(startDecoderTask=%d) scanning=%d camera=%d preview=%d.\n",
                  startDecoderTask,
                  scanning_,
                  cameraInitialized_,
                  debugPreviewActive_);
    if (cameraInitialized_) {
        if (startDecoderTask && reader_ && !reader_->begun) {
            Serial.println("QR service: camera already initialized; starting decoder task on existing reader.");
            if (!reader_->beginOnCore(kQrDecodeCore)) {
                Serial.println("QR service: failed to start QR decoder task on existing camera session.");
                deinitCamera();
                return false;
            }
        }
        return true;
    }

    reader_ = new ESP32QRCodeReader(makeCameraPins(), kQrFrameSize);
    reader_->cameraConfig = makeDirectCameraConfig();
    reader_->setRawFrameObserver(handleRawFrameCapturedStatic, this);

    configureCameraRouting();

    if (!psramFound()) {
        Serial.println("QR service: QR decoder requires PSRAM.");
        delete reader_;
        reader_ = nullptr;
        disableCameraHardware();
        return false;
    }

    const esp_err_t err = esp_camera_init(&reader_->cameraConfig);
    if (err != ESP_OK) {
        Serial.printf("QR service: direct esp_camera_init failed: 0x%lx\n",
                      static_cast<unsigned long>(err));
        delete reader_;
        reader_ = nullptr;
        disableCameraHardware();
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_hmirror(sensor, 1);
        sensor->set_aec2(sensor, 1);
        sensor->set_ae_level(sensor, -1);
    }

    cameraInitialized_ = true;
    if (startDecoderTask) {
        if (!reader_->beginOnCore(kQrDecodeCore)) {
            Serial.println("QR service: QR decoder task startup failed.");
            deinitCamera();
            return false;
        }
    }
    Serial.printf("QR service: OV5640 camera initialized (decoder=%d, frameSize=%s).\n",
                  startDecoderTask,
                  qrFrameSizeName(kQrFrameSize));
    return true;
}

void QrService::deinitCamera() {
    Serial.printf("QR service: deinitCamera scanning=%d camera=%d preview=%d.\n",
                  scanning_,
                  cameraInitialized_,
                  debugPreviewActive_);
    if (reader_) {
        reader_->end();
        delete reader_;
        reader_ = nullptr;
    }

    if (cameraInitialized_) {
        const esp_err_t err = esp_camera_deinit();
        if (err != ESP_OK) {
            Serial.printf("QR service: esp_camera_deinit failed: 0x%lx\n",
                          static_cast<unsigned long>(err));
        }
        cameraInitialized_ = false;
    }

    releaseDebugPreviewScratch();
    disableCameraHardware();
    Serial.println("QR service: camera deinitialized.");
}

void QrService::configureCameraRouting() const {
    if (!ioExpanderDigitalWrite(kIoExpanderCameraRouteSelectPin, HIGH)) {
        Serial.println("QR service: failed to select camera routing.");
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!ioExpanderDigitalWrite(kIoExpanderCameraEnablePin, LOW)) {
        Serial.println("QR service: failed to enable camera power.");
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}

void QrService::disableCameraHardware() const {
    if (!ioExpanderDigitalWrite(kIoExpanderCameraEnablePin, HIGH)) {
        Serial.println("QR service: failed to disable camera power.");
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // EXIO6 selects the board's camera routing: HIGH uses the TX/RX path,
    // LOW switches the camera to the USB D+/D- path. Normal runtime keeps the
    // camera on the TX/RX path, so scan exit only cuts camera power here.
}
