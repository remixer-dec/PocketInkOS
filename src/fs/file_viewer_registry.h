#ifndef FILE_VIEWER_REGISTRY_H
#define FILE_VIEWER_REGISTRY_H

#include <Adafruit_GFX.h>
#include "sys/touch_input.h"
#include <stddef.h>
#include <stdint.h>

enum class FileViewerOpenResult : uint8_t {
  Opened,
  NotImplemented,
  Unsupported,
  Failed,
};

struct FileViewerRequest {
  const char *providerId = nullptr;
  const char *path = nullptr;
  const char *extension = nullptr;
  struct FileViewerActivity *activity = nullptr;
};

typedef void (*FileViewerKeepAwakeHandler)(void *user);
typedef void (*FileViewerSleepGuardHandler)(void *user, bool active);

struct FileViewerActivity {
  void *user = nullptr;
  FileViewerKeepAwakeHandler keepAwake = nullptr;
  FileViewerSleepGuardHandler sleepGuard = nullptr;
};

struct FileViewerRuntime {
  const char *providerId = nullptr;
  const char *path = nullptr;
  const char *title = nullptr;
  uint32_t offset = 0;
  uint32_t size = 0;
  bool fullscreen = false;
  uint8_t imageDither = 1;
  bool imageScaleToFit = true;
  FileViewerActivity *activity = nullptr;
};

inline void fileViewerKeepAwake(FileViewerActivity *activity) {
  if (activity != nullptr && activity->keepAwake != nullptr) {
    activity->keepAwake(activity->user);
  }
}

class FileViewerScopedSleepGuard {
public:
  explicit FileViewerScopedSleepGuard(FileViewerActivity *activity)
      : activity(activity),
        active(activity != nullptr && activity->sleepGuard != nullptr) {
    if (active) {
      activity->sleepGuard(activity->user, true);
    } else {
      fileViewerKeepAwake(activity);
    }
  }

  ~FileViewerScopedSleepGuard() {
    if (active) {
      activity->sleepGuard(activity->user, false);
    } else {
      fileViewerKeepAwake(activity);
    }
  }

  FileViewerScopedSleepGuard(const FileViewerScopedSleepGuard &) = delete;
  FileViewerScopedSleepGuard &
  operator=(const FileViewerScopedSleepGuard &) = delete;

private:
  FileViewerActivity *activity;
  bool active;
};

typedef FileViewerOpenResult (*FileViewerOpenHandler)(
    const FileViewerRequest &request);
typedef void (*FileViewerDrawHandler)(Adafruit_GFX &gfx,
                                      const FileViewerRuntime &runtime);
typedef void (*FileViewerScrollHandler)(FileViewerRuntime &runtime,
                                        int8_t lines);
typedef bool (*FileViewerTouchHandler)(FileViewerRuntime &runtime,
                                       const TouchPoint &point);
typedef bool (*FileViewerUpdateHandler)(FileViewerRuntime &runtime);
typedef uint32_t (*FileViewerVisibleBytesHandler)(
    const FileViewerRuntime &runtime);

struct FileViewerExtension {
  const char *id = nullptr;
  const char *label = nullptr;
  const char *const *extensions = nullptr;
  uint8_t extensionCount = 0;
  FileViewerOpenHandler open = nullptr;
  FileViewerDrawHandler draw = nullptr;
  FileViewerScrollHandler scroll = nullptr;
  FileViewerTouchHandler touch = nullptr;
  FileViewerUpdateHandler update = nullptr;
  FileViewerVisibleBytesHandler visibleBytes = nullptr;
};

const FileViewerExtension *findFileViewerForPath(const char *path);
FileViewerOpenResult openFileWithViewer(const char *providerId,
                                        const char *path,
                                        const FileViewerExtension **viewer,
                                        FileViewerActivity *activity = nullptr);

#endif
