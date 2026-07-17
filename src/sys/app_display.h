#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "sys/drivers/epaper_driver_bsp.h"
#include "sys/global.h"
#include <Adafruit_GFX.h>
#include <atomic>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/// Adafruit_GFX-backed display wrapper for full and partial e-paper updates.
class AppDisplay : public Adafruit_GFX {
public:
  AppDisplay();
  void begin();
  void beginColdPartial();
  void beginRetainedPartial();
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void drawBlackPixelUnchecked(int16_t x, int16_t y);
  void drawBlackBlock2x2Unchecked(int16_t x, int16_t y);
  /// Globally swaps logical black and white for subsequent rendering.
  void setInverted(bool enabled);
  void toggleInverted();
  bool isInverted() const;
  void flush();
  /// Transfers the full framebuffer using the panel's partial-refresh waveform.
  void flushPartial();
  /// Starts a refresh on a worker and returns after snapshotting the frame.
  bool flushAsync();
  /// Starts a partial refresh on a worker and returns after snapshotting.
  bool flushPartialAsync();
  /// True when another frame may be rendered and submitted.
  bool ready() const;
  void clear();
  void seedPartialBothImages();
  void requestFullRefresh();
  void lock();
  void unlock();
  /// Provides read-only access to the current one-bit framebuffer.
  /// Callers that need a stable frame must hold the display lock.
  const uint8_t *framebufferData() const;
  /// Returns the number of bytes in framebufferData().
  size_t framebufferSize() const;

private:
  enum class AsyncRefresh : uint8_t { Full, Partial };

  epaper_driver_display driver;
  std::atomic<bool> partialReady{false};
  std::atomic<bool> fullRefreshRequested{false};
  std::atomic<bool> refreshBusy{false};
  std::atomic_flag locked = ATOMIC_FLAG_INIT;
  bool inverted = false;
  uint8_t *refreshFrame = nullptr;
  TaskHandle_t refreshTask = nullptr;
  AsyncRefresh pendingRefresh = AsyncRefresh::Full;

  static void refreshTaskMain(void *context);
  void refreshLoop();
  bool ensureRefreshWorker();
  bool startAsyncRefresh();
  void waitForRefresh();
};

#endif
