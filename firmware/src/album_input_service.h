#pragma once

#include <Arduino.h>

#include <functional>

#if !defined(ZAUBERBOX_INPUT_QR) && !defined(ZAUBERBOX_INPUT_RC522)
#define ZAUBERBOX_INPUT_QR 1
#endif

#if defined(ZAUBERBOX_INPUT_QR) && defined(ZAUBERBOX_INPUT_RC522)
#error "Only one album input backend can be selected at a time."
#endif

class AlbumInputService {
  public:
    using AlbumSelectedCallback = std::function<bool(const String &albumId)>;

    virtual ~AlbumInputService() = default;

    virtual bool begin(AlbumSelectedCallback onAlbumSelected) = 0;
    virtual void update() = 0;
    virtual bool isSelectionActive() const = 0;
    virtual bool isHardwareActive() const = 0;
    virtual bool stopsBeforePlayback() const = 0;
};
