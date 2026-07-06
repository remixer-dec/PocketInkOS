#include "apps/deghost_app.h"

#include <Arduino.h>

static const uint16_t ZEBRA_FRAME_MS = 90;
static const uint16_t NOISE_FRAME_MS = 120;
static const uint16_t QUICK_FRAME_MS = 120;
static const uint8_t QUICK_BLINK_STEPS = 10;
static const uint8_t ZEBRA_ITERATIONS = 10;
static const uint8_t NOISE_ITERATIONS = 20;

void DeghostApp::reset() {
  mode = MODE_SELECT;
  pendingEvent = AppEventResult::Unhandled;
  running = false;
  finished = false;
  stepIndex = 0;
  stepStartedAt = 0;
  lastAnimAt = 0;
  zebraFrame = 0;
  noiseFrame = 0;
}

void DeghostApp::start(AppDisplay &targetDisplay) {
  display = &targetDisplay;
  if (display != nullptr) {
    display->requestFullRefresh();
  }
}

void DeghostApp::beginSequence(Mode nextMode) {
  mode = nextMode;
  pendingEvent = AppEventResult::Unhandled;
  running = true;
  finished = false;
  stepIndex = 0;
  stepStartedAt = millis();
  lastAnimAt = stepStartedAt;
  zebraFrame = 0;
  noiseFrame = 0;
  if (display != nullptr) {
    display->requestFullRefresh();
  }
}

void DeghostApp::advanceStep() {
  if (stepIndex + 1 >= stepCount()) {
    running = false;
    finished = true;
    if (mode == MODE_QUICK) {
      pendingEvent = AppEventResult::GoMenu;
    }
    return;
  }

  stepIndex++;
  stepStartedAt = millis();
  lastAnimAt = stepStartedAt;
  zebraFrame = 0;
  noiseFrame = 0;
  if (display != nullptr) {
    display->requestFullRefresh();
  }
}

bool DeghostApp::update() {
  if (!running) {
    return false;
  }
  unsigned long now = millis();
  Pattern pattern = patternForStep(stepIndex);
  if (isZebraPattern(pattern)) {
    if (now - lastAnimAt < ZEBRA_FRAME_MS) {
      return false;
    }
    lastAnimAt = now;
    zebraFrame++;
    if (zebraFrame >= ZEBRA_ITERATIONS) {
      advanceStep();
    }
    return true;
  }
  if (isNoisePattern(pattern)) {
    if (now - lastAnimAt < NOISE_FRAME_MS) {
      return false;
    }
    lastAnimAt = now;
    noiseFrame++;
    if (noiseFrame >= NOISE_ITERATIONS) {
      advanceStep();
    }
    return true;
  }
  if (now - stepStartedAt < holdMsForStep(stepIndex)) {
    return false;
  }
  advanceStep();
  return true;
}

bool DeghostApp::handleTouch(const TouchPoint &point) {
  if (running) {
    return false;
  }
  if (mode == MODE_SELECT) {
    if (containsQuickButton(point)) {
      beginSequence(MODE_QUICK);
      return true;
    }
    if (containsSlowButton(point)) {
      beginSequence(MODE_SLOW);
      return true;
    }
    return false;
  }
  if (finished && mode == MODE_SLOW) {
    beginSequence(MODE_SLOW);
    return true;
  }
  mode = MODE_SELECT;
  if (display != nullptr) {
    display->requestFullRefresh();
  }
  return true;
}

AppEventResult DeghostApp::handleBackButton() {
  if (running) {
    return AppEventResult::QuitRequested;
  }
  return AppEventResult::GoMenu;
}

bool DeghostApp::handlePowerButton() {
  if (!running && finished && mode == MODE_SLOW) {
    beginSequence(MODE_SLOW);
    return true;
  }
  if (running) {
    advanceStep();
    return true;
  }
  return false;
}

AppEventResult DeghostApp::consumePendingEvent() {
  AppEventResult result = pendingEvent;
  pendingEvent = AppEventResult::Unhandled;
  return result;
}

bool DeghostApp::hasActiveSession() const { return running; }

void DeghostApp::draw(Adafruit_GFX &gfx) {
  if (running) {
    drawPattern(gfx, patternForStep(stepIndex));
    return;
  }

  if (mode == MODE_SELECT) {
    drawModeSelect(gfx);
    return;
  }

  drawFinished(gfx);
}

bool DeghostApp::containsQuickButton(const TouchPoint &point) const {
  static const int16_t buttonW = 72;
  static const int16_t buttonH = 42;
  static const int16_t gap = 16;
  static const int16_t y = 112;
  const int16_t x = (EPD_WIDTH - buttonW * 2 - gap) / 2;
  return point.x >= x && point.x < x + buttonW && point.y >= y &&
         point.y < y + buttonH;
}

bool DeghostApp::containsSlowButton(const TouchPoint &point) const {
  static const int16_t buttonW = 72;
  static const int16_t buttonH = 42;
  static const int16_t gap = 16;
  static const int16_t y = 112;
  const int16_t x = (EPD_WIDTH - buttonW * 2 - gap) / 2 + buttonW + gap;
  return point.x >= x && point.x < x + buttonW && point.y >= y &&
         point.y < y + buttonH;
}

void DeghostApp::drawCenteredText(Adafruit_GFX &gfx, const char *text,
                                  int16_t y, uint8_t size) {
  gfx.setTextSize(size);
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  gfx.setCursor((EPD_WIDTH - static_cast<int>(textW)) / 2 - textX, y);
  gfx.print(text);
}

void DeghostApp::drawModeButton(Adafruit_GFX &gfx, int16_t x, int16_t y,
                                int16_t w, int16_t h, const char *label) {
  gfx.drawRect(x, y, w, h, 1);
  gfx.setTextSize(1);
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(label, 0, 0, &textX, &textY, &textW, &textH);
  gfx.setCursor(x + (w - static_cast<int>(textW)) / 2 - textX,
                y + (h - static_cast<int>(textH)) / 2 - textY);
  gfx.print(label);
}

void DeghostApp::drawModeSelect(Adafruit_GFX &gfx) {
  static const int16_t buttonW = 72;
  static const int16_t buttonH = 42;
  static const int16_t gap = 16;
  static const int16_t buttonY = 112;
  const int16_t buttonX = (EPD_WIDTH - buttonW * 2 - gap) / 2;

  gfx.fillScreen(0);
  gfx.setTextColor(1);
  drawCenteredText(gfx, "DEGHOST", 64, 2);
  drawCenteredText(gfx, "Select mode", 92, 1);
  drawModeButton(gfx, buttonX, buttonY, buttonW, buttonH, "QUICK");
  drawModeButton(gfx, buttonX + buttonW + gap, buttonY, buttonW, buttonH,
                 "SLOW");
}

void DeghostApp::drawFinished(Adafruit_GFX &gfx) {
  gfx.fillScreen(0);
  gfx.setTextColor(1);
  drawCenteredText(gfx, "DEGHOST", 70, 2);
  drawCenteredText(gfx, "Done.", 104, 1);
  if (mode == MODE_SLOW) {
    drawCenteredText(gfx, "PWR repeat", 124, 1);
    drawCenteredText(gfx, "BACK quit", 140, 1);
  }
}

uint8_t DeghostApp::stepCount() const {
  if (mode == MODE_QUICK) {
    return QUICK_BLINK_STEPS;
  }
  return 14;
}

DeghostApp::Pattern DeghostApp::patternForStep(uint8_t index) const {
  if (mode == MODE_QUICK) {
    return index % 2 == 0 ? PATTERN_BLACK : PATTERN_WHITE;
  }
  switch (index) {
  case 0:
    return PATTERN_WHITE;
  case 1:
    return PATTERN_BLACK;
  case 2:
    return PATTERN_WHITE;
  case 3:
    return PATTERN_BLACK;
  case 4:
    return PATTERN_ZEBRA_0;
  case 5:
    return PATTERN_ZEBRA_45;
  case 6:
    return PATTERN_ZEBRA_90;
  case 7:
    return PATTERN_ZEBRA_150;
  case 8:
    return PATTERN_BLACK;
  case 9:
    return PATTERN_WHITE;
  case 10:
    return PATTERN_NOISE;
  case 11:
    return PATTERN_NOISE_COPY;
  case 12:
    return PATTERN_BLACK;
  default:
    return PATTERN_WHITE;
  }
}

uint16_t DeghostApp::holdMsForStep(uint8_t index) const {
  if (mode == MODE_QUICK) {
    return QUICK_FRAME_MS;
  }
  switch (index) {
  case 0:
  case 1:
    return 260;
  case 2:
  case 3:
    return 180;
  case 4:
  case 5:
  case 6:
  case 7:
    return 220;
  case 8:
    return 320;
  case 9:
    return 360;
  case 10:
  case 11:
    return 0;
  case 12:
    return 900;
  case 13:
    return 700;
  default:
    return 700;
  }
}

bool DeghostApp::isZebraPattern(Pattern pattern) const {
  return pattern == PATTERN_ZEBRA_0 || pattern == PATTERN_ZEBRA_30 ||
         pattern == PATTERN_ZEBRA_45 || pattern == PATTERN_ZEBRA_60 ||
         pattern == PATTERN_ZEBRA_90 || pattern == PATTERN_ZEBRA_120 ||
         pattern == PATTERN_ZEBRA_150;
}

bool DeghostApp::isNoisePattern(Pattern pattern) const {
  return pattern == PATTERN_NOISE || pattern == PATTERN_NOISE_COPY;
}

void DeghostApp::drawPattern(Adafruit_GFX &gfx, Pattern pattern) {
  int phasePx = static_cast<int>(zebraFrame);
  switch (pattern) {
  case PATTERN_WHITE:
    gfx.fillScreen(0);
    break;
  case PATTERN_BLACK:
    gfx.fillScreen(1);
    break;
  case PATTERN_ZEBRA_0:
    drawZebra(gfx, 1000, 0, phasePx);
    break;
  case PATTERN_ZEBRA_30:
    drawZebra(gfx, 866, 500, phasePx);
    break;
  case PATTERN_ZEBRA_45:
    drawZebra(gfx, 707, 707, phasePx);
    break;
  case PATTERN_ZEBRA_60:
    drawZebra(gfx, 500, 866, phasePx);
    break;
  case PATTERN_ZEBRA_90:
    drawZebra(gfx, 0, 1000, phasePx);
    break;
  case PATTERN_ZEBRA_120:
    drawZebra(gfx, -500, 866, phasePx);
    break;
  case PATTERN_ZEBRA_150:
    drawZebra(gfx, -866, 500, phasePx);
    break;
  case PATTERN_NOISE:
    drawNoiseClassic(gfx);
    break;
  case PATTERN_NOISE_COPY:
    drawNoiseSeeded(gfx, 0xC2B2AE35u);
    break;
  }
}

void DeghostApp::drawZebra(Adafruit_GFX &gfx, int dx, int dy, int phasePx) {
  static const int STRIPE_PX = 12;
  static const int SCALE = 1000;
  const int period = STRIPE_PX * 2 * SCALE;
  int phase = (phasePx * SCALE) % period;
  if (phase < 0) {
    phase += period;
  }

  gfx.fillScreen(0);
  for (int y = 0; y < EPD_HEIGHT; y++) {
    for (int x = 0; x < EPD_WIDTH; x++) {
      int projection = x * dx + y * dy + phase;
      int stripePos = projection % period;
      if (stripePos < 0) {
        stripePos += period;
      }
      if (stripePos < STRIPE_PX * SCALE) {
        gfx.drawPixel(x, y, 1);
      }
    }
  }
}

void DeghostApp::drawNoiseClassic(Adafruit_GFX &gfx) {
  uint32_t rng = 0x9E3779B9u ^
                 (static_cast<uint32_t>(noiseFrame) * 0x85EBCA6Bu) ^
                 static_cast<uint32_t>(stepStartedAt);
  int pixelSize = 1 + static_cast<int>(noiseFrame / 4);
  gfx.fillScreen(0);
  for (int y = 0; y < EPD_HEIGHT; y += pixelSize) {
    for (int x = 0; x < EPD_WIDTH; x += pixelSize) {
      rng = rng * 1664525u + 1013904223u;
      if (rng & 1u) {
        gfx.fillRect(x, y, pixelSize, pixelSize, 1);
      }
    }
  }
}

void DeghostApp::drawNoiseSeeded(Adafruit_GFX &gfx, uint32_t salt) {
  uint32_t rng = static_cast<uint32_t>(micros());
  rng ^= static_cast<uint32_t>(millis()) << 11;
  rng ^= static_cast<uint32_t>(noiseFrame) * 0x9E3779B9u;
  rng ^= static_cast<uint32_t>(stepStartedAt);
  rng ^= salt;
  if (rng == 0) {
    rng = 0xA341316Cu;
  }
  int pixelSize = 1 + static_cast<int>(noiseFrame / 4);
  gfx.fillScreen(0);
  for (int y = 0; y < EPD_HEIGHT; y += pixelSize) {
    for (int x = 0; x < EPD_WIDTH; x += pixelSize) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      if (rng & 1u) {
        gfx.fillRect(x, y, pixelSize, pixelSize, 1);
      }
    }
  }
}
