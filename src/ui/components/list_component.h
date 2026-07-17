#ifndef LIST_COMPONENT_H
#define LIST_COMPONENT_H

#include <stdint.h>

// .pink apps use the same compact list behavior without pulling Arduino.h
// into their freestanding build.
#ifndef PINK_APP_API_H
#include "sys/services/touch_input.h"
#else
struct TouchPoint {
  uint16_t x;
  uint16_t y;
};
#endif

/// Result action from list hit testing.
enum class UiListTouchAction : uint8_t {
  None,
  Scroll,
  Select,
};

/// List hit-test result.
struct UiListTouchResult {
  UiListTouchAction action = UiListTouchAction::None;
  uint16_t index = 0;
};

/// Returns the largest valid scroll offset for a list.
inline uint16_t uiListMaxOffset(uint16_t itemCount, uint8_t visibleCount) {
  return itemCount > visibleCount ? itemCount - visibleCount : 0;
}

/// Clamps a list scroll offset in place.
inline void uiListClampOffset(uint16_t &offset, uint16_t itemCount,
                              uint8_t visibleCount) {
  const uint16_t maxOffset = uiListMaxOffset(itemCount, visibleCount);
  if (offset > maxOffset) {
    offset = maxOffset;
  }
}

/// Scrolls a list offset by a signed row delta.
inline bool uiListScrollBy(uint16_t &offset, uint16_t itemCount,
                           uint8_t visibleCount, int8_t delta) {
  const uint16_t previous = offset;
  int32_t next = static_cast<int32_t>(offset) + delta;
  if (next < 0) {
    next = 0;
  }
  const uint16_t maxOffset = uiListMaxOffset(itemCount, visibleCount);
  if (next > maxOffset) {
    next = maxOffset;
  }
  offset = static_cast<uint16_t>(next);
  return offset != previous;
}

/// Maps a touch point to scroll or select behavior for a simple list.
inline UiListTouchResult
uiListHandleTouch(const TouchPoint &point, uint16_t itemCount,
                  uint8_t visibleCount, int16_t listY, int16_t rowHeight,
                  int16_t listHeight, int16_t splitX, uint16_t &offset) {
  UiListTouchResult result;
  uiListClampOffset(offset, itemCount, visibleCount);
  if (point.y < listY || point.y >= listY + listHeight || rowHeight <= 0) {
    return result;
  }

  if (point.x >= splitX) {
    const int8_t delta = point.y < listY + listHeight / 2 ? -1 : 1;
    if (uiListScrollBy(offset, itemCount, visibleCount, delta)) {
      result.action = UiListTouchAction::Scroll;
    }
    return result;
  }

  const uint8_t row = static_cast<uint8_t>((point.y - listY) / rowHeight);
  if (row >= visibleCount) {
    return result;
  }
  const uint16_t index = offset + row;
  if (index >= itemCount) {
    return result;
  }
  result.action = UiListTouchAction::Select;
  result.index = index;
  return result;
}

#endif
