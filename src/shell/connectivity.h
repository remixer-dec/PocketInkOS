#ifndef SHELL_CONNECTIVITY_H
#define SHELL_CONNECTIVITY_H

/// Provides the status-bar glyph for the shared Wi-Fi state.
char wifiStatusIcon();
/// Reports whether the shell intends Wi-Fi to be enabled, even while connecting.
bool wifiIsOn();
/// Reports whether the shared Wi-Fi client currently has a network connection.
bool wifiIsConnected();
/// Enables Wi-Fi from shell UI such as quick settings.
void wifiTurnOn();
/// Disables Wi-Fi and lets shell UI reflect the power-saving state.
void wifiTurnOff();
/// Toggles Wi-Fi from the quick-settings device page.
void wifiToggle();
/// Advances the Wi-Fi service; the shell redraws when the visible state changes.
bool wifiUpdate();
/// Reapplies retained Wi-Fi preference after boot or deep-sleep wake.
void restoreWifiOn(bool enabled);

#endif
