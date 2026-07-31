/// Device IPv4 interface and local-hotspot controls used by the shell and
/// servers. Station addresses come from LAN DHCP automatically.
#ifndef POCKETINK_NET_NETWORK_INTERFACE_H
#define POCKETINK_NET_NETWORK_INTERFACE_H

#include <stddef.h>

namespace net {

struct InterfaceSnapshot {
  char stationAddress[16] = {};
  char tetherAddress[16] = {};
  char tetherSsid[33] = {};
  char tetherPassword[64] = {};
  bool stationConnected = false;
  bool tethering = false;
};

/// Selects STA or AP+STA without discarding an active tethering interface.
bool startStationInterface();
/// Stops only STA, retaining a local hotspot when it is active.
void stopStationInterface();

/// Starts a WPA2 local hotspot. Passwords shorter than eight bytes and open
/// hotspots are rejected.
bool startTethering(const char *ssid, const char *password);
/// Generates a per-boot SSID and password from the ESP32 hardware RNG.
bool startGeneratedTethering();
void stopTethering();
bool tetheringActive();

/// Returns station DHCP IP when available, otherwise the hotspot IP.
bool localAddress(char *out, size_t outSize);
InterfaceSnapshot interfaceSnapshot();

} // namespace net

#endif
