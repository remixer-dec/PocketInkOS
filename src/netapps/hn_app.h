#ifndef HN_APP_H
#define HN_APP_H

#include "netapps/post_feed.h"
#include <Adafruit_GFX.h>

class HnApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handlePowerButton();
  bool hasActiveSession() const;

private:
  enum State { STATE_IDLE, STATE_LOADING, STATE_READY, STATE_FAILED };

  static const unsigned long FETCH_TIMEOUT_MS = 10000;
  static const int STORY_LIMIT = 6;
  static const int TOP_IDS_MAX_BYTES = 4096;
  static const int ITEM_MAX_BYTES = 4096;

  State state = STATE_IDLE;
  bool requested = false;
  char status[32] = "";
  PostFeed feed;

  bool fetch();
  bool fetchTopIds(int32_t *ids, int maxIds, int &count);
  bool fetchItem(int32_t id);
  void drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y) const;
  void setStatus(const char *text);
};

#endif
