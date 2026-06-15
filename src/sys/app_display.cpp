#include "sys/app_display.h"

static custom_lcd_spi_t spiSettings = {.cs = EPD_CS_PIN,
                                       .dc = EPD_DC_PIN,
                                       .rst = EPD_RST_PIN,
                                       .busy = EPD_BUSY_PIN,
                                       .mosi = EPD_MOSI_PIN,
                                       .scl = EPD_SCK_PIN,
                                       .spi_host = EPD_SPI_HOST,
                                       .buffer_len =
                                           (EPD_WIDTH * EPD_HEIGHT) / 8};

AppDisplay::AppDisplay()
    : Adafruit_GFX(EPD_WIDTH, EPD_HEIGHT),
      driver(EPD_WIDTH, EPD_HEIGHT, spiSettings) {}

void AppDisplay::begin() {
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW);
  delay(50);
  driver.EPD_Init();
  partialReady = false;
}

void AppDisplay::beginRetainedPartial() {
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW);
  delay(10);
  driver.EPD_ReattachPartial();
  partialReady = true;
}

void AppDisplay::beginColdPartial() {
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW);
  delay(50);
  driver.EPD_InitColdPartial();
  partialReady = true;
}

void AppDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
    return;
  }
  driver.EPD_DrawColorPixel(x, y,
                            color ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE);
}

// Fast path for hot renderers that have already clipped coordinates and only
// need to set black pixels. Use normal drawPixel() for general UI rendering,
// white pixels, or any caller that cannot prove the coordinates are in bounds.
void AppDisplay::drawBlackPixelUnchecked(int16_t x, int16_t y) {
  driver.EPD_DrawBlackPixelUnchecked(x, y);
}

// Same contract as drawBlackPixelUnchecked(), specialized for dense 2x2 shader
// writes. The full block must be in bounds before calling.
void AppDisplay::drawBlackBlock2x2Unchecked(int16_t x, int16_t y) {
  driver.EPD_DrawBlackBlock2x2Unchecked(x, y);
}

void AppDisplay::clear() {
  driver.EPD_Clear();
  fillScreen(0);
}

void AppDisplay::seedPartialBothImages() { driver.EPD_LoadPartBothImages(); }

void AppDisplay::requestFullRefresh() { partialReady = false; }

void AppDisplay::flush() {
  if (partialReady) {
    driver.EPD_DisplayPart();
    return;
  }

  driver.EPD_Init();
  driver.EPD_Display();
  driver.EPD_LoadPartBaseImage();
  driver.EPD_Init_Partial();
  partialReady = true;
}

void AppDisplay::flushPartial(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!partialReady) {
    flush();
    return;
  }

  (void)x;
  (void)y;
  (void)w;
  (void)h;
  driver.EPD_DisplayPart();
}

void AppDisplay::lock() {
  while (__sync_lock_test_and_set(&locked, true)) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void AppDisplay::unlock() {
  __sync_lock_release(&locked);
}
