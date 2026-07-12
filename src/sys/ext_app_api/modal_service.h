/// Coordinates shell-owned modal UI requested by sandboxed apps. It serializes
/// permission prompts, file picking, keyboard input, and date input so external
/// runtimes poll results without drawing or handling system widgets themselves.
#ifndef EXT_APP_API_MODAL_SERVICE_H
#define EXT_APP_API_MODAL_SERVICE_H

#include "sys/ext_app_api/service_result.h"
#include "sys/ext_app_api/permission_service.h"
#include "ui/components/date_time_picker_component.h"
#include "ui/components/file_picker_component.h"
#include "ui/components/t9_keyboard_component.h"

namespace ext_app_api {

/// State machine for all system UI that may temporarily cover an external app.
class ModalService {
public:
  /// Creates modal state tied to one app identity and permission store.
  ModalService(PermissionService &permissions, const char *appId);

  /// Activates pending permission UI; returns true when visual state changes.
  bool update();
  /// Reports whether a system modal currently owns input and drawing.
  bool active();
  /// Draws the active system-owned modal over the external app.
  void draw(Adafruit_GFX &gfx);
  /// Routes one touch to the active modal and reports whether it was consumed.
  bool handleTouch(const TouchPoint &point);
  /// Routes the menu button to the active modal.
  bool handleMenuButton();
  /// Routes the power/back button to the active modal.
  bool handlePowerButton();

  /// Opens or polls the system file picker for a filtered initial location.
  ServiceResult requestFilePicker(const char *initialPath,
                                  const char *extensionFilter);
  /// Consumes a completed picker selection into the caller's buffer.
  ServiceResult takePickedFile(char *out, size_t outSize);
  /// Opens or polls bounded keyboard input using the requested layout.
  ServiceResult keyboardInput(const char *initialText, uint8_t layout,
                              size_t maxLength, char *out, size_t outSize);
  /// Opens or polls the system date picker.
  ServiceResult dateInput(const DatePickerDate &initial, DatePickerDate &out);
  /// Closes UI and releases all modal state at the session boundary.
  void release();

private:
  enum class ActiveModal : uint8_t {
    None,
    Permission,
    FilePicker,
    Keyboard,
    Date
  };
  enum class InputResult : uint8_t { Idle, Active, Selected, Cancelled };

  struct KeyboardState {
    T9KeyboardComponent component;
    char text[256] = {};
    uint16_t maxLength = 255;
    InputResult result = InputResult::Idle;
  };

  struct DateState {
    DateTimePickerComponent component;
    InputResult result = InputResult::Idle;
  };

  PermissionService &permissions;
  FilePickerComponent filePicker;
  KeyboardState keyboard;
  DateState date;
  ActiveModal activeModal = ActiveModal::None;
  char appId[24] = {};

  void activatePermissionPrompt();
  bool canOpen(ActiveModal modal) const;
};

} // namespace ext_app_api

#endif
