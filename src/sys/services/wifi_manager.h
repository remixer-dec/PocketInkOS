#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include "sys/services/time_sync.h"

/// Coarse Wi-Fi state used by compact status indicators.
enum WifiDisplayState {
  /// Wi-Fi is disabled.
  WIFI_DISPLAY_OFF,
  /// Wi-Fi is enabled but not connected.
  WIFI_DISPLAY_ON,
  /// Wi-Fi is enabled and connected.
  WIFI_DISPLAY_CONNECTED
};

/// Owns station-mode Wi-Fi lifecycle, remembered networks, and time sync updates.
class WifiManager {
public:
  /// Detailed connection state for settings and status UI.
  enum ConnectionState : uint8_t {
    /// Wi-Fi is idle or disabled.
    STATE_IDLE,
    /// Station connection attempt is in progress.
    STATE_CONNECTING,
    /// Station is connected.
    STATE_CONNECTED,
    /// Last connection attempt failed.
    STATE_FAILED
  };

  /// Resets manager state without clearing persisted credentials.
  void reset();
  /// Advances connection and time-sync work; returns true when UI should redraw.
  bool update();
  /// Starts station mode if it is not already active.
  bool startSta();
  /// Connects to the preferred or remembered network.
  void connect();
  /// Connects to explicit credentials without first requiring persisted config.
  void connectTo(const char *ssid, const char *password);
  /// Disconnects station mode.
  void disconnect();
  /// Toggles Wi-Fi on/off from user input.
  void toggle();
  /// Restores Wi-Fi enabled state after wake.
  void restoreOn(bool enabled);

  /// Returns true when Wi-Fi station mode is enabled.
  bool isOn() const;
  /// Returns true when station mode has an IP-connected network.
  bool isConnected() const;
  /// Returns true while a connection attempt is active.
  bool isConnecting() const;
  /// Returns true while Wi-Fi owns a foreground session.
  bool hasActiveSession() const;
  /// Returns the compact display state for status UI.
  WifiDisplayState displayState() const;
  /// Returns the detailed connection state.
  ConnectionState connectionState() const { return state; }

  /// Returns true when ssid matches the configured preferred network.
  bool preferredMatches(const char *ssid) const;
  /// Saves one remembered network slot.
  void saveNetworkSlot(uint8_t slot, const char *ssid, const char *password,
                       const char *bssid);
  /// Saves the preferred network used by connect().
  void savePreferred(const char *ssid, const char *password);
  /// Clears the preferred network only if it matches ssid.
  void clearPreferredIfMatches(const char *ssid);

  /// Returns shared time-sync status text.
  const char *statusText() const { return timeSync.statusText(); }
  /// Returns shared time-sync date/time text.
  const char *datetimeText() const { return timeSync.datetimeText(); }
  /// Replaces shared time-sync status text.
  void setStatus(const char *status) { timeSync.setStatus(status); }

private:
  static const unsigned long CONNECT_TIMEOUT_MS = 15000;
  static const unsigned long REDISCOVERY_MIN_MS = 30000;
  static const unsigned long REDISCOVERY_MAX_MS = 600000;

  void connectPreferred(bool rediscovery);

  ConnectionState state = STATE_IDLE;
  unsigned long startedAt = 0;
  /// Pause before retrying the preferred network after a failed attempt.
  unsigned long rediscoveryDelayMs = 0;
  bool autoConnectPending = false;
  bool staStarted = false;
  /// True while failures of the preferred network should be retried, so
  /// repeater uplinks recover without user input.
  bool rediscoverPreferred = false;
};

/// Process-wide Wi-Fi manager instance.
extern WifiManager wifiManager;

#endif
