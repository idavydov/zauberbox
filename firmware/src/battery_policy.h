#pragma once

#include <Arduino.h>

namespace BatteryPolicy {

constexpr uint16_t kJumpThresholdMv = 150;
constexpr uint16_t kLowThresholdMv = 3600;
constexpr uint16_t kLowClearThresholdMv = 3675;
constexpr uint16_t kCriticalThresholdMv = 3450;
constexpr uint16_t kCriticalClearThresholdMv = 3525;

constexpr uint8_t kPlaybackBatterySaverPercentThreshold = 90;
constexpr uint32_t kLowBatteryBlinkPeriodMs = 4000;
constexpr uint32_t kLowBatteryBlinkOnMs = 180;
constexpr uint32_t kPlayingRainbowWindowMs = 10000;
constexpr uint32_t kLowPowerPlayingCyclePeriodMs = 8000;
constexpr uint32_t kLowPowerPlayingBreathStartMs = 6000;
constexpr uint32_t kLowPowerPlayingBreathDurationMs = 2000;
constexpr float kLowPowerPlayingBreathPeakBrightness = 40.0F;
constexpr uint32_t kSparsePausedPulsePeriodMs = 3000;
constexpr uint32_t kSparsePausedPulseOnMs = 140;
constexpr uint32_t kLowBatteryPlaybackBeepIntervalMs = 60000;

constexpr uint32_t kIdleSleepTimeoutMs = 10000;
constexpr uint32_t kPausedSleepTimeoutMs = 300000;
constexpr uint32_t kCriticalSleepSettleMs = 2500;

} // namespace BatteryPolicy
