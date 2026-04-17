#include "battery_service.h"

#include <cmath>

#include "debug_log.h"

namespace {

constexpr uint8_t kBatteryAdcPin = 6;
constexpr float kBatteryDividerScale = 3.0F;
constexpr float kBatteryMeasurementOffset = 0.990476F;
constexpr uint32_t kSampleIntervalMs = 5000;
constexpr uint32_t kSettlingSampleIntervalMs = 500;
constexpr uint8_t kSamplesPerMeasurement = 4;
constexpr uint8_t kBootstrapSamplesPerMeasurement = 12;
constexpr uint8_t kBootstrapDiscardInitialSamples = 2;
constexpr uint8_t kBootstrapUsableSamples =
    kBootstrapSamplesPerMeasurement - kBootstrapDiscardInitialSamples;
constexpr uint16_t kInvalidSampleFloorMv = 100;
constexpr float kFilterAlpha = 0.25F;
constexpr float kSettlingFilterAlpha = 0.4F;
constexpr uint8_t kSettledSamplesRequired = 3;
constexpr uint16_t kStableDeltaMv = 20;
constexpr uint16_t kLowThresholdMv = 3600;
constexpr uint16_t kLowClearThresholdMv = 3675;
constexpr uint16_t kCriticalThresholdMv = 3450;
constexpr uint16_t kCriticalClearThresholdMv = 3525;

constexpr uint16_t kPercentCurveMv[] = {
    3300,
    3450,
    3600,
    3700,
    3800,
    3900,
    4050,
    4200,
};

constexpr uint8_t kPercentCurveValues[] = {
    0,
    8,
    20,
    38,
    58,
    75,
    92,
    100,
};

static_assert(sizeof(kPercentCurveMv) / sizeof(kPercentCurveMv[0]) ==
                  sizeof(kPercentCurveValues) / sizeof(kPercentCurveValues[0]),
              "Battery percent curve arrays must match");

uint16_t averageBatteryRawMilliVolts(uint8_t sampleCount) {
    uint32_t rawMilliVoltsTotal = 0;
    for (uint8_t i = 0; i < sampleCount; ++i) {
        rawMilliVoltsTotal += static_cast<uint32_t>(analogReadMilliVolts(kBatteryAdcPin));
    }

    return static_cast<uint16_t>(rawMilliVoltsTotal / sampleCount);
}

void sortSamples(uint16_t *samples, uint8_t count) {
    for (uint8_t i = 1; i < count; ++i) {
        const uint16_t value = samples[i];
        int8_t insertIndex = static_cast<int8_t>(i) - 1;
        while (insertIndex >= 0 && samples[insertIndex] > value) {
            samples[insertIndex + 1] = samples[insertIndex];
            --insertIndex;
        }
        samples[insertIndex + 1] = value;
    }
}

uint16_t bootstrapBatteryRawMilliVolts() {
    uint16_t samples[kBootstrapSamplesPerMeasurement];
    for (uint8_t i = 0; i < kBootstrapSamplesPerMeasurement; ++i) {
        samples[i] = static_cast<uint16_t>(analogReadMilliVolts(kBatteryAdcPin));
    }

    uint16_t usableSamples[kBootstrapUsableSamples];
    for (uint8_t i = 0; i < kBootstrapUsableSamples; ++i) {
        usableSamples[i] = samples[i + kBootstrapDiscardInitialSamples];
    }

    sortSamples(usableSamples, kBootstrapUsableSamples);

    uint8_t trimCount = 0;
    if (kBootstrapUsableSamples > 4) {
        trimCount = 1;
    }

    uint32_t rawMilliVoltsTotal = 0;
    uint8_t includedSampleCount = 0;
    for (uint8_t i = trimCount; i < kBootstrapUsableSamples - trimCount; ++i) {
        rawMilliVoltsTotal += usableSamples[i];
        ++includedSampleCount;
    }

    return static_cast<uint16_t>(rawMilliVoltsTotal / includedSampleCount);
}

} // namespace

void BatteryService::begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(kBatteryAdcPin, ADC_11db);

    hasFilteredBatteryVolts_ = false;
    filteredBatteryVolts_ = 0.0F;
    bootReadingStable_ = false;
    consecutiveSettledSamples_ = 0;
    nextSampleAtMs_ = 0;

    portENTER_CRITICAL(&mux_);
    snapshot_ = {};
    snapshot_.initialized = true;
    snapshot_.availability = BatteryAvailability::Unknown;
    portEXIT_CRITICAL(&mux_);

    Serial.printf("Battery service: initialized on ADC pin %u.\n", kBatteryAdcPin);
}

void BatteryService::update() {
    const uint32_t now = millis();
    if (nextSampleAtMs_ != 0 && static_cast<int32_t>(now - nextSampleAtMs_) < 0) {
        return;
    }

    takeMeasurement();
}

BatterySnapshot BatteryService::snapshot() const {
    BatterySnapshot snapshot;
    portENTER_CRITICAL(&mux_);
    snapshot = snapshot_;
    portEXIT_CRITICAL(&mux_);
    return snapshot;
}

const char *BatteryService::powerSourceName(BatteryPowerSource source) {
    switch (source) {
        case BatteryPowerSource::Unknown:
            return "unknown";
        case BatteryPowerSource::External:
            return "external";
        case BatteryPowerSource::Battery:
            return "battery";
    }

    return "unknown";
}

const char *BatteryService::availabilityName(BatteryAvailability availability) {
    switch (availability) {
        case BatteryAvailability::Unknown:
            return "unknown";
        case BatteryAvailability::Unavailable:
            return "unavailable";
        case BatteryAvailability::Settling:
            return "settling";
        case BatteryAvailability::Available:
            return "available";
    }

    return "unknown";
}

void BatteryService::takeMeasurement() {
    const uint32_t now = millis();
    const uint16_t rawMilliVolts = hasFilteredBatteryVolts_
        ? averageBatteryRawMilliVolts(kSamplesPerMeasurement)
        : bootstrapBatteryRawMilliVolts();
    if (rawMilliVolts < kInvalidSampleFloorMv) {
        hasFilteredBatteryVolts_ = false;
        bootReadingStable_ = false;
        consecutiveSettledSamples_ = 0;

        BatterySnapshot nextSnapshot;
        portENTER_CRITICAL(&mux_);
        nextSnapshot = snapshot_;
        nextSnapshot.initialized = true;
        nextSnapshot.hasReading = false;
        nextSnapshot.readingAvailable = false;
        nextSnapshot.readingStable = false;
        nextSnapshot.rawAdcMilliVolts = rawMilliVolts;
        nextSnapshot.batteryMilliVolts = 0;
        nextSnapshot.percent = 0;
        nextSnapshot.low = false;
        nextSnapshot.critical = false;
        nextSnapshot.powerSource = BatteryPowerSource::Unknown;
        nextSnapshot.availability = BatteryAvailability::Unavailable;
        nextSnapshot.updatedAtMs = now;
        snapshot_ = nextSnapshot;
        portEXIT_CRITICAL(&mux_);

        nextSampleAtMs_ = now + kSettlingSampleIntervalMs;

        Serial.printf("Battery service: raw=%umV unavailable.\n", rawMilliVolts);
        return;
    }

    const float measuredBatteryVolts =
        (static_cast<float>(rawMilliVolts) * kBatteryDividerScale / 1000.0F) /
        kBatteryMeasurementOffset;

    if (!hasFilteredBatteryVolts_) {
        filteredBatteryVolts_ = measuredBatteryVolts;
        hasFilteredBatteryVolts_ = true;
        consecutiveSettledSamples_ = 1;
    } else {
        const float filterAlpha = bootReadingStable_ ? kFilterAlpha : kSettlingFilterAlpha;
        filteredBatteryVolts_ =
            (filteredBatteryVolts_ * (1.0F - filterAlpha)) +
            (measuredBatteryVolts * filterAlpha);

        const uint16_t filterDeltaMv = static_cast<uint16_t>(
            roundf(fabsf(measuredBatteryVolts - filteredBatteryVolts_) * 1000.0F));
        if (!bootReadingStable_) {
            if (filterDeltaMv <= kStableDeltaMv) {
                if (consecutiveSettledSamples_ < kSettledSamplesRequired) {
                    ++consecutiveSettledSamples_;
                }
            } else {
                consecutiveSettledSamples_ = 0;
            }
            if (consecutiveSettledSamples_ >= kSettledSamplesRequired) {
                bootReadingStable_ = true;
            }
        }
    }

    const uint16_t batteryMilliVolts =
        static_cast<uint16_t>(roundf(filteredBatteryVolts_ * 1000.0F));
    const uint8_t percent = estimatePercent(batteryMilliVolts);
    const bool readingStable = bootReadingStable_;
    const BatteryAvailability availability =
        readingStable ? BatteryAvailability::Available : BatteryAvailability::Settling;

    BatterySnapshot previousSnapshot;
    BatterySnapshot nextSnapshot;
    portENTER_CRITICAL(&mux_);
    previousSnapshot = snapshot_;
    nextSnapshot = snapshot_;
    nextSnapshot.initialized = true;
    nextSnapshot.hasReading = true;
    nextSnapshot.readingAvailable = true;
    nextSnapshot.readingStable = readingStable;
    nextSnapshot.rawAdcMilliVolts = rawMilliVolts;
    nextSnapshot.batteryMilliVolts = batteryMilliVolts;
    nextSnapshot.percent = percent;
    nextSnapshot.updatedAtMs = now;
    nextSnapshot.powerSource = BatteryPowerSource::Unknown;
    nextSnapshot.availability = availability;

    if (readingStable) {
        nextSnapshot.low = previousSnapshot.low
            ? batteryMilliVolts <= kLowClearThresholdMv
            : batteryMilliVolts <= kLowThresholdMv;
        nextSnapshot.critical = previousSnapshot.critical
            ? batteryMilliVolts <= kCriticalClearThresholdMv
            : batteryMilliVolts <= kCriticalThresholdMv;
        if (previousSnapshot.low && batteryMilliVolts > kLowClearThresholdMv) {
            nextSnapshot.low = false;
        }
        if (previousSnapshot.critical && batteryMilliVolts > kCriticalClearThresholdMv) {
            nextSnapshot.critical = false;
        }
    } else {
        nextSnapshot.low = false;
        nextSnapshot.critical = false;
    }

    snapshot_ = nextSnapshot;
    portEXIT_CRITICAL(&mux_);

    nextSampleAtMs_ = now + (readingStable ? kSampleIntervalMs : kSettlingSampleIntervalMs);

    Serial.printf(
        "Battery service: raw=%umV battery=%umV percent=%u low=%d critical=%d stable=%d availability=%s.\n",
                  rawMilliVolts,
                  batteryMilliVolts,
                  percent,
                  nextSnapshot.low ? 1 : 0,
                  nextSnapshot.critical ? 1 : 0,
                  readingStable ? 1 : 0,
                  availabilityName(availability));
}

uint8_t BatteryService::estimatePercent(uint16_t batteryMilliVolts) {
    constexpr size_t pointCount = sizeof(kPercentCurveMv) / sizeof(kPercentCurveMv[0]);
    if (batteryMilliVolts <= kPercentCurveMv[0]) {
        return kPercentCurveValues[0];
    }
    if (batteryMilliVolts >= kPercentCurveMv[pointCount - 1]) {
        return kPercentCurveValues[pointCount - 1];
    }

    for (size_t i = 1; i < pointCount; ++i) {
        if (batteryMilliVolts > kPercentCurveMv[i]) {
            continue;
        }

        const uint16_t mvStart = kPercentCurveMv[i - 1];
        const uint16_t mvEnd = kPercentCurveMv[i];
        const uint8_t percentStart = kPercentCurveValues[i - 1];
        const uint8_t percentEnd = kPercentCurveValues[i];
        const float ratio =
            static_cast<float>(batteryMilliVolts - mvStart) /
            static_cast<float>(mvEnd - mvStart);
        const float interpolatedPercent =
            static_cast<float>(percentStart) +
            (static_cast<float>(percentEnd - percentStart) * ratio);
        return static_cast<uint8_t>(roundf(interpolatedPercent));
    }

    return 0;
}

BatteryService &batteryService() {
    static BatteryService service;
    return service;
}
