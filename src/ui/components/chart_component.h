#ifndef CHART_COMPONENT_H
#define CHART_COMPONENT_H

#include <Adafruit_GFX.h>
#include <stdint.h>

/// Monochrome chart rendering modes. Column is an alias for Bar.
enum class UiChartType : uint8_t {
  Bar,
  Column = Bar,
  Line,
  Area,
};

/// Black-and-white patterns used to distinguish chart series on e-paper.
enum class UiChartTexture : uint8_t {
  Solid,
  Dots,
  Horizontal,
  Vertical,
  Diagonal,
  Crosshatch,
};

struct UiChartRect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;
};

/// One equally-spaced series. Values remain owned by the caller.
struct UiChartSeries {
  const float *values = nullptr;
  uint16_t count = 0;
  UiChartTexture texture = UiChartTexture::Solid;
};

/// Draws compact monochrome charts without allocating a framebuffer.
class ChartComponent {
public:
  /// Draws every series inside bounds, including a one-pixel plot border.
  static void draw(Adafruit_GFX &gfx, const UiChartRect &bounds,
                   UiChartType type, const UiChartSeries *series,
                   uint8_t seriesCount);

  /// Computes the finite value range used by draw. Bar charts include zero.
  static bool valueRange(UiChartType type, const UiChartSeries *series,
                         uint8_t seriesCount, float &minimum, float &maximum);

private:
  static bool texturePixel(UiChartTexture texture, int16_t x, int16_t y);
  static bool linePixel(UiChartTexture texture, uint16_t phase);
  static int16_t mapY(float value, float minimum, float maximum,
                      int16_t top, int16_t height);
  static void drawTexturedLine(Adafruit_GFX &gfx, int16_t x0, int16_t y0,
                               int16_t x1, int16_t y1,
                               UiChartTexture texture);
  static void drawBars(Adafruit_GFX &gfx, const UiChartRect &plot,
                       const UiChartSeries *series, uint8_t seriesCount,
                       float minimum, float maximum);
  static void drawLines(Adafruit_GFX &gfx, const UiChartRect &plot,
                        UiChartType type, const UiChartSeries *series,
                        uint8_t seriesCount, float minimum, float maximum);
};

#endif
