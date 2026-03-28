#include "wifi_service.h"

#include "config_service.h"

void WifiService::begin(WifiConnectedCallback onConnected) {
    onConnected_ = onConnected;

    manager_.setHtmlPathPrefix("/wifimanager");
    manager_.setAPCredentials("Zauberbox-Config", "789456123");
    manager_.setPortalTimeout(300);
    manager_.setAPClientCheck(true);
    manager_.setWebClientCheck(true);
    manager_.begin();

    if (configService().hasWifiCredentials()) {
        appStateStore().syncWifiMode(WifiMode::Connecting);
        lastMode_ = WifiMode::Connecting;
    }
}

void WifiService::runStartup() {
    manager_.run();
    syncAppState();
}

void WifiService::update() {
    manager_.update();
    if (!manager_.isConnected()) {
        manager_.reintentarConexionSiNecesario();
    }
    syncAppState();
}

WifiMode WifiService::detectMode() {
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
