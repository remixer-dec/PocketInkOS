#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "reddit_app.h"
#include "lightweight_json_parser.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>

const char *RedditApp::SUBREDDITS[SUBREDDIT_COUNT] = {
    "localLLaMA",
    "MachineLearning",
    "OpenAI",
    "claudeAI"
};

const char *RedditApp::SORT_LABELS[SORT_COUNT] = {"hot", "new", "top1w",
                                                  "top1m"};

class RedditListingListener : public JsonStreamListener {
public:
  RedditListingListener(PostFeed &targetFeed, const char *fallbackSubreddit)
      : feed(targetFeed), fallback(fallbackSubreddit) {}

  void onArrayStart(int depth, const char *key) override {
    if (strcmp(key, "children") == 0) {
      inChildren = true;
      childrenDepth = depth;
    }
  }

  void onArrayEnd(int depth) override {
    if (inChildren && depth == childrenDepth) {
      inChildren = false;
      childrenDepth = -1;
    }
  }

  void onObjectStart(int depth, const char *key) override {
    if (inChildren && strcmp(key, "data") == 0) {
      memset(&current, 0, sizeof(current));
      inPostData = true;
      postDepth = depth;
    }
  }

  void onObjectEnd(int depth) override {
    if (!inPostData || depth != postDepth) {
      return;
    }
    if (current.source[0] == '\0') {
      snprintf(current.source, sizeof(current.source), "r/%s", fallback);
    }
    feed.add(current);
    inPostData = false;
    postDepth = -1;
  }

  void onStringValue(int, const char *key, const char *value) override {
    if (!inPostData) {
      return;
    }
    if (strcmp(key, "subreddit") == 0) {
      snprintf(current.source, sizeof(current.source), "r/%s", value);
    } else if (strcmp(key, "title") == 0) {
      copy(current.title, sizeof(current.title), value);
    } else if (strcmp(key, "selftext") == 0) {
      copy(current.text, sizeof(current.text), value);
    }
  }

  void onNumberValue(int, const char *key, int32_t value) override {
    if (inPostData && strcmp(key, "ups") == 0) {
      current.score = value;
    }
  }

private:
  PostFeed &feed;
  const char *fallback;
  bool inChildren = false;
  bool inPostData = false;
  int childrenDepth = -1;
  int postDepth = -1;
  FeedPost current = {};

  static void copy(char *out, int outSize, const char *value) {
    strncpy(out, value ? value : "", outSize - 1);
    out[outSize - 1] = '\0';
  }
};

void RedditApp::reset() {
  state = STATE_PICKER;
  requested = false;
  setStatus("");
  feed.reset();
}

bool RedditApp::hasActiveSession() const {
  return state == STATE_LOADING || state == STATE_READY;
}

void RedditApp::draw(Adafruit_GFX &gfx) {
  if (state == STATE_PICKER) {
    drawPicker(gfx);
    return;
  }

  if (state == STATE_LOADING) {
    gfx.setTextColor(1);
    gfx.setTextSize(1);
    gfx.setCursor(4, 6);
    gfx.print("REDDIT");
    drawCentered(gfx, "Loading posts...", 82);
    char subline[32];
    snprintf(subline, sizeof(subline), "r/%s / %s", SUBREDDITS[subredditIndex],
             SORT_LABELS[sortIndex]);
    drawCentered(gfx, subline, 102);
    return;
  }

  if (state == STATE_FAILED) {
    gfx.setTextColor(1);
    gfx.setTextSize(1);
    gfx.setCursor(18, 82);
    gfx.print(status[0] ? status : "Reddit failed");
    gfx.setCursor(32, 108);
    gfx.print("Touch to retry");
    return;
  }

  char rightHeader[24];
  snprintf(rightHeader, sizeof(rightHeader), "r/%s", SUBREDDITS[subredditIndex]);
  feed.draw(gfx, "REDDIT", "up", rightHeader);
}

bool RedditApp::update() {
  if (state == STATE_PICKER) {
    return false;
  }
  if (requested) {
    return false;
  }
  requested = true;
  state = STATE_LOADING;
  bool ok = fetch();
  state = ok ? STATE_READY : STATE_FAILED;
  return true;
}

bool RedditApp::handleTouch(const TouchPoint &point) {
  if (state == STATE_PICKER) {
    if (point.y >= 46 && point.y < 104) {
      subredditIndex = (subredditIndex + 1) % SUBREDDIT_COUNT;
      return true;
    }
    if (point.y >= 110 && point.y < 150) {
      sortIndex = (sortIndex + 1) % SORT_COUNT;
      return true;
    }
    if (point.y >= 160) {
      requested = false;
      state = STATE_LOADING;
      return true;
    }
    return false;
  }

  if (state == STATE_FAILED) {
    requested = false;
    state = STATE_PICKER;
    return true;
  }
  if (state != STATE_READY) {
    return false;
  }
  return feed.handleTouch(point);
}

bool RedditApp::handleMenuButton() {
  if (state == STATE_READY && feed.closeExpanded()) {
    return true;
  }
  if (state == STATE_READY || state == STATE_FAILED) {
    requested = false;
    state = STATE_PICKER;
    feed.reset();
    return true;
  }
  return false;
}

bool RedditApp::handlePowerButton() {
  if (state != STATE_READY || feed.isExpanded()) {
    return false;
  }
  feed.toggleSelectMode();
  return true;
}

bool RedditApp::fetch() {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected");
    return false;
  }

  char url[96];
  buildUrl(url, sizeof(url));

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(FETCH_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    setStatus("HTTP setup fail");
    return false;
  }
  http.addHeader("User-Agent", "PocketInk/0.1");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setStatus("HTTP error");
    http.end();
    return false;
  }

  int size = http.getSize();

  feed.reset();
  LightweightJsonParser parser;
  RedditListingListener listener(feed, SUBREDDITS[subredditIndex]);
  bool ok = parser.parse(http.getStreamPtr(), size, FETCH_TIMEOUT_MS,
                         MAX_JSON_BYTES, listener, status, sizeof(status));
  http.end();
  if (!ok && feed.count() == 0) {
    return false;
  }
  if (feed.count() == 0) {
    setStatus("No posts parsed");
    return false;
  }
  return true;
}

void RedditApp::buildUrl(char *out, int outSize) const {
  switch (sortIndex) {
  case SORT_NEW:
    snprintf(out, outSize, "https://www.reddit.com/r/%s/new.json?limit=%d",
             SUBREDDITS[subredditIndex], PostFeed::MAX_POSTS);
    return;
  case SORT_TOP_1W:
    snprintf(out, outSize,
             "https://www.reddit.com/r/%s/top.json?t=week&limit=%d",
             SUBREDDITS[subredditIndex], PostFeed::MAX_POSTS);
    return;
  case SORT_TOP_1M:
    snprintf(out, outSize,
             "https://www.reddit.com/r/%s/top.json?t=month&limit=%d",
             SUBREDDITS[subredditIndex], PostFeed::MAX_POSTS);
    return;
  case SORT_HOT:
  default:
    snprintf(out, outSize, "https://www.reddit.com/r/%s/hot.json?limit=%d",
             SUBREDDITS[subredditIndex], PostFeed::MAX_POSTS);
    return;
  }
}

void RedditApp::drawPicker(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  gfx.setCursor(56, 16);
  gfx.print("REDDIT");
  gfx.setTextSize(1);
  gfx.drawRect(20, 48, 160, 44, 1);
  gfx.setCursor(34, 58);
  gfx.print("Subreddit");
  gfx.setCursor(34, 74);
  gfx.print("r/");
  gfx.print(SUBREDDITS[subredditIndex]);

  gfx.drawRect(20, 108, 160, 34, 1);
  gfx.setCursor(34, 118);
  gfx.print("Sort: ");
  gfx.print(SORT_LABELS[sortIndex]);

  gfx.drawRect(48, 160, 104, 26, 1);
  gfx.setCursor(84, 169);
  gfx.print("LOAD");
}

void RedditApp::drawCentered(Adafruit_GFX &gfx, const char *text,
                             int16_t y) const {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor((200 - static_cast<int>(w)) / 2 - x1, y);
  gfx.print(text);
}

void RedditApp::setStatus(const char *text) {
  if (text == nullptr) {
    status[0] = '\0';
    return;
  }
  strncpy(status, text, sizeof(status) - 1);
  status[sizeof(status) - 1] = '\0';
}

#endif
