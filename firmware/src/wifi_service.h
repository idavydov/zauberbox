#pragma once

#include <AyresWiFiManager.h>

#include <functional>

#include "app_state.h"

using WifiConnectedCallback = std::function<void()>;

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
