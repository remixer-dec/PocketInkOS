#ifndef SHELL_CONNECTIVITY_H
#define SHELL_CONNECTIVITY_H

char wifiStatusIcon();
bool wifiIsOn();
bool wifiIsConnected();
void wifiTurnOn();
void wifiTurnOff();
void wifiToggle();
bool wifiUpdate();
void restoreWifiOn(bool enabled);

#endif
