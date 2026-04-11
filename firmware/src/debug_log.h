#pragma once

#include <Arduino.h>
#include <Print.h>
#include <freertos/semphr.h>

class DebugLogService {
  public:
    void begin();
    void appendBytes(const uint8_t *data, size_t size);
    String snapshotText(size_t maxLines = 120) const;

  private:
    static constexpr size_t kPsramBufferBytes = 16 * 1024;
    static constexpr size_t kFallbackBufferBytes = 2048;

    mutable SemaphoreHandle_t mutex_ = nullptr;
    mutable StaticSemaphore_t mutexStorage_{};
    char *buffer_ = nullptr;
    size_t capacity_ = 0;
    size_t writePos_ = 0;
    size_t used_ = 0;
    bool initialized_ = false;
};

class DebugLogSerialProxy : public Print {
  public:
    using Print::write;

    void begin(unsigned long baud);
    int printf(const char *format, ...) __attribute__((format(printf, 2, 3)));
    void flush();
    size_t write(uint8_t byte) override;
    size_t write(const uint8_t *buffer, size_t size) override;
};

DebugLogService &debugLogService();
DebugLogSerialProxy &debugLogSerial();

#if !defined(ZAUBERBOX_USE_REAL_SERIAL)
#ifdef Serial
#undef Serial
#endif
#define Serial (::debugLogSerial())
#endif
