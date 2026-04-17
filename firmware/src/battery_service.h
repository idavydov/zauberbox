#pragma once

#include <Arduino.h>

enum class BatteryPowerSource : uint8_t {
    Unknown,
    External,
    Battery,
};

enum class BatteryAvailability : uint8_t {
    Unknown,
    Unavailable,
    Settling,
    Available,
};

struct BatterySnapshot {
    bool initialized = false;
    bool hasReading = false;
    bool readingAvailable = false;
    bool readingStable = false;
    uint16_t rawAdcMilliVolts = 0;
    uint16_t batteryMilliVolts = 0;
    uint8_t percent = 0;
    bool low = false;
    bool critical = false;
    // Placeholder for future hardware support. This board currently has no
    // reliable digital signal for external-vs-battery power detection.
    BatteryPowerSource powerSource = BatteryPowerSource::Unknown;
    BatteryAvailability availability = BatteryAvailability::Unknown;
    uint32_t updatedAtMs = 0;
};

struct BatteryDebugOverrideStatus {
    bool enabled = false;
    bool active = false;
    uint16_t targetBatteryMilliVolts = 0;
    uint32_t activateAtMs = 0;
};

class BatteryService {
  public:
    void begin();
    void update();
    BatterySnapshot snapshot() const;
    void setDebugVoltageOverride(uint16_t targetBatteryMilliVolts, uint32_t delayMs);
    void clearDebugVoltageOverride();
    BatteryDebugOverrideStatus debugVoltageOverrideStatus() const;
    static const char *powerSourceName(BatteryPowerSource source);
    static const char *availabilityName(BatteryAvailability availability);

  private:
    void takeMeasurement();
    static uint8_t estimatePercent(uint16_t batteryMilliVolts);
    static void applyDerivedState(BatterySnapshot *snapshot, uint16_t batteryMilliVolts);
    uint16_t currentAverageBatteryMilliVolts() const;
    void clearMeasurementHistory();
    void pushBatteryMeasurement(uint16_t batteryMilliVolts);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    BatterySnapshot snapshot_ = {};
    BatteryDebugOverrideStatus debugOverride_ = {};
    uint16_t measurementHistory_[4] = {};
    uint8_t measurementHistoryCount_ = 0;
    uint8_t measurementHistoryIndex_ = 0;
    uint32_t nextSampleAtMs_ = 0;
};

BatteryService &batteryService();
