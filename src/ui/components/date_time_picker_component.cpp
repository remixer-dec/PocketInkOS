#include "ui/components/date_time_picker_component.h"
#include "sys/global.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <cstdio>

namespace {

static const int16_t NAV_Y = 4;
static const int16_t NAV_X = 4;
static const int16_t NAV_W = 39;
static const int16_t NAV_H = 26;
static const int16_t NAV_HIT_PAD = 5;
static const int16_t GRID_X = 5;
static const int16_t GRID_Y = 54;
static const int16_t CELL_W = 27;
static const int16_t CELL_H = 21;
static const int16_t WEEKDAY_Y = 41;
static const int16_t DETAIL_Y = 190;
static const int16_t TIME_DETAIL_Y = 174;
static const int16_t TIME_BUTTON_Y = 181;
static const unsigned long PRESS_HIGHLIGHT_MS = 160;

enum PickerControl : int8_t {
  CONTROL_NONE = -1,
  CONTROL_PREV_MONTH = 0,
  CONTROL_NEXT_MONTH = 1,
  CONTROL_HOUR_DOWN = 2,
  CONTROL_HOUR_UP = 3,
  CONTROL_MINUTE_DOWN = 4,
  CONTROL_MINUTE_UP = 5,
};

} // namespace

void DateTimePickerComponent::reset(const DatePickerDate &date) {
  viewedYear = date.year;
  viewedMonth = date.month;
  selectedDay = date.day;
  clampSelectedDay();
  yearKeyboardOpen = false;
  yearInput = "";
  yearKeyboard.setLayout(T9KeyboardComponent::Layout::Numbers);
  pressedControl = CONTROL_NONE;
  pressedUntil = 0;
}

void DateTimePickerComponent::setToday(const DatePickerDate &date,
                                       bool available) {
  today = date;
  hasToday = available;
}

void DateTimePickerComponent::setTime(const TimePickerTime &nextTime) {
  time.hour = nextTime.hour % 24U;
  time.minute = nextTime.minute % 60U;
}

void DateTimePickerComponent::setTimePickerEnabled(bool enabled) {
  showTimePicker = enabled;
}

void DateTimePickerComponent::setMondayFirst(bool enabled) {
  mondayFirst = enabled;
}

void DateTimePickerComponent::draw(Adafruit_GFX &gfx) {
  if (yearKeyboardOpen) {
    yearKeyboard.draw(gfx, yearInput, 4);
    return;
  }

  gfx.setTextColor(1);
  gfx.setTextSize(1);

  uiDrawBorderlessIconButton(
      gfx, {NAV_X, NAV_Y, NAV_W, NAV_H}, '<',
      isControlPressed(CONTROL_PREV_MONTH));
  uiDrawBorderlessIconButton(
      gfx, {static_cast<int16_t>(EPD_WIDTH - NAV_W - NAV_X), NAV_Y, NAV_W,
            NAV_H},
      '>', isControlPressed(CONTROL_NEXT_MONTH));

  char title[16];
  snprintf(title, sizeof(title), "%s %d", monthName(viewedMonth), viewedYear);
  drawCenteredText(gfx, title, 8, 2);

  for (int i = 0; i < 7; i++) {
    const char *label = weekdayName((i + (mondayFirst ? 1 : 0)) % 7);
    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    gfx.getTextBounds(label, 0, 0, &textX, &textY, &textW, &textH);
    gfx.setCursor(GRID_X + i * CELL_W +
                      (CELL_W - static_cast<int16_t>(textW)) / 2 - textX,
                  WEEKDAY_Y);
    gfx.print(label);
  }

  gfx.drawRect(GRID_X, GRID_Y, CELL_W * 7, CELL_H * 6, 1);
  for (int col = 1; col < 7; col++) {
    const int16_t x = GRID_X + col * CELL_W;
    gfx.drawLine(x, GRID_Y, x, GRID_Y + CELL_H * 6, 1);
  }
  for (int row = 1; row < 6; row++) {
    const int16_t y = GRID_Y + row * CELL_H;
    gfx.drawLine(GRID_X, y, GRID_X + CELL_W * 7, y, 1);
  }

  const int first = firstWeekday();
  const int days = daysInMonth(viewedYear, viewedMonth);
  for (int day = 1; day <= days; day++) {
    const int cell = first + day - 1;
    const int col = cell % 7;
    const int row = cell / 7;
    const int16_t x = GRID_X + col * CELL_W;
    const int16_t y = GRID_Y + row * CELL_H;
    const bool isToday = hasToday && viewedYear == today.year &&
                         viewedMonth == today.month && day == today.day;
    const bool isSelected = day == selectedDay;

    if (isToday) {
      gfx.fillRect(x + 2, y + 2, CELL_W - 4, CELL_H - 4, 1);
    }
    if (isSelected) {
      gfx.drawRect(x, y, CELL_W + 1, CELL_H + 1, 1);
      gfx.drawRect(x + 1, y + 1, CELL_W - 1, CELL_H - 1, 1);
    }

    char dayText[3];
    snprintf(dayText, sizeof(dayText), "%d", day);
    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    gfx.setTextColor(isToday ? 0 : 1);
    gfx.getTextBounds(dayText, 0, 0, &textX, &textY, &textW, &textH);
    gfx.setCursor(x + (CELL_W - static_cast<int16_t>(textW)) / 2 - textX,
                  y + 7);
    gfx.print(dayText);
    gfx.setTextColor(1);
  }

  const int weekday = weekdayForDate(viewedYear, viewedMonth, selectedDay);
  char detail[24];
  snprintf(detail, sizeof(detail), "%s %d %s %d", weekdayName(weekday),
           selectedDay, monthName(viewedMonth), viewedYear);
  drawCenteredText(gfx, detail, showTimePicker ? TIME_DETAIL_Y : DETAIL_Y);

  if (!showTimePicker) {
    return;
  }

  char timeText[8];
  snprintf(timeText, sizeof(timeText), "%02u:%02u",
           static_cast<unsigned>(time.hour),
           static_cast<unsigned>(time.minute));
  drawButton(gfx, 16, TIME_BUTTON_Y, 24, 17, "-",
             isControlPressed(CONTROL_HOUR_DOWN));
  drawButton(gfx, 46, TIME_BUTTON_Y, 24, 17, "+",
             isControlPressed(CONTROL_HOUR_UP));
  drawCenteredText(gfx, timeText, 186, 1);
  drawButton(gfx, 130, TIME_BUTTON_Y, 24, 17, "-",
             isControlPressed(CONTROL_MINUTE_DOWN));
  drawButton(gfx, 160, TIME_BUTTON_Y, 24, 17, "+",
             isControlPressed(CONTROL_MINUTE_UP));
}

bool DateTimePickerComponent::handleTouch(const TouchPoint &point) {
  if (yearKeyboardOpen) {
    return handleYearKeyboardTouch(point);
  }

  const int x = point.x;
  const int y = point.y;

  if (y >= NAV_Y && y < NAV_Y + NAV_H) {
    const int leftHitEnd = NAV_X + NAV_W + NAV_HIT_PAD;
    const int rightHitStart = EPD_WIDTH - NAV_X - NAV_W - NAV_HIT_PAD;
    if (x < leftHitEnd) {
      pressControl(CONTROL_PREV_MONTH);
      moveMonth(-1);
      return true;
    }
    if (x >= rightHitStart) {
      pressControl(CONTROL_NEXT_MONTH);
      moveMonth(1);
      return true;
    }
    if (x >= leftHitEnd && x < rightHitStart) {
      openYearKeyboard();
      return true;
    }
  }

  if (showTimePicker) {
    const UiRect hourDown = {16, TIME_BUTTON_Y, 24, 17};
    const UiRect hourUp = {46, TIME_BUTTON_Y, 24, 17};
    const UiRect minuteDown = {130, TIME_BUTTON_Y, 24, 17};
    const UiRect minuteUp = {160, TIME_BUTTON_Y, 24, 17};
    if (uiTouchContains(hourDown, point)) {
      pressControl(CONTROL_HOUR_DOWN);
      adjustHour(-1);
      return true;
    }
    if (uiTouchContains(hourUp, point)) {
      pressControl(CONTROL_HOUR_UP);
      adjustHour(1);
      return true;
    }
    if (uiTouchContains(minuteDown, point)) {
      pressControl(CONTROL_MINUTE_DOWN);
      adjustMinute(-1);
      return true;
    }
    if (uiTouchContains(minuteUp, point)) {
      pressControl(CONTROL_MINUTE_UP);
      adjustMinute(1);
      return true;
    }
  }

  if (x < GRID_X || x >= GRID_X + CELL_W * 7 || y < GRID_Y ||
      y >= GRID_Y + CELL_H * 6) {
    return false;
  }

  const int col = (x - GRID_X) / CELL_W;
  const int row = (y - GRID_Y) / CELL_H;
  const int day = row * 7 + col - firstWeekday() + 1;
  if (day < 1 || day > daysInMonth(viewedYear, viewedMonth)) {
    return false;
  }

  selectedDay = day;
  return true;
}

bool DateTimePickerComponent::dateAtPoint(const TouchPoint &point,
                                          DatePickerDate &out) const {
  if (yearKeyboardOpen || point.x < GRID_X ||
      point.x >= GRID_X + CELL_W * 7 || point.y < GRID_Y ||
      point.y >= GRID_Y + CELL_H * 6) {
    return false;
  }
  const int col = (point.x - GRID_X) / CELL_W;
  const int row = (point.y - GRID_Y) / CELL_H;
  const int day = row * 7 + col - firstWeekday() + 1;
  if (day < 1 || day > daysInMonth(viewedYear, viewedMonth)) {
    return false;
  }
  out = {viewedYear, viewedMonth, day};
  return true;
}

bool DateTimePickerComponent::update() {
  if (yearKeyboardOpen && yearKeyboard.update()) {
    return true;
  }
  if (pressedControl == CONTROL_NONE || !millisReached(pressedUntil)) {
    return false;
  }
  pressedControl = CONTROL_NONE;
  pressedUntil = 0;
  return true;
}

DatePickerDate DateTimePickerComponent::selectedDate() const {
  DatePickerDate out;
  out.year = viewedYear;
  out.month = viewedMonth;
  out.day = selectedDay;
  return out;
}

TimePickerTime DateTimePickerComponent::selectedTime() const { return time; }

bool DateTimePickerComponent::timePickerEnabled() const {
  return showTimePicker;
}

void DateTimePickerComponent::openYearKeyboard() {
  char text[8];
  snprintf(text, sizeof(text), "%d", viewedYear);
  yearInput = text;
  yearKeyboard.setLayout(T9KeyboardComponent::Layout::Numbers);
  yearKeyboardOpen = true;
}

bool DateTimePickerComponent::handleYearKeyboardTouch(const TouchPoint &point) {
  KeyboardEvent event = yearKeyboard.hitTest(point, yearInput, 4);
  if (event.action == KEY_NONE) {
    return false;
  }
  if (event.action == KEY_OK) {
    applyYearInput();
    yearKeyboardOpen = false;
  }
  return true;
}

void DateTimePickerComponent::applyYearInput() {
  int nextYear = 0;
  for (size_t i = 0; i < yearInput.length(); i++) {
    const char digit = yearInput[i];
    if (digit < '0' || digit > '9') {
      continue;
    }
    nextYear = nextYear * 10 + (digit - '0');
  }
  if (nextYear < 1) {
    return;
  }
  viewedYear = nextYear;
  clampSelectedDay();
}

void DateTimePickerComponent::moveMonth(int delta) {
  viewedMonth += delta;
  while (viewedMonth < 1) {
    viewedMonth += 12;
    viewedYear--;
  }
  while (viewedMonth > 12) {
    viewedMonth -= 12;
    viewedYear++;
  }
  clampSelectedDay();
}

void DateTimePickerComponent::clampSelectedDay() {
  const int days = daysInMonth(viewedYear, viewedMonth);
  if (selectedDay < 1) {
    selectedDay = 1;
  } else if (selectedDay > days) {
    selectedDay = days;
  }
}

int DateTimePickerComponent::firstWeekday() const {
  const int weekday = weekdayForDate(viewedYear, viewedMonth, 1);
  return mondayFirst ? (weekday + 6) % 7 : weekday;
}

void DateTimePickerComponent::adjustHour(int delta) {
  int next = static_cast<int>(time.hour) + delta;
  while (next < 0) {
    next += 24;
  }
  time.hour = static_cast<uint8_t>(next % 24);
}

void DateTimePickerComponent::adjustMinute(int delta) {
  int next = static_cast<int>(time.minute) + delta;
  while (next < 0) {
    next += 60;
  }
  time.minute = static_cast<uint8_t>(next % 60);
}

void DateTimePickerComponent::pressControl(int8_t control) {
  pressedControl = control;
  pressedUntil = millis() + PRESS_HIGHLIGHT_MS;
}

bool DateTimePickerComponent::isControlPressed(int8_t control) const {
  return pressedControl == control;
}

int DateTimePickerComponent::daysInMonth(int year, int month) {
  static const int DAYS[] = {31, 28, 31, 30, 31, 30,
                             31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  if (month < 1 || month > 12) {
    return 30;
  }
  return DAYS[month - 1];
}

bool DateTimePickerComponent::isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DateTimePickerComponent::daysFromCivil(int year, int month, int day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned mp = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
  const unsigned doy =
      (153 * mp + 2) / 5 + static_cast<unsigned>(day) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

DatePickerDate DateTimePickerComponent::dateFromUnixDays(int days) {
  int z = days + 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int year = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned day = doy - (153 * mp + 2) / 5 + 1;
  const unsigned month = mp + (mp < 10 ? 3 : -9);
  year += month <= 2;

  DatePickerDate out;
  out.year = year;
  out.month = static_cast<int>(month);
  out.day = static_cast<int>(day);
  return out;
}

int DateTimePickerComponent::weekdayForDate(int year, int month, int day) {
  int weekday = (daysFromCivil(year, month, day) + 4) % 7;
  if (weekday < 0) {
    weekday += 7;
  }
  return weekday;
}

const char *DateTimePickerComponent::monthName(int month) {
  static const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                 "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  if (month < 1 || month > 12) {
    return "---";
  }
  return MONTHS[month - 1];
}

const char *DateTimePickerComponent::weekdayName(int weekday) {
  static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed",
                                   "Thu", "Fri", "Sat"};
  if (weekday < 0 || weekday > 6) {
    return "---";
  }
  return WEEKDAYS[weekday];
}

void DateTimePickerComponent::drawCenteredText(Adafruit_GFX &gfx,
                                               const char *text, int16_t y,
                                               uint8_t textSize) {
  gfx.setTextSize(textSize);
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(textW)) / 2 - textX, y);
  gfx.print(text);
  gfx.setTextSize(1);
}

void DateTimePickerComponent::drawButton(Adafruit_GFX &gfx, int16_t x,
                                         int16_t y, int16_t w, int16_t h,
                                         const char *label, bool pressed) {
  UiRect rect = {x, y, w, h};
  uiDrawButton(gfx, rect, label, pressed);
}
