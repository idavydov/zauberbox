#include "led_controller.h"

#include "app_state.h"

namespace {

constexpr uint8_t kLedPin = 38;
constexpr uint16_t kLedCount = 7;

} // namespace

LedController::LedController()
    : ring_(kLedCount, kLedPin, NEO_RGB + NEO_KHZ800) {
}

void LedController::begin() {
    ring_.begin();
    ring_.setBrightness(50);

    if (!taskHandle_) {
        xTaskCreatePinnedToCore(taskEntry,
                                "LED_Task",
                                4096,
                                this,
                                1,
                                &taskHandle_,
                                1);
    }
}

void LedController::taskEntry(void *context) {
    static_cast<LedController *>(context)->runTask();
}

void LedController::runTask() {
    uint16_t hue = 0;
    uint8_t scanFrame = 0;

    while (true) {
        switch (appStateStore().current()) {
            case AppState::Boot: {
                const bool on = (millis() / 250) % 2 == 0;
                const uint32_t color = on ? ring_.Color(24, 18, 8) : ring_.Color(4, 2, 0);
                for (int i = 0; i < kLedCount; i++) {
                    ring_.setPixelColor(i, color);
                }
                ring_.show();
                vTaskDelay(pdMS_TO_TICKS(60));
                break;
            }
            case AppState::WifiPortal: {
                const float breath = (exp(sin(millis() / 500.0 * PI)) - 0.36787944F) * 108.0F;
                for (int i = 0; i < kLedCount; i++) {
                    ring_.setPixelColor(i, ring_.Color(0, breath, breath * 0.15F));
                }
                ring_.show();
                vTaskDelay(pdMS_TO_TICKS(10));
                break;
            }
            case AppState::QrScan: {
                const uint8_t head = scanFrame % kLedCount;
                for (int i = 0; i < kLedCount; i++) {
                    uint8_t level = 0;
                    const uint8_t distance = (i + kLedCount - head) % kLedCount;
                    if (distance == 0) {
                        level = 180;
                    } else if (distance == 1) {
                        level = 70;
                    } else if (distance == 2) {
                        level = 24;
                    }
                    ring_.setPixelColor(i, ring_.Color(level, level / 5, 0));
                }
                ring_.show();
                scanFrame++;
                vTaskDelay(pdMS_TO_TICKS(70));
                break;
            }
            case AppState::Idle: {
                const float breath = (exp(sin(millis() / 1300.0 * PI)) - 0.36787944F) * 22.0F;
                for (int i = 0; i < kLedCount; i++) {
                    ring_.setPixelColor(i, ring_.Color(breath * 0.4F, breath * 0.25F, 0));
                }
                ring_.show();
                vTaskDelay(pdMS_TO_TICKS(25));
                break;
            }
            case AppState::Playing: {
                for (int i = 0; i < kLedCount; i++) {
                    ring_.setPixelColor(i, ring_.gamma32(ring_.ColorHSV(hue + (i * 65536 / kLedCount))));
                }
                ring_.show();
                hue += 240;
                vTaskDelay(pdMS_TO_TICKS(35));
                break;
            }
            case AppState::Paused: {
                const bool on = (millis() / 400) % 2 == 0;
                const uint32_t color = on ? ring_.Color(0, 18, 24) : ring_.Color(0, 4, 6);
                for (int i = 0; i < kLedCount; i++) {
                    ring_.setPixelColor(i, color);
                }
                ring_.show();
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            }
            case AppState::Sleep: {
                ring_.clear();
                ring_.show();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }
            case AppState::Resetting: {
                const bool on = (millis() / 150) % 2 == 0;
                const uint32_t color = on ? ring_.Color(180, 0, 0) : 0;
                for (int i = 0; i < kLedCount; i++) {
                    ring_.setPixelColor(i, color);
                }
                ring_.show();
                vTaskDelay(pdMS_TO_TICKS(30));
                break;
            }
        }
    }
}
