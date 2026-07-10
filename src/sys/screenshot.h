#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <stdint.h>

class AppDisplay;

/// Result of exporting the current display buffer to the SD card.
enum class ScreenshotResult : uint8_t {
  Saved,
  NoSdCard,
  NoFramebuffer,
  WriteFailed,
};

/// Writes the current one-bit framebuffer as a BMP under /screenshots.
/// The caller must keep the display framebuffer stable for the duration.
ScreenshotResult saveFramebufferScreenshot(const AppDisplay &display);
/// Provides a short UI-safe message for an unsuccessful screenshot export.
const char *screenshotResultMessage(ScreenshotResult result);

#endif
