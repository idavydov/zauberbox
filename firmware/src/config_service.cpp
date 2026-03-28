#include "config_service.h"

#include <LittleFS.h>

namespace {

constexpr char kWifiCredentialsPath[] = "/wifi.json";
constexpr char kWebAuthConfigPath[] = "/web-auth.json";
constexpr char kRuntimeConfigPath[] = "/config.json";

constexpr const char *kFactoryResetPaths[] = {
    kWifiCredentialsPath,
    kWebAuthConfigPath,
    kRuntimeConfigPath,
};

} // namespace

ConfigService &configService() {
    static ConfigService service;
    return service;
}

void ConfigService::begin() {
    Serial.printf("Config service: Wi-Fi credentials %s\n",
                  hasWifiCredentials() ? "present" : "not present");
}

bool ConfigService::hasWifiCredentials() const {
    return LittleFS.exists(kWifiCredentialsPath);
}

FactoryResetReport ConfigService::eraseFactoryData() const {
    FactoryResetReport report = {
        .removedCount = 0,
        .missingCount = 0,
        .failedCount = 0,
    };

    for (const char *path : kFactoryResetPaths) {
        removeIfPresent(path, &report);
    }

    return report;
}

bool ConfigService::removeIfPresent(const char *path, FactoryResetReport *report) const {
    if (!report) {
        return false;
    }

    if (!LittleFS.exists(path)) {
        report->missingCount++;
        return true;
    }

    if (!LittleFS.remove(path)) {
        report->failedCount++;
        Serial.printf("Factory reset: failed to remove %s\n", path);
        return false;
    }

    report->removedCount++;
    Serial.printf("Factory reset: removed %s\n", path);
    return true;
}
