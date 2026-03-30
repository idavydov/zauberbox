#include "app_state.h"

#include <freertos/FreeRTOS.h>

bool AppStateStore::isValidTransition(AppState fromState, AppState toState) const {
    if (fromState == toState) {
        return true;
    }

    switch (fromState) {
        case AppState::Boot:
            return toState == AppState::QrScan ||
                   toState == AppState::WifiPortal ||
                   toState == AppState::Resetting;
        case AppState::QrScan:
            return toState == AppState::Idle ||
                   toState == AppState::Playing ||
                   toState == AppState::Sleep ||
                   toState == AppState::WifiPortal ||
                   toState == AppState::Resetting;
        case AppState::Idle:
            return toState == AppState::QrScan ||
                   toState == AppState::Playing ||
                   toState == AppState::Sleep ||
                   toState == AppState::WifiPortal ||
                   toState == AppState::Resetting;
        case AppState::Playing:
            return toState == AppState::Paused ||
                   toState == AppState::Idle ||
                   toState == AppState::QrScan ||
                   toState == AppState::WifiPortal ||
                   toState == AppState::Resetting;
        case AppState::Paused:
            return toState == AppState::Playing ||
                   toState == AppState::Idle ||
                   toState == AppState::QrScan ||
                   toState == AppState::WifiPortal ||
                   toState == AppState::Resetting;
        case AppState::Sleep:
            return toState == AppState::QrScan ||
                   toState == AppState::Resetting;
        case AppState::WifiPortal:
            return toState == AppState::QrScan ||
                   toState == AppState::Idle ||
                   toState == AppState::Resetting;
        case AppState::Resetting:
            return false;
    }

    return false;
}

AppStateStore &appStateStore() {
    static AppStateStore stateStore;
    return stateStore;
}

void AppStateStore::init() {
    portENTER_CRITICAL(&stateMux_);
    snapshot_ = {
        .appState = AppState::Boot,
        .wifiMode = WifiMode::Disabled,
    };
    portEXIT_CRITICAL(&stateMux_);
}

AppRuntimeSnapshot AppStateStore::snapshot() const {
    AppRuntimeSnapshot snapshot;
    portENTER_CRITICAL(&stateMux_);
    snapshot = snapshot_;
    portEXIT_CRITICAL(&stateMux_);
    return snapshot;
}

AppState AppStateStore::current() const {
    return snapshot().appState;
}

WifiMode AppStateStore::wifiMode() const {
    return snapshot().wifiMode;
}

bool AppStateStore::transitionTo(AppState nextState) {
    AppState previousState = AppState::Boot;
    bool changed = false;
    bool invalid = false;

    portENTER_CRITICAL(&stateMux_);
    previousState = snapshot_.appState;
    if (previousState == nextState) {
        portEXIT_CRITICAL(&stateMux_);
        return false;
    }
    if (!isValidTransition(previousState, nextState)) {
        invalid = true;
    } else {
        snapshot_.appState = nextState;
        changed = true;
    }
    portEXIT_CRITICAL(&stateMux_);

    if (invalid) {
        Serial.printf("Rejected app state transition: %s -> %s\n",
                      stateName(previousState),
                      stateName(nextState));
        return false;
    }
    if (changed) {
        Serial.printf("App state: %s -> %s\n",
                      stateName(previousState),
                      stateName(nextState));
    }
    return changed;
}

void AppStateStore::completeBoot() {
    transitionTo(AppState::QrScan);
}

void AppStateStore::requestFactoryReset() {
    transitionTo(AppState::Resetting);
}

void AppStateStore::syncWifiMode(WifiMode mode) {
    WifiMode previousMode = WifiMode::Disabled;
    AppState previousState = AppState::Boot;
    AppState nextState = AppState::Boot;
    AppState requestedState = AppState::Boot;
    bool modeChanged = false;
    bool stateChanged = false;
    bool invalidTransition = false;

    portENTER_CRITICAL(&stateMux_);
    previousMode = snapshot_.wifiMode;
    previousState = snapshot_.appState;
    nextState = previousState;
    requestedState = previousState;

    if (previousMode != mode) {
        snapshot_.wifiMode = mode;
        modeChanged = true;
    }

    if (mode == WifiMode::PortalActive && snapshot_.appState != AppState::WifiPortal) {
        nextState = AppState::WifiPortal;
    } else if (previousMode == WifiMode::PortalActive && mode != WifiMode::PortalActive &&
               snapshot_.appState == AppState::WifiPortal) {
        nextState = mode == WifiMode::Connecting || mode == WifiMode::Connected
                        ? AppState::Idle
                        : AppState::QrScan;
    }
    requestedState = nextState;

    if (nextState != snapshot_.appState) {
        if (isValidTransition(snapshot_.appState, nextState)) {
            snapshot_.appState = nextState;
            stateChanged = true;
        } else {
            invalidTransition = true;
        }
    }
    portEXIT_CRITICAL(&stateMux_);

    if (modeChanged) {
        Serial.printf("Wi-Fi mode: %s -> %s\n",
                      AppStateStore::wifiModeName(previousMode),
                      AppStateStore::wifiModeName(mode));
    }
    if (invalidTransition) {
        Serial.printf("Rejected app state transition during Wi-Fi sync: %s -> %s\n",
                      stateName(previousState),
                      stateName(requestedState));
        return;
    }
    if (stateChanged) {
        Serial.printf("App state: %s -> %s\n",
                      stateName(previousState),
                      stateName(nextState));
    }
}

bool AppStateStore::allowsFactoryReset() const {
    return current() == AppState::Boot;
}

const char *AppStateStore::stateName(AppState state) {
    switch (state) {
        case AppState::Boot:
            return "Boot";
        case AppState::QrScan:
            return "QrScan";
        case AppState::Idle:
            return "Idle";
        case AppState::Playing:
            return "Playing";
        case AppState::Paused:
            return "Paused";
        case AppState::Sleep:
            return "Sleep";
        case AppState::WifiPortal:
            return "WifiPortal";
        case AppState::Resetting:
            return "Resetting";
    }

    return "Unknown";
}

const char *AppStateStore::wifiModeName(WifiMode mode) {
    switch (mode) {
        case WifiMode::Disabled:
            return "Disabled";
        case WifiMode::Connecting:
            return "Connecting";
        case WifiMode::Connected:
            return "Connected";
        case WifiMode::PortalActive:
            return "PortalActive";
    }

    return "Unknown";
}
