#include "rc522_service.h"

#include "app_state.h"

bool Rc522Service::begin(AlbumSelectedCallback onAlbumSelected) {
    onAlbumSelected_ = onAlbumSelected;
    active_ = false;
    Serial.println("RC522 service: stub backend initialized.");
    return true;
}

void Rc522Service::update() {
    const AppState state = appStateStore().current();
    active_ = state != AppState::Boot &&
              state != AppState::Sleep &&
              state != AppState::Resetting;
}

bool Rc522Service::isSelectionActive() const {
    return active_;
}

bool Rc522Service::isHardwareActive() const {
    return active_;
}

bool Rc522Service::stopsBeforePlayback() const {
    return false;
}

bool Rc522Service::usesSelectionStartAudioCue() const {
    return false;
}

AlbumInputBackend Rc522Service::backend() const {
    return AlbumInputBackend::Rc522;
}

bool Rc522Service::supportsDebugCameraPreview() const {
    return false;
}

bool Rc522Service::supportsQrAlbumCards() const {
    return false;
}

void Rc522Service::prepareForSleep() {
    active_ = false;
}
