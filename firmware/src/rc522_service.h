#pragma once

#include <Arduino.h>

#include "album_input_service.h"

class Rc522Service : public AlbumInputService {
  public:
    bool begin(AlbumSelectedCallback onAlbumSelected) override;
    void update() override;
    bool isSelectionActive() const override;
    bool isHardwareActive() const override;
    bool stopsBeforePlayback() const override;
    AlbumInputBackend backend() const override;
    bool supportsDebugCameraPreview() const override;
    bool supportsQrAlbumCards() const override;

  private:
    AlbumSelectedCallback onAlbumSelected_;
    bool active_ = false;
};
