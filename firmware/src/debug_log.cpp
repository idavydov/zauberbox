#define ZAUBERBOX_USE_REAL_SERIAL 1
#include "debug_log.h"

#include <cstdarg>

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace {

vprintf_like_t gPreviousEspLogWriter = nullptr;

int debugLogEspVprintf(const char *format, va_list args) {
    char buffer[256];
    va_list copy;
    va_copy(copy, args);
    const int formattedLength = vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);

    if (formattedLength > 0) {
        const size_t bytesToStore =
            static_cast<size_t>(formattedLength) < sizeof(buffer)
                ? static_cast<size_t>(formattedLength)
                : sizeof(buffer) - 1;
        debugLogService().appendBytes(reinterpret_cast<const uint8_t *>(buffer), bytesToStore);
    }

    if (!gPreviousEspLogWriter) {
        return formattedLength;
    }

    va_list passthrough;
    va_copy(passthrough, args);
    const int result = gPreviousEspLogWriter(format, passthrough);
    va_end(passthrough);
    return result;
}

} // namespace

void DebugLogService::begin() {
    if (initialized_) {
        return;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutexStorage_);
    if (psramFound()) {
        buffer_ = static_cast<char *>(
            heap_caps_malloc(kPsramBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        capacity_ = buffer_ ? kPsramBufferBytes : 0;
    }
    if (!buffer_) {
        buffer_ = static_cast<char *>(
            heap_caps_malloc(kFallbackBufferBytes, MALLOC_CAP_8BIT));
        capacity_ = buffer_ ? kFallbackBufferBytes : 0;
    }
    gPreviousEspLogWriter = esp_log_set_vprintf(debugLogEspVprintf);
    initialized_ = true;
}

void DebugLogService::appendBytes(const uint8_t *data, size_t size) {
    if (!initialized_ || !mutex_ || !buffer_ || capacity_ == 0 || !data || size == 0) {
        return;
    }
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return;
    }

    for (size_t i = 0; i < size; ++i) {
        buffer_[writePos_] = static_cast<char>(data[i]);
        writePos_ = (writePos_ + 1) % capacity_;
        if (used_ < capacity_) {
            used_ += 1;
        }
    }

    xSemaphoreGive(mutex_);
}

String DebugLogService::snapshotText(size_t maxLines) const {
    if (!initialized_ || !mutex_ || !buffer_ || used_ == 0 || maxLines == 0) {
        return "";
    }
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return "";
    }

    String body;
    body.reserve(used_ + 1);

    const size_t oldestIndex = used_ == capacity_ ? writePos_ : 0;
    for (size_t i = 0; i < used_; ++i) {
        const char ch = buffer_[(oldestIndex + i) % capacity_];
        if (ch != '\r') {
            body += ch;
        }
    }

    xSemaphoreGive(mutex_);

    if (used_ == capacity_ && !body.isEmpty() && body[0] != '\n') {
        const int firstNewline = body.indexOf('\n');
        if (firstNewline >= 0) {
            body.remove(0, firstNewline + 1);
        }
    }

    if (body.isEmpty()) {
        return body;
    }

    size_t lineCount = 0;
    int startIndex = 0;
    for (int i = static_cast<int>(body.length()) - 1; i >= 0; --i) {
        if (body[static_cast<unsigned int>(i)] == '\n') {
            lineCount += 1;
            if (lineCount >= maxLines) {
                startIndex = i + 1;
                break;
            }
        }
    }

    if (startIndex > 0) {
        body.remove(0, startIndex);
    }

    return body;
}

void DebugLogSerialProxy::begin(unsigned long baud) {
    ::Serial.begin(baud);
}

int DebugLogSerialProxy::printf(const char *format, ...) {
    char buffer[384];
    va_list args;
    va_start(args, format);
    const int formattedLength = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (formattedLength <= 0) {
        return formattedLength;
    }

    const size_t bytesToWrite =
        static_cast<size_t>(formattedLength) < sizeof(buffer)
            ? static_cast<size_t>(formattedLength)
            : sizeof(buffer) - 1;
    write(reinterpret_cast<const uint8_t *>(buffer), bytesToWrite);
    return formattedLength;
}

void DebugLogSerialProxy::flush() {
    ::Serial.flush();
}

size_t DebugLogSerialProxy::write(uint8_t byte) {
    const size_t written = ::Serial.write(byte);
    if (written == 1) {
        debugLogService().appendBytes(&byte, 1);
    }
    return written;
}

size_t DebugLogSerialProxy::write(const uint8_t *buffer, size_t size) {
    const size_t written = ::Serial.write(buffer, size);
    if (written > 0) {
        debugLogService().appendBytes(buffer, written);
    }
    return written;
}

DebugLogService &debugLogService() {
    static DebugLogService service;
    return service;
}

DebugLogSerialProxy &debugLogSerial() {
    static DebugLogSerialProxy proxy;
    return proxy;
}
