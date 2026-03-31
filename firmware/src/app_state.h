#pragma once

#include <Arduino.h>

enum class AppState : uint8_t {
    Boot,
    QrScan,
    Idle,
    Playing,
    Paused,
    Sleep,
    WifiPortal,
    Resetting,
};

enum class WifiMode : uint8_t {
    Disabled,
    Connecting,
    Connected,
    PortalActive,
};

struct AppRuntimeSnapshot {
    AppState appState;
    WifiMode wifiMode;
};

class AppStateStore {
  public:
    void init();
    AppRuntimeSnapshot snapshot() const;
    AppState current() const;
    WifiMode wifiMode() const;
    bool transitionTo(AppState nextState);
    void completeBoot();
    void requestFactoryReset();
    void syncWifiMode(WifiMode mode);
    bool allowsFactoryReset() const;
    static const char *stateName(AppState state);
    static const char *wifiModeName(WifiMode mode);

  private:
    bool isValidTransition(AppState fromState, AppState toState) const;

    mutable portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
    AppRuntimeSnapshot snapshot_ = {
        .appState = AppState::Boot,
        .wifiMode = WifiMode::Disabled,
    };
    AppState wifiPortalReturnState_ = AppState::QrScan;
};

AppStateStore &appStateStore();
