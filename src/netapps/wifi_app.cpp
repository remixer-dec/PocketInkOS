#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "netapps/wifi_app.h"
#include "sys/psram_allocator.h"
#include "sys/global.h"
#include "sys/system_config.h"
#include "ui/icon_ascii_font.h"
#include "ui/components/list_component.h"
#include "ui/components/select_button_component.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <atomic>
#include <ctype.h>
#include <cstring>
#include <new>

extern "C" {
  #include "ping/ping_sock.h"
  #include "lwip/ip_addr.h"
}

#include "lwip/etharp.h"
#include "lwip/netif.h"

struct WifiPingSlot {
  uint32_t ip = 0;
  uint16_t pingMs = 0;
  bool reachable = false;
  std::atomic<bool> done{false};
};

static void on_ping_success(esp_ping_handle_t hdl, void *args) {
  WifiPingSlot *slot = static_cast<WifiPingSlot *>(args);
  uint32_t elapsed_ms;
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_ms, sizeof(elapsed_ms));
  slot->reachable = true;
  slot->pingMs = elapsed_ms > 65535 ? 65535 : static_cast<uint16_t>(elapsed_ms);
  slot->done.store(true, std::memory_order_release);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args) {
  (void)hdl;
  WifiPingSlot *slot = static_cast<WifiPingSlot *>(args);
  slot->reachable = false;
  slot->pingMs = 65535;
  slot->done.store(true, std::memory_order_release);
}

static void on_ping_end(esp_ping_handle_t hdl, void *args) {
  (void)args;
  esp_ping_delete_session(hdl);
}

static const UiRect SCAN_BUTTON = {4, 172, 60, 24};
static const UiRect WIFI_TOGGLE_BUTTON = {136, 172, 60, 24};
static const UiRect POPUP_RECT = {10, 50, 180, 110};
static const UiRect PASSWORD_CANCEL_BUTTON = {4, 4, 58, 20};
static const UiRect DEVICE_BACK_BUTTON = {4, 172, 60, 24};
static const UiRect DEVICE_SCAN_BUTTON = {136, 172, 60, 24};
static const UiRect DEVICE_POPUP_RECT = {20, 50, 160, 104};
static const uint8_t NETWORK_ROW_COUNT = 4;
static const int16_t NETWORK_ROW_Y = 20;
static const int16_t NETWORK_ROW_H = 36;
static const int16_t NETWORK_LIST_H = NETWORK_ROW_COUNT * NETWORK_ROW_H;
static const int16_t NETWORK_LIST_SPLIT_X = 194;
static const int16_t NETWORK_SCROLLBAR_X = 198;
static const int16_t NETWORK_SCROLLBAR_ARROW_X = 194;
static const int16_t NETWORK_ROW_X = 4;
static const int16_t TAB_Y = 4;
static const int16_t TAB_H = 28;
static const int16_t TAB_W = 96;
static const int16_t TAB_GAP = 4;
static const int16_t CONTENT_Y = TAB_Y + TAB_H + 4;
static const int16_t INFO_LINE_HEIGHT = 20;
static const int16_t INFO_SEP_X = 68;
static const int16_t NET_ROW_H = 26;
static const int16_t NET_ROW_Y = CONTENT_Y;
static const uint8_t NET_VISIBLE_ROWS = 4;
static const int16_t NET_LIST_H_VAL = NET_VISIBLE_ROWS * NET_ROW_H;
static const int16_t NET_SPLIT_X = 140;
static const int16_t PORT_SCAN_MAX_WIDTH = 192;
static const char *PUBLIC_IPV4_URL = "http://api4.ipify.org";
static const char *PUBLIC_IPV6_URL = "http://api6.ipify.org";
static const int16_t DIALOG_BACKDROP_TOP_Y = 20;
static const int16_t DIALOG_HATCH_STEP = 6;

static const uint16_t QUICK_PORTS[] = {
  22, 23, 68, 80, 135, 443, 445, 515, 631, 2999,
  2222, 2631, 3389, 4000, 4096, 4500, 4600, 5000, 5001, 5060,
  5228, 5229, 5230, 5235, 5236, 5353, 5555, 5577, 5900, 5901,
  6000, 6100, 7080, 8080, 8081, 8230, 8258, 8282, 8382, 8682,
  8990, 9000, 20817, 30020, 39003, 50001, 55283, 59150, 59152, 62078
};
static const uint16_t QUICK_PORT_COUNT = sizeof(QUICK_PORTS) / sizeof(QUICK_PORTS[0]);

static void copyText(char *out, size_t outSize, const char *value) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  strncpy(out, value != nullptr ? value : "", outSize - 1);
  out[outSize - 1] = '\0';
}

static void copyString(char *out, size_t outSize, const String &value) {
  copyText(out, outSize, value.c_str());
}

static void copyUtf8Prefix(char *out, size_t outSize, const char *value,
                           size_t maxBytes) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const char *text = value != nullptr ? value : "";
  size_t read = 0;
  size_t write = 0;
  while (text[read] != '\0' && write + 1 < outSize && write < maxBytes) {
    const unsigned char c = static_cast<unsigned char>(text[read]);
    size_t charLen = 1;
    if ((c & 0xE0) == 0xC0) {
      charLen = 2;
    } else if ((c & 0xF0) == 0xE0) {
      charLen = 3;
    } else if ((c & 0xF8) == 0xF0) {
      charLen = 4;
    }
    if (write + charLen + 1 > outSize || write + charLen > maxBytes) {
      break;
    }
    for (size_t i = 0; i < charLen && text[read + i] != '\0'; i++) {
      out[write++] = text[read + i];
    }
    read += charLen;
  }
  out[write] = '\0';
}

static void ipToText(char *out, size_t outSize, const IPAddress &ip) {
  copyString(out, outSize, ip.toString());
}

static void rawIpToText(char *out, size_t outSize, uint32_t raw) {
  snprintf(out, outSize, "%u.%u.%u.%u",
           (raw >> 24) & 0xFF,
           (raw >> 16) & 0xFF,
           (raw >> 8) & 0xFF,
           raw & 0xFF);
}

static uint32_t ipToRaw(const IPAddress &ip) {
  return (static_cast<uint32_t>(ip[0]) << 24) |
         (static_cast<uint32_t>(ip[1]) << 16) |
         (static_cast<uint32_t>(ip[2]) << 8) |
         static_cast<uint32_t>(ip[3]);
}

static bool arpMacToText(uint32_t raw, char *out, size_t outSize) {
  if (out == nullptr || outSize < 18 || netif_default == nullptr) {
    return false;
  }

  ip4_addr_t ip;
  IP4_ADDR(&ip, (raw >> 24) & 0xFF, (raw >> 16) & 0xFF, (raw >> 8) & 0xFF,
           raw & 0xFF);

  struct eth_addr *eth = nullptr;
  const ip4_addr_t *foundIp = nullptr;
  if (etharp_find_addr(netif_default, &ip, &eth, &foundIp) < 0 ||
      eth == nullptr) {
    return false;
  }

  snprintf(out, outSize, "%02x:%02x:%02x:%02x:%02x:%02x",
           eth->addr[0], eth->addr[1], eth->addr[2], eth->addr[3],
           eth->addr[4], eth->addr[5]);
  return true;
}

static void *appAlloc(size_t size) {
  return psram::allocateOrDefault(size);
}

static UiRect popupButtonRect(uint8_t index, uint8_t count) {
  static const int16_t BUTTON_W = 78;
  static const int16_t BUTTON_H = 24;
  static const int16_t BUTTON_GAP = 10;
  const uint8_t rows = static_cast<uint8_t>((count + 1U) / 2U);
  const uint8_t row = static_cast<uint8_t>(index / 2U);
  const uint8_t rowStart = static_cast<uint8_t>(row * 2U);
  uint8_t rowCount = count > rowStart ? count - rowStart : 0;
  if (rowCount > 2) {
    rowCount = 2;
  }
  const uint8_t col = static_cast<uint8_t>(index - rowStart);
  const int16_t totalW =
      rowCount * BUTTON_W + (rowCount > 1 ? BUTTON_GAP : 0);
  const int16_t x =
      POPUP_RECT.x + (POPUP_RECT.w - totalW) / 2 +
      col * (BUTTON_W + BUTTON_GAP);
  const int16_t startY = rows <= 1 ? 108 : 88;
  return {x, static_cast<int16_t>(startY + row * 34), BUTTON_W, BUTTON_H};
}

static UiRect devicePopupButtonRect(uint8_t index, uint8_t count) {
  static const int16_t BW = 68;
  static const int16_t BH = 24;
  static const int16_t GAP = 12;
  const int16_t totalW = count * BW + (count > 1 ? GAP : 0);
  const int16_t x = DEVICE_POPUP_RECT.x + (DEVICE_POPUP_RECT.w - totalW) / 2 +
                     index * (BW + GAP);
  return {x, static_cast<int16_t>(DEVICE_POPUP_RECT.y + DEVICE_POPUP_RECT.h - 34), BW, BH};
}

static const char *popupActionLabel(WifiApp::PopupAction action) {
  switch (action) {
  case WifiApp::POPUP_ACTION_CONNECT:
    return "CONNECT";
  case WifiApp::POPUP_ACTION_PREFERRED:
    return "PREFERRED";
  case WifiApp::POPUP_ACTION_REMOVE:
    return "REMOVE";
  case WifiApp::POPUP_ACTION_DEVICES:
    return "DEVICES";
  case WifiApp::POPUP_ACTION_DISCONNECT:
    return "DISCONNECT";
  }
  return "";
}

static void drawDialogRect(Adafruit_GFX &gfx, const UiRect &rect) {
  gfx.drawRect(rect.x, rect.y, rect.w, rect.h, 1);
  gfx.drawLine(rect.x + 1, rect.y + rect.h, rect.x + rect.w - 1,
               rect.y + rect.h, 1);
  gfx.drawLine(rect.x + 2, rect.y + rect.h + 1, rect.x + rect.w,
               rect.y + rect.h + 1, 1);
}

static void drawDialogBackdropHatch(Adafruit_GFX &gfx) {
  for (int16_t start = 0; start < EPD_WIDTH + EPD_HEIGHT;
       start += DIALOG_HATCH_STEP) {
    int16_t x0 = start;
    int16_t y0 = DIALOG_BACKDROP_TOP_Y;
    int16_t x1 = start - (EPD_HEIGHT - 1 - DIALOG_BACKDROP_TOP_Y);
    int16_t y1 = EPD_HEIGHT - 1;
    if (x0 >= EPD_WIDTH) {
      y0 += x0 - (EPD_WIDTH - 1);
      x0 = EPD_WIDTH - 1;
    }
    if (x1 < 0) {
      y1 += x1;
      x1 = 0;
    }
    if (y0 >= DIALOG_BACKDROP_TOP_Y && y0 < EPD_HEIGHT &&
        y1 >= DIALOG_BACKDROP_TOP_Y && y1 < EPD_HEIGHT) {
      gfx.drawLine(x0, y0, x1, y1, 1);
    }
  }
}

static void drawListScrollbar(Adafruit_GFX &gfx, int16_t x, int16_t y,
                              int16_t h, uint16_t offset,
                              uint16_t itemCount, uint8_t visibleCount) {
  if (itemCount <= visibleCount || visibleCount == 0) {
    return;
  }
  const int16_t trackX = x;
  const int16_t trackY = y + 7;
  const int16_t trackH = h - 14;
  gfx.drawLine(trackX, trackY, trackX, trackY + trackH, 1);

  int16_t thumbH = static_cast<int16_t>(
      (static_cast<uint32_t>(visibleCount) * trackH) / itemCount);
  if (thumbH < 10) {
    thumbH = 10;
  }
  if (thumbH > trackH) {
    thumbH = trackH;
  }

  const uint16_t maxOffset = uiListMaxOffset(itemCount, visibleCount);
  const int16_t travel = trackH - thumbH;
  const int16_t thumbY =
      trackY + (maxOffset > 0 ? (static_cast<int32_t>(offset) * travel) /
                                    maxOffset
                              : 0);
  gfx.fillRect(trackX, thumbY, 2, thumbH, 1);
}

static void drawCenteredPopupTitle(Adafruit_GFX &gfx, const char *text) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  gfx.setCursor(POPUP_RECT.x + (POPUP_RECT.w - static_cast<int16_t>(textW)) / 2 -
                    textX,
                64);
  gfx.print(text);
}

void WifiApp::open() {
  loadSavedNetworks();
}

void WifiApp::release() {
  popupOpen = false;
  popupConfirm = false;
  devicePopupOpen = false;
  pendingSaveAfterConnect = false;
  view = VIEW_NETWORKS;
  networkScanState = SCAN_IDLE;
  portScanState = PORT_IDLE;
  freeNetDevices();
  freePortResults();

  if (savedNetworks != nullptr) {
    heap_caps_free(savedNetworks);
    savedNetworks = nullptr;
  }
  if (scanNetworksCache != nullptr) {
    heap_caps_free(scanNetworksCache);
    scanNetworksCache = nullptr;
  }
  savedCount = 0;
  scanCount = 0;
  scanDone = false;
  if (networkScanState != SCAN_RUNNING) {
    releasePingSlotsIfIdle();
  }
}

bool WifiApp::ensureNetworkCaches() {
  if (savedNetworks == nullptr) {
    savedNetworks = static_cast<SavedNetwork *>(
        appAlloc(sizeof(SavedNetwork) * MAX_SAVED_NETWORKS));
    if (savedNetworks == nullptr) {
      savedCount = 0;
      wifiManager.setStatus("Low app heap");
      return false;
    }
    memset(savedNetworks, 0, sizeof(SavedNetwork) * MAX_SAVED_NETWORKS);
    for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) {
      savedNetworks[i].rssi = -999;
    }
  }
  if (scanNetworksCache == nullptr) {
    scanNetworksCache = static_cast<ScanNetwork *>(
        appAlloc(sizeof(ScanNetwork) * MAX_SCANNED_NETWORKS));
    if (scanNetworksCache == nullptr) {
      scanCount = 0;
      wifiManager.setStatus("Low app heap");
      return false;
    }
    memset(scanNetworksCache, 0, sizeof(ScanNetwork) * MAX_SCANNED_NETWORKS);
    for (uint8_t i = 0; i < MAX_SCANNED_NETWORKS; i++) {
      scanNetworksCache[i].rssi = -999;
    }
  }
  return true;
}

bool WifiApp::ensurePingSlots() {
  if (pingSlots != nullptr) {
    return true;
  }
  pingSlots = static_cast<WifiPingSlot *>(
      appAlloc(sizeof(WifiPingSlot) * NET_SCAN_TRIES_PER_FRAME));
  if (pingSlots == nullptr) {
    return false;
  }
  for (uint8_t i = 0; i < NET_SCAN_TRIES_PER_FRAME; i++) {
    new (&pingSlots[i]) WifiPingSlot();
  }
  clearPingSlots();
  return true;
}

void WifiApp::clearPingSlots() {
  if (pingSlots == nullptr) {
    return;
  }
  for (uint8_t i = 0; i < NET_SCAN_TRIES_PER_FRAME; i++) {
    pingSlots[i].ip = 0;
    pingSlots[i].pingMs = 0;
    pingSlots[i].reachable = false;
    pingSlots[i].done.store(false, std::memory_order_relaxed);
  }
}

bool WifiApp::pingSlotsBusy() const {
  if (pingSlots == nullptr) {
    return false;
  }
  for (uint8_t i = 0; i < NET_SCAN_TRIES_PER_FRAME; i++) {
    if (pingSlots[i].ip != 0 &&
        !pingSlots[i].done.load(std::memory_order_acquire)) {
      return true;
    }
  }
  return false;
}

void WifiApp::releasePingSlotsIfIdle() {
  if (pingSlots != nullptr && !pingSlotsBusy()) {
    for (uint8_t i = 0; i < NET_SCAN_TRIES_PER_FRAME; i++) {
      pingSlots[i].~WifiPingSlot();
    }
    heap_caps_free(pingSlots);
    pingSlots = nullptr;
  }
}

void WifiApp::reset() {
  clearScanCache();
  scanRequested = false;
  savedCount = 0;
  if (savedNetworks != nullptr) {
    loadSavedNetworks();
  }
  view = VIEW_NETWORKS;
  popupOpen = false;
  popupConfirm = false;
  press.clear();
  devicePopupOpen = false;
  passwordInput[0] = '\0';
  networkScanState = SCAN_IDLE;
  portScanState = PORT_IDLE;
  infoResolvedExtIp = false;
  infoExtIpVal[0] = '\0';
  infoExtIpv6Val[0] = '\0';
  freeNetDevices();
  freePortResults();
}

void WifiApp::connect() {
  wifiManager.connect();
}

bool WifiApp::startSta() {
  return wifiManager.startSta();
}

void WifiApp::connectTo(const char *ssid, const char *password) {
  clearScanCache();
  scanRequested = false;
  wifiManager.connectTo(ssid, password);
}

void WifiApp::disconnect() {
  clearScanCache();
  wifiManager.disconnect();
}

bool WifiApp::hasActiveSession() const {
  return wifiManager.hasActiveSession() ||
         networkScanState == SCAN_RUNNING || portScanState == PORT_RUNNING;
}

WifiDisplayState WifiApp::displayState() const {
  return wifiManager.displayState();
}

void WifiApp::draw(Adafruit_GFX &gfx) {
  if (view == VIEW_PASSWORD) {
    drawPassword(gfx);
  } else if (view == VIEW_DEVICES) {
    drawDevices(gfx);
  } else {
    drawNetworks(gfx);
  }
}

bool WifiApp::update() {
  bool dirty = wifiManager.update();
  if (press.update()) {
    dirty = true;
  }

  if (networkScanState != SCAN_RUNNING) {
    releasePingSlotsIfIdle();
  }

  if (scanPending) {
    scanPending = false;
    return true;
  }

  if (scanRequested && !scanDone) {
    if (!ensureNetworkCaches()) {
      scanRequested = false;
      scanDone = true;
      return true;
    }
    clearScanCache();
    const int found = WiFi.scanNetworks();
    scanDone = true;
    if (found > 0) {
      for (int i = 0; i < found && scanCount < MAX_SCANNED_NETWORKS; i++) {
        char ssid[WIFI_SSID_SIZE];
        copyString(ssid, sizeof(ssid), WiFi.SSID(i));
        if (ssid[0] == '\0' || findSavedNetwork(ssid) >= 0 ||
            preferredMatches(ssid) || scannedSsidCached(ssid)) {
          continue;
        }
        ScanNetwork &target = scanNetworksCache[scanCount++];
        copyText(target.ssid, sizeof(target.ssid), ssid);
        copyString(target.bssid, sizeof(target.bssid), WiFi.BSSIDstr(i));
        target.rssi = WiFi.RSSI(i);
      }
      for (int i = 0; i < found; i++) {
        char ssid[WIFI_SSID_SIZE];
        copyString(ssid, sizeof(ssid), WiFi.SSID(i));
        if (ssid[0] == '\0') continue;
        for (uint8_t s = 0; s < savedCount; s++) {
          if (strcmp(savedNetworks[s].ssid, ssid) == 0) {
            copyString(savedNetworks[s].bssid, sizeof(savedNetworks[s].bssid),
                       WiFi.BSSIDstr(i));
            savedNetworks[s].rssi = WiFi.RSSI(i);
            break;
          }
        }
      }
      WiFi.scanDelete();
    }
    return true;
  }

  if (view == VIEW_PASSWORD && passwordKeyboard.update()) {
    return true;
  }

  if (pendingSaveAfterConnect && wifiManager.isConnected()) {
    pendingSaveAfterConnect = false;
    saveNetwork(pendingSsid, passwordInput, pendingBssid, true);
    dirty = true;
  }

  if (pendingSaveAfterConnect && !wifiManager.isConnecting()) {
    pendingSaveAfterConnect = false;
    dirty = true;
  }

  if (view == VIEW_DEVICES) {
    if (networkScanState == SCAN_RUNNING) {
      advanceNetworkScan();
      return true;
    }
    if (portScanState == PORT_RUNNING) {
      advancePortScan();
      return true;
    }
    if (networkScanState == SCAN_PENDING) {
      startNetworkScan();
      return true;
    }
  }

  return dirty;
}

bool WifiApp::handleTouch(const TouchPoint &point) {
  if (view == VIEW_PASSWORD) {
    handlePasswordTouch(point);
  } else if (view == VIEW_DEVICES) {
    handleDevicesTouch(point);
  } else if (popupOpen) {
    handlePopupTouch(point);
  } else {
    handleNetworkTouch(point);
  }
  return true;
}

bool WifiApp::handleMenuButton() {
  if (view == VIEW_PASSWORD) {
    view = VIEW_NETWORKS;
    pendingSaveAfterConnect = false;
    return true;
  }
  if (view == VIEW_DEVICES) {
    if (devicePopupOpen) {
      devicePopupOpen = false;
      return true;
    }
    if (portScanState != PORT_IDLE) {
      portScanState = PORT_IDLE;
      freePortResults();
      return true;
    }
    view = VIEW_NETWORKS;
    networkScanState = SCAN_IDLE;
    freeNetDevices();
    return true;
  }
  return closePopup();
}

void WifiApp::loadSavedNetworks() {
  savedCount = 0;
  if (!ensureNetworkCaches()) {
    return;
  }
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) {
    savedNetworks[i] = {};
    savedNetworks[i].rssi = -999;
  }
  const SystemConfig &cfg = currentSystemConfig();
  for (uint8_t i = 0; i < CONFIG_WIFI_SAVED_MAX && savedCount < MAX_SAVED_NETWORKS;
       i++) {
    const ConfigWifiSavedNetwork &source = cfg.wifi.saved[i];
    if (source.ssid[0] != '\0') {
      copyText(savedNetworks[savedCount].ssid,
               sizeof(savedNetworks[savedCount].ssid), source.ssid);
      copyText(savedNetworks[savedCount].password,
               sizeof(savedNetworks[savedCount].password), source.password);
      copyText(savedNetworks[savedCount].bssid,
               sizeof(savedNetworks[savedCount].bssid), source.bssid);
      savedCount++;
    }
  }
  if (cfg.wifi.ssid[0] != '\0' && findSavedNetwork(cfg.wifi.ssid) < 0 &&
      savedCount < MAX_SAVED_NETWORKS) {
    copyText(savedNetworks[savedCount].ssid, sizeof(savedNetworks[0].ssid),
             cfg.wifi.ssid);
    copyText(savedNetworks[savedCount].password,
             sizeof(savedNetworks[0].password), cfg.wifi.password);
    savedNetworks[savedCount].bssid[0] = '\0';
    savedCount++;
  }
}

uint8_t WifiApp::savedNetworkCount() const {
  return savedCount;
}

int8_t WifiApp::findSavedNetwork(const char *ssid) const {
  if (ssid == nullptr || ssid[0] == '\0') {
    return -1;
  }
  if (savedNetworks == nullptr) {
    return -1;
  }
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) {
    if (i < savedCount && strcmp(savedNetworks[i].ssid, ssid) == 0) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

bool WifiApp::scanNetworkVisible(uint8_t scanIndex) const {
  if (scanNetworksCache == nullptr || scanIndex >= scanCount) {
    return false;
  }
  const char *ssid = scanNetworksCache[scanIndex].ssid;
  return ssid[0] != '\0' && findSavedNetwork(ssid) < 0 &&
         !preferredMatches(ssid);
}

uint8_t WifiApp::visibleScanNetworkCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < scanCount; i++) {
    if (scanNetworkVisible(i)) {
      count++;
    }
  }
  return count;
}

bool WifiApp::visibleScanNetworkAt(uint8_t visibleIndex,
                                   uint8_t &scanIndex) const {
  if (scanNetworksCache == nullptr) {
    return false;
  }
  uint8_t seen = 0;
  for (uint8_t i = 0; i < scanCount; i++) {
    if (!scanNetworkVisible(i)) {
      continue;
    }
    if (seen == visibleIndex) {
      scanIndex = i;
      return true;
    }
    seen++;
  }
  return false;
}

bool WifiApp::scannedSsidCached(const char *ssid) const {
  if (ssid == nullptr || ssid[0] == '\0') {
    return true;
  }
  if (scanNetworksCache == nullptr) {
    return false;
  }
  for (uint8_t i = 0; i < scanCount; i++) {
    if (strcmp(scanNetworksCache[i].ssid, ssid) == 0) {
      return true;
    }
  }
  return false;
}

void WifiApp::clearScanCache() {
  scanCount = 0;
  scanDone = false;
  if (scanNetworksCache == nullptr) {
    return;
  }
  for (uint8_t i = 0; i < MAX_SCANNED_NETWORKS; i++) {
    scanNetworksCache[i] = {};
    scanNetworksCache[i].rssi = -999;
  }
}

bool WifiApp::preferredMatches(const char *ssid) const {
  return wifiManager.preferredMatches(ssid);
}

void WifiApp::saveNetwork(const char *ssid, const char *password,
                          const char *bssid, bool preferred) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return;
  }
  int8_t slot = findSavedNetwork(ssid);
  if (slot < 0) {
    slot = savedCount < MAX_SAVED_NETWORKS ? savedCount : MAX_SAVED_NETWORKS - 1;
  }
  wifiManager.saveNetworkSlot(static_cast<uint8_t>(slot), ssid, password,
                              bssid);
  if (preferred) {
    wifiManager.savePreferred(ssid, password);
  }
  loadSavedNetworks();
}

void WifiApp::removeSavedNetwork(uint8_t index) {
  if (savedNetworks == nullptr || index >= savedCount) {
    return;
  }
  char removedSsid[WIFI_SSID_SIZE];
  copyText(removedSsid, sizeof(removedSsid), savedNetworks[index].ssid);
  for (uint8_t i = index; i < MAX_SAVED_NETWORKS; i++) {
    const SavedNetwork *next = i + 1 < savedCount ? &savedNetworks[i + 1] : nullptr;
    wifiManager.saveNetworkSlot(i, next != nullptr ? next->ssid : "",
                                next != nullptr ? next->password : "",
                                next != nullptr ? next->bssid : "");
  }
  wifiManager.clearPreferredIfMatches(removedSsid);
  loadSavedNetworks();
}

void WifiApp::setPreferredNetwork(uint8_t index) {
  if (savedNetworks == nullptr || index >= savedCount) {
    return;
  }
  wifiManager.savePreferred(savedNetworks[index].ssid,
                            savedNetworks[index].password);
  loadSavedNetworks();
}

void WifiApp::beginPasswordEntry(const char *ssid, const char *bssid) {
  copyText(pendingSsid, sizeof(pendingSsid), ssid);
  copyText(pendingBssid, sizeof(pendingBssid), bssid);
  passwordInput[0] = '\0';
  passwordKeyboard.setLayout(T9KeyboardComponent::Layout::Text);
  popupOpen = false;
  view = VIEW_PASSWORD;
}

void WifiApp::scanNetworks() {
  if (!startSta()) {
    return;
  }
  if (!ensureNetworkCaches()) {
    return;
  }
  scanRequested = true;
  scanPending = true;
  scanDone = false;
  listOffset = 0;
}

void WifiApp::scanDevices() {
  view = VIEW_DEVICES;
  deviceTab = TAB_INFO;
  networkScanState = SCAN_IDLE;
  portScanState = PORT_IDLE;
  devicePopupOpen = false;
  infoResolvedExtIp = false;
  infoExtIpVal[0] = '\0';
  infoExtIpv6Val[0] = '\0';
  freeNetDevices();
  freePortResults();
}

void WifiApp::freeNetDevices() {
  if (networkDevices != nullptr) {
    heap_caps_free(networkDevices);
    networkDevices = nullptr;
  }
  networkScanDeviceCount = 0;
  releasePingSlotsIfIdle();
}

void WifiApp::freePortResults() {
  if (portResults != nullptr) {
    heap_caps_free(portResults);
    portResults = nullptr;
  }
  portResultCount = 0;
}

bool WifiApp::closePopup() {
  if (!popupOpen) {
    return false;
  }
  popupOpen = false;
  popupConfirm = false;
  return true;
}

void WifiApp::drawNetworkRow(Adafruit_GFX &gfx, int16_t y, const char *ssid,
                             const char *bssid, int32_t rssi, bool saved,
                             bool preferred, bool connected) const {
  gfx.drawRect(NETWORK_ROW_X, y, 192, NETWORK_ROW_H - 2, 1);
  gfx.setTextSize(1);
  gfx.setCursor(NETWORK_ROW_X + 4, y + 5);
  if (preferred) {
    gfx.print("* ");
  } else if (saved) {
    gfx.print("+ ");
  } else {
    gfx.print("  ");
  }
  char name[20];
  copyUtf8Prefix(name, sizeof(name), ssid, 17);
  if (ssid != nullptr && strlen(name) < strlen(ssid) && strlen(name) + 1 < sizeof(name)) {
    strcat(name, "~");
  }
  gfx.print(name);

  char info[34];
  if (rssi > -200) {
    snprintf(info, sizeof(info), "%lddb %s", static_cast<long>(rssi),
             bssid != nullptr && bssid[0] != '\0' ? bssid : "--");
  } else {
    snprintf(info, sizeof(info), "--db %s",
             bssid != nullptr && bssid[0] != '\0' ? bssid : "--");
  }
  if (strlen(info) > 31) {
    info[31] = '\0';
  }
  gfx.setCursor(NETWORK_ROW_X + 4, y + 20);
  gfx.print(info);

  if (connected) {
    gfx.setFont(&iconASCII12pt7b);
    gfx.setTextSize(1);
    int16_t iconX;
    int16_t iconY;
    uint16_t iconW;
    uint16_t iconH;
    gfx.getTextBounds("g", 0, 0, &iconX, &iconY, &iconW, &iconH);
    gfx.setCursor(NETWORK_ROW_X + 192 - 5 - static_cast<int16_t>(iconW) -
                      iconX,
                  y + 17);
    gfx.print("g");
    gfx.setFont();
    gfx.setTextSize(1);
  }
}

void WifiApp::drawLoadingIndicator(Adafruit_GFX &gfx, int16_t x, int16_t y) {
  unsigned long now = millis();
  unsigned phase = (now / 500) % 4;
  gfx.setCursor(x, y);
  gfx.setTextSize(1);

  if (view == VIEW_NETWORKS) {
    if (scanRequested && displayState() != WIFI_DISPLAY_OFF && !scanDone) {
      char buf[16];
      snprintf(buf, sizeof(buf), "Scanning");
      size_t len = strlen(buf);
      for (unsigned i = 0; i < phase && len + 1 < sizeof(buf); i++) {
        buf[len++] = '.';
      }
      buf[len] = '\0';
      int16_t tx;
      int16_t ty;
      uint16_t tw;
      uint16_t th;
      gfx.getTextBounds(buf, 0, 0, &tx, &ty, &tw, &th);
      gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(tw)) / 2 - tx, 74);
      gfx.print(buf);
    }
  } else if (view == VIEW_DEVICES) {
    if (deviceTab == TAB_NETSCAN && networkScanState == SCAN_RUNNING) {
      uint16_t done = static_cast<uint16_t>(networkScanCurrent - networkScanStart);
      uint16_t total = static_cast<uint16_t>(networkScanEnd - networkScanStart);
      gfx.setCursor(4, NET_ROW_Y + NET_VISIBLE_ROWS * NET_ROW_H + 4);
      gfx.print("Scanning");
      for (unsigned i = 0; i < phase; i++) {
        gfx.print(".");
      }
      gfx.print(" ");
      gfx.print(done);
      gfx.print("/");
      gfx.print(total);
    }
  }
}

void WifiApp::drawNetworks(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(4, 4);
  gfx.print("WiFi");
  gfx.setCursor(42, 4);
  if (WiFi.status() == WL_CONNECTED) {
    gfx.print("connected ");
    gfx.print(currentSystemConfig().wifi.displayName[0] != '\0'
                  ? currentSystemConfig().wifi.displayName
                  : WiFi.SSID().c_str());
  } else if (wifiManager.connectionState() == WifiManager::STATE_CONNECTING) {
    gfx.print("connecting");
  } else if (wifiManager.connectionState() == WifiManager::STATE_FAILED) {
    const char *status = wifiManager.statusText();
    gfx.print(status[0] != '\0' ? status : "failed");
  } else {
    gfx.print("off");
  }

  if (scanRequested && !scanDone && displayState() != WIFI_DISPLAY_OFF) {
    drawLoadingIndicator(gfx, 0, 0);
    if (displayState() != WIFI_DISPLAY_OFF) {
      uiDrawButton(gfx, SCAN_BUTTON, "SCAN", press.isPressed(0));
    }
    uiDrawButton(gfx, WIFI_TOGGLE_BUTTON,
                 displayState() == WIFI_DISPLAY_OFF ? "ON" : "OFF",
                 press.isPressed(1));
    if (popupOpen) {
      drawPopup(gfx);
    }
    return;
  }

  const uint8_t savedCount = savedNetworkCount();
  const uint16_t total = savedCount + visibleScanNetworkCount();
  uiListClampOffset(listOffset, total, NETWORK_ROW_COUNT);
  drawListScrollbar(gfx, NETWORK_SCROLLBAR_X, NETWORK_ROW_Y, NETWORK_LIST_H,
                    listOffset, total, NETWORK_ROW_COUNT);
  if (listOffset > 0) {
    gfx.setCursor(NETWORK_SCROLLBAR_ARROW_X, 18);
    gfx.print("^");
  }
  uint8_t row = 0;
  for (uint16_t i = listOffset; i < total && row < NETWORK_ROW_COUNT;
       i++, row++) {
    const int16_t y = NETWORK_ROW_Y + row * NETWORK_ROW_H;
    if (i < savedCount) {
      const SavedNetwork &network = savedNetworks[i];
      const char *label = preferredMatches(network.ssid) &&
                                  currentSystemConfig().wifi.displayName[0] != '\0'
                              ? currentSystemConfig().wifi.displayName
                              : network.ssid;
      drawNetworkRow(gfx, y, label, network.bssid, network.rssi, true,
                     preferredMatches(network.ssid),
                     preferredMatches(network.ssid) &&
                         WiFi.status() == WL_CONNECTED);
    } else {
      uint8_t scanIndex = 0;
      if (visibleScanNetworkAt(i - savedCount, scanIndex)) {
        const ScanNetwork &network = scanNetworksCache[scanIndex];
        drawNetworkRow(gfx, y, network.ssid, network.bssid, network.rssi,
                       false, preferredMatches(network.ssid));
      }
    }
  }
  if (listOffset + NETWORK_ROW_COUNT < total) {
    gfx.setCursor(NETWORK_SCROLLBAR_ARROW_X, 164);
    gfx.print("v");
  }
  if (total == 0) {
    const char *msg = savedCount > 0 || scanDone
                          ? "No networks found"
                          : (displayState() == WIFI_DISPLAY_OFF ? "Wifi off" : "No networks");
    int16_t tx;
    int16_t ty;
    uint16_t tw;
    uint16_t th;
    gfx.getTextBounds(msg, 0, 0, &tx, &ty, &tw, &th);
    gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(tw)) / 2 - tx, 74);
    gfx.print(msg);
  }

  if (displayState() != WIFI_DISPLAY_OFF) {
    uiDrawButton(gfx, SCAN_BUTTON, "SCAN", press.isPressed(0));
  }
  uiDrawButton(gfx, WIFI_TOGGLE_BUTTON,
               displayState() == WIFI_DISPLAY_OFF ? "ON" : "OFF",
               press.isPressed(1));
  if (popupOpen) {
    drawPopup(gfx);
  }
}

uint8_t WifiApp::selectedPopupActions(PopupAction *actions, uint8_t capacity,
                                      char *ssid, size_t ssidSize,
                                      char *password,
                                      size_t passwordSize) const {
  if (ssid != nullptr && ssidSize > 0) {
    ssid[0] = '\0';
  }
  if (password != nullptr && passwordSize > 0) {
    password[0] = '\0';
  }
  if (actions == nullptr || capacity == 0) {
    return 0;
  }

  uint8_t actionCount = 0;
  if (selectedKind == SELECT_SCAN && scanNetworksCache != nullptr &&
      selectedIndex < scanCount) {
    copyText(ssid, ssidSize, scanNetworksCache[selectedIndex].ssid);
    actions[actionCount++] = POPUP_ACTION_CONNECT;
    return actionCount;
  }

  if (selectedKind != SELECT_SAVED || savedNetworks == nullptr ||
      selectedIndex >= savedCount) {
    return 0;
  }

  copyText(ssid, ssidSize, savedNetworks[selectedIndex].ssid);
  copyText(password, passwordSize, savedNetworks[selectedIndex].password);
  const bool preferred = preferredMatches(ssid);
  const bool connectedToThis = preferred && WiFi.status() == WL_CONNECTED;

  if (!connectedToThis && actionCount < capacity) {
    actions[actionCount++] = POPUP_ACTION_CONNECT;
  }
  if (!preferred && actionCount < capacity) {
    actions[actionCount++] = POPUP_ACTION_PREFERRED;
  }
  if (connectedToThis) {
    if (actionCount < capacity) {
      actions[actionCount++] = POPUP_ACTION_DISCONNECT;
    }
    if (actionCount < capacity) {
      actions[actionCount++] = POPUP_ACTION_DEVICES;
    }
  } else if (actionCount < capacity) {
    actions[actionCount++] = POPUP_ACTION_REMOVE;
  }
  return actionCount;
}

void WifiApp::performPopupAction(PopupAction action, const char *ssid,
                                 const char *password) {
  if (action == POPUP_ACTION_CONNECT && selectedKind == SELECT_SCAN) {
    if (scanNetworksCache != nullptr && selectedIndex < scanCount) {
      beginPasswordEntry(scanNetworksCache[selectedIndex].ssid,
                         scanNetworksCache[selectedIndex].bssid);
    }
    return;
  }

  if (action == POPUP_ACTION_CONNECT) {
    connectTo(ssid, password);
  } else if (action == POPUP_ACTION_PREFERRED) {
    setPreferredNetwork(selectedIndex);
  } else if (action == POPUP_ACTION_REMOVE) {
    popupConfirm = true;
    return;
  } else if (action == POPUP_ACTION_DEVICES) {
    scanDevices();
  } else if (action == POPUP_ACTION_DISCONNECT) {
    disconnect();
  }
  popupOpen = false;
}

void WifiApp::drawPopup(Adafruit_GFX &gfx) {
  drawDialogBackdropHatch(gfx);
  gfx.fillRect(POPUP_RECT.x, POPUP_RECT.y, POPUP_RECT.w, POPUP_RECT.h, 0);
  drawDialogRect(gfx, POPUP_RECT);
  gfx.setTextSize(1);

  if (popupConfirm) {
    const char *msg = "Remove?";
    int16_t tx, ty;
    uint16_t tw, th;
    gfx.getTextBounds(msg, 0, 0, &tx, &ty, &tw, &th);
    gfx.setCursor(POPUP_RECT.x + (POPUP_RECT.w - static_cast<int16_t>(tw)) / 2 - tx,
                  POPUP_RECT.y + 30);
    gfx.print(msg);

    UiRect yesBtn = popupButtonRect(0, 2);
    UiRect noBtn = popupButtonRect(1, 2);
    uiDrawButton(gfx, yesBtn, "Yes");
    uiDrawButton(gfx, noBtn, "No");
    return;
  }

  char rawSsid[WIFI_SSID_SIZE] = "";
  char label[WIFI_SSID_SIZE] = "";
  if (selectedKind == SELECT_SAVED && savedNetworks != nullptr &&
      selectedIndex < savedCount) {
    copyText(rawSsid, sizeof(rawSsid), savedNetworks[selectedIndex].ssid);
    if (preferredMatches(rawSsid) &&
        currentSystemConfig().wifi.displayName[0] != '\0') {
      copyText(label, sizeof(label), currentSystemConfig().wifi.displayName);
    } else {
      copyText(label, sizeof(label), rawSsid);
    }
  } else if (selectedKind == SELECT_SCAN && scanNetworksCache != nullptr &&
             selectedIndex < scanCount) {
    copyText(rawSsid, sizeof(rawSsid), scanNetworksCache[selectedIndex].ssid);
    copyText(label, sizeof(label), rawSsid);
  }
  char title[24];
  copyUtf8Prefix(title, sizeof(title), label, 21);
  if (label[0] != '\0' && strlen(title) < strlen(label) &&
      strlen(title) + 1 < sizeof(title)) {
    strcat(title, "~");
  }
  drawCenteredPopupTitle(gfx, title);

  PopupAction actions[4];
  char password[WIFI_PASS_SIZE];
  const uint8_t actionCount =
      selectedPopupActions(actions, 4, rawSsid, sizeof(rawSsid), password,
                           sizeof(password));
  for (uint8_t i = 0; i < actionCount; i++) {
    uiDrawButton(gfx, popupButtonRect(i, actionCount),
                 popupActionLabel(actions[i]));
  }
}

void WifiApp::drawPassword(Adafruit_GFX &gfx) {
  uiDrawButton(gfx, PASSWORD_CANCEL_BUTTON, "CANCEL", press.isPressed(6));
  gfx.setTextSize(1);
  gfx.setCursor(68, 10);
  gfx.print("Password");
  gfx.setCursor(4, 28);
  gfx.print(pendingSsid);
  passwordKeyboard.draw(gfx, passwordInput, WIFI_PASS_SIZE - 1);
}

void WifiApp::drawDevices(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);

  if (portScanState != PORT_IDLE) {
    drawPortScan(gfx);
    return;
  }

  if (devicePopupOpen) {
    drawDevicePopup(gfx);
    return;
  }

  drawDeviceTabs(gfx);
  if (deviceTab == TAB_INFO) {
    drawInfoTab(gfx);
  } else {
    drawNetScanTab(gfx);
  }
}

void WifiApp::drawDeviceTabs(Adafruit_GFX &gfx) {
  int16_t tab1X = (EPD_WIDTH - TAB_W * 2 - TAB_GAP) / 2;
  int16_t tab2X = tab1X + TAB_W + TAB_GAP;

  UiRect tab1 = {tab1X, TAB_Y, TAB_W, TAB_H};
  UiRect tab2 = {tab2X, TAB_Y, TAB_W, TAB_H};
  const SelectButtonOption tabs[] = {
      {tab1, "INFO", static_cast<int8_t>(TAB_INFO)},
      {tab2, "SCAN", static_cast<int8_t>(TAB_NETSCAN)},
  };

  SelectButtonGroup(tabs, 2).draw(gfx, static_cast<int8_t>(deviceTab),
                                  nullptr, 1);

  gfx.drawLine(0, CONTENT_Y - 1, EPD_WIDTH - 1, CONTENT_Y - 1, 1);
}

void WifiApp::drawInfoTab(Adafruit_GFX &gfx) {
  int16_t y = CONTENT_Y;
  const char *labels[] = {"device", "router", "mask", "dns", "internet", "ipv6"};
  const uint8_t total = sizeof(labels) / sizeof(labels[0]);

  for (uint8_t i = 0; i < total; i++) {
    char value[46] = "";
    if (i == 0) {
      ipToText(value, sizeof(value), WiFi.localIP());
    } else if (i == 1) {
      ipToText(value, sizeof(value), WiFi.gatewayIP());
    } else if (i == 2) {
      ipToText(value, sizeof(value), WiFi.subnetMask());
    } else if (i == 3) {
      ipToText(value, sizeof(value), WiFi.dnsIP());
    } else if (i == 4) {
      if (infoResolvedExtIp && infoExtIpVal[0] != '\0') {
        copyText(value, sizeof(value), infoExtIpVal);
      } else if (infoResolvedExtIp) {
        copyText(value, sizeof(value), "unavailable");
      } else {
        copyText(value, sizeof(value), "tap to resolve");
      }
    } else if (i == 5) {
      if (infoResolvedExtIpv6 && infoExtIpv6Val[0] != '\0') {
        copyText(value, sizeof(value), infoExtIpv6Val);
      } else if (infoResolvedExtIpv6) {
        copyText(value, sizeof(value), "unavailable");
      } else {
        copyText(value, sizeof(value), "tap to resolve");
      }
    }

    int16_t rowY = y + i * INFO_LINE_HEIGHT;
    int16_t textY = rowY + INFO_LINE_HEIGHT - 12;
    gfx.drawLine(4, rowY + INFO_LINE_HEIGHT - 2, 196,
                 rowY + INFO_LINE_HEIGHT - 2, 1);
    gfx.drawLine(INFO_SEP_X, rowY + 1, INFO_SEP_X,
                 rowY + INFO_LINE_HEIGHT - 3, 1);

    gfx.setCursor(6, textY);
    gfx.print(labels[i]);
    gfx.setCursor(INFO_SEP_X + 4, textY);
    gfx.print(value);
  }

  uiDrawButton(gfx, DEVICE_BACK_BUTTON, "BACK", press.isPressed(4));
}

void WifiApp::drawNetworkDeviceRow(Adafruit_GFX &gfx, uint16_t index,
                                   uint8_t row) const {
  if (networkDevices == nullptr || index >= networkScanDeviceCount) {
    return;
  }
  const NetworkDevice &dev = networkDevices[index];
  const int16_t rowY = NET_ROW_Y + row * NET_ROW_H;
  gfx.drawRect(4, rowY, 192, NET_ROW_H - 1, 1);

  char ipStr[16];
  rawIpToText(ipStr, sizeof(ipStr), dev.ip);
  gfx.setCursor(8, rowY + 3);
  gfx.print(ipStr);

  char pingStr[8];
  snprintf(pingStr, sizeof(pingStr), "%ums", dev.pingMs);
  int16_t px;
  int16_t py;
  uint16_t pw;
  uint16_t ph;
  gfx.getTextBounds(pingStr, 0, 0, &px, &py, &pw, &ph);
  gfx.setCursor(197 - static_cast<int16_t>(pw), rowY + 3);
  gfx.print(pingStr);

  char label[24];
  const char *marker = index == 0 ? "* " : (index == 1 ? "r " : "");
  if (dev.hasMac) {
    snprintf(label, sizeof(label), "%s%s", marker, dev.mac);
  } else if (index == 0) {
    copyText(label, sizeof(label), "* this device");
  } else if (index == 1) {
    copyText(label, sizeof(label), "r router");
  } else {
    copyText(label, sizeof(label), "no mac");
  }
  gfx.setCursor(8, rowY + 13);
  gfx.print(label);
}

void WifiApp::drawNetScanTab(Adafruit_GFX &gfx) {
  if (networkScanState == SCAN_DONE && networkScanDeviceCount == 0) {
    const char *msg = "No devices found";
    int16_t tx;
    int16_t ty;
    uint16_t tw;
    uint16_t th;
    gfx.getTextBounds(msg, 0, 0, &tx, &ty, &tw, &th);
    gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(tw)) / 2 - tx, 80);
    gfx.print(msg);
    uiDrawButton(gfx, DEVICE_SCAN_BUTTON, "SCAN", press.isPressed(5));
    uiDrawButton(gfx, DEVICE_BACK_BUTTON, "BACK", press.isPressed(4));
    return;
  }

  if (networkScanState == SCAN_RUNNING) {
    uiListClampOffset(netScanVisibleOffset, networkScanDeviceCount,
                      NET_VISIBLE_ROWS);
    for (uint8_t i = 0; i < NET_VISIBLE_ROWS; i++) {
      uint16_t idx = netScanVisibleOffset + i;
      if (idx >= networkScanDeviceCount) {
        break;
      }
      drawNetworkDeviceRow(gfx, idx, i);
    }
    drawLoadingIndicator(gfx, 0, 0);
    uiDrawButton(gfx, DEVICE_BACK_BUTTON, "BACK", press.isPressed(4));
    return;
  }

  uiListClampOffset(netScanVisibleOffset, networkScanDeviceCount,
                    NET_VISIBLE_ROWS);

  if (netScanVisibleOffset > 0) {
    gfx.setCursor(186, NET_ROW_Y - 2);
    gfx.print("^");
  }

  for (uint8_t i = 0; i < NET_VISIBLE_ROWS; i++) {
    uint16_t idx = netScanVisibleOffset + i;
    if (idx >= networkScanDeviceCount) {
      break;
    }
    drawNetworkDeviceRow(gfx, idx, i);
  }

  if (netScanVisibleOffset + NET_VISIBLE_ROWS < networkScanDeviceCount) {
    gfx.setCursor(186, NET_ROW_Y + NET_VISIBLE_ROWS * NET_ROW_H - 4);
    gfx.print("v");
  }

  gfx.setCursor(4, NET_ROW_Y + NET_VISIBLE_ROWS * NET_ROW_H + 4);
  gfx.print("Found: ");
  gfx.print(static_cast<int>(networkScanDeviceCount));

  uiDrawButton(gfx, DEVICE_SCAN_BUTTON, "SCAN", press.isPressed(5));
  uiDrawButton(gfx, DEVICE_BACK_BUTTON, "BACK", press.isPressed(4));
}

void WifiApp::drawDevicePopup(Adafruit_GFX &gfx) {
  drawDialogBackdropHatch(gfx);
  gfx.fillRect(DEVICE_POPUP_RECT.x, DEVICE_POPUP_RECT.y,
               DEVICE_POPUP_RECT.w, DEVICE_POPUP_RECT.h, 0);
  drawDialogRect(gfx, DEVICE_POPUP_RECT);
  gfx.setTextSize(1);
  gfx.setTextColor(1);

  if (networkDevices != nullptr && selectedDeviceIndex >= 0 &&
      static_cast<uint16_t>(selectedDeviceIndex) < networkScanDeviceCount) {
    const NetworkDevice &dev = networkDevices[selectedDeviceIndex];
    char ipStr[16];
    rawIpToText(ipStr, sizeof(ipStr), dev.ip);
    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    gfx.getTextBounds(ipStr, 0, 0, &textX, &textY, &textW, &textH);
    gfx.setCursor(
        DEVICE_POPUP_RECT.x + (DEVICE_POPUP_RECT.w - static_cast<int16_t>(textW)) / 2 - textX,
        DEVICE_POPUP_RECT.y + 24);
    gfx.print(ipStr);

    if (dev.hasMac) {
      gfx.getTextBounds(dev.mac, 0, 0, &textX, &textY, &textW, &textH);
      gfx.setCursor(
          DEVICE_POPUP_RECT.x + (DEVICE_POPUP_RECT.w - static_cast<int16_t>(textW)) / 2 - textX,
          DEVICE_POPUP_RECT.y + 44);
      gfx.print(dev.mac);
    }
  }

  UiRect quickBtn = devicePopupButtonRect(0, 2);
  UiRect fullBtn = devicePopupButtonRect(1, 2);
  uiDrawButton(gfx, quickBtn, "QUICK", press.isPressed(2));
  uiDrawButton(gfx, fullBtn, "FULL", press.isPressed(3));
}

void WifiApp::drawPortProgress(Adafruit_GFX &gfx, int16_t x, int16_t y,
                               int16_t w, int16_t h, uint16_t current,
                               uint16_t total) {
  gfx.drawRect(x, y, w, h, 1);
  if (total > 0) {
    uint16_t fillW = static_cast<uint16_t>(
        (static_cast<uint32_t>(current) * static_cast<uint32_t>(w - 2)) /
        static_cast<uint32_t>(total));
    if (fillW > 0 && static_cast<uint16_t>(w - 2) > 0) {
      gfx.fillRect(x + 1, y + 1, fillW, static_cast<uint16_t>(h - 2), 1);
    }
  }
}

void WifiApp::drawPortScan(Adafruit_GFX &gfx) {
  drawDialogBackdropHatch(gfx);
  gfx.fillRect(10, 34, 180, 130, 0);
  drawDialogRect(gfx, {10, 34, 180, 130});

  gfx.setTextSize(1);
  gfx.setTextColor(1);
  gfx.setCursor(18, 42);
  gfx.print("Port scan");
  gfx.setCursor(18, 56);
  if (portScanMode == PORT_MODE_QUICK) {
    gfx.print("Mode: quick");
  } else {
    gfx.print("Mode: full");
  }

  uint8_t pctVal = portScanTotal > 0
                       ? static_cast<uint8_t>(
                             (static_cast<uint32_t>(portScanCurrent) * 100U) /
                             static_cast<uint32_t>(portScanTotal))
                       : 0;
  char pct[10];
  snprintf(pct, sizeof(pct), "%u%%", static_cast<unsigned>(pctVal));
  int16_t textX, textY;
  uint16_t textW, textH;
  gfx.getTextBounds(pct, 0, 0, &textX, &textY, &textW, &textH);
  gfx.setCursor(182 - static_cast<int16_t>(textW), 42);
  gfx.print(pct);

  char targetStr[20];
  snprintf(targetStr, sizeof(targetStr), "%u.%u.%u.%u",
           portScanTargetIp[0], portScanTargetIp[1],
           portScanTargetIp[2], portScanTargetIp[3]);
  gfx.setCursor(18, 70);
  gfx.print("Target: ");
  gfx.print(targetStr);

  int16_t barY = 86;
  int16_t barH = 8;
  drawPortProgress(gfx, 18, barY, 164, barH,
                   portScanCurrent, portScanTotal);

  int16_t resultY = barY + barH + 6;
  gfx.drawLine(16, resultY, 183, resultY, 1);
  resultY += 2;

  gfx.setCursor(18, resultY);
  gfx.print("Open ports:");
  resultY += 12;

  char line[40];
  uint8_t lineCount = 0;
  uint16_t displayStart = 0;
  if (portResultCount > 12) {
    displayStart = portResultCount - 12;
  }
  for (uint16_t i = displayStart; i < portResultCount && lineCount < 12;
       i++, lineCount++) {
    int16_t ly = resultY + lineCount * 12;
    if (ly > 150) {
      break;
    }
    snprintf(line, sizeof(line), "%u", portResults[i].port);
    gfx.fillRect(18, ly, 164, 10, 1);
    gfx.setCursor(22, ly + 1);
    gfx.setTextColor(0);
    gfx.print(line);
    gfx.setTextColor(1);
  }

  uiDrawButton(gfx, DEVICE_BACK_BUTTON,
               portScanState == PORT_DONE ? "DONE" : "CANCEL",
               press.isPressed(4));
}

void WifiApp::drawStatusIcon(Adafruit_GFX &gfx, int16_t x, int16_t y) const {
  drawWifiIcon(gfx, x, y);
}

void WifiApp::drawWifiIcon(Adafruit_GFX &gfx, int16_t x, int16_t y) const {
  WifiDisplayState iconState = displayState();
  gfx.drawCircle(x + 14, y + 28, 2, 1);
  gfx.drawLine(x + 4, y + 18, x + 14, y + 8, 1);
  gfx.drawLine(x + 14, y + 8, x + 24, y + 18, 1);
  gfx.drawLine(x + 8, y + 22, x + 14, y + 16, 1);
  gfx.drawLine(x + 14, y + 16, x + 20, y + 22, 1);

  if (iconState == WIFI_DISPLAY_CONNECTED) {
    gfx.drawLine(x + 31, y + 24, x + 35, y + 28, 1);
    gfx.drawLine(x + 35, y + 28, x + 43, y + 16, 1);
    return;
  }

  if (iconState == WIFI_DISPLAY_ON) {
    gfx.fillRect(x + 32, y + 24, 4, 4, 1);
    return;
  }

  gfx.drawLine(x + 31, y + 16, x + 43, y + 28, 1);
  gfx.drawLine(x + 43, y + 16, x + 31, y + 28, 1);
}

void WifiApp::populateDeviceMac(NetworkDevice &device) const {
  device.mac[0] = '\0';
  device.hasMac = false;
  if (device.ip == ipToRaw(WiFi.localIP())) {
    copyString(device.mac, sizeof(device.mac), WiFi.macAddress());
    device.hasMac = device.mac[0] != '\0';
    return;
  }
  device.hasMac = arpMacToText(device.ip, device.mac, sizeof(device.mac));
}

// ---- Network scanning ----

void WifiApp::startNetworkScan() {
  networkScanState = SCAN_RUNNING;
  networkScanStartedAt = millis();
  freeNetDevices();
  // A previous scan can still be completing callbacks after the screen closes.
  // Reusing its slots would race those callbacks, so wait for a later scan.
  if (pingSlotsBusy()) {
    networkScanState = SCAN_DONE;
    return;
  }
  if (!ensurePingSlots()) {
    networkScanState = SCAN_DONE;
    return;
  }

  IPAddress localIP = WiFi.localIP();
  IPAddress subnetMask = WiFi.subnetMask();

  uint32_t ipR = ipToRaw(localIP);
  uint32_t maskR = ipToRaw(subnetMask);
  networkScanStart = (ipR & maskR) + 1;
  networkScanEnd = (ipR & maskR) | (~maskR);
  networkScanCurrent = networkScanStart;
  networkScanDeviceCount = 0;

  networkDevices = static_cast<NetworkDevice *>(
      psram::allocate(sizeof(NetworkDevice) * MAX_NET_DEVICES));
  if (networkDevices == nullptr) {
    networkScanState = SCAN_DONE;
    return;
  }
  memset(networkDevices, 0, sizeof(NetworkDevice) * MAX_NET_DEVICES);

  NetworkDevice selfDev;
  selfDev.ip = ipToRaw(WiFi.localIP());
  selfDev.reachable = true;
  selfDev.pingMs = 0;
  populateDeviceMac(selfDev);
  networkDevices[networkScanDeviceCount++] = selfDev;

  NetworkDevice gateway;
  gateway.ip = ipToRaw(WiFi.gatewayIP());
  gateway.reachable = true;
  gateway.pingMs = 0;
  populateDeviceMac(gateway);
  networkDevices[networkScanDeviceCount++] = gateway;

  if (networkScanEnd > networkScanStart + MAX_NET_DEVICES) {
    networkScanEnd = networkScanStart + MAX_NET_DEVICES - 1;
  }

  clearPingSlots();
}

void WifiApp::advanceNetworkScan() {
  if (networkDevices == nullptr || pingSlots == nullptr) {
    networkScanState = SCAN_DONE;
    return;
  }

  for (uint8_t i = 0; i < NET_SCAN_TRIES_PER_FRAME; i++) {
    if (pingSlots[i].ip == 0 ||
        !pingSlots[i].done.load(std::memory_order_acquire)) {
      continue;
    }

    if (pingSlots[i].reachable && networkScanDeviceCount < MAX_NET_DEVICES) {
      NetworkDevice &dev = networkDevices[networkScanDeviceCount++];
      dev.ip = pingSlots[i].ip;
      dev.reachable = true;
      dev.pingMs = pingSlots[i].pingMs;
      populateDeviceMac(dev);
    }
    pingSlots[i].ip = 0;
  }

  for (uint8_t i = 0; i < NET_SCAN_TRIES_PER_FRAME; i++) {
    if (pingSlots[i].ip != 0) continue;

    if (networkScanCurrent >= networkScanEnd) {
      // Check if all pings have completed before declaring done
      bool allDone = true;
      for (uint8_t j = 0; j < NET_SCAN_TRIES_PER_FRAME; j++) {
        if (pingSlots[j].ip != 0 &&
            !pingSlots[j].done.load(std::memory_order_acquire)) {
          allDone = false;
          break;
        }
      }
      if (allDone) networkScanState = SCAN_DONE;
      return;
    }

    uint32_t target = networkScanCurrent++;

    if (target == ipToRaw(WiFi.localIP()) ||
        target == ipToRaw(WiFi.gatewayIP())) {
      continue;
    }

    ip_addr_t targetAddr;
    IP_ADDR4(&targetAddr, (target >> 24) & 0xFF, (target >> 16) & 0xFF,
             (target >> 8) & 0xFF, target & 0xFF);

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = targetAddr;
    config.count = 1;
    config.interval_ms = 100;
    config.timeout_ms = 1000;
    config.data_size = 32;

    esp_ping_callbacks_t callbacks = {};
    callbacks.on_ping_success = on_ping_success;
    callbacks.on_ping_timeout = on_ping_timeout;
    callbacks.on_ping_end = on_ping_end;
    callbacks.cb_args = &pingSlots[i];

    esp_ping_handle_t ping;
    esp_err_t err = esp_ping_new_session(&config, &callbacks, &ping);
    if (err == ESP_OK) {
      pingSlots[i].ip = target;
      pingSlots[i].pingMs = 0;
      pingSlots[i].reachable = false;
      pingSlots[i].done.store(false, std::memory_order_relaxed);
      esp_ping_start(ping);
    }
  }
}

// ---- Port scanning ----

void WifiApp::startPortScan(uint8_t ip0, uint8_t ip1, uint8_t ip2,
                            uint8_t ip3, PortScanMode mode) {
  portScanTargetIp[0] = ip0;
  portScanTargetIp[1] = ip1;
  portScanTargetIp[2] = ip2;
  portScanTargetIp[3] = ip3;
  portScanMode = mode;
  portScanState = PORT_RUNNING;
  portScanStartedAt = millis();
  portScanCurrent = 0;
  portResultCount = 0;
  lastPortProgressUpdateMs = 0;
  freePortResults();

  if (mode == PORT_MODE_QUICK) {
    portScanTotal = QUICK_PORT_COUNT;
    portResults = static_cast<PortResult *>(
        psram::allocate(sizeof(PortResult) * QUICK_PORT_COUNT));
    if (portResults != nullptr) {
      memset(portResults, 0, sizeof(PortResult) * QUICK_PORT_COUNT);
    }
  } else {
    portScanTotal = 65535;
    portResults = static_cast<PortResult *>(
        psram::allocate(sizeof(PortResult) * MAX_PORT_RESULTS));
    if (portResults != nullptr) {
      memset(portResults, 0, sizeof(PortResult) * MAX_PORT_RESULTS);
    }
  }
}

void WifiApp::advancePortScan() {
  if (portResults == nullptr) {
    portScanState = PORT_DONE;
    return;
  }

  if (portScanCurrent >= portScanTotal) {
    portScanState = PORT_DONE;
    return;
  }

  uint16_t port;
  if (portScanMode == PORT_MODE_QUICK) {
    port = QUICK_PORTS[portScanCurrent];
  } else {
    port = portScanCurrent + 1;
  }

  IPAddress ip(portScanTargetIp[0], portScanTargetIp[1],
               portScanTargetIp[2], portScanTargetIp[3]);
  WiFiClient client;
  bool open = client.connect(ip, port, PORT_SCAN_TIMEOUT_MS);
  client.stop();

  if (open && portResultCount < MAX_PORT_RESULTS) {
    portResults[portResultCount].port = port;
    portResults[portResultCount].open = true;
    portResultCount++;
  }

  portScanCurrent++;

  if (portScanCurrent >= portScanTotal) {
    portScanState = PORT_DONE;
  }
}

// ---- Touch handlers ----

void WifiApp::handleNetworkTouch(const TouchPoint &point) {
  if (displayState() != WIFI_DISPLAY_OFF && uiContains(SCAN_BUTTON, point)) {
    press.press(0);
    scanNetworks();
    return;
  }
  if (uiContains(WIFI_TOGGLE_BUTTON, point)) {
    press.press(1);
    if (displayState() == WIFI_DISPLAY_OFF) {
      connect();
    } else {
      disconnect();
    }
    return;
  }
  const uint8_t savedCount = savedNetworkCount();
  const uint16_t total = savedCount + visibleScanNetworkCount();
  if (point.x >= NETWORK_LIST_SPLIT_X && point.y >= 16 && point.y < 170) {
    const int8_t delta =
        point.y < NETWORK_ROW_Y + NETWORK_LIST_H / 2 ? -1 : 1;
    uiListScrollBy(listOffset, total, NETWORK_ROW_COUNT, delta);
    return;
  }
  const UiListTouchResult listTouch = uiListHandleTouch(
      point, total, NETWORK_ROW_COUNT, NETWORK_ROW_Y, NETWORK_ROW_H,
      NETWORK_LIST_H, NETWORK_LIST_SPLIT_X, UiListNavMode::Split, listOffset);
  if (listTouch.action == UiListTouchAction::Scroll) {
    return;
  }
  if (listTouch.action != UiListTouchAction::Select) {
    return;
  }
  const uint16_t index = listTouch.index;
  if (index < savedCount) {
    selectedKind = SELECT_SAVED;
    selectedIndex = static_cast<uint8_t>(index);
  } else {
    selectedKind = SELECT_SCAN;
    uint8_t scanIndex = 0;
    if (!visibleScanNetworkAt(index - savedCount, scanIndex)) {
      return;
    }
    selectedIndex = scanIndex;
  }
  popupOpen = true;
}

void WifiApp::handlePopupTouch(const TouchPoint &point) {
  if (!uiContains(POPUP_RECT, point)) {
    popupOpen = false;
    popupConfirm = false;
    return;
  }

  if (popupConfirm) {
    if (uiContains(popupButtonRect(0, 2), point)) {
      removeSavedNetwork(selectedIndex);
      popupOpen = false;
      popupConfirm = false;
      return;
    }
    if (uiContains(popupButtonRect(1, 2), point)) {
      popupOpen = false;
      popupConfirm = false;
    }
    return;
  }

  PopupAction actions[4];
  char ssid[WIFI_SSID_SIZE];
  char password[WIFI_PASS_SIZE];
  const uint8_t actionCount =
      selectedPopupActions(actions, 4, ssid, sizeof(ssid), password,
                           sizeof(password));
  for (uint8_t i = 0; i < actionCount; i++) {
    if (uiContains(popupButtonRect(i, actionCount), point)) {
      performPopupAction(actions[i], ssid, password);
      return;
    }
  }
}

void WifiApp::handlePasswordTouch(const TouchPoint &point) {
  if (uiContains(PASSWORD_CANCEL_BUTTON, point)) {
    press.press(6);
    view = VIEW_NETWORKS;
    return;
  }
  KeyboardEvent event =
      passwordKeyboard.hitTest(point, passwordInput, sizeof(passwordInput));
  if (event.action != KEY_OK) {
    return;
  }
  pendingSaveAfterConnect = true;
  connectTo(pendingSsid, passwordInput);
  view = VIEW_NETWORKS;
}

void WifiApp::handleDevicesTouch(const TouchPoint &point) {
  if (portScanState != PORT_IDLE) {
    handlePortScanTouch(point);
    return;
  }

  if (devicePopupOpen) {
    handleDevicePopupTouch(point);
    return;
  }

  if (point.y >= TAB_Y && point.y < TAB_Y + TAB_H) {
    handleDeviceTabsTouch(point);
    return;
  }

  if (deviceTab == TAB_INFO) {
    handleInfoTabTouch(point);
  } else {
    handleNetScanTabTouch(point);
  }
}

void WifiApp::handleDeviceTabsTouch(const TouchPoint &point) {
  int16_t tab1X = (EPD_WIDTH - TAB_W * 2 - TAB_GAP) / 2;
  int16_t tab2X = tab1X + TAB_W + TAB_GAP;
  UiRect tab1 = {tab1X, TAB_Y, TAB_W, TAB_H};
  UiRect tab2 = {tab2X, TAB_Y, TAB_W, TAB_H};
  const SelectButtonOption tabs[] = {
      {tab1, "INFO", static_cast<int8_t>(TAB_INFO)},
      {tab2, "SCAN", static_cast<int8_t>(TAB_NETSCAN)},
  };

  int8_t nextTab = -1;
  if (SelectButtonGroup(tabs, 2).hitTest(point, nextTab, 0)) {
    deviceTab = static_cast<DeviceTab>(nextTab);
  }
}

bool WifiApp::fetchPlainTextUrl(const char *url, char *out,
                                size_t outSize) const {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';

  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(url)) {
    return false;
  }
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  const char *text = body.c_str();
  while (*text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    text++;
  }

  size_t len = strlen(text);
  while (len > 0 && isspace(static_cast<unsigned char>(text[len - 1]))) {
    len--;
  }
  if (len == 0 || len >= outSize) {
    return false;
  }

  memcpy(out, text, len);
  out[len] = '\0';
  return true;
}

void WifiApp::handleInfoTabTouch(const TouchPoint &point) {
  if (uiContains(DEVICE_BACK_BUTTON, point)) {
    press.press(4);
    view = VIEW_NETWORKS;
    networkScanState = SCAN_IDLE;
    freeNetDevices();
    freePortResults();
    return;
  }

  int16_t internetRowY = CONTENT_Y + 4 * INFO_LINE_HEIGHT;
  if (point.y >= internetRowY && point.y < internetRowY + INFO_LINE_HEIGHT &&
      !infoResolvedExtIp && WiFi.status() == WL_CONNECTED) {
    infoResolvedExtIp = true;
    if (!fetchPlainTextUrl(PUBLIC_IPV4_URL, infoExtIpVal,
                           sizeof(infoExtIpVal)) ||
        strchr(infoExtIpVal, '.') == nullptr) {
      infoExtIpVal[0] = '\0';
    }
  }

  int16_t ipv6RowY = CONTENT_Y + 5 * INFO_LINE_HEIGHT;
  if (point.y >= ipv6RowY && point.y < ipv6RowY + INFO_LINE_HEIGHT &&
      !infoResolvedExtIpv6 && WiFi.status() == WL_CONNECTED) {
    infoResolvedExtIpv6 = true;
    if (!fetchPlainTextUrl(PUBLIC_IPV6_URL, infoExtIpv6Val,
                           sizeof(infoExtIpv6Val)) ||
        strchr(infoExtIpv6Val, ':') == nullptr) {
      infoExtIpv6Val[0] = '\0';
    }
  }
}

void WifiApp::handleNetScanTabTouch(const TouchPoint &point) {
  if (uiContains(DEVICE_BACK_BUTTON, point)) {
    press.press(4);
    view = VIEW_NETWORKS;
    networkScanState = SCAN_IDLE;
    freeNetDevices();
    freePortResults();
    return;
  }

  if (uiContains(DEVICE_SCAN_BUTTON, point)) {
    press.press(5);
    if (networkScanState == SCAN_IDLE || networkScanState == SCAN_DONE) {
      networkScanState = SCAN_PENDING;
      networkScanDeviceCount = 0;
      freeNetDevices();
    }
    return;
  }

  if (networkScanState != SCAN_DONE || networkScanDeviceCount == 0) {
    return;
  }

  const UiListTouchResult listTouch = uiListHandleTouch(
      point, networkScanDeviceCount, NET_VISIBLE_ROWS, NET_ROW_Y,
      NET_ROW_H, NET_LIST_H_VAL, NET_SPLIT_X, UiListNavMode::Split,
      netScanVisibleOffset);
  if (listTouch.action == UiListTouchAction::Scroll) {
    return;
  }
  if (listTouch.action != UiListTouchAction::Select) {
    return;
  }

  selectedDeviceIndex = static_cast<int8_t>(listTouch.index);
  devicePopupOpen = true;
}

void WifiApp::handleDevicePopupTouch(const TouchPoint &point) {
  if (!uiContains(DEVICE_POPUP_RECT, point)) {
    devicePopupOpen = false;
    return;
  }

  UiRect quickBtn = devicePopupButtonRect(0, 2);
  UiRect fullBtn = devicePopupButtonRect(1, 2);

  if (uiContains(quickBtn, point)) {
    press.press(2);
    devicePopupOpen = false;
    if (selectedDeviceIndex >= 0 &&
        static_cast<uint16_t>(selectedDeviceIndex) < networkScanDeviceCount) {
      const NetworkDevice &dev = networkDevices[selectedDeviceIndex];
      startPortScan((dev.ip >> 24) & 0xFF, (dev.ip >> 16) & 0xFF,
                    (dev.ip >> 8) & 0xFF, dev.ip & 0xFF,
                    PORT_MODE_QUICK);
    }
    return;
  }

  if (uiContains(fullBtn, point)) {
    press.press(3);
    devicePopupOpen = false;
    if (selectedDeviceIndex >= 0 &&
        static_cast<uint16_t>(selectedDeviceIndex) < networkScanDeviceCount) {
      const NetworkDevice &dev = networkDevices[selectedDeviceIndex];
      startPortScan((dev.ip >> 24) & 0xFF, (dev.ip >> 16) & 0xFF,
                    (dev.ip >> 8) & 0xFF, dev.ip & 0xFF,
                    PORT_MODE_FULL);
    }
    return;
  }
}

void WifiApp::handlePortScanTouch(const TouchPoint &point) {
  if (uiContains(DEVICE_BACK_BUTTON, point)) {
    press.press(4);
    portScanState = PORT_IDLE;
    freePortResults();
    return;
  }
}

#endif
