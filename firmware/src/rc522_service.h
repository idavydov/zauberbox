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
    bool supportsNfcTagWrite() const override;
    bool beginNfcTagWrite(const String &albumId, String *errorMessage = nullptr) override;
    void cancelNfcTagWrite() override;
    TagWriteStatus nfcTagWriteStatus() const override;
    void prepareForSleep() override;
    bool beginDebugPreview(String *errorMessage = nullptr) override;
    void endDebugPreview() override;
    bool captureDebugJpeg(std::vector<uint8_t> *jpegData,
                          int transformIndex,
                          String *errorMessage = nullptr) override;

  private:
    bool readCurrentTagAlbumId(String *albumId);
    bool writeCurrentTagAlbumId(const String &albumId, String *errorMessage);
    void finishTagWrite(AlbumInputService::TagWriteState state,
                        const String &message,
                        const String &tagUid = "");
    void noteNoCardPresent();
    void logReaderVersion() const;

    AlbumSelectedCallback onAlbumSelected_;
    TagWriteStatus tagWriteStatus_;
    bool active_ = false;
    bool initialized_ = false;
    bool presentedTagProcessed_ = false;
    bool tagWritePending_ = false;
    uint8_t missingPollCount_ = 0;
    uint32_t nextPollAtMs_ = 0;
    uint32_t lastSerialReadFailureLogAtMs_ = 0;
    uint32_t tagWriteTimeoutAtMs_ = 0;
    String presentedUid_;
};
