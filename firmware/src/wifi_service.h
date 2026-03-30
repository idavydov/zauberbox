#pragma once

#include "AyresWiFiManager/src/AyresWiFiManager.h"

#include <functional>

#include "app_state.h"

using WifiConnectedCallback = std::function<void()>;
using WifiConnectionFailedCallback = std::function<void(bool reopenPortal)>;

class WifiService {
  public:
    void begin(WifiConnectedCallback onConnected,
               WifiConnectionFailedCallback onConnectionFailed);
    bool enable();
    void resumePortalAfterFailure();
    void disable();
    bool isEnabled() const;
    void update();

  private:
    WifiMode detectMode();
    void syncAppState();
    void beginConnectionAttempt(const char *reason);
    void beginPortalConnectionAttempt();
    void failConnectionAttempt();

    AyresWiFiManager manager_;
    bool enabled_ = false;
    bool awaitingPortalCredentials_ = false;
    bool connectAttemptInProgress_ = false;
    bool connectAttemptFromPortal_ = false;
    uint32_t connectAttemptStartedAtMs_ = 0;
    uint32_t nextConnectLogAtMs_ = 0;
    WifiMode lastMode_ = WifiMode::Disabled;
    WifiConnectedCallback onConnected_ = nullptr;
    WifiConnectionFailedCallback onConnectionFailed_ = nullptr;
};
