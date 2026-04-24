#pragma once

#include <Arduino.h>

#include <vector>

#include "album_input_service.h"

class Rc522Service : public AlbumInputService {
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
    bool beginDebugPreview(String *errorMessage = nullptr) override;
    void endDebugPreview() override;
    bool captureDebugJpeg(std::vector<uint8_t> *jpegData,
                          int transformIndex,
                          String *errorMessage = nullptr) override;

  private:
    AlbumSelectedCallback onAlbumSelected_;
    bool active_ = false;
};
