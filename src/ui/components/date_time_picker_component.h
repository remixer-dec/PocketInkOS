#ifndef DATE_TIME_PICKER_COMPONENT_H
#define DATE_TIME_PICKER_COMPONENT_H

#include "sys/touch_input.h"
#include "ui/components/t9_keyboard_component.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

struct DatePickerDate {
  int year = 1970;
  int month = 1;
  int day = 1;
};

struct TimePickerTime {
  uint8_t hour = 0;
  uint8_t minute = 0;
};

class DateTimePickerComponent {
public:
  void reset(const DatePickerDate &date);
  void setToday(const DatePickerDate &date, bool available);
  void setTime(const TimePickerTime &time);
  void setTimePickerEnabled(bool enabled);

  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool update();

  DatePickerDate selectedDate() const;
  TimePickerTime selectedTime() const;
  bool timePickerEnabled() const;

  static int daysInMonth(int year, int month);
  static int weekdayForDate(int year, int month, int day);
  static DatePickerDate dateFromUnixDays(int days);
  static const char *monthName(int month);
  static const char *weekdayName(int weekday);

private:
  int viewedYear = 1970;
  int viewedMonth = 1;
  int selectedDay = 1;
  DatePickerDate today;
  TimePickerTime time;
  bool hasToday = false;
  bool showTimePicker = false;
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
