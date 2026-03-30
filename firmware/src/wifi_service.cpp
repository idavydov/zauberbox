#include "wifi_service.h"

#include <WiFi.h>

#include "config_service.h"

namespace {

constexpr uint32_t kPortalConnectTimeoutMs = 30000;
constexpr uint32_t kReconnectBackoffMs = 2000;
constexpr uint32_t kReconnectAttemptMs = 10000;
constexpr uint32_t kPortalConnectLogIntervalMs = 2000;

} // namespace

void WifiService::begin(WifiConnectedCallback onConnected,
                        WifiConnectionFailedCallback onConnectionFailed) {
    onConnected_ = onConnected;
    onConnectionFailed_ = onConnectionFailed;

    manager_.setHtmlPathPrefix("/wifimanager");
    manager_.setAPCredentials("Zauberbox-Config", "789456123");
    manager_.setPortalTimeout(300);
    manager_.setAPClientCheck(true);
    manager_.setWebClientCheck(true);
    manager_.setHostname("zauberbox");
    manager_.setReconnectBackoffMs(kReconnectBackoffMs);
    manager_.setReconnectAttemptMs(kReconnectAttemptMs);
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
    awaitingPortalCredentials_ = false;
    portalConnectInProgress_ = false;
    portalConnectStartedAtMs_ = 0;
    nextPortalConnectLogAtMs_ = 0;
    const bool hasCredentials = configService().hasWifiCredentials();
    Serial.printf("Wi-Fi service: enabling (%s credentials).\n",
                  hasCredentials ? "with" : "without");
    if (hasCredentials) {
        appStateStore().syncWifiMode(WifiMode::Connecting);
        lastMode_ = WifiMode::Connecting;
        manager_.forzarReconexion();
    } else {
        Serial.println("Wi-Fi service: opening provisioning portal.");
        awaitingPortalCredentials_ = true;
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
    awaitingPortalCredentials_ = false;
    portalConnectInProgress_ = false;
    portalConnectStartedAtMs_ = 0;
    nextPortalConnectLogAtMs_ = 0;

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
    if (awaitingPortalCredentials_ &&
        manager_.isPortalActive() &&
        configService().hasWifiCredentials()) {
        beginPortalConnectionAttempt();
    }

    if (portalConnectInProgress_) {
        if (manager_.isConnected()) {
            Serial.printf("Wi-Fi service: STA connection established after %lu ms.\n",
                          static_cast<unsigned long>(millis() - portalConnectStartedAtMs_));
            portalConnectInProgress_ = false;
            portalConnectStartedAtMs_ = 0;
            nextPortalConnectLogAtMs_ = 0;
            syncAppState();
            return;
        }
        if (millis() - portalConnectStartedAtMs_ >= kPortalConnectTimeoutMs) {
            failPortalConnectionAttempt();
            return;
        }
        if (nextPortalConnectLogAtMs_ == 0 || millis() >= nextPortalConnectLogAtMs_) {
            Serial.printf("Wi-Fi service: still connecting... %lu/%lu ms elapsed.\n",
                          static_cast<unsigned long>(millis() - portalConnectStartedAtMs_),
                          static_cast<unsigned long>(kPortalConnectTimeoutMs));
            nextPortalConnectLogAtMs_ = millis() + kPortalConnectLogIntervalMs;
        }

        syncAppState();
        return;
    }

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

void WifiService::beginPortalConnectionAttempt() {
    awaitingPortalCredentials_ = false;
    portalConnectInProgress_ = true;
    portalConnectStartedAtMs_ = millis();
    nextPortalConnectLogAtMs_ = portalConnectStartedAtMs_;

    Serial.println("Wi-Fi service: credentials saved in portal, attempting STA connection.");
    manager_.closePortal();
    appStateStore().syncWifiMode(WifiMode::Connecting);
    lastMode_ = WifiMode::Connecting;
    manager_.forzarReconexion();
}

void WifiService::failPortalConnectionAttempt() {
    Serial.printf("Wi-Fi service: portal connection attempt timed out after %lu ms, reopening portal.\n",
                  static_cast<unsigned long>(millis() - portalConnectStartedAtMs_));
    portalConnectInProgress_ = false;
    portalConnectStartedAtMs_ = 0;
    nextPortalConnectLogAtMs_ = 0;
    manager_.eraseCredentials();
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    enabled_ = false;
    appStateStore().syncWifiMode(WifiMode::Disabled);
    lastMode_ = WifiMode::Disabled;
    if (onConnectionFailed_) {
        onConnectionFailed_();
    }
}
