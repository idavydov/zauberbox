#pragma once

#include <AyresWiFiManager.h>
#include "app_state.h"

using WifiConnectedCallback = bool (*)();

class WifiService {
  public:
    void begin(WifiConnectedCallback onConnected);
    void runStartup();
    void update();

  private:
    WifiMode detectMode();
    void syncAppState();

    AyresWiFiManager manager_;
    WifiMode lastMode_ = WifiMode::Disabled;
    WifiConnectedCallback onConnected_ = nullptr;
};
