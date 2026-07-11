#include "sys/services/app_api/modal_service.h"

#include "sys/global.h"
#include "sys/services/app_api/service_utils.h"

#include <cstring>

namespace app_api {
namespace {

void drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.setTextSize(1);
  gfx.getTextBounds(text != nullptr ? text : "", 0, 0, &x1, &y1, &width,
                    &height);
  gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(width)) / 2 - x1, y);
  gfx.print(text != nullptr ? text : "");
}

bool validDate(const DatePickerDate &date) {
  return date.year >= 1 && date.year <= 9999 && date.month >= 1 &&
         date.month <= 12 && date.day >= 1 &&
         date.day <= DateTimePickerComponent::daysInMonth(date.year,
                                                          date.month);
}

} // namespace

ModalService::ModalService(PermissionService &permissions, const char *appId)
    : permissions(permissions) {
  copyText(this->appId, sizeof(this->appId), appId);
}

void ModalService::activatePermissionPrompt() {
  if (activeModal == ActiveModal::None && permissions.promptActive()) {
    activeModal = ActiveModal::Permission;
  }
}

bool ModalService::canOpen(ActiveModal modal) const {
  return activeModal == ActiveModal::None || activeModal == modal;
}

bool ModalService::update() {
  activatePermissionPrompt();
  switch (activeModal) {
  case ActiveModal::FilePicker:
    return filePicker.update();
  case ActiveModal::Keyboard:
    return keyboard.component.update();
  case ActiveModal::Date:
    return date.component.update();
  case ActiveModal::None:
  case ActiveModal::Permission:
    return false;
  }
  return false;
}

bool ModalService::active() {
  activatePermissionPrompt();
  switch (activeModal) {
  case ActiveModal::Permission:
    return permissions.promptActive();
  case ActiveModal::FilePicker:
    return filePicker.overlayActive();
  case ActiveModal::Keyboard:
    return keyboard.result == InputResult::Active;
  case ActiveModal::Date:
    return date.result == InputResult::Active;
  case ActiveModal::None:
    return false;
  }
  return false;
}

void ModalService::draw(Adafruit_GFX &gfx) {
  activatePermissionPrompt();
  switch (activeModal) {
  case ActiveModal::Permission:
    permissions.drawPrompt(gfx, appId);
    break;
  case ActiveModal::FilePicker:
    filePicker.draw(gfx);
    break;
  case ActiveModal::Keyboard:
    gfx.fillScreen(0);
    keyboard.component.draw(gfx, keyboard.text, keyboard.maxLength);
    break;
  case ActiveModal::Date:
    gfx.fillScreen(0);
    date.component.draw(gfx);
    gfx.fillRect(0, 184, EPD_WIDTH, 16, 0);
    drawCentered(gfx, "Tap a date to pick", 190);
    break;
  case ActiveModal::None:
    break;
  }
}

bool ModalService::handleTouch(const TouchPoint &point) {
  activatePermissionPrompt();
  switch (activeModal) {
  case ActiveModal::Permission:
    permissions.handlePromptTouch(point);
    if (!permissions.promptActive()) {
      activeModal = ActiveModal::None;
    }
    return true;
  case ActiveModal::FilePicker:
    return filePicker.handleTouch(point);
  case ActiveModal::Keyboard: {
    const KeyboardEvent event = keyboard.component.hitTest(
        point, keyboard.text, static_cast<int>(keyboard.maxLength) + 1);
    if (event.action == KEY_OK) {
      keyboard.result = InputResult::Selected;
      activeModal = ActiveModal::None;
    }
    return true;
  }
  case ActiveModal::Date: {
    DatePickerDate picked;
    const bool pickedDate = date.component.dateAtPoint(point, picked);
    date.component.handleTouch(point);
    if (pickedDate) {
      date.result = InputResult::Selected;
      activeModal = ActiveModal::None;
    }
    return true;
  }
  case ActiveModal::None:
    return false;
  }
  return false;
}

bool ModalService::handleMenuButton() {
  if (activeModal == ActiveModal::FilePicker) {
    return filePicker.handleMenuButton();
  }
  if (activeModal == ActiveModal::Keyboard) {
    keyboard.component.toggleCaps();
    return true;
  }
  return false;
}

bool ModalService::handlePowerButton() {
  if (activeModal == ActiveModal::Permission) {
    return false;
  }
  if (activeModal == ActiveModal::FilePicker) {
    return filePicker.handlePowerButton();
  }
  if (activeModal == ActiveModal::Keyboard) {
    keyboard.result = InputResult::Cancelled;
    activeModal = ActiveModal::None;
    return true;
  }
  if (activeModal == ActiveModal::Date) {
    date.result = InputResult::Cancelled;
    activeModal = ActiveModal::None;
    return true;
  }
  return false;
}

ServiceResult ModalService::requestFilePicker(const char *initialPath,
                                              const char *extensionFilter) {
  const FilePickerComponent::Result result = filePicker.result();
  if (result == FilePickerComponent::Result::Selected) {
    return ServiceResult::ok();
  }
  if (result == FilePickerComponent::Result::Cancelled) {
    return ServiceResult::fail(ServiceError::Denied, "file picker cancelled");
  }
  if (result == FilePickerComponent::Result::Opening ||
      result == FilePickerComponent::Result::Active) {
    return ServiceResult::pending();
  }
  if (!canOpen(ActiveModal::FilePicker)) {
    return ServiceResult::fail(ServiceError::Busy, "another modal is active");
  }
  if (result == FilePickerComponent::Result::Error) {
    filePicker.release();
  }
  if (!filePicker.request(initialPath, extensionFilter)) {
    return ServiceResult::fail(ServiceError::Io, "file picker failed to open");
  }
  activeModal = ActiveModal::FilePicker;
  return ServiceResult::pending();
}

ServiceResult ModalService::takePickedFile(char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "missing file path output");
  }
  const FilePickerComponent::Result result = filePicker.result();
  if (result == FilePickerComponent::Result::Opening ||
      result == FilePickerComponent::Result::Active ||
      result == FilePickerComponent::Result::Idle) {
    return ServiceResult::pending();
  }
  activeModal = ActiveModal::None;
  if (result == FilePickerComponent::Result::Cancelled) {
    filePicker.release();
    return ServiceResult::fail(ServiceError::Denied, "file picker cancelled");
  }
  if (result != FilePickerComponent::Result::Selected ||
      !filePicker.takePath(out, outSize)) {
    return ServiceResult::fail(ServiceError::Io,
                               "picked path could not be copied");
  }
  filePicker.release();
  return ServiceResult::ok();
}

ServiceResult ModalService::keyboardInput(const char *initialText,
                                          uint8_t layout, size_t maxLength,
                                          char *out, size_t outSize) {
  if (out == nullptr || outSize == 0 || layout > 2 || maxLength == 0 ||
      maxLength > 255) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid keyboard request");
  }
  if (keyboard.result == InputResult::Selected) {
    const bool copied = copyText(out, outSize, keyboard.text);
    keyboard = {};
    return copied ? ServiceResult::ok()
                  : ServiceResult::fail(ServiceError::InvalidArgument,
                                        "keyboard output too small");
  }
  if (keyboard.result == InputResult::Cancelled) {
    keyboard = {};
    return ServiceResult::fail(ServiceError::Denied, "keyboard cancelled");
  }
  if (keyboard.result == InputResult::Active) {
    return ServiceResult::pending();
  }
  if (!canOpen(ActiveModal::Keyboard)) {
    return ServiceResult::fail(ServiceError::Busy, "another modal is active");
  }
  if (!copyText(keyboard.text, sizeof(keyboard.text), initialText) ||
      strlen(keyboard.text) > maxLength) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid initial keyboard text");
  }
  keyboard.maxLength = static_cast<uint16_t>(maxLength);
  keyboard.component.setLayout(
      layout == 1 ? T9KeyboardComponent::Layout::Numbers
                  : layout == 2 ? T9KeyboardComponent::Layout::Numeric
                                : T9KeyboardComponent::Layout::Text);
  keyboard.result = InputResult::Active;
  activeModal = ActiveModal::Keyboard;
  return ServiceResult::pending();
}

ServiceResult ModalService::dateInput(const DatePickerDate &initial,
                                      DatePickerDate &out) {
  if (!validDate(initial)) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid initial date");
  }
  if (date.result == InputResult::Selected) {
    out = date.component.selectedDate();
    date = {};
    return ServiceResult::ok();
  }
  if (date.result == InputResult::Cancelled) {
    date = {};
    return ServiceResult::fail(ServiceError::Denied, "date picker cancelled");
  }
  if (date.result == InputResult::Active) {
    return ServiceResult::pending();
  }
  if (!canOpen(ActiveModal::Date)) {
    return ServiceResult::fail(ServiceError::Busy, "another modal is active");
  }
  date.component.reset(initial);
  date.component.setTimePickerEnabled(false);
  date.result = InputResult::Active;
  activeModal = ActiveModal::Date;
  return ServiceResult::pending();
}

void ModalService::release() {
  filePicker.release();
  keyboard = {};
  date = {};
  activeModal = ActiveModal::None;
}

} // namespace app_api
