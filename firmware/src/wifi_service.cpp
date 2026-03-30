#include "wifi_service.h"

#include <WiFi.h>

#include "config_service.h"

void WifiService::begin(WifiConnectedCallback onConnected) {
    onConnected_ = onConnected;

    manager_.setHtmlPathPrefix("/wifimanager");
    manager_.setAPCredentials("Zauberbox-Config", "789456123");
    manager_.setPortalTimeout(300);
    manager_.setAPClientCheck(true);
    manager_.setWebClientCheck(true);
    manager_.setFallbackPolicy(AyresWiFiManager::FallbackPolicy::BUTTON_ONLY);
    manager_.enableButtonPortal(false);
    manager_.begin();
}

bool WifiService::enable() {
    if (enabled_) {
        Serial.println("Wi-Fi service: enable requested while already enabled.");
        return true;
    }

    enabled_ = true;
    const bool hasCredentials = configService().hasWifiCredentials();
    Serial.printf("Wi-Fi service: enabling (%s credentials).\n",
                  hasCredentials ? "with" : "without");
    if (hasCredentials) {
        appStateStore().syncWifiMode(WifiMode::Connecting);
        lastMode_ = WifiMode::Connecting;
        manager_.forzarReconexion();
    } else {
        Serial.println("Wi-Fi service: opening provisioning portal.");
        appStateStore().syncWifiMode(WifiMode::PortalActive);
        lastMode_ = WifiMode::PortalActive;
        manager_.openPortal();
    }

    return true;
}

void WifiService::disable() {
    if (!enabled_ && lastMode_ == WifiMode::Disabled) {
        return;
    }

    Serial.println("Wi-Fi service: disabling.");

    manager_.closePortal();
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    enabled_ = false;
    appStateStore().syncWifiMode(WifiMode::Disabled);
    lastMode_ = WifiMode::Disabled;
}

bool WifiService::isEnabled() const {
    return enabled_;
}

void WifiService::update() {
    if (!enabled_) {
        return;
    }

    manager_.update();
    if (!configService().hasWifiCredentials() && !manager_.isPortalActive()) {
        enabled_ = false;
        appStateStore().syncWifiMode(WifiMode::Disabled);
        lastMode_ = WifiMode::Disabled;
        return;
    }

    if (!manager_.isConnected() && configService().hasWifiCredentials()) {
        manager_.reintentarConexionSiNecesario();
    }
    syncAppState();
}

WifiMode WifiService::detectMode() {
    if (!enabled_) {
        return WifiMode::Disabled;
    }
    if (manager_.isPortalActive()) {
        return WifiMode::PortalActive;
    }
    if (manager_.isConnected()) {
        return WifiMode::Connected;
    }
    return WifiMode::Connecting;
}

void WifiService::syncAppState() {
    const WifiMode currentMode = detectMode();
    if (currentMode == lastMode_) {
        return;
    }

    appStateStore().syncWifiMode(currentMode);
    if (currentMode == WifiMode::Connected && onConnected_) {
        onConnected_();
    }
    lastMode_ = currentMode;
}
