#ifndef TOUCH_BUTTON_H
#define TOUCH_BUTTON_H

#include "sys/drivers/touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

/// Integer rectangle used by touchable UI controls.
struct UiRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

/// Built-in shape kinds for touch button rendering and hit testing.
enum class TouchButtonShapeType : uint8_t {
  Rectangle,
  RoundedRectangle,
  Circle,
  Custom,
};

/// Custom touch-button hit-test callback.
typedef bool (*TouchButtonHitTest)(const UiRect &bounds,
                                   const TouchPoint &point, void *context);
/// Custom touch-button painter callback.
typedef void (*TouchButtonPainter)(Adafruit_GFX &gfx, const UiRect &bounds,
                                   bool pressed, void *context);

/// Shape configuration for a touch button.
struct TouchButtonShape {
  TouchButtonShapeType type = TouchButtonShapeType::Rectangle;
  uint8_t radius = 0;
  TouchButtonHitTest hitTest = nullptr;
  TouchButtonPainter painter = nullptr;
  void *context = nullptr;

  /// Creates a rectangular button shape.
  static TouchButtonShape rectangle();
  /// Creates a rounded-rectangle button shape.
  static TouchButtonShape rounded(uint8_t radius);
  /// Creates a circular button shape.
  static TouchButtonShape circle();
  /// Creates a custom button shape.
  static TouchButtonShape custom(TouchButtonHitTest hitTest,
                                 TouchButtonPainter painter = nullptr,
                                 void *context = nullptr);
};

/// Icon placement relative to text.
enum class TouchButtonIconPlacement : uint8_t {
  None,
  Only,
  AboveText,
  Left,
  Right,
};

/// Alignment value used for button text positioning.
enum class TouchButtonAlign : uint8_t {
  Start,
  Center,
  End,
};

/// Text and icon content for a touch button.
struct TouchButtonContent {
  const char *text = nullptr;
  const char *icon = nullptr;
  TouchButtonIconPlacement iconPlacement = TouchButtonIconPlacement::None;
  bool iconUsesIconFont = true;
  uint8_t iconTextSize = 1;
  TouchButtonAlign textX = TouchButtonAlign::Center;
  TouchButtonAlign textY = TouchButtonAlign::Center;
  uint8_t textSize = 1;
  int8_t textOffsetX = 0;
  int8_t textOffsetY = 0;
  int8_t iconOffsetX = 0;
  int8_t iconOffsetY = 0;
};

/// Drawing and hit-test style for a touch button.
struct TouchButtonStyle {
  uint8_t color = 1;
  uint8_t backgroundColor = 0;
  uint8_t pressedColor = 0;
  uint8_t pressedBackgroundColor = 1;
  uint8_t padding = 4;
  uint8_t touchPadding = 0;
  bool drawOutline = true;
  bool fillPressed = true;
};

/// Reusable touchable button primitive for e-paper UI controls.
class TouchButton {
public:
  /// Creates a button with only bounds and shape.
  TouchButton(const UiRect &bounds,
              TouchButtonShape shape = TouchButtonShape::rectangle());
  /// Creates a button with content, shape, and style.
  TouchButton(const UiRect &bounds, const TouchButtonContent &content,
              TouchButtonShape shape = TouchButtonShape::rectangle(),
              const TouchButtonStyle &style = TouchButtonStyle());

  /// Returns true when a point hits the button.
  bool contains(const TouchPoint &point) const;
  /// Draws the button in normal or pressed state.
  void draw(Adafruit_GFX &gfx, bool pressed = false) const;

  /// Returns the button bounds.
  const UiRect &bounds() const;

private:
  UiRect _bounds;
  TouchButtonShape _shape;
  TouchButtonContent _content;
  TouchButtonStyle _style;
};

#endif
