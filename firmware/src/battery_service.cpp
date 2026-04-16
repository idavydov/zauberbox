#include "battery_service.h"

#include <cmath>

#include "debug_log.h"

namespace {

constexpr uint8_t kBatteryAdcPin = 6;
constexpr float kBatteryDividerScale = 3.0F;
constexpr float kBatteryMeasurementOffset = 0.990476F;
constexpr uint32_t kSampleIntervalMs = 5000;
constexpr uint8_t kSamplesPerMeasurement = 4;
constexpr uint16_t kInvalidSampleFloorMv = 100;
constexpr uint8_t kSamplesUntilStable = 2;
constexpr float kFilterAlpha = 0.25F;
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

} // namespace

void BatteryService::begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(kBatteryAdcPin, ADC_11db);

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
    uint32_t rawMilliVoltsTotal = 0;
    for (uint8_t i = 0; i < kSamplesPerMeasurement; ++i) {
        rawMilliVoltsTotal += static_cast<uint32_t>(analogReadMilliVolts(kBatteryAdcPin));
    }

    const uint16_t rawMilliVolts =
        static_cast<uint16_t>(rawMilliVoltsTotal / kSamplesPerMeasurement);
    if (rawMilliVolts < kInvalidSampleFloorMv) {
        hasFilteredBatteryVolts_ = false;
        consecutiveValidSamples_ = 0;

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

        nextSampleAtMs_ = now + kSampleIntervalMs;

        Serial.printf("Battery service: raw=%umV unavailable.\n", rawMilliVolts);
        return;
    }

    const float measuredBatteryVolts =
        (static_cast<float>(rawMilliVolts) * kBatteryDividerScale / 1000.0F) /
        kBatteryMeasurementOffset;

    if (!hasFilteredBatteryVolts_) {
        filteredBatteryVolts_ = measuredBatteryVolts;
        hasFilteredBatteryVolts_ = true;
        consecutiveValidSamples_ = 1;
    } else {
        filteredBatteryVolts_ =
            (filteredBatteryVolts_ * (1.0F - kFilterAlpha)) +
            (measuredBatteryVolts * kFilterAlpha);
        if (consecutiveValidSamples_ < kSamplesUntilStable) {
            ++consecutiveValidSamples_;
        }
    }

    const uint16_t batteryMilliVolts =
        static_cast<uint16_t>(roundf(filteredBatteryVolts_ * 1000.0F));
    const uint8_t percent = estimatePercent(batteryMilliVolts);
    const bool readingStable = consecutiveValidSamples_ >= kSamplesUntilStable;
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

    nextSampleAtMs_ = now + kSampleIntervalMs;

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
