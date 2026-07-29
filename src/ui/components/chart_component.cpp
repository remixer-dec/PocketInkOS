#include "ui/components/chart_component.h"

#include <cmath>

namespace {

uint16_t largestSeries(const UiChartSeries *series, uint8_t seriesCount) {
  uint16_t largest = 0;
  for (uint8_t i = 0; i < seriesCount; i++) {
    if (series[i].values != nullptr && series[i].count > largest) {
      largest = series[i].count;
    }
  }
  return largest;
}

} // namespace

void ChartComponent::draw(Adafruit_GFX &gfx, const UiChartRect &bounds,
                          UiChartType type, const UiChartSeries *series,
                          uint8_t seriesCount) {
  if (series == nullptr || seriesCount == 0 || bounds.width < 3 ||
      bounds.height < 3) {
    return;
  }

  float minimum = 0.0f;
  float maximum = 0.0f;
  if (!valueRange(type, series, seriesCount, minimum, maximum)) {
    return;
  }

  gfx.drawRect(bounds.x, bounds.y, bounds.width, bounds.height, 1);
  const UiChartRect plot = {
      static_cast<int16_t>(bounds.x + 1),
      static_cast<int16_t>(bounds.y + 1),
      static_cast<int16_t>(bounds.width - 2),
      static_cast<int16_t>(bounds.height - 2),
  };
  if (type == UiChartType::Bar) {
    drawBars(gfx, plot, series, seriesCount, minimum, maximum);
    return;
  }
  drawLines(gfx, plot, type, series, seriesCount, minimum, maximum);
}

bool ChartComponent::valueRange(UiChartType type,
                                const UiChartSeries *series,
                                uint8_t seriesCount, float &minimum,
                                float &maximum) {
  if (series == nullptr || seriesCount == 0) {
    return false;
  }

  bool found = false;
  for (uint8_t seriesIndex = 0; seriesIndex < seriesCount; seriesIndex++) {
    const UiChartSeries &current = series[seriesIndex];
    if (current.values == nullptr) {
      continue;
    }
    for (uint16_t valueIndex = 0; valueIndex < current.count; valueIndex++) {
      const float value = current.values[valueIndex];
      if (!std::isfinite(value)) {
        continue;
      }
      if (!found) {
        minimum = value;
        maximum = value;
        found = true;
      } else {
        minimum = value < minimum ? value : minimum;
        maximum = value > maximum ? value : maximum;
      }
    }
  }
  if (!found) {
    return false;
  }

  if (type == UiChartType::Bar) {
    minimum = minimum > 0.0f ? 0.0f : minimum;
    maximum = maximum < 0.0f ? 0.0f : maximum;
  }
  if (minimum == maximum) {
    float padding = std::fabs(minimum) * 0.05f;
    if (padding < 0.0001f) {
      padding = 1.0f;
    }
    minimum -= padding;
    maximum += padding;
  }
  return true;
}

bool ChartComponent::texturePixel(UiChartTexture texture, int16_t x,
                                  int16_t y) {
  switch (texture) {
  case UiChartTexture::Dots:
    return (x & 3) == 0 && (y & 3) == 0;
  case UiChartTexture::Horizontal:
    return (y & 3) == 0;
  case UiChartTexture::Vertical:
    return (x & 3) == 0;
  case UiChartTexture::Diagonal:
    return (x + y) % 4 == 0;
  case UiChartTexture::Crosshatch:
    return (x + y) % 6 == 0 || (x - y) % 6 == 0;
  case UiChartTexture::Solid:
  default:
    return true;
  }
}

bool ChartComponent::linePixel(UiChartTexture texture, uint16_t phase) {
  switch (texture) {
  case UiChartTexture::Dots:
    return phase % 3 == 0;
  case UiChartTexture::Horizontal:
    return phase % 6 < 4;
  case UiChartTexture::Vertical:
    return phase % 7 < 3;
  case UiChartTexture::Diagonal:
    return phase % 8 < 5;
  case UiChartTexture::Crosshatch:
    return phase % 4 != 3;
  case UiChartTexture::Solid:
  default:
    return true;
  }
}

int16_t ChartComponent::mapY(float value, float minimum, float maximum,
                             int16_t top, int16_t height) {
  const float normalized = (value - minimum) / (maximum - minimum);
  const int16_t offset =
      static_cast<int16_t>(std::lround(normalized * (height - 1)));
  return static_cast<int16_t>(top + height - 1 - offset);
}

void ChartComponent::drawTexturedLine(Adafruit_GFX &gfx, int16_t x0,
                                      int16_t y0, int16_t x1, int16_t y1,
                                      UiChartTexture texture) {
  const int16_t dx = static_cast<int16_t>(std::abs(x1 - x0));
  const int16_t stepX = x0 < x1 ? 1 : -1;
  const int16_t dy = static_cast<int16_t>(-std::abs(y1 - y0));
  const int16_t stepY = y0 < y1 ? 1 : -1;
  int16_t error = static_cast<int16_t>(dx + dy);
  uint16_t phase = 0;

  while (true) {
    if (linePixel(texture, phase++)) {
      gfx.drawPixel(x0, y0, 1);
    }
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int16_t doubledError = static_cast<int16_t>(error * 2);
    if (doubledError >= dy) {
      error = static_cast<int16_t>(error + dy);
      x0 = static_cast<int16_t>(x0 + stepX);
    }
    if (doubledError <= dx) {
      error = static_cast<int16_t>(error + dx);
      y0 = static_cast<int16_t>(y0 + stepY);
    }
  }
}

void ChartComponent::drawBars(Adafruit_GFX &gfx, const UiChartRect &plot,
                              const UiChartSeries *series,
                              uint8_t seriesCount, float minimum,
                              float maximum) {
  const uint16_t sampleCount = largestSeries(series, seriesCount);
  if (sampleCount == 0) {
    return;
  }
  const int16_t baseline = mapY(0.0f, minimum, maximum, plot.y, plot.height);

  for (uint16_t sample = 0; sample < sampleCount; sample++) {
    const int16_t slotLeft = static_cast<int16_t>(
        plot.x + static_cast<int32_t>(sample) * plot.width / sampleCount);
    const int16_t slotRight = static_cast<int16_t>(
        plot.x + static_cast<int32_t>(sample + 1) * plot.width / sampleCount);
    const int16_t slotWidth =
        slotRight > slotLeft ? slotRight - slotLeft : 1;
    const int16_t barWidth =
        slotWidth / seriesCount > 0 ? slotWidth / seriesCount : 1;

    for (uint8_t seriesIndex = 0; seriesIndex < seriesCount; seriesIndex++) {
      const UiChartSeries &current = series[seriesIndex];
      if (current.values == nullptr || sample >= current.count ||
          !std::isfinite(current.values[sample])) {
        continue;
      }
      const int16_t valueY =
          mapY(current.values[sample], minimum, maximum, plot.y, plot.height);
      const int16_t top = valueY < baseline ? valueY : baseline;
      const int16_t bottom = valueY > baseline ? valueY : baseline;
      const int16_t left =
          static_cast<int16_t>(slotLeft + seriesIndex * barWidth);
      const int16_t right = static_cast<int16_t>(
          left + barWidth < slotRight ? left + barWidth : slotRight);
      for (int16_t x = left; x < right; x++) {
        for (int16_t y = top; y <= bottom; y++) {
          if (texturePixel(current.texture, x, y)) {
            gfx.drawPixel(x, y, 1);
          }
        }
      }
    }
  }
}

void ChartComponent::drawLines(Adafruit_GFX &gfx, const UiChartRect &plot,
                               UiChartType type,
                               const UiChartSeries *series,
                               uint8_t seriesCount, float minimum,
                               float maximum) {
  const int16_t bottom = static_cast<int16_t>(plot.y + plot.height - 1);
  for (uint8_t seriesIndex = 0; seriesIndex < seriesCount; seriesIndex++) {
    const UiChartSeries &current = series[seriesIndex];
    if (current.values == nullptr || current.count == 0) {
      continue;
    }

    bool hasPrevious = false;
    int16_t previousX = 0;
    int16_t previousY = 0;
    for (uint16_t valueIndex = 0; valueIndex < current.count; valueIndex++) {
      const float value = current.values[valueIndex];
      if (!std::isfinite(value)) {
        hasPrevious = false;
        continue;
      }
      const int16_t x =
          current.count == 1
              ? static_cast<int16_t>(plot.x + plot.width / 2)
              : static_cast<int16_t>(
                    plot.x + static_cast<int32_t>(valueIndex) *
                                 (plot.width - 1) / (current.count - 1));
      const int16_t y = mapY(value, minimum, maximum, plot.y, plot.height);

      if (!hasPrevious) {
        if (type == UiChartType::Area) {
          for (int16_t fillY = y; fillY <= bottom; fillY++) {
            if (texturePixel(current.texture, x, fillY)) {
              gfx.drawPixel(x, fillY, 1);
            }
          }
        }
        gfx.drawPixel(x, y, 1);
        previousX = x;
        previousY = y;
        hasPrevious = true;
        continue;
      }

      if (type == UiChartType::Area) {
        const int16_t span = x - previousX;
        for (int16_t fillX = previousX; fillX <= x; fillX++) {
          const int16_t fillY =
              span == 0
                  ? y
                  : static_cast<int16_t>(
                        previousY + static_cast<int32_t>(y - previousY) *
                                        (fillX - previousX) / span);
          for (int16_t pixelY = fillY; pixelY <= bottom; pixelY++) {
            if (texturePixel(current.texture, fillX, pixelY)) {
              gfx.drawPixel(fillX, pixelY, 1);
            }
          }
        }
      }
      drawTexturedLine(gfx, previousX, previousY, x, y, current.texture);
      previousX = x;
      previousY = y;
    }
  }
}
