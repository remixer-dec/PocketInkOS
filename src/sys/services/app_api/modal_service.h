#ifndef APP_API_MODAL_SERVICE_H
#define APP_API_MODAL_SERVICE_H

#include "sys/services/app_api/service_result.h"
#include "sys/services/app_api/permission_service.h"
#include "ui/components/date_time_picker_component.h"
#include "ui/components/file_picker_component.h"
#include "ui/components/t9_keyboard_component.h"

namespace app_api {

class ModalService {
public:
  ModalService(PermissionService &permissions, const char *appId);

  bool update();
  bool active();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handlePowerButton();

  ServiceResult requestFilePicker(const char *initialPath,
                                  const char *extensionFilter);
  ServiceResult takePickedFile(char *out, size_t outSize);
  ServiceResult keyboardInput(const char *initialText, uint8_t layout,
                              size_t maxLength, char *out, size_t outSize);
  ServiceResult dateInput(const DatePickerDate &initial, DatePickerDate &out);
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

} // namespace app_api

#endif
