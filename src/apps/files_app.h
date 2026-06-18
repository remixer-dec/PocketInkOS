#ifndef FILES_APP_H
#define FILES_APP_H

#include "fs/file_viewer_registry.h"
#include "fs/file_provider.h"
#include "sys/touch_input.h"
#include "ui/components/t9_keyboard_component.h"
#include <Adafruit_GFX.h>
#include <FS.h>
#include <stddef.h>
#include <stdint.h>

static const uint16_t FILES_PATH_CAPACITY = 288;
static const uint16_t FILES_NAME_CAPACITY = 256;
static const uint16_t FILES_LABEL_CAPACITY = 96;
static const uint16_t FILES_NAME_POOL_CAPACITY = 16 * 1024;

class FilesApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handlePowerButton();
  bool update();
  bool hasActiveSession() const;
  bool consumeDirtyRegion(int16_t *x, int16_t *y, int16_t *w, int16_t *h);
  size_t saveContext(uint8_t *buffer, size_t capacity) const;
  void restoreContext(const uint8_t *buffer, size_t length);

private:
  enum class Mode : uint8_t { Scroll = 0, Select = 1 };
  enum class DirectoryScanPass : uint8_t { Folders = 0, Files = 1 };

  static const int16_t NO_SELECTION = -1;

  struct FileEntry {
    uint16_t nameOffset = 0;
    uint32_t size = 0;
    bool directory = false;
    bool nameComplete = true;
    char icon = 'I';
  };

  static const uint8_t MAX_VISIBLE_ENTRIES = 8;
  static const uint16_t MAX_CACHED_ENTRIES = 128;

  char currentPath[FILES_PATH_CAPACITY] = "/";
  char statusText[28] = {};
  FileEntry entries[MAX_CACHED_ENTRIES] = {};
  char entryNamePool[FILES_NAME_POOL_CAPACITY] = {};
  uint16_t entryCount = 0;
  uint16_t folderCount = 0;
  uint16_t entryNamePoolUsed = 0;
  uint16_t scrollOffset = 0;
  int16_t selectedIndex = NO_SELECTION;
  uint32_t viewerOffset = 0;
  uint32_t viewerSize = 0;
  const FileViewerExtension *activeViewer = nullptr;
  bool viewerFullscreen = false;
  bool viewerOptionsOpen = false;
  bool viewerScaleToFit = true;
  bool viewerPageJumpOpen = false;
  uint8_t viewerDither = 1;
  String viewerPageInput;
  T9KeyboardComponent viewerPageKeyboard;
  unsigned long nextPdfProgressRedrawMs = 0;
  unsigned long statusUntil = 0;
  Mode mode = Mode::Scroll;
  bool loaded = false;
  bool cardMounted = false;
  bool truncatedDirectory = false;
  bool directoryLoading = false;
  DirectoryScanPass directoryScanPass = DirectoryScanPass::Folders;
  bool storageSelected = false;
  bool hasDirtyRegion = false;
  int16_t dirtyX = 0;
  int16_t dirtyY = 0;
  int16_t dirtyW = 0;
  int16_t dirtyH = 0;
  char providerId[12] = {};
  char viewerPath[FILES_PATH_CAPACITY] = {};
  char viewerTitle[FILES_LABEL_CAPACITY] = {};
  File directoryScanRoot;

  void loadDirectory();
  bool loadDirectoryFast(const FileProvider *provider);
  void finishDirectoryLoad();
  bool continueDirectoryLoad(uint8_t budget);
  static bool cacheProviderFolderEntry(const FileProviderEntry &entry,
                                       void *context);
  static bool cacheProviderFileEntry(const FileProviderEntry &entry,
                                     void *context);
  bool cacheDirectoryEntry(const char *name, bool directory, uint32_t size);
  int compareEntryNames(const char *left, const char *right) const;
  uint16_t sortedInsertIndex(const char *name, bool directory) const;
  const char *entryName(const FileEntry &entry) const;
  void openDirectory(const char *path);
  void goUp();
  void setStatus(const char *text);
  void setTransientStatus(const char *text);
  bool clearExpiredStatus();
  void scrollBy(int8_t delta);
  bool activateIndex(uint16_t index);
  bool activateStorage();
  bool openViewer(const char *path, const char *title, const char *viewerId);
  bool providerMounted() const;
  void closeViewer();
  FileViewerActivity viewerActivity() const;
  FileViewerRuntime makeViewerRuntime(bool fullscreen,
                                      FileViewerActivity *activity) const;
  void scrollViewerBy(int8_t lines);
  bool handleViewerTouch(const TouchPoint &point);
  bool handleViewerPageJumpTouch(const TouchPoint &point);
  void clampView();
  void drawViewer(Adafruit_GFX &gfx);
  void drawViewerOptions(Adafruit_GFX &gfx);
  void drawScrollbar(Adafruit_GFX &gfx) const;
  void drawViewerScrollbar(Adafruit_GFX &gfx) const;
  void drawStoragePicker(Adafruit_GFX &gfx);
  void markDirtyRegion(int16_t x, int16_t y, int16_t w, int16_t h);
  void markListDirty();
  const char *displayName(const char *path) const;
};

#endif
