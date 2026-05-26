#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "post_feed.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstring>

PostFeed::PostFeed() {
  posts = static_cast<FeedPost *>(
      heap_caps_malloc(sizeof(FeedPost) * MAX_POSTS,
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (posts == nullptr) {
    posts = static_cast<FeedPost *>(
        heap_caps_malloc(sizeof(FeedPost) * MAX_POSTS, MALLOC_CAP_8BIT));
  }
  if (posts != nullptr) {
    memset(posts, 0, sizeof(FeedPost) * MAX_POSTS);
  }
}

void PostFeed::reset() {
  if (posts != nullptr) {
    memset(posts, 0, sizeof(FeedPost) * MAX_POSTS);
  }
  postCount = 0;
  scrollOffset = 0;
  expandedIndex = -1;
  expandedLineOffset = 0;
  listMode = MODE_SCROLL;
}

bool PostFeed::add(const FeedPost &post) {
  if (posts == nullptr || postCount >= MAX_POSTS || post.title[0] == '\0') {
    return false;
  }
  posts[postCount++] = post;
  return true;
}

bool PostFeed::handleTouch(const TouchPoint &point) {
  if (expandedIndex >= 0) {
    if (point.y < 100 && expandedLineOffset > 0) {
      expandedLineOffset--;
      return true;
    }
    if (point.y >= 100) {
      expandedLineOffset++;
      return true;
    }
    return false;
  }

  if (listMode == MODE_SELECT && point.y >= 24 && point.y < 184) {
    int slot = (point.y - 24) / 80;
    int index = scrollOffset + slot;
    if (slot >= 0 && slot < 2 && index >= 0 && index < postCount) {
      expandedIndex = index;
      expandedLineOffset = 0;
      return true;
    }
  }
  if (point.y < 96 && scrollOffset > 0) {
    scrollOffset--;
    return true;
  }
  if (point.y >= 96 && scrollOffset + 2 < postCount) {
    scrollOffset++;
    return true;
  }
  return false;
}

void PostFeed::draw(Adafruit_GFX &gfx, const char *header,
                    const char *scoreLabel, const char *rightHeader) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  if (expandedIndex >= 0 && expandedIndex < postCount) {
    drawExpanded(gfx, header, scoreLabel, rightHeader);
    return;
  }
  drawList(gfx, header, scoreLabel, rightHeader);
}

uint8_t PostFeed::count() const { return postCount; }

bool PostFeed::closeExpanded() {
  if (expandedIndex < 0) {
    return false;
  }
  expandedIndex = -1;
  expandedLineOffset = 0;
  return true;
}

void PostFeed::toggleSelectMode() {
  if (expandedIndex >= 0) {
    return;
  }
  listMode = listMode == MODE_SCROLL ? MODE_SELECT : MODE_SCROLL;
}

bool PostFeed::isExpanded() const { return expandedIndex >= 0; }

void PostFeed::drawList(Adafruit_GFX &gfx, const char *header,
                        const char *scoreLabel,
                        const char *rightHeader) const {
  gfx.setCursor(4, 6);
  gfx.print(header);
  drawRightText(gfx, rightHeader, 6);
  gfx.setCursor(78, 188);
  gfx.print(listMode == MODE_SCROLL ? "SCROLL" : "SELECT");

  if (postCount == 0) {
    gfx.setCursor(52, 92);
    gfx.print("No posts");
    return;
  }

  for (int i = 0; i < 2; i++) {
    int index = scrollOffset + i;
    if (index >= postCount) {
      break;
    }
    const FeedPost &post = posts[index];
    int y = 28 + i * 80;
    gfx.drawLine(4, y - 6, 196, y - 6, 1);
    gfx.setCursor(6, y);
    gfx.print(post.score);
    gfx.print(" ");
    gfx.print(scoreLabel);
    int lines = drawWrappedText(gfx, post.title, 6, y + 14, 31, 3);
    if (lines < 3 && post.text[0]) {
      drawWrappedText(gfx, post.text, 6, y + 14 + lines * 10, 31, 3 - lines);
    }
  }
}

void PostFeed::drawExpanded(Adafruit_GFX &gfx, const char *header,
                            const char *scoreLabel,
                            const char *rightHeader) const {
  const FeedPost &post = posts[expandedIndex];
  gfx.setCursor(4, 6);
  gfx.print(header);
  drawRightText(gfx, rightHeader, 6);
  gfx.setCursor(6, 22);
  gfx.print(post.score);
  gfx.print(" ");
  gfx.print(scoreLabel);
  gfx.drawLine(4, 34, 196, 34, 1);

  int y = 42;
  int drawn = drawWrappedText(gfx, post.title, 6, y, 31, 15,
                              expandedLineOffset);
  y += drawn * 10;
  if (y < 190 && post.text[0]) {
    drawWrappedText(gfx, post.text, 6, y + 4, 31, (190 - y) / 10,
                    expandedLineOffset > drawn ? expandedLineOffset - drawn
                                                : 0);
  }
}

void PostFeed::drawRightText(Adafruit_GFX &gfx, const char *text, int16_t y) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor(196 - static_cast<int>(w) - x1, y);
  gfx.print(text);
}

int PostFeed::drawWrappedText(Adafruit_GFX &gfx, const char *text, int16_t x,
                              int16_t y, int maxChars, int maxLines,
                              int skipLines, bool draw) {
  int emittedLines = 0;
  int visibleLines = 0;
  const char *cursor = skipWordSpaces(text);
  while (*cursor && visibleLines < maxLines) {
    char line[40];
    int len = 0;
    int lastSpace = -1;
    const char *lineStart = cursor;
    while (*cursor && *cursor != '\n' && len < maxChars &&
           len + 1 < static_cast<int>(sizeof(line))) {
      if (*cursor == ' ') {
        lastSpace = len;
      }
      line[len++] = *cursor++;
    }
    if (*cursor && *cursor != '\n' && len >= maxChars && lastSpace > 0) {
      cursor = lineStart + lastSpace + 1;
      len = lastSpace;
    }
    if (*cursor == '\n') {
      cursor++;
    }
    line[len] = '\0';
    if (emittedLines++ >= skipLines) {
      if (draw) {
        gfx.setCursor(x, y + visibleLines * 10);
        gfx.print(line);
      }
      visibleLines++;
    }
    cursor = skipWordSpaces(cursor);
  }
  return visibleLines;
}

const char *PostFeed::skipWordSpaces(const char *text) {
  while (*text == ' ') {
    text++;
  }
  return text;
}

#endif
