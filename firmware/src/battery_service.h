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
    BatteryPowerSource powerSource = BatteryPowerSource::Unknown;
    BatteryAvailability availability = BatteryAvailability::Unknown;
    uint32_t updatedAtMs = 0;
};

class BatteryService {
  public:
    void begin();
    void update();
    BatterySnapshot snapshot() const;
    static const char *powerSourceName(BatteryPowerSource source);
    static const char *availabilityName(BatteryAvailability availability);

  private:
    void takeMeasurement();
    static uint8_t estimatePercent(uint16_t batteryMilliVolts);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    BatterySnapshot snapshot_ = {};
    float filteredBatteryVolts_ = 0.0F;
    bool hasFilteredBatteryVolts_ = false;
    uint8_t consecutiveValidSamples_ = 0;
    uint32_t nextSampleAtMs_ = 0;
};

BatteryService &batteryService();
