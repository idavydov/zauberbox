#include "config_service.h"

#include <ArduinoJson.h>
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
    if (!LittleFS.exists(kWifiCredentialsPath)) {
        return false;
    }

    File file = LittleFS.open(kWifiCredentialsPath, "r");
    if (!file) {
        Serial.println("Config service: failed to open /wifi.json.");
        return false;
    }

    StaticJsonDocument<192> doc;
    const DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        Serial.println("Config service: /wifi.json is invalid.");
        return false;
    }

    const String ssid = doc["ssid"].as<String>();
    const String password = doc["password"].as<String>();
    return !ssid.isEmpty() && !password.isEmpty();
}

bool ConfigService::eraseWifiCredentials() const {
    FactoryResetReport report = {
        .removedCount = 0,
        .missingCount = 0,
        .failedCount = 0,
    };
    return removeIfPresent(kWifiCredentialsPath, &report);
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
