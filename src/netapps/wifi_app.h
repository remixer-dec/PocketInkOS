#ifndef WIFI_APP_H
#define WIFI_APP_H

#include <Adafruit_GFX.h>
#include <stddef.h>
#include "sys/services/wifi_manager.h"
#include "sys/services/touch_input.h"
#include "ui/components/network_settings_component.h"
#include "ui/components/t9_keyboard_component.h"
#include "ui/system_qr.h"
#include "ui/ui_helpers.h"

struct WifiPingSlot;

class WifiApp {
public:
  enum PopupAction : uint8_t {
    POPUP_ACTION_CONNECT,
    POPUP_ACTION_PREFERRED,
    POPUP_ACTION_REMOVE,
    POPUP_ACTION_DEVICES,
    POPUP_ACTION_DISCONNECT,
    POPUP_ACTION_SHARE,
  };

  void open();
  void release();
  void reset();
  void connect();
  void disconnect();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool hasActiveSession() const;
  WifiDisplayState displayState() const;
  void drawStatusIcon(Adafruit_GFX &gfx, int16_t x, int16_t y) const;

private:
  enum View : uint8_t {
    VIEW_NETWORKS,
    VIEW_PASSWORD,
    VIEW_DEVICES,
    VIEW_ROUTING
  };
  enum SelectedKind : uint8_t { SELECT_NONE, SELECT_SAVED, SELECT_SCAN };
  enum DeviceTab : uint8_t { TAB_INFO, TAB_NETSCAN };
  enum ScanState : uint8_t {
    SCAN_IDLE,
    SCAN_PENDING,
    SCAN_RUNNING,
    SCAN_DONE
  };
  enum PortScanState : uint8_t {
    PORT_IDLE,
    PORT_RUNNING,
    PORT_DONE
  };
  enum PortScanMode : uint8_t {
    PORT_MODE_QUICK,
    PORT_MODE_FULL
  };
  enum MacResolveState : uint8_t {
    MAC_RESOLVE_IDLE,
    MAC_RESOLVE_RUNNING,
    MAC_RESOLVE_DONE
  };

  static const uint8_t MAX_SAVED_NETWORKS = 3;
  static const uint8_t MAX_SCANNED_NETWORKS = 6;
  static const uint8_t WIFI_SSID_SIZE = 33;
  static const uint8_t WIFI_PASS_SIZE = 64;
  static const uint8_t WIFI_BSSID_SIZE = 18;
  static const uint16_t MAX_NET_DEVICES = 256;
  static const uint16_t NET_SCAN_TRIES_PER_FRAME = 10;
  static const uint16_t MAX_PORT_RESULTS = 1024;
  static const uint16_t PORT_SCAN_TIMEOUT_MS = 500;
  // 26 characters fit inside the device detail popup at the default font.
  static const uint8_t MAC_COMPANY_SIZE = 27;

  struct SavedNetwork {
    char ssid[WIFI_SSID_SIZE];
    char password[WIFI_PASS_SIZE];
    char bssid[WIFI_BSSID_SIZE];
    int32_t rssi;
  };

  struct ScanNetwork {
    char ssid[WIFI_SSID_SIZE];
    char bssid[WIFI_BSSID_SIZE];
    int32_t rssi;
  };

  struct NetworkDevice {
    uint32_t ip;
    uint16_t pingMs;
    char mac[18];
    char company[MAC_COMPANY_SIZE];
    bool hasMac;
    bool hasCompany;
    bool reachable;
  };

  struct PortResult {
    uint16_t port;
    bool open;
  };

  View view = VIEW_NETWORKS;
  SelectedKind selectedKind = SELECT_NONE;
  uint8_t selectedIndex = 0;
  uint16_t listOffset = 0;
  uint8_t savedCount = 0;
  uint8_t scanCount = 0;
  bool scanDone = false;
  bool popupOpen = false;
  bool popupConfirm = false;
  bool tetherPopupOpen = false;
  bool tetherQrOpen = false;
  SystemQrCode tetherQr;
  bool networkQrOpen = false;
  SystemQrCode networkQr;
  UiPressFeedback press;
  bool scanRequested = false;
  bool scanPending = false;
  bool signalScanRunning = false;
  bool pendingSaveAfterConnect = false;
  char pendingSsid[WIFI_SSID_SIZE] = "";
  char pendingBssid[WIFI_BSSID_SIZE] = "";
  char passwordInput[WIFI_PASS_SIZE] = "";
  T9KeyboardComponent passwordKeyboard;
  NetworkSettingsComponent routingSettings;
  SavedNetwork *savedNetworks = nullptr;
  ScanNetwork *scanNetworksCache = nullptr;
  DeviceTab deviceTab = TAB_INFO;
  ScanState networkScanState = SCAN_IDLE;
  unsigned long networkScanStartedAt = 0;
  uint32_t networkScanCurrent = 0;
  uint32_t networkScanStart = 0;
  uint32_t networkScanEnd = 0;
  uint16_t networkScanDeviceCount = 0;
  NetworkDevice *networkDevices = nullptr;
  uint16_t netScanVisibleOffset = 0;
  MacResolveState macResolveState = MAC_RESOLVE_IDLE;
  uint16_t macResolveCurrent = 0;
  bool macResolveWakeLockHeld = false;
  bool infoResolvedExtIp = false;
  char infoExtIpVal[46] = "";
  bool infoResolvedExtIpv6 = false;
  char infoExtIpv6Val[46] = "";

  bool devicePopupOpen = false;
  int8_t selectedDeviceIndex = -1;

  PortScanState portScanState = PORT_IDLE;
  PortScanMode portScanMode = PORT_MODE_QUICK;
  unsigned long portScanStartedAt = 0;
  uint16_t portScanCurrent = 0;
  uint16_t portScanTotal = 0;
  uint16_t portResultCount = 0;
  PortResult *portResults = nullptr;
  bool fullPortScanWakeLockHeld = false;
  WifiPingSlot *pingSlots = nullptr;
  uint32_t lastPortProgressUpdateMs = 0;
  uint8_t portScanTargetIp[4];

  bool ensureNetworkCaches();
  bool ensurePingSlots();
  void clearPingSlots();
  bool pingSlotsBusy() const;
  void releasePingSlotsIfIdle();
  void loadSavedNetworks();
  uint8_t savedNetworkCount() const;
  bool scanNetworkVisible(uint8_t scanIndex) const;
  uint8_t visibleScanNetworkCount() const;
  bool visibleScanNetworkAt(uint8_t visibleIndex, uint8_t &scanIndex) const;
  bool scannedSsidCached(const char *ssid) const;
  void clearScanCache();
  void saveNetwork(const char *ssid, const char *password, const char *bssid,
                   bool preferred);
  void removeSavedNetwork(uint8_t index);
  void setPreferredNetwork(uint8_t index);
  bool preferredMatches(const char *ssid) const;
  int8_t findSavedNetwork(const char *ssid) const;
  void beginPasswordEntry(const char *ssid, const char *bssid);
  bool startSta();
  void startSignalScan();
  bool advanceSignalScan();
  void updateSavedNetworkSignals(int found);
  void connectTo(const char *ssid, const char *password);
  void scanNetworks();
  void scanDevices();
  bool closePopup();
  void drawNetworks(Adafruit_GFX &gfx);
  void drawNetworkRow(Adafruit_GFX &gfx, int16_t y, const char *ssid,
                      const char *bssid, int32_t rssi, bool saved,
                      bool preferred, bool connected = false) const;
  void drawNetworkDeviceRow(Adafruit_GFX &gfx, uint16_t index,
                            uint8_t row) const;
  void drawPopup(Adafruit_GFX &gfx);
  void drawTetherPopup(Adafruit_GFX &gfx);
  void drawPassword(Adafruit_GFX &gfx);
  void drawDevices(Adafruit_GFX &gfx);
  void drawDeviceTabs(Adafruit_GFX &gfx);
  void drawInfoTab(Adafruit_GFX &gfx);
  void drawNetScanTab(Adafruit_GFX &gfx);
  void drawDevicePopup(Adafruit_GFX &gfx);
  void drawPortScan(Adafruit_GFX &gfx);
  void drawPortProgress(Adafruit_GFX &gfx, int16_t x, int16_t y,
                        int16_t w, int16_t h, uint16_t current,
                        uint16_t total);
  void drawLoadingIndicator(Adafruit_GFX &gfx, int16_t x, int16_t y);
  void handleNetworkTouch(const TouchPoint &point);
  void handlePopupTouch(const TouchPoint &point);
  void handleTetherTouch(const TouchPoint &point);
  void handlePasswordTouch(const TouchPoint &point);
  void handleDevicesTouch(const TouchPoint &point);
  void handleDeviceTabsTouch(const TouchPoint &point);
  void handleInfoTabTouch(const TouchPoint &point);
  void handleNetScanTabTouch(const TouchPoint &point);
  void handleDevicePopupTouch(const TouchPoint &point);
  void handlePortScanTouch(const TouchPoint &point);
  bool fetchPlainTextUrl(const char *url, char *out, size_t outSize) const;
  bool fetchMacCompany(const char *mac, char *out, size_t outSize) const;
  void startMacResolution();
  void advanceMacResolution();
  void releaseMacResolutionWakeLock();
  void resetMacResolution();
  void populateDeviceMac(NetworkDevice &device) const;
  uint8_t selectedPopupActions(PopupAction *actions, uint8_t capacity,
                               char *ssid, size_t ssidSize, char *password,
                               size_t passwordSize) const;
  void performPopupAction(PopupAction action, const char *ssid,
                          const char *password);
  void startNetworkScan();
  void advanceNetworkScan();
  void startPortScan(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                     PortScanMode mode);
  void releaseFullPortScanWakeLock();
  void completePortScan();
  void advancePortScan();
  void freeNetDevices();
  void freePortResults();
  void drawWifiIcon(Adafruit_GFX &gfx, int16_t x, int16_t y) const;
};

#endif
