#ifndef DATE_TIME_PICKER_COMPONENT_H
#define DATE_TIME_PICKER_COMPONENT_H

#include "sys/touch_input.h"
#include "ui/components/t9_keyboard_component.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

/// Calendar date selected by the date picker.
struct DatePickerDate {
  int year = 1970;
  int month = 1;
  int day = 1;
};

/// Clock time selected by the time picker.
struct TimePickerTime {
  uint8_t hour = 0;
  uint8_t minute = 0;
};

/// Draws and edits a small date picker with optional time selection.
class DateTimePickerComponent {
public:
  /// Resets the picker to a selected date.
  void reset(const DatePickerDate &date);
  /// Sets today's date and whether it should be highlighted.
  void setToday(const DatePickerDate &date, bool available);
  /// Sets the selected time.
  void setTime(const TimePickerTime &time);
  /// Enables or hides time selection controls.
  void setTimePickerEnabled(bool enabled);
  /// Chooses whether weekday columns start on Monday or Sunday.
  void setMondayFirst(bool enabled);

  /// Draws the picker.
  void draw(Adafruit_GFX &gfx);
  /// Handles touch input and returns true when state changed.
  bool handleTouch(const TouchPoint &point);
  /// Advances transient UI state and returns true when redraw is needed.
  bool update();

  /// Returns the selected date.
  DatePickerDate selectedDate() const;
  /// Returns the selected time.
  TimePickerTime selectedTime() const;
  /// Returns true when time selection is enabled.
  bool timePickerEnabled() const;

  /// Returns the number of days in a month.
  static int daysInMonth(int year, int month);
  /// Returns weekday index for a Gregorian date.
  static int weekdayForDate(int year, int month, int day);
  /// Converts days since Unix epoch to a date.
  static DatePickerDate dateFromUnixDays(int days);
  /// Returns a short month label.
  static const char *monthName(int month);
  /// Returns a short weekday label.
  static const char *weekdayName(int weekday);

private:
  int viewedYear = 1970;
  int viewedMonth = 1;
  int selectedDay = 1;
  DatePickerDate today;
  TimePickerTime time;
  bool hasToday = false;
  bool showTimePicker = false;
  bool mondayFirst = true;
  bool yearKeyboardOpen = false;
  String yearInput;
  T9KeyboardComponent yearKeyboard;
  int8_t pressedControl = -1;
  unsigned long pressedUntil = 0;

  void openYearKeyboard();
  bool handleYearKeyboardTouch(const TouchPoint &point);
  void applyYearInput();
  void moveMonth(int delta);
  void clampSelectedDay();
  int firstWeekday() const;
  void adjustHour(int delta);
  void adjustMinute(int delta);
  void pressControl(int8_t control);
  bool isControlPressed(int8_t control) const;
  static bool isLeapYear(int year);
  static int daysFromCivil(int year, int month, int day);
  static void drawCenteredText(Adafruit_GFX &gfx, const char *text, int16_t y,
                               uint8_t textSize = 1);
  static void drawButton(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t w,
                         int16_t h, const char *label, bool pressed);
};

#endif
