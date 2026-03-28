#include "qr_service.h"

#include <esp_camera.h>

#include "app_state.h"
#include "io_expander.h"

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
constexpr int kCameraI2cPort = 0;
constexpr uint32_t kCameraRetryDelayMs = 1000;

camera_config_t makeCameraConfig() {
    camera_config_t config = {};
    config.pin_pwdn = kCameraPinPwdn;
    config.pin_reset = kCameraPinReset;
    config.pin_xclk = kCameraPinXclk;
    config.pin_sccb_sda = kCameraPinSccbSda;
    config.pin_sccb_scl = kCameraPinSccbScl;
    config.pin_d7 = kCameraPinD7;
    config.pin_d6 = kCameraPinD6;
    config.pin_d5 = kCameraPinD5;
    config.pin_d4 = kCameraPinD4;
    config.pin_d3 = kCameraPinD3;
    config.pin_d2 = kCameraPinD2;
    config.pin_d1 = kCameraPinD1;
    config.pin_d0 = kCameraPinD0;
    config.pin_vsync = kCameraPinVsync;
    config.pin_href = kCameraPinHref;
    config.pin_pclk = kCameraPinPclk;
    config.xclk_freq_hz = 20000000;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.sccb_i2c_port = kCameraI2cPort;
    return config;
}

} // namespace

bool QrService::begin(AlbumScanCallback onAlbumScanned) {
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

void QrService::update() {
    if (!available_) {
        return;
    }

    const AppState state = appStateStore().current();
    if (state == AppState::QrScan) {
        if (!scanning_ && millis() >= nextStartAttemptAtMs_) {
            (void)startScanning();
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

    String albumId;
    if (!parseAlbumId(payload, &albumId)) {
        Serial.printf("QR service: ignored unsupported payload: %s\n",
                      payload ? payload : "(null)");
        return false;
    }

    Serial.printf("QR service: decoded album payload: %s\n", albumId.c_str());
    if (onAlbumScanned_) {
        onAlbumScanned_(albumId);
    }
    return true;
}

bool QrService::isCameraReady() const {
    return cameraInitialized_;
}

bool QrService::isScanning() const {
    return scanning_;
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

    const String candidate = payloadString.substring(strlen(kPrefix));
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

bool QrService::startScanning() {
    if (scanning_) {
        return true;
    }

    if (!initCamera()) {
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

    scanning_ = false;
    deinitCamera();
    Serial.println("QR service: scanner inactive.");
}

bool QrService::initCamera() {
    if (cameraInitialized_) {
        return true;
    }

    configureCameraRouting();
    const camera_config_t config = makeCameraConfig();
    const esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("QR service: esp_camera_init failed: 0x%lx\n",
                      static_cast<unsigned long>(err));
        disableCameraHardware();
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_hmirror(sensor, 1);
    }

    cameraInitialized_ = true;
    Serial.println("QR service: OV5640 camera initialized.");
    return true;
}

void QrService::deinitCamera() {
    if (cameraInitialized_) {
        const esp_err_t err = esp_camera_deinit();
        if (err != ESP_OK) {
            Serial.printf("QR service: esp_camera_deinit failed: 0x%lx\n",
                          static_cast<unsigned long>(err));
        }
        cameraInitialized_ = false;
    }

    disableCameraHardware();
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

    if (!ioExpanderDigitalWrite(kIoExpanderCameraRouteSelectPin, LOW)) {
        Serial.println("QR service: failed to restore default camera routing.");
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}
