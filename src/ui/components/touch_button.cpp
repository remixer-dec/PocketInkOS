#include "ui/components/touch_button.h"
#include "ui/icon_ascii_font.h"
#include <string.h>

namespace {

bool hasText(const char *text) { return text != nullptr && text[0] != '\0'; }

int16_t min16(int16_t a, int16_t b) { return a < b ? a : b; }

int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

int16_t alignStart(int16_t min, int16_t span, int16_t content,
                   TouchButtonAlign align) {
  switch (align) {
  case TouchButtonAlign::Start:
    return min;
  case TouchButtonAlign::End:
    return min + span - content;
  case TouchButtonAlign::Center:
    return min + (span - content) / 2;
  }
  return min;
}

bool rectContains(const UiRect &rect, const TouchPoint &point) {
  return point.x >= rect.x && point.x < rect.x + rect.w && point.y >= rect.y &&
         point.y < rect.y + rect.h;
}

UiRect expandedRect(const UiRect &rect, uint8_t padding) {
  return {static_cast<int16_t>(rect.x - padding),
          static_cast<int16_t>(rect.y - padding),
          static_cast<int16_t>(rect.w + padding * 2),
          static_cast<int16_t>(rect.h + padding * 2)};
}

bool circleContains(const UiRect &rect, const TouchPoint &point) {
  const int16_t radius = min16(rect.w, rect.h) / 2;
  const int16_t centerX = rect.x + rect.w / 2;
  const int16_t centerY = rect.y + rect.h / 2;
  const int32_t dx = static_cast<int32_t>(point.x) - centerX;
  const int32_t dy = static_cast<int32_t>(point.y) - centerY;
  return dx * dx + dy * dy <= static_cast<int32_t>(radius) * radius;
}

bool roundedContains(const UiRect &rect, uint8_t radius,
                     const TouchPoint &point) {
  if (!rectContains(rect, point)) {
    return false;
  }
  if (radius == 0) {
    return true;
  }

  const int16_t r = min16(radius, min16(rect.w, rect.h) / 2);
  const bool inHorizontalCore =
      point.x >= rect.x + r && point.x < rect.x + rect.w - r;
  const bool inVerticalCore =
      point.y >= rect.y + r && point.y < rect.y + rect.h - r;
  if (inHorizontalCore || inVerticalCore) {
    return true;
  }

  const int16_t centerX = point.x < rect.x + r ? rect.x + r
                                                : rect.x + rect.w - r - 1;
  const int16_t centerY = point.y < rect.y + r ? rect.y + r
                                                : rect.y + rect.h - r - 1;
  const int32_t dx = static_cast<int32_t>(point.x) - centerX;
  const int32_t dy = static_cast<int32_t>(point.y) - centerY;
  return dx * dx + dy * dy <= static_cast<int32_t>(r) * r;
}

void drawDefaultShape(Adafruit_GFX &gfx, const UiRect &rect,
                      const TouchButtonShape &shape,
                      const TouchButtonStyle &style, bool pressed) {
  const uint8_t ink = style.color;
  if (pressed && style.fillPressed) {
    switch (shape.type) {
    case TouchButtonShapeType::Circle:
      gfx.fillCircle(rect.x + rect.w / 2, rect.y + rect.h / 2,
                     min16(rect.w, rect.h) / 2,
                     style.pressedBackgroundColor);
      break;
    case TouchButtonShapeType::RoundedRectangle:
      gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, shape.radius,
                        style.pressedBackgroundColor);
      break;
    case TouchButtonShapeType::Rectangle:
    case TouchButtonShapeType::Custom:
      if (rect.w > 2 && rect.h > 2) {
        gfx.fillRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2,
                     style.pressedBackgroundColor);
      }
      break;
    }
  }

  if (!style.drawOutline) {
    return;
  }

  switch (shape.type) {
  case TouchButtonShapeType::Circle:
    gfx.drawCircle(rect.x + rect.w / 2, rect.y + rect.h / 2,
                   min16(rect.w, rect.h) / 2, ink);
    break;
  case TouchButtonShapeType::RoundedRectangle:
    gfx.drawRoundRect(rect.x, rect.y, rect.w, rect.h, shape.radius, ink);
    break;
  case TouchButtonShapeType::Rectangle:
  case TouchButtonShapeType::Custom:
    gfx.drawRect(rect.x, rect.y, rect.w, rect.h, ink);
    break;
  }
}

void drawText(Adafruit_GFX &gfx, const UiRect &rect, const char *text,
              uint8_t textSize, TouchButtonAlign alignX,
              TouchButtonAlign alignY, int8_t offsetX, int8_t offsetY) {
  if (!hasText(text)) {
    return;
  }

  gfx.setFont();
  gfx.setTextSize(textSize);
  int16_t textX = 0;
  int16_t textY = 0;
  uint16_t textW = 0;
  uint16_t textH = 0;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  gfx.setCursor(alignStart(rect.x, rect.w, static_cast<int16_t>(textW),
                           alignX) -
                    textX + offsetX,
                alignStart(rect.y, rect.h, static_cast<int16_t>(textH),
                           alignY) -
                    textY + offsetY);
  gfx.print(text);
}

void drawIcon(Adafruit_GFX &gfx, const UiRect &rect, const char *icon,
              bool usesIconFont, uint8_t textSize, int8_t offsetX,
              int8_t offsetY) {
  if (!hasText(icon)) {
    return;
  }

  gfx.setFont(usesIconFont ? &iconASCII12pt7b : nullptr);
  gfx.setTextSize(textSize);
  int16_t iconX = 0;
  int16_t iconY = 0;
  uint16_t iconW = 0;
  uint16_t iconH = 0;
  gfx.getTextBounds(icon, 0, 0, &iconX, &iconY, &iconW, &iconH);
  gfx.setCursor(rect.x + (rect.w - static_cast<int16_t>(iconW)) / 2 - iconX +
                    offsetX,
                rect.y + (rect.h - static_cast<int16_t>(iconH)) / 2 - iconY +
                    offsetY);
  gfx.print(icon);
  gfx.setFont();
  gfx.setTextSize(1);
}

void drawContent(Adafruit_GFX &gfx, const UiRect &bounds,
                 const TouchButtonContent &content,
                 const TouchButtonStyle &style) {
  const int16_t pad = style.padding;
  UiRect inner = {static_cast<int16_t>(bounds.x + pad),
                  static_cast<int16_t>(bounds.y + pad),
                  static_cast<int16_t>(bounds.w - pad * 2),
                  static_cast<int16_t>(bounds.h - pad * 2)};
  if (inner.w <= 0 || inner.h <= 0) {
    inner = bounds;
  }

  const bool iconPresent = hasText(content.icon);
  const bool textPresent = hasText(content.text);
  TouchButtonIconPlacement placement = content.iconPlacement;
  if (!iconPresent) {
    placement = TouchButtonIconPlacement::None;
  } else if (!textPresent && placement == TouchButtonIconPlacement::None) {
    placement = TouchButtonIconPlacement::Only;
  }

  switch (placement) {
  case TouchButtonIconPlacement::Only:
    drawIcon(gfx, inner, content.icon, content.iconUsesIconFont,
             content.iconTextSize, content.iconOffsetX, content.iconOffsetY);
    break;
  case TouchButtonIconPlacement::AboveText: {
    const int16_t labelH =
        textPresent ? static_cast<int16_t>(10 * content.textSize) : 0;
    const int16_t iconH = textPresent ? max16(0, inner.h - labelH) : inner.h;
    UiRect iconRect = {inner.x, inner.y, inner.w, iconH};
    UiRect textRect = {inner.x, static_cast<int16_t>(inner.y + iconH),
                       inner.w, static_cast<int16_t>(inner.h - iconH)};
    drawIcon(gfx, iconRect, content.icon, content.iconUsesIconFont,
             content.iconTextSize, content.iconOffsetX, content.iconOffsetY);
    drawText(gfx, textRect, content.text, content.textSize, content.textX,
             content.textY, content.textOffsetX, content.textOffsetY);
    break;
  }
  case TouchButtonIconPlacement::Left:
  case TouchButtonIconPlacement::Right: {
    const int16_t iconW = min16(inner.h, inner.w / 2);
    UiRect iconRect = inner;
    UiRect textRect = inner;
    if (placement == TouchButtonIconPlacement::Left) {
      iconRect.w = iconW;
      textRect.x = inner.x + iconW;
      textRect.w = inner.w - iconW;
    } else {
      iconRect.x = inner.x + inner.w - iconW;
      iconRect.w = iconW;
      textRect.w = inner.w - iconW;
    }
    drawIcon(gfx, iconRect, content.icon, content.iconUsesIconFont,
             content.iconTextSize, content.iconOffsetX, content.iconOffsetY);
    drawText(gfx, textRect, content.text, content.textSize, content.textX,
             content.textY, content.textOffsetX, content.textOffsetY);
    break;
  }
  case TouchButtonIconPlacement::None:
    drawText(gfx, inner, content.text, content.textSize, content.textX,
             content.textY, content.textOffsetX, content.textOffsetY);
    break;
  }
}

} // namespace

TouchButtonShape TouchButtonShape::rectangle() { return TouchButtonShape(); }

TouchButtonShape TouchButtonShape::rounded(uint8_t radius) {
  TouchButtonShape shape;
  shape.type = TouchButtonShapeType::RoundedRectangle;
  shape.radius = radius;
  return shape;
}

TouchButtonShape TouchButtonShape::circle() {
  TouchButtonShape shape;
  shape.type = TouchButtonShapeType::Circle;
  return shape;
}

TouchButtonShape TouchButtonShape::custom(TouchButtonHitTest hitTest,
                                          TouchButtonPainter painter,
                                          void *context) {
  TouchButtonShape shape;
  shape.type = TouchButtonShapeType::Custom;
  shape.hitTest = hitTest;
  shape.painter = painter;
  shape.context = context;
  return shape;
}

TouchButton::TouchButton(const UiRect &bounds, TouchButtonShape shape)
    : _bounds(bounds), _shape(shape) {}

TouchButton::TouchButton(const UiRect &bounds,
                         const TouchButtonContent &content,
                         TouchButtonShape shape,
                         const TouchButtonStyle &style)
    : _bounds(bounds), _shape(shape), _content(content), _style(style) {}

bool TouchButton::contains(const TouchPoint &point) const {
  const UiRect hitBounds = expandedRect(_bounds, _style.touchPadding);
  switch (_shape.type) {
  case TouchButtonShapeType::Circle:
    return circleContains(hitBounds, point);
  case TouchButtonShapeType::RoundedRectangle:
    return roundedContains(hitBounds, _shape.radius, point);
  case TouchButtonShapeType::Custom:
    if (_shape.hitTest != nullptr) {
      return _shape.hitTest(hitBounds, point, _shape.context);
    }
    return rectContains(hitBounds, point);
  case TouchButtonShapeType::Rectangle:
    return rectContains(hitBounds, point);
  }
  return false;
}

void TouchButton::draw(Adafruit_GFX &gfx, bool pressed) const {
  if (_shape.type == TouchButtonShapeType::Custom &&
      _shape.painter != nullptr) {
    _shape.painter(gfx, _bounds, pressed, _shape.context);
  } else {
    drawDefaultShape(gfx, _bounds, _shape, _style, pressed);
  }

  gfx.setTextColor(pressed ? _style.pressedColor : _style.color);
  drawContent(gfx, _bounds, _content, _style);
  gfx.setFont();
  gfx.setTextSize(1);
  gfx.setTextColor(_style.color);
}

const UiRect &TouchButton::bounds() const { return _bounds; }
