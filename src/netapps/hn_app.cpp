#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "netapps/hn_app.h"
#include "netapps/lightweight_json_parser.h"
#include "sys/builtin_apps.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>

static const char *HN_TOP_URL =
    "https://hacker-news.firebaseio.com/v0/topstories.json";

class HnTopIdsListener : public JsonStreamListener {
public:
  HnTopIdsListener(int32_t *targetIds, int targetMax, int &targetCount)
      : ids(targetIds), maxIds(targetMax), count(targetCount) {}

  void onNumberValue(int, const char *, int32_t value) override {
    if (count < maxIds) {
      ids[count++] = value;
    }
  }

private:
  int32_t *ids;
  int maxIds;
  int &count;
};

class HnItemListener : public JsonStreamListener {
public:
  explicit HnItemListener(FeedPost &targetPost) : post(targetPost) {
    strncpy(post.source, "HN", sizeof(post.source) - 1);
  }

  void onStringValue(int, const char *key, const char *value) override {
    if (strcmp(key, "title") == 0) {
      copy(post.title, sizeof(post.title), value);
    } else if (strcmp(key, "text") == 0) {
      copy(post.text, sizeof(post.text), value);
    } else if (strcmp(key, "by") == 0 && post.text[0] == '\0') {
      snprintf(post.text, sizeof(post.text), "by %s", value);
    }
  }

  void onNumberValue(int, const char *key, int32_t value) override {
    if (strcmp(key, "score") == 0) {
      post.score = value;
    }
  }

private:
  FeedPost &post;

  static void copy(char *out, int outSize, const char *value) {
    strncpy(out, value ? value : "", outSize - 1);
    out[outSize - 1] = '\0';
  }
};

void HnApp::reset() {
  state = STATE_IDLE;
  requested = false;
  setStatus("");
  feed.reset();
}

bool HnApp::hasActiveSession() const {
  return state == STATE_LOADING || state == STATE_READY;
}

void HnApp::draw(Adafruit_GFX &gfx) {
  if (state == STATE_IDLE || state == STATE_LOADING) {
    gfx.setTextColor(1);
    gfx.setTextSize(1);
    gfx.setCursor(4, 6);
    gfx.print("HACKER NEWS");
    drawCentered(gfx, "Loading...", 92);
    return;
  }

  if (state == STATE_FAILED) {
    gfx.setTextColor(1);
    gfx.setTextSize(1);
    drawCentered(gfx, status[0] ? status : "HN failed", 88);
    drawCentered(gfx, "Touch: WiFi off", 112);
    return;
  }

  feed.draw(gfx, "HACKER NEWS", "pt", "top");
}

bool HnApp::update() {
  if (requested) {
    return false;
  }
  requested = true;
  state = STATE_LOADING;
  bool ok = fetch();
  state = ok ? STATE_READY : STATE_FAILED;
  return true;
}

bool HnApp::handleTouch(const TouchPoint &point) {
  if (state == STATE_FAILED) {
    wifiTurnOff();
    setStatus("WiFi off");
    return true;
  }
  if (state != STATE_READY) {
    return false;
  }
  return feed.handleTouch(point);
}

bool HnApp::handleMenuButton() {
  if (state == STATE_READY && feed.closeExpanded()) {
    return true;
  }
  return false;
}

bool HnApp::handlePowerButton() {
  if (state != STATE_READY || feed.isExpanded()) {
    return false;
  }
  feed.toggleSelectMode();
  return true;
}

bool HnApp::fetch() {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected");
    return false;
  }

  int32_t ids[STORY_LIMIT] = {};
  int idCount = 0;
  if (!fetchTopIds(ids, STORY_LIMIT, idCount)) {
    return false;
  }

  feed.reset();
  for (int i = 0; i < idCount; i++) {
    fetchItem(ids[i]);
  }

  if (feed.count() == 0) {
    setStatus("No stories parsed");
    return false;
  }
  return true;
}

bool HnApp::fetchTopIds(int32_t *ids, int maxIds, int &count) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(FETCH_TIMEOUT_MS);
  if (!http.begin(client, HN_TOP_URL)) {
    setStatus("HTTP setup fail");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setStatus("Top IDs error");
    http.end();
    return false;
  }

  int size = http.getSize();

  LightweightJsonParser parser;
  HnTopIdsListener listener(ids, maxIds, count);
  bool ok = parser.parse(http.getStreamPtr(), size, FETCH_TIMEOUT_MS,
                         TOP_IDS_MAX_BYTES, listener, status, sizeof(status));
  http.end();
  return count > 0 || ok;
}

bool HnApp::fetchItem(int32_t id) {
  char url[96];
  snprintf(url, sizeof(url),
           "https://hacker-news.firebaseio.com/v0/item/%ld.json",
           static_cast<long>(id));

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(FETCH_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  int size = http.getSize();

  FeedPost post = {};
  HnItemListener listener(post);
  LightweightJsonParser parser;
  parser.parse(http.getStreamPtr(), size, FETCH_TIMEOUT_MS, ITEM_MAX_BYTES,
               listener, status, sizeof(status));
  http.end();
  if (post.title[0] == '\0') {
    return false;
  }
  feed.add(post);
  return true;
}

void HnApp::drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y) const {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor((200 - static_cast<int>(w)) / 2 - x1, y);
  gfx.print(text);
}

void HnApp::setStatus(const char *text) {
  if (text == nullptr) {
    status[0] = '\0';
    return;
  }
  strncpy(status, text, sizeof(status) - 1);
  status[sizeof(status) - 1] = '\0';
}

#endif
