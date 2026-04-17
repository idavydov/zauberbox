#include "led_controller.h"

#include "app_state.h"
#include "battery_service.h"

namespace {

constexpr uint8_t kLedPin = 38;
constexpr uint16_t kLedCount = 7;
constexpr uint32_t kLowBatteryBlinkPeriodMs = 4000;
constexpr uint32_t kLowBatteryBlinkOnMs = 180;
constexpr uint8_t kBatteryHighPercentThreshold = 90;
constexpr uint32_t kPlayingRainbowWindowMs = 10000;
constexpr uint32_t kSparsePausedPulsePeriodMs = 3000;
constexpr uint32_t kSparsePausedPulseOnMs = 140;

bool shouldShowLowBatteryBlink(AppState state, const BatterySnapshot &battery) {
    if (state == AppState::Boot ||
        state == AppState::Sleep ||
        state == AppState::Resetting) {
        return false;
    }

    if (!battery.initialized ||
        !battery.hasReading ||
        !battery.readingAvailable ||
        !battery.readingStable ||
        battery.availability != BatteryAvailability::Available ||
        !battery.low) {
        return false;
    }

    return (millis() % kLowBatteryBlinkPeriodMs) < kLowBatteryBlinkOnMs;
}

bool shouldLimitPlayingRainbow(const BatterySnapshot &battery) {
    return battery.initialized &&
           battery.hasReading &&
           battery.readingAvailable &&
           battery.readingStable &&
           battery.availability == BatteryAvailability::Available &&
           battery.percent < kBatteryHighPercentThreshold;
}

bool shouldUseSparsePausedAnimation(const BatterySnapshot &battery) {
    return shouldLimitPlayingRainbow(battery);
}

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
    AppState lastState = AppState::Boot;
    uint32_t playingStartedAtMs = 0;

    while (true) {
        const AppState state = appStateStore().current();
        const BatterySnapshot battery = batteryService().snapshot();

        if (state != lastState) {
            if (state == AppState::Playing && lastState != AppState::Paused) {
                playingStartedAtMs = millis();
            } else if (state != AppState::Playing && state != AppState::Paused) {
                playingStartedAtMs = 0;
            }
            lastState = state;
        }

        if (shouldShowLowBatteryBlink(state, battery)) {
            for (int i = 0; i < kLedCount; i++) {
                ring_.setPixelColor(i, ring_.Color(180, 0, 0));
            }
            ring_.show();
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }

        switch (state) {
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
            case AppState::DebugCameraPreview: {
                const float breath = (exp(sin(millis() / 1000.0 * PI)) - 0.36787944F) * 20.0F;
                for (int i = 0; i < kLedCount; i++) {
                    ring_.setPixelColor(i, ring_.Color(breath * 0.5F, breath * 0.25, 0));
                }
                ring_.show();
                vTaskDelay(pdMS_TO_TICKS(25));
                break;
            }
            case AppState::Playing: {
                if (shouldLimitPlayingRainbow(battery) &&
                    playingStartedAtMs != 0 &&
                    millis() - playingStartedAtMs >= kPlayingRainbowWindowMs) {
                    ring_.clear();
                    ring_.show();
                    vTaskDelay(pdMS_TO_TICKS(60));
                    break;
                }

                for (int i = 0; i < kLedCount; i++) {
                    ring_.setPixelColor(i, ring_.gamma32(ring_.ColorHSV(hue + (i * 65536 / kLedCount))));
                }
                ring_.show();
                hue += 240;
                vTaskDelay(pdMS_TO_TICKS(35));
                break;
            }
            case AppState::Paused: {
                if (shouldUseSparsePausedAnimation(battery)) {
                    const bool on = (millis() % kSparsePausedPulsePeriodMs) < kSparsePausedPulseOnMs;
                    const uint32_t color = on ? ring_.Color(0, 10, 12) : 0;
                    for (int i = 0; i < kLedCount; i++) {
                        ring_.setPixelColor(i, color);
                    }
                    ring_.show();
                    vTaskDelay(pdMS_TO_TICKS(60));
                    break;
                }

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
