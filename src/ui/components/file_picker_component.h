#ifndef FILE_PICKER_COMPONENT_H
#define FILE_PICKER_COMPONENT_H

#include "sys/drivers/touch_input.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

class FilesApp;

/// System-owned file browser used by modal file-selection APIs.
///
/// The directory cache is allocated only while the picker is open. Keeping the
/// browser outside FilesApp prevents sandbox runtimes from owning or repeatedly
/// driving a second copy of file-navigation UI.
class FilePickerComponent {
public:
  enum class Result : uint8_t {
    Idle,
    Opening,
    Active,
    Selected,
    Cancelled,
    Error
  };

  bool request(const char *initialPath, const char *extensionFilter);
  void draw(Adafruit_GFX &gfx) const;
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handlePowerButton();
  bool update();
  void cancel();
  Result result() const;
  bool overlayActive() const;
  bool takePath(char *out, size_t outSize);
  void release();

private:
  static constexpr size_t kPathCapacity = 288;
  static constexpr size_t kFilterCapacity = 80;

  FilesApp *browser = nullptr;
  Result state = Result::Idle;
  char initialPath[kPathCapacity] = "/";
  char extensionFilter[kFilterCapacity] = {};
};

#endif
