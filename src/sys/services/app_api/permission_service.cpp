#include "sys/services/app_api/permission_service.h"

#include "sys/global.h"
#include "sys/psram_allocator.h"
#include "sys/services/app_api/service_utils.h"
#include "ui/ui_helpers.h"

#include <cstring>
#include <new>

namespace app_api {
namespace {

const char *permissionTitle(PermissionKind kind) {
  switch (kind) {
  case PermissionKind::FileWrite:
    return "WRITE FILE?";
  case PermissionKind::FileDelete:
    return "DELETE FILE?";
  case PermissionKind::Gpio:
    return "ALLOW GPIO?";
  case PermissionKind::WifiScan:
    return "SCAN WI-FI?";
  }
  return "ALLOW?";
}

void drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y,
                  uint8_t size = 1) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.setTextSize(size);
  gfx.getTextBounds(text != nullptr ? text : "", 0, 0, &x1, &y1, &width,
                    &height);
  gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(width)) / 2 - x1, y);
  gfx.print(text != nullptr ? text : "");
}

} // namespace

const PermissionService::Record *
PermissionService::find(PermissionKind kind, const char *resource) const {
  for (size_t i = 0; i < recordCount; i++) {
    if (records[i].kind == kind && strcmp(records[i].resource, resource) == 0) {
      return &records[i];
    }
  }
  if (hasEmergencyRecord && emergencyRecord.kind == kind &&
      strcmp(emergencyRecord.resource, resource) == 0) {
    return &emergencyRecord;
  }
  return nullptr;
}

ServiceResult PermissionService::request(PermissionKind kind,
                                         const char *resource) {
  const char *key = resource != nullptr ? resource : "";
  const Record *record = find(kind, key);
  if (record != nullptr) {
    return record->decision == PermissionDecision::Granted
               ? ServiceResult::ok()
               : ServiceResult::fail(ServiceError::Denied,
                                     "permission denied");
  }
  if (storageExhausted) {
    return ServiceResult::fail(ServiceError::Denied,
                               "permission storage exhausted");
  }
  if (!hasPrompt) {
    prompt.kind = kind;
    copyText(prompt.resource, sizeof(prompt.resource), key);
    hasPrompt = true;
  }
  return ServiceResult::pending();
}

bool PermissionService::promptActive() const { return hasPrompt; }

bool PermissionService::ensureCapacity() {
  if (storageExhausted || recordCount < recordCapacity) {
    return !storageExhausted;
  }
  const size_t nextCapacity = recordCapacity == 0 ? 4 : recordCapacity * 2U;
  if (nextCapacity < recordCapacity ||
      nextCapacity > SIZE_MAX / sizeof(Record)) {
    return false;
  }
  Record *next = static_cast<Record *>(
      psram::allocate(nextCapacity * sizeof(Record)));
  if (next == nullptr) {
    return false;
  }
  for (size_t i = 0; i < nextCapacity; i++) {
    new (&next[i]) Record();
  }
  for (size_t i = 0; i < recordCount; i++) {
    next[i] = records[i];
  }
  for (size_t i = 0; i < recordCapacity; i++) {
    records[i].~Record();
  }
  psram::release(records);
  records = next;
  recordCapacity = nextCapacity;
  return true;
}

void PermissionService::resolve(PermissionDecision decision) {
  if (!hasPrompt) {
    return;
  }
  Record *record = nullptr;
  if (ensureCapacity()) {
    record = &records[recordCount++];
  } else {
    storageExhausted = true;
    emergencyRecord = {};
    hasEmergencyRecord = true;
    record = &emergencyRecord;
  }
  record->kind = prompt.kind;
  record->decision = decision;
  copyText(record->resource, sizeof(record->resource), prompt.resource);
  prompt = {};
  hasPrompt = false;
}

void PermissionService::drawPrompt(Adafruit_GFX &gfx,
                                   const char *appId) const {
  if (!hasPrompt) {
    return;
  }
  gfx.fillRect(8, 42, 184, 116, 0);
  gfx.drawRect(8, 42, 184, 116, 1);
  gfx.setTextColor(1);
  drawCentered(gfx, permissionTitle(prompt.kind), 58, 2);
  if (prompt.kind == PermissionKind::FileWrite ||
      prompt.kind == PermissionKind::FileDelete) {
    const char *name = strrchr(prompt.resource, '/');
    name = name != nullptr ? name + 1 : prompt.resource;
    const size_t length = strlen(name);
    drawCentered(gfx, length > 28 ? name + length - 28 : name, 92);
  } else {
    drawCentered(gfx, appId, 92);
  }
  uiDrawButton(gfx, {24, 120, 68, 26}, "ALLOW");
  uiDrawButton(gfx, {108, 120, 68, 26}, "DENY");
}

bool PermissionService::handlePromptTouch(const TouchPoint &point) {
  if (!hasPrompt) {
    return false;
  }
  if (uiContains({24, 120, 68, 26}, point)) {
    resolve(PermissionDecision::Granted);
  } else if (uiContains({108, 120, 68, 26}, point)) {
    resolve(PermissionDecision::Denied);
  }
  return true;
}

void PermissionService::release() {
  for (size_t i = 0; i < recordCapacity; i++) {
    records[i].~Record();
  }
  psram::release(records);
  records = nullptr;
  recordCount = 0;
  recordCapacity = 0;
  emergencyRecord = {};
  prompt = {};
  hasEmergencyRecord = false;
  storageExhausted = false;
  hasPrompt = false;
}

} // namespace app_api
