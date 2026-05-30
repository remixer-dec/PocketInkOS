#ifndef POST_FEED_H
#define POST_FEED_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

struct FeedPost {
  char source[18];
  char title[192];
  char text[768];
  int32_t score;
};

class PostFeed {
public:
  static const int MAX_POSTS = 12;

  PostFeed();

  void reset();
  bool add(const FeedPost &post);
  bool handleTouch(const TouchPoint &point);
  void draw(Adafruit_GFX &gfx, const char *header,
            const char *scoreLabel = "score", const char *rightHeader = "");
  uint8_t count() const;
  bool closeExpanded();
  void toggleSelectMode();
  bool isExpanded() const;

private:
  enum ListMode { MODE_SCROLL, MODE_SELECT };

  FeedPost *posts = nullptr;
  uint8_t postCount = 0;
  uint8_t scrollOffset = 0;
  int8_t expandedIndex = -1;
  uint8_t expandedLineOffset = 0;
  ListMode listMode = MODE_SCROLL;

  void drawList(Adafruit_GFX &gfx, const char *header,
                const char *scoreLabel, const char *rightHeader) const;
  void drawExpanded(Adafruit_GFX &gfx, const char *header,
                    const char *scoreLabel, const char *rightHeader) const;
  static void drawRightText(Adafruit_GFX &gfx, const char *text, int16_t y);
  static int drawWrappedText(Adafruit_GFX &gfx, const char *text, int16_t x,
                             int16_t y, int maxChars, int maxLines,
                             int skipLines = 0, bool draw = true);
  static const char *skipWordSpaces(const char *text);
};

#endif
