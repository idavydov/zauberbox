#include "wifi_service.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "config_service.h"

namespace {

constexpr uint32_t kPortalConnectTimeoutMs = 30000;
constexpr uint32_t kReconnectBackoffMs = 2000;
constexpr uint32_t kReconnectAttemptMs = 10000;
constexpr uint32_t kPortalConnectLogIntervalMs = 2000;
constexpr char kWifiStaHostname[] = "zauberbox";

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
    manager_.setHostname(kWifiStaHostname);
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
    connectAttemptInProgress_ = false;
    connectAttemptFromPortal_ = false;
    connectAttemptStartedAtMs_ = 0;
    nextConnectLogAtMs_ = 0;
    const bool hasCredentials = configService().hasWifiCredentials();
    Serial.printf("Wi-Fi service: enabling (%s credentials).\n",
                  hasCredentials ? "with" : "without");
    if (hasCredentials) {
        beginConnectionAttempt("using stored credentials");
    } else {
        Serial.println("Wi-Fi service: opening provisioning portal.");
        awaitingPortalCredentials_ = true;
        appStateStore().syncWifiMode(WifiMode::PortalActive);
        lastMode_ = WifiMode::PortalActive;
        manager_.openPortal();
    }

    return true;
}

void WifiService::resumePortalAfterFailure() {
    enabled_ = true;
    awaitingPortalCredentials_ = true;
    connectAttemptInProgress_ = false;
    connectAttemptFromPortal_ = false;
    connectAttemptStartedAtMs_ = 0;
    nextConnectLogAtMs_ = 0;
    Serial.println("Wi-Fi service: reopening provisioning portal.");
    appStateStore().syncWifiMode(WifiMode::PortalActive);
    lastMode_ = WifiMode::PortalActive;
    manager_.openPortal();
}

void WifiService::disable() {
    if (!enabled_ && lastMode_ == WifiMode::Disabled) {
        return;
    }

    Serial.println("Wi-Fi service: disabling.");
    awaitingPortalCredentials_ = false;
    connectAttemptInProgress_ = false;
    connectAttemptFromPortal_ = false;
    connectAttemptStartedAtMs_ = 0;
    nextConnectLogAtMs_ = 0;

    manager_.closePortal();
    MDNS.end();
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

    if (connectAttemptInProgress_) {
        if (manager_.isConnected()) {
            Serial.printf("Wi-Fi service: STA connection established after %lu ms.\n",
                          static_cast<unsigned long>(millis() - connectAttemptStartedAtMs_));
            connectAttemptInProgress_ = false;
            connectAttemptFromPortal_ = false;
            connectAttemptStartedAtMs_ = 0;
            nextConnectLogAtMs_ = 0;
            syncAppState();
            return;
        }
        if (millis() - connectAttemptStartedAtMs_ >= kPortalConnectTimeoutMs) {
            failConnectionAttempt();
            return;
        }
        if (nextConnectLogAtMs_ == 0 || millis() >= nextConnectLogAtMs_) {
            Serial.printf("Wi-Fi service: still connecting... %lu/%lu ms elapsed.\n",
                          static_cast<unsigned long>(millis() - connectAttemptStartedAtMs_),
                          static_cast<unsigned long>(kPortalConnectTimeoutMs));
            nextConnectLogAtMs_ = millis() + kPortalConnectLogIntervalMs;
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
        (void)manager_.setConnectedOnce(true);
        onConnected_();
    }
    lastMode_ = currentMode;
}

void WifiService::beginConnectionAttempt(const char *reason) {
    connectAttemptInProgress_ = true;
    connectAttemptStartedAtMs_ = millis();
    nextConnectLogAtMs_ = connectAttemptStartedAtMs_;

    Serial.printf("Wi-Fi service: %s, attempting STA connection.\n", reason);
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    WiFi.setHostname(kWifiStaHostname);
    appStateStore().syncWifiMode(WifiMode::Connecting);
    lastMode_ = WifiMode::Connecting;
    manager_.forzarReconexion();
}

void WifiService::beginPortalConnectionAttempt() {
    awaitingPortalCredentials_ = false;
    connectAttemptFromPortal_ = true;
    manager_.closePortal();
    beginConnectionAttempt("credentials saved in portal");
}

void WifiService::failConnectionAttempt() {
    Serial.printf("Wi-Fi service: connection attempt timed out after %lu ms.\n",
                  static_cast<unsigned long>(millis() - connectAttemptStartedAtMs_));
    const bool reopenPortal = connectAttemptFromPortal_ && !manager_.hasConnectedOnce();
    connectAttemptInProgress_ = false;
    connectAttemptStartedAtMs_ = 0;
    nextConnectLogAtMs_ = 0;

    if (reopenPortal) {
        Serial.println("Wi-Fi service: unvalidated portal credentials timed out, erasing them.");
        manager_.eraseCredentials();
    } else {
        Serial.println("Wi-Fi service: keeping stored credentials and disabling Wi-Fi.");
    }
    connectAttemptFromPortal_ = false;
    MDNS.end();
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    enabled_ = false;
    appStateStore().syncWifiMode(WifiMode::Disabled);
    lastMode_ = WifiMode::Disabled;
    if (onConnectionFailed_) {
        onConnectionFailed_(reopenPortal);
    }
}
