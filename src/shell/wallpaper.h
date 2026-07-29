/// Canonical home and lockscreen wallpaper storage.
#ifndef SHELL_WALLPAPER_H
#define SHELL_WALLPAPER_H

#include <stdint.h>

class AppDisplay;

enum class WallpaperTarget : uint8_t {
  Home,
  Lockscreen,
};

enum class WallpaperResult : uint8_t {
  Saved,
  NoSdCard,
  NoFramebuffer,
  WriteFailed,
};

constexpr const char *WALLPAPER_HOME_PATH = "/misc/wallpaper_home.bmp";
constexpr const char *WALLPAPER_LOCKSCREEN_PATH = "/misc/wallpaper_ls.bmp";

/// Saves the current 200x200 display buffer as the selected wallpaper.
WallpaperResult saveFramebufferWallpaper(const AppDisplay &display,
                                          WallpaperTarget target);
/// Draws a wallpaper only when SD is already mounted; it never mounts storage.
bool drawWallpaper(AppDisplay &display, WallpaperTarget target);
const char *wallpaperResultMessage(WallpaperResult result);

#endif
