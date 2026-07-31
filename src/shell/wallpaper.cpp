#include "shell/wallpaper.h"

#include "shell/screenshot.h"
#include "sys/app_display.h"
#include "sys/global.h"
#include "sys/services/sd_storage.h"

#include <SD_MMC.h>

namespace {

const char *pathFor(WallpaperTarget target) {
  return target == WallpaperTarget::Home ? WALLPAPER_HOME_PATH
                                         : WALLPAPER_LOCKSCREEN_PATH;
}

} // namespace

WallpaperResult saveFramebufferWallpaper(const AppDisplay &display,
                                          WallpaperTarget target) {
  if (!sdStorageMounted()) {
    return WallpaperResult::NoSdCard;
  }
  const uint8_t *framebuffer = display.framebufferData();
  constexpr size_t rowBytes = EPD_WIDTH / 8U;
  if (framebuffer == nullptr ||
      display.framebufferSize() < rowBytes * EPD_HEIGHT) {
    return WallpaperResult::NoFramebuffer;
  }

  SD_MMC.mkdir("/misc");
  const char *path = pathFor(target);
  SD_MMC.remove(path);
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    return WallpaperResult::WriteFailed;
  }
  const bool written = writeMonochromeBmp(file, framebuffer, EPD_WIDTH,
                                          EPD_HEIGHT, rowBytes);
  file.close();
  return written ? WallpaperResult::Saved : WallpaperResult::WriteFailed;
}

WallpaperResult removeWallpaper(WallpaperTarget target) {
  if (!sdStorageMounted()) {
    return WallpaperResult::NoSdCard;
  }
  const char *path = pathFor(target);
  if (!SD_MMC.exists(path)) {
    return WallpaperResult::NotFound;
  }
  return SD_MMC.remove(path) ? WallpaperResult::Removed
                             : WallpaperResult::WriteFailed;
}

bool drawWallpaper(AppDisplay &display, WallpaperTarget target) {
  if (!sdStorageMounted()) {
    return false;
  }
  File file = SD_MMC.open(pathFor(target));
  if (!file) {
    return false;
  }
  const bool drawn = drawMonochromeBmp(display, file);
  file.close();
  return drawn;
}

const char *wallpaperResultMessage(WallpaperResult result) {
  switch (result) {
  case WallpaperResult::Saved:
    return "WALLPAPER SAVED";
  case WallpaperResult::Removed:
    return "WALLPAPER REMOVED";
  case WallpaperResult::NotFound:
    return "NO WALLPAPER";
  case WallpaperResult::NoSdCard:
    return "NO SD CARD";
  case WallpaperResult::NoFramebuffer:
  case WallpaperResult::WriteFailed:
    return "SAVE FAILED";
  }
  return "SAVE FAILED";
}
