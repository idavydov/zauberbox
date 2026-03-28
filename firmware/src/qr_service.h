#pragma once

#include <Arduino.h>

#include <functional>

class ESP32QRCodeReader;
class QrService {
  public:
    using AlbumScanCallback = std::function<void(const String &albumId)>;

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
    void configureCameraRouting() const;
    void disableCameraHardware() const;

    AlbumScanCallback onAlbumScanned_;
    ESP32QRCodeReader *reader_ = nullptr;
    uint32_t nextStartAttemptAtMs_ = 0;
    bool cameraInitialized_ = false;
    bool scanning_ = false;
    bool available_ = false;
};
