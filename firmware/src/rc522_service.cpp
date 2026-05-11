#include "rc522_service.h"

#if defined(ZAUBERBOX_INPUT_RC522)
#include <MFRC522.h>
#include <SPI.h>

#include <algorithm>

#include "album_id.h"
#endif

#include "app_state.h"

#if defined(ZAUBERBOX_INPUT_RC522)
namespace {

constexpr uint8_t kRc522SckPin = 4;
constexpr uint8_t kRc522ResetPin = 5;
constexpr uint8_t kRc522CsPin = 7; // RC522 boards often label this SPI CS pin as "SDA".
constexpr uint8_t kRc522MisoPin = 8;
constexpr uint8_t kRc522MosiPin = 9;

constexpr uint32_t kRc522PollIntervalMs = 100;
constexpr uint32_t kRc522IdleLogIntervalMs = 5000;
constexpr uint32_t kRc522SerialReadFailureLogIntervalMs = 1000;
constexpr uint8_t kRc522MissingPollsBeforeRemoval = 3;
constexpr uint32_t kRc522ResetPulseMs = 50;
constexpr uint8_t kNtagFirstUserPage = 4;
constexpr uint8_t kNtagLastUserPage = 39; // NTAG213 user memory: 36 pages, 144 bytes.

constexpr uint8_t kNdefTlv = 0x03;
constexpr uint8_t kTerminatorTlv = 0xFE;
constexpr uint8_t kNullTlv = 0x00;

MFRC522 gRc522(kRc522CsPin, MFRC522::UNUSED_PIN);

String formatUid(const MFRC522::Uid &uid) {
    String value;
    value.reserve(uid.size * 2);
    for (byte i = 0; i < uid.size; i++) {
        if (uid.uidByte[i] < 0x10) {
            value += '0';
        }
        value += String(uid.uidByte[i], HEX);
    }
    value.toUpperCase();
    return value;
}

bool readNtagUserMemory(std::vector<uint8_t> *memory) {
    if (!memory) {
        return false;
    }

    memory->clear();
    memory->reserve((kNtagLastUserPage - kNtagFirstUserPage + 1) * 4);

    for (uint8_t page = kNtagFirstUserPage; page <= kNtagLastUserPage; page += 4) {
        byte buffer[18] = {};
        byte bufferSize = sizeof(buffer);
        const MFRC522::StatusCode status = gRc522.MIFARE_Read(page, buffer, &bufferSize);
        if (status != MFRC522::STATUS_OK) {
            Serial.printf("RC522 service: failed to read NTAG page %u: %s\n",
                          page,
                          gRc522.GetStatusCodeName(status));
            return false;
        }

        const uint8_t pageCount = std::min<uint8_t>(4, kNtagLastUserPage - page + 1);
        memory->insert(memory->end(), buffer, buffer + pageCount * 4);
    }

    return true;
}

bool extractNdefMessage(const std::vector<uint8_t> &memory, std::vector<uint8_t> *message) {
    if (!message) {
        return false;
    }

    message->clear();
    size_t offset = 0;
    while (offset < memory.size()) {
        const uint8_t type = memory[offset++];
        if (type == kNullTlv) {
            continue;
        }
        if (type == kTerminatorTlv) {
            return false;
        }
        if (offset >= memory.size()) {
            return false;
        }

        size_t length = memory[offset++];
        if (length == 0xFF) {
            if (offset + 2 > memory.size()) {
                return false;
            }
            length = (static_cast<size_t>(memory[offset]) << 8) | memory[offset + 1];
            offset += 2;
        }

        if (offset + length > memory.size()) {
            return false;
        }

        if (type == kNdefTlv) {
            message->assign(memory.begin() + offset, memory.begin() + offset + length);
            return !message->empty();
        }

        offset += length;
    }

    return false;
}

bool decodeNdefTextRecord(const uint8_t *payload, size_t payloadLength, String *value) {
    if (!payload || !value || payloadLength < 1) {
        return false;
    }

    const uint8_t status = payload[0];
    const bool utf16 = (status & 0x80) != 0;
    const uint8_t languageLength = status & 0x3F;
    if (utf16 || payloadLength < static_cast<size_t>(1 + languageLength)) {
        return false;
    }

    *value = "";
    value->reserve(payloadLength - 1 - languageLength);
    for (size_t i = 1 + languageLength; i < payloadLength; i++) {
        *value += static_cast<char>(payload[i]);
    }
    value->trim();
    return !value->isEmpty();
}

const char *ndefUriPrefix(uint8_t code) {
    switch (code) {
        case 0x00:
            return "";
        case 0x01:
            return "http://www.";
        case 0x02:
            return "https://www.";
        case 0x03:
            return "http://";
        case 0x04:
            return "https://";
        case 0x1D:
            return "file://";
        default:
            return nullptr;
    }
}

bool decodeNdefUriRecord(const uint8_t *payload, size_t payloadLength, String *value) {
    if (!payload || !value || payloadLength < 1) {
        return false;
    }

    const char *prefix = ndefUriPrefix(payload[0]);
    if (!prefix) {
        return false;
    }

    *value = prefix;
    value->reserve(strlen(prefix) + payloadLength - 1);
    for (size_t i = 1; i < payloadLength; i++) {
        *value += static_cast<char>(payload[i]);
    }
    value->trim();
    return !value->isEmpty();
}

bool extractPayloadFromNdefRecord(const std::vector<uint8_t> &message, String *payload) {
    if (!payload) {
        return false;
    }

    size_t offset = 0;
    while (offset < message.size()) {
        const uint8_t header = message[offset++];
        const bool shortRecord = (header & 0x10) != 0;
        const bool hasId = (header & 0x08) != 0;
        const uint8_t tnf = header & 0x07;
        if (!shortRecord || offset + 2 > message.size()) {
            return false;
        }

        const uint8_t typeLength = message[offset++];
        const size_t payloadLength = message[offset++];
        uint8_t idLength = 0;
        if (hasId) {
            if (offset >= message.size()) {
                return false;
            }
            idLength = message[offset++];
        }

        if (offset + typeLength + idLength + payloadLength > message.size()) {
            return false;
        }

        const uint8_t *type = message.data() + offset;
        offset += typeLength;
        offset += idLength;
        const uint8_t *recordPayload = message.data() + offset;
        offset += payloadLength;

        if (tnf != 0x01 || typeLength != 1) {
            continue;
        }

        if (type[0] == 'T' &&
            decodeNdefTextRecord(recordPayload, payloadLength, payload)) {
            return true;
        }
        if (type[0] == 'U' &&
            decodeNdefUriRecord(recordPayload, payloadLength, payload)) {
            return true;
        }
    }

    return false;
}

bool readNdefAlbumId(String *albumId, String *payload) {
    std::vector<uint8_t> memory;
    if (!readNtagUserMemory(&memory)) {
        return false;
    }

    std::vector<uint8_t> message;
    if (!extractNdefMessage(memory, &message)) {
        Serial.println("RC522 service: tag does not contain an NDEF message.");
        return false;
    }

    String decodedPayload;
    if (!extractPayloadFromNdefRecord(message, &decodedPayload)) {
        Serial.println("RC522 service: no supported NDEF Text or URI record found.");
        return false;
    }

    if (payload) {
        *payload = decodedPayload;
    }
    if (!parseAlbumSelectorPayload(decodedPayload.c_str(), albumId)) {
        Serial.printf("RC522 service: NDEF payload is not a supported album URL: %s\n",
                      decodedPayload.c_str());
        return false;
    }

    return true;
}

} // namespace
#endif

bool Rc522Service::begin(AlbumSelectedCallback onAlbumSelected) {
    onAlbumSelected_ = onAlbumSelected;
    active_ = false;

#if defined(ZAUBERBOX_INPUT_RC522)
    pinMode(kRc522CsPin, OUTPUT);
    digitalWrite(kRc522CsPin, HIGH);
    pinMode(kRc522ResetPin, OUTPUT);

    // Some RC522 breakouts do not keep RST pulled up reliably; keep reset under firmware control.
    digitalWrite(kRc522ResetPin, LOW);
    delay(kRc522ResetPulseMs);
    digitalWrite(kRc522ResetPin, HIGH);
    delay(kRc522ResetPulseMs);

    SPI.begin(kRc522SckPin, kRc522MisoPin, kRc522MosiPin, kRc522CsPin);
    gRc522.PCD_Init();
    digitalWrite(kRc522ResetPin, HIGH);
    logReaderVersion();
    gRc522.PCD_AntennaOn();
    initialized_ = true;
    Serial.printf("RC522 service: initialized (SCK=%u MISO=%u MOSI=%u CS=%u RST=%u).\n",
                  kRc522SckPin,
                  kRc522MisoPin,
                  kRc522MosiPin,
                  kRc522CsPin,
                  kRc522ResetPin);
#else
    Serial.println("RC522 service: unavailable in this build.");
#endif
    return true;
}

void Rc522Service::update() {
    const AppState state = appStateStore().current();
    active_ = state != AppState::Boot &&
              state != AppState::Sleep &&
              state != AppState::Resetting;

#if defined(ZAUBERBOX_INPUT_RC522)
    if (!initialized_ || !active_) {
        return;
    }

    const uint32_t now = millis();
    if (nextPollAtMs_ != 0 && static_cast<int32_t>(now - nextPollAtMs_) < 0) {
        return;
    }
    nextPollAtMs_ = now + kRc522PollIntervalMs;

    if (!gRc522.PICC_IsNewCardPresent()) {
        if (lastIdleLogAtMs_ == 0 ||
            static_cast<int32_t>(now - lastIdleLogAtMs_) >= static_cast<int32_t>(kRc522IdleLogIntervalMs)) {
            lastIdleLogAtMs_ = now;
            Serial.println("RC522 service: polling, no tag present.");
        }
        noteNoCardPresent();
        return;
    }
    if (!gRc522.PICC_ReadCardSerial()) {
        if (lastSerialReadFailureLogAtMs_ == 0 ||
            static_cast<int32_t>(now - lastSerialReadFailureLogAtMs_) >=
                static_cast<int32_t>(kRc522SerialReadFailureLogIntervalMs)) {
            lastSerialReadFailureLogAtMs_ = now;
            Serial.println("RC522 service: tag present but serial read failed.");
        }
        noteNoCardPresent();
        return;
    }

    lastIdleLogAtMs_ = 0;
    lastSerialReadFailureLogAtMs_ = 0;
    missingPollCount_ = 0;
    const String uid = formatUid(gRc522.uid);
    if (uid == presentedUid_ && presentedTagProcessed_) {
        gRc522.PICC_HaltA();
        gRc522.PCD_StopCrypto1();
        return;
    }

    presentedUid_ = uid;
    presentedTagProcessed_ = true;

    String albumId;
    if (!readCurrentTagAlbumId(&albumId)) {
        Serial.printf("RC522 service: ignored tag UID=%s.\n", uid.c_str());
        gRc522.PICC_HaltA();
        gRc522.PCD_StopCrypto1();
        return;
    }

    Serial.printf("RC522 service: selected album %s from tag UID=%s.\n",
                  albumId.c_str(),
                  uid.c_str());
    if (onAlbumSelected_) {
        (void)onAlbumSelected_(albumId);
    }

    gRc522.PICC_HaltA();
    gRc522.PCD_StopCrypto1();
#endif
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
#if defined(ZAUBERBOX_INPUT_RC522)
    if (initialized_) {
        gRc522.PCD_AntennaOff();
    }
    digitalWrite(kRc522ResetPin, LOW);
#endif
}

bool Rc522Service::beginDebugPreview(String *errorMessage) {
    if (errorMessage) {
        *errorMessage = "Camera preview unavailable for active input backend";
    }
    return false;
}

void Rc522Service::endDebugPreview() {
}

bool Rc522Service::captureDebugJpeg(std::vector<uint8_t> *jpegData,
                                    int transformIndex,
                                    String *errorMessage) {
    (void)jpegData;
    (void)transformIndex;
    if (errorMessage) {
        *errorMessage = "Camera preview unavailable for active input backend";
    }
    return false;
}

bool Rc522Service::readCurrentTagAlbumId(String *albumId) {
#if defined(ZAUBERBOX_INPUT_RC522)
    String payload;
    if (!readNdefAlbumId(albumId, &payload)) {
        return false;
    }

    Serial.printf("RC522 service: decoded NDEF payload: %s\n", payload.c_str());
    return true;
#else
    (void)albumId;
    return false;
#endif
}

void Rc522Service::logReaderVersion() const {
#if defined(ZAUBERBOX_INPUT_RC522)
    const byte version = gRc522.PCD_ReadRegister(MFRC522::VersionReg);
    Serial.printf("RC522 service: version register=0x%02X", version);
    if (version == 0x00 || version == 0xFF) {
        Serial.print(" (no SPI response; check wiring/power/CS/RST)");
    } else if (version == 0x91 || version == 0x92) {
        Serial.print(" (MFRC522 detected)");
    } else {
        Serial.print(" (unexpected value)");
    }
    Serial.println();
#endif
}

void Rc522Service::noteNoCardPresent() {
#if defined(ZAUBERBOX_INPUT_RC522)
    if (missingPollCount_ < 0xFF) {
        missingPollCount_++;
    }
    if (missingPollCount_ >= kRc522MissingPollsBeforeRemoval && !presentedUid_.isEmpty()) {
        Serial.printf("RC522 service: tag removed UID=%s.\n", presentedUid_.c_str());
        presentedUid_ = "";
        presentedTagProcessed_ = false;
    }
#else
    missingPollCount_ = 0;
    presentedUid_ = "";
    presentedTagProcessed_ = false;
#endif
}
