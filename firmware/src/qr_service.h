#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

#include "album_input_service.h"
#include "app_state.h"

class ESP32QRCodeReader;
class MediaService;
class QrService : public AlbumInputService {
  public:
    bool begin(AlbumSelectedCallback onAlbumSelected) override;
    void update() override;
    bool isSelectionActive() const override;
    bool isHardwareActive() const override;
    bool stopsBeforePlayback() const override;
    bool usesSelectionStartAudioCue() const override;
    AlbumInputBackend backend() const override;
    bool supportsDebugCameraPreview() const override;
    bool supportsQrAlbumCards() const override;
    void prepareForSleep() override;

    bool submitDecodedPayload(const char *payload);
    bool beginDebugPreview(String *errorMessage = nullptr);
    void endDebugPreview();
    bool captureDebugJpeg(std::vector<uint8_t> *jpegData, int transformIndex, String *errorMessage = nullptr);
    void setMediaService(MediaService *mediaService);


  private:
    static void handleRawFrameCapturedStatic(void *context,
                                             const uint8_t *buffer,
                                             size_t length,
                                             uint16_t width,
                                             uint16_t height,
                                             uint32_t frameCounter,
                                             bool decodedAnyValid);
    void pollDecodedQrs();
    bool startScanning();
    void stopScanning();
    void stopDebugPreviewSession();
    bool initCamera(bool startDecoderTask);
    void deinitCamera();
    void handleStateTransition(AppState state);
    void observeState(AppState state);
    void startScanSession();
    void stopScanSession();
    void recordDecodedActivity();
    bool isDuplicatePayload(const String &payload) const;
    void configureCameraRouting() const;
    void disableCameraHardware() const;
    void refreshRawFrameCaptureState();
    bool createRawFrameCaptureSessionDir();
    void handleRawFrameCaptured(const uint8_t *buffer,
                                size_t length,
                                uint16_t width,
                                uint16_t height,
                                uint32_t frameCounter,
                                bool decodedAnyValid);
    bool saveRawFrameCapture(const uint8_t *buffer,
                             size_t length,
                             uint16_t width,
                             uint16_t height,
                             uint32_t frameCounter);
    bool ensureDebugPreviewScratch(size_t requiredBytes);
    void releaseDebugPreviewScratch();

    AlbumSelectedCallback onAlbumSelected_;
    MediaService *mediaService_ = nullptr;
    ESP32QRCodeReader *reader_ = nullptr;
    String lastDecodedPayload_;
    String rawFrameCaptureSessionDir_;
    uint32_t lastQrActivityAtMs_ = 0;
    uint32_t lastDecodedPayloadAtMs_ = 0;
    uint32_t nextStartAttemptAtMs_ = 0;
    AppState lastObservedState_ = AppState::Boot;
    bool cameraInitialized_ = false;
    bool scanning_ = false;
    bool available_ = false;
    bool debugPreviewActive_ = false;
    bool rawFrameCaptureEnabled_ = false;
    uint32_t rawFrameCaptureSavedCount_ = 0;
    AppState debugPreviewReturnState_ = AppState::Idle;
    uint8_t *debugPreviewScratch_ = nullptr;
    size_t debugPreviewScratchSize_ = 0;
};
