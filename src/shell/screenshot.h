/// Exports the e-paper display's live one-bit framebuffer as a timestamped BMP
/// on the SD card. This is the firmware screenshot boundary used by shell UI,
/// including stable result codes suitable for short on-device messages.
#ifndef SHELL_SCREENSHOT_H
#define SHELL_SCREENSHOT_H

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
