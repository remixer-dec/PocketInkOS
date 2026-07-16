#include "sys/app_display.h"

#include "esp_heap_caps.h"

#include <cstring>

namespace {

constexpr uint32_t kRefreshTaskStackBytes = 4096;
constexpr UBaseType_t kRefreshTaskPriority = 1;
constexpr BaseType_t kRefreshTaskCore = 1;

} // namespace

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
  waitForRefresh();
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW);
  delay(50);
  driver.EPD_Init();
  partialReady = false;
  fullRefreshRequested = false;
}

void AppDisplay::beginRetainedPartial() {
  waitForRefresh();
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW);
  delay(10);
  driver.EPD_ReattachPartial();
  partialReady = true;
  fullRefreshRequested = false;
}

void AppDisplay::beginColdPartial() {
  waitForRefresh();
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW);
  delay(50);
  driver.EPD_InitColdPartial();
  partialReady = true;
  fullRefreshRequested = false;
}

void AppDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
    return;
  }
  if (color != 0U) {
    driver.EPD_DrawBlackPixelUnchecked(x, y);
    return;
  }
  driver.EPD_DrawColorPixel(x, y,
                            DRIVER_COLOR_WHITE);
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

void AppDisplay::seedPartialBothImages() {
  waitForRefresh();
  driver.EPD_LoadPartBothImages();
}

void AppDisplay::requestFullRefresh() {
  fullRefreshRequested.store(true, std::memory_order_release);
}

void AppDisplay::flush() {
  waitForRefresh();
  const bool needsFull =
      fullRefreshRequested.exchange(false, std::memory_order_acq_rel) ||
      !partialReady.load(std::memory_order_acquire);
  if (!needsFull) {
    driver.EPD_DisplayPart();
    return;
  }

  driver.EPD_Init();
  driver.EPD_Display();
  driver.EPD_LoadPartBaseImage();
  driver.EPD_Init_Partial();
  partialReady = true;
}

void AppDisplay::flushPartial() {
  waitForRefresh();
  if (fullRefreshRequested.load(std::memory_order_acquire) ||
      !partialReady.load(std::memory_order_acquire)) {
    flush();
    return;
  }

  driver.EPD_DisplayPart();
}

bool AppDisplay::flushAsync() { return startAsyncRefresh(); }

bool AppDisplay::flushPartialAsync() { return startAsyncRefresh(); }

bool AppDisplay::ready() const {
  return !refreshBusy.load(std::memory_order_acquire);
}

void AppDisplay::refreshTaskMain(void *context) {
  static_cast<AppDisplay *>(context)->refreshLoop();
}

void AppDisplay::refreshLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (pendingRefresh == AsyncRefresh::Partial) {
      driver.EPD_DisplayPart(refreshFrame);
    } else {
      driver.EPD_Init();
      driver.EPD_Display(refreshFrame);
      driver.EPD_LoadPartBaseImage(refreshFrame);
      driver.EPD_Init_Partial();
      partialReady.store(true, std::memory_order_release);
    }
    refreshBusy.store(false, std::memory_order_release);
  }
}

bool AppDisplay::ensureRefreshWorker() {
  if (refreshTask != nullptr) {
    return true;
  }
  if (refreshFrame == nullptr) {
    refreshFrame = static_cast<uint8_t *>(heap_caps_malloc(
        framebufferSize(), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA |
                               MALLOC_CAP_8BIT));
    if (refreshFrame == nullptr) {
      return false;
    }
  }
  if (xTaskCreatePinnedToCore(refreshTaskMain, "epaper-refresh",
                              kRefreshTaskStackBytes, this,
                              kRefreshTaskPriority, &refreshTask,
                              kRefreshTaskCore) != pdPASS) {
    refreshTask = nullptr;
    heap_caps_free(refreshFrame);
    refreshFrame = nullptr;
    return false;
  }
  return true;
}

bool AppDisplay::startAsyncRefresh() {
  if (!ready() || !ensureRefreshWorker()) {
    return false;
  }
  const uint8_t *frame = framebufferData();
  const size_t bytes = framebufferSize();
  if (frame == nullptr || bytes == 0U) {
    return false;
  }
  memcpy(refreshFrame, frame, bytes);
  const bool needsFull =
      fullRefreshRequested.exchange(false, std::memory_order_acq_rel) ||
      !partialReady.load(std::memory_order_acquire);
  pendingRefresh = needsFull ? AsyncRefresh::Full : AsyncRefresh::Partial;
  refreshBusy.store(true, std::memory_order_release);
  xTaskNotifyGive(refreshTask);
  return true;
}

void AppDisplay::waitForRefresh() {
  while (!ready()) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void AppDisplay::lock() {
  while (locked.test_and_set(std::memory_order_acquire)) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void AppDisplay::unlock() { locked.clear(std::memory_order_release); }

const uint8_t *AppDisplay::framebufferData() const {
  return driver.framebufferData();
}

size_t AppDisplay::framebufferSize() const { return driver.framebufferSize(); }
