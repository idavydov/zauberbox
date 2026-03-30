#pragma once

#include <Arduino.h>

struct FactoryResetReport {
    size_t removedCount;
    size_t missingCount;
    size_t failedCount;
};

struct WebAuthConfig {
    String username;
    String password;
    bool isDefault;

    bool isValid() const {
        return !username.isEmpty() && !password.isEmpty();
    }
};

class ConfigService {
  public:
    void begin();
    bool hasWifiCredentials() const;
    WebAuthConfig loadWebAuthConfig() const;
    bool eraseWifiCredentials() const;
    FactoryResetReport eraseFactoryData() const;

  private:
    bool removeIfPresent(const char *path, FactoryResetReport *report) const;
};

ConfigService &configService();
