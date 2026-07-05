#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include "sys/time_sync.h"

enum WifiDisplayState {
  WIFI_DISPLAY_OFF,
  WIFI_DISPLAY_ON,
  WIFI_DISPLAY_CONNECTED
};

class WifiManager {
public:
  enum ConnectionState : uint8_t {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_FAILED
  };

  void reset();
  bool update();
  bool startSta();
  void connect();
  void connectTo(const char *ssid, const char *password);
  void disconnect();
  void toggle();
  void restoreOn(bool enabled);

  bool isOn() const;
  bool isConnected() const;
  bool isConnecting() const;
  bool hasActiveSession() const;
  WifiDisplayState displayState() const;
  ConnectionState connectionState() const { return state; }

  bool preferredMatches(const char *ssid) const;
  void saveNetworkSlot(uint8_t slot, const char *ssid, const char *password,
                       const char *bssid);
  void savePreferred(const char *ssid, const char *password);
  void clearPreferredIfMatches(const char *ssid);

  const char *statusText() const { return timeSync.statusText(); }
  const char *datetimeText() const { return timeSync.datetimeText(); }
  void setStatus(const char *status) { timeSync.setStatus(status); }

private:
  static const unsigned long CONNECT_TIMEOUT_MS = 15000;

  ConnectionState state = STATE_IDLE;
  unsigned long startedAt = 0;
  bool autoConnectPending = false;
  bool staStarted = false;
};

extern WifiManager wifiManager;

#endif
