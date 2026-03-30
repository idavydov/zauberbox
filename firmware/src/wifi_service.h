#pragma once

#include "AyresWiFiManager/src/AyresWiFiManager.h"

#include <functional>

#include "app_state.h"

using WifiConnectedCallback = std::function<void()>;
using WifiConnectionFailedCallback = std::function<void()>;

class WifiService {
  public:
    void begin(WifiConnectedCallback onConnected,
               WifiConnectionFailedCallback onConnectionFailed);
    bool enable();
    void disable();
    bool isEnabled() const;
    void update();

  private:
    WifiMode detectMode();
    void syncAppState();
    void beginPortalConnectionAttempt();
    void failPortalConnectionAttempt();

    AyresWiFiManager manager_;
    bool enabled_ = false;
    bool awaitingPortalCredentials_ = false;
    bool portalConnectInProgress_ = false;
    uint32_t portalConnectStartedAtMs_ = 0;
    uint32_t nextPortalConnectLogAtMs_ = 0;
    WifiMode lastMode_ = WifiMode::Disabled;
    WifiConnectedCallback onConnected_ = nullptr;
    WifiConnectionFailedCallback onConnectionFailed_ = nullptr;
};
