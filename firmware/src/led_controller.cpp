#include "led_controller.h"

#include <algorithm>

#include "app_state.h"
#include "battery_policy.h"
#include "battery_service.h"

namespace {

constexpr uint8_t kLedPin = 38;
constexpr uint16_t kLedCount = 7;
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

    return (millis() % BatteryPolicy::kLowBatteryBlinkPeriodMs) <
           BatteryPolicy::kLowBatteryBlinkOnMs;
}

bool shouldLimitPlayingRainbow(const BatterySnapshot &battery) {
    return battery.initialized &&
           battery.hasReading &&
           battery.readingAvailable &&
           battery.readingStable &&
           battery.availability == BatteryAvailability::Available &&
           battery.percent < BatteryPolicy::kPlaybackBatterySaverPercentThreshold;
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
            case kAlbumSelectionState: {
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
                hue += 160;

                float targetBrightness = 140.0f;
                int currentDelay = 45;
                bool useLowPowerRainbow = false;

                if (shouldLimitPlayingRainbow(battery) &&
                    playingStartedAtMs != 0 &&
                    (millis() - playingStartedAtMs >= BatteryPolicy::kPlayingRainbowWindowMs)) {
                    const uint32_t phase = millis() % BatteryPolicy::kLowPowerPlayingCyclePeriodMs;
                    if (phase < BatteryPolicy::kLowPowerPlayingBreathStartMs) {
                        targetBrightness = 0.0f;
                        const uint32_t remainingOffMs =
                            BatteryPolicy::kLowPowerPlayingBreathStartMs - phase;
                        currentDelay = static_cast<int>(std::min<uint32_t>(remainingOffMs, 400));
                    } else {
                        const float breathPhase =
                            (phase - BatteryPolicy::kLowPowerPlayingBreathStartMs) /
                            static_cast<float>(BatteryPolicy::kLowPowerPlayingBreathDurationMs);
                        const float wave = sin(breathPhase * PI);
                        const float breath = wave * wave;
                        targetBrightness =
                            constrain(breath * BatteryPolicy::kLowPowerPlayingBreathPeakBrightness,
                                      0.0f,
                                      255.0f);
                        currentDelay = 60;
                    }
                    useLowPowerRainbow = true;
                }

                for (int i = 0; i < kLedCount; i++) {
                    uint16_t pixelHue = hue + (i * 65536 / kLedCount);
                    uint32_t color = ring_.ColorHSV(pixelHue, 220, static_cast<uint8_t>(targetBrightness));
                    ring_.setPixelColor(i, useLowPowerRainbow ? color : ring_.gamma32(color));
                }

                ring_.show();
                vTaskDelay(pdMS_TO_TICKS(currentDelay));
                break;
            }
            case AppState::Paused: {
                if (shouldUseSparsePausedAnimation(battery)) {
                    const uint32_t phase = millis() % BatteryPolicy::kSparsePausedPulsePeriodMs;
                    const bool on = phase < BatteryPolicy::kSparsePausedPulseOnMs;
                    const uint32_t color = on ? ring_.Color(0, 10, 12) : 0;
                    for (int i = 0; i < kLedCount; i++) {
                        ring_.setPixelColor(i, color);
                    }
                    ring_.show();
                    const uint32_t delayMs = on
                        ? BatteryPolicy::kSparsePausedPulseOnMs - phase
                        : std::min<uint32_t>(BatteryPolicy::kSparsePausedPulsePeriodMs - phase, 500);
                    vTaskDelay(pdMS_TO_TICKS(delayMs));
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
                vTaskDelay(pdMS_TO_TICKS(1000));
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
