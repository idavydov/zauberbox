#pragma once

#include <Arduino.h>

#include <functional>

#include "app_state.h"

class ESP32QRCodeReader;
class QrService {
  public:
    using AlbumScanCallback = std::function<bool(const String &albumId)>;

    bool begin(AlbumScanCallback onAlbumScanned);
    void update();

    bool submitDecodedPayload(const char *payload);
    bool isCameraReady() const;
    bool isScanning() const;

  private:
    static bool parseAlbumId(const char *payload, String *albumId);

    void pollDecodedQrs();
    bool startScanning();
    void stopScanning();
    bool initCamera();
    void deinitCamera();
    void handleStateTransition(AppState state);
    void startScanSession();
    void stopScanSession();
    void recordDecodedActivity();
    bool isDuplicatePayload(const String &payload) const;
    void configureCameraRouting() const;
    void disableCameraHardware() const;

    AlbumScanCallback onAlbumScanned_;
    ESP32QRCodeReader *reader_ = nullptr;
    String lastDecodedPayload_;
    uint32_t lastQrActivityAtMs_ = 0;
    uint32_t lastDecodedPayloadAtMs_ = 0;
    uint32_t nextStartAttemptAtMs_ = 0;
    AppState lastObservedState_ = AppState::Boot;
    bool cameraInitialized_ = false;
    bool scanning_ = false;
    bool available_ = false;
};
