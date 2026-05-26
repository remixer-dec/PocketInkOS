#ifndef REDDIT_APP_H
#define REDDIT_APP_H

#include "post_feed.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

class RedditApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handlePowerButton();
  bool hasActiveSession() const;

private:
  enum State { STATE_PICKER, STATE_LOADING, STATE_READY, STATE_FAILED };
  enum SortMode { SORT_HOT, SORT_NEW, SORT_TOP_1W, SORT_TOP_1M };

  static const int SUBREDDIT_COUNT = 4;
  static const int SORT_COUNT = 4;
  static const unsigned long FETCH_TIMEOUT_MS = 10000;
  static const int MAX_JSON_BYTES = 30000;

  static const char *SUBREDDITS[SUBREDDIT_COUNT];
  static const char *SORT_LABELS[SORT_COUNT];

  State state = STATE_PICKER;
  uint8_t subredditIndex = 0;
  uint8_t sortIndex = SORT_HOT;
  bool requested = false;
  char status[32] = "";
  PostFeed feed;

  bool fetch();
  void buildUrl(char *out, int outSize) const;
  void drawPicker(Adafruit_GFX &gfx);
  void drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y) const;
  void setStatus(const char *text);
};

#endif
