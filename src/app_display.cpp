#include "app_display.h"

static custom_lcd_spi_t spiSettings = {
    .cs = EPD_CS_PIN,
    .dc = EPD_DC_PIN,
    .rst = EPD_RST_PIN,
    .busy = EPD_BUSY_PIN,
    .mosi = EPD_MOSI_PIN,
    .scl = EPD_SCK_PIN,
    .spi_host = EPD_SPI_HOST,
    .buffer_len = (EPD_WIDTH * EPD_HEIGHT) / 8};

AppDisplay::AppDisplay()
    : Adafruit_GFX(EPD_WIDTH, EPD_HEIGHT),
      driver(EPD_WIDTH, EPD_HEIGHT, spiSettings) {}

void AppDisplay::begin() {
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW);
  delay(50);
  driver.EPD_Init();
  clear();
  driver.EPD_Display();
  partialReady = false;
}

void AppDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
    return;
  }
  driver.EPD_DrawColorPixel(x, y,
                            color ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE);
}

void AppDisplay::clear() {
  driver.EPD_Clear();
  fillScreen(0);
}

void AppDisplay::flush() {
  driver.EPD_Init();
  driver.EPD_Display();
  partialReady = false;
}

void AppDisplay::flushPartial(int16_t, int16_t, int16_t, int16_t) {
  if (!partialReady) {
    driver.EPD_DisplayPartBaseImage();
    driver.EPD_Init_Partial();
    partialReady = true;
    return;
  }
  driver.EPD_DisplayPart();
}
