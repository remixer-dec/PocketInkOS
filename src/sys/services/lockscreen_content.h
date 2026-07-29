/// Retained, low-memory content shared by app periodic hooks and the lockscreen.
#ifndef LOCKSCREEN_CONTENT_H
#define LOCKSCREEN_CONTENT_H

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

constexpr size_t LOCKSCREEN_WIDGET_VALUE_CAPACITY = 48;

/// Captures the configured widget before entering the periodic sleep cycle.
void lockscreenContentPrepareForSleep();
/// Clears the one-shot notification when the user returns to an interactive boot.
void lockscreenNotificationClear();

/// Updates an app widget only when that app is the selected lockscreen widget.
/// Returns false without copying values when the app is not selected.
bool lockscreenWidgetUpdate(const char *appId, const void *value,
                            size_t valueSize);

/// Replaces the single retained notification. Text is truncated to fit storage.
bool lockscreenNotify(const char *first, const char *second = nullptr);

/// Draws the notification or selected app widget in the centered 192x64 slot.
/// Returns true when content replaced the normal lock icon.
bool drawLockscreenContent(Adafruit_GFX &gfx);

/// Returns and clears the request made by a widget/notification update.
bool lockscreenContentTakeRedrawRequest();

#endif
