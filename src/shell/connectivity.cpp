#include "shell/connectivity.h"
#include "sys/wifi_manager.h"

namespace {

char wifiIconCharForState(WifiDisplayState state) {
  if (state == WIFI_DISPLAY_CONNECTED) {
    return 'g';
  }
  if (state == WIFI_DISPLAY_ON) {
    return 'f';
  }
  return '\0';
}

} // namespace

char wifiStatusIcon() {
#if ENABLE_NETWORK_APPS
  return wifiIconCharForState(wifiManager.displayState());
#else
  return '\0';
#endif
}

bool wifiIsOn() {
#if ENABLE_NETWORK_APPS
  return wifiManager.isOn();
#else
  return false;
#endif
}

bool wifiIsConnected() {
#if ENABLE_NETWORK_APPS
  return wifiManager.isConnected();
#else
  return false;
#endif
}

void wifiTurnOn() {
#if ENABLE_NETWORK_APPS
  wifiManager.connect();
#endif
}

void wifiTurnOff() {
#if ENABLE_NETWORK_APPS
  wifiManager.disconnect();
#endif
}

void wifiToggle() {
#if ENABLE_NETWORK_APPS
  wifiManager.toggle();
#endif
}

bool wifiUpdate() {
#if ENABLE_NETWORK_APPS
  return wifiManager.update();
#else
  return false;
#endif
}

void restoreWifiOn(bool enabled) {
#if ENABLE_NETWORK_APPS
  wifiManager.restoreOn(enabled);
#else
  (void)enabled;
#endif
}
