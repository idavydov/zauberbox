#pragma once

#include <Arduino.h>

struct FactoryResetReport {
    size_t removedCount;
    size_t missingCount;
    size_t failedCount;
};

class ConfigService {
  public:
    void begin();
    bool hasWifiCredentials() const;
    FactoryResetReport eraseFactoryData() const;

  private:
    bool removeIfPresent(const char *path, FactoryResetReport *report) const;
};

ConfigService &configService();
