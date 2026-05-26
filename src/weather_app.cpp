#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "weather_app.h"
#include "lightweight_json_parser.h"
#include "secrets_config.h"

#include "../fonts/generated/WeatherSymbols_16pt7b.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>

namespace {
constexpr int16_t kInfoLabelX = 16;
constexpr int16_t kInfoValueX = 124;
constexpr int16_t kInfoRowStartY = 112;
constexpr int16_t kInfoRowStepY = 16;
constexpr int32_t kSecondsPerHour = 3600L;
constexpr int32_t kSecondsPerDay = 86400L;
constexpr int16_t kUnsafeUvThresholdX10 = 60;
} // namespace

static const char *WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=" SECRET_WEATHER_LATITUDE "&longitude=" SECRET_WEATHER_LONGITUDE
    "&"
    "daily=sunset,uv_index_max,precipitation_hours&"
    "hourly=temperature_2m,precipitation_probability,apparent_temperature,"
    "precipitation,uv_index&"
    "current=temperature_2m,precipitation,wind_speed_10m&"
    "timezone=Europe%2FBerlin&timeformat=unixtime";

class WeatherJsonListener : public JsonStreamListener {
public:
  explicit WeatherJsonListener(WeatherApp::HourPoint *targetHours,
                               WeatherApp::DayPoint *targetDays)
      : hours(targetHours), days(targetDays) {}

  void onObjectStart(int depth, const char *key) override {
    if (strcmp(key, "current") == 0) {
      section = SECTION_CURRENT;
      sectionDepth = depth;
    } else if (strcmp(key, "hourly") == 0) {
      section = SECTION_HOURLY;
      sectionDepth = depth;
    } else if (strcmp(key, "daily") == 0) {
      section = SECTION_DAILY;
      sectionDepth = depth;
    }
  }

  void onObjectEnd(int depth) override {
    if (depth == sectionDepth) {
      section = SECTION_NONE;
      sectionDepth = -1;
    }
  }

  void onArrayStart(int depth, const char *key) override {
    arrayDepth = depth;
    arrayIndex = 0;
    array = ARRAY_IGNORE;

    if (section == SECTION_HOURLY) {
      if (strcmp(key, "time") == 0) {
        array = ARRAY_HOURLY_TIME;
      } else if (strcmp(key, "temperature_2m") == 0) {
        array = ARRAY_HOURLY_TEMP;
      } else if (strcmp(key, "apparent_temperature") == 0) {
        array = ARRAY_HOURLY_FEELS;
      } else if (strcmp(key, "precipitation") == 0) {
        array = ARRAY_HOURLY_PRECIP;
      } else if (strcmp(key, "precipitation_probability") == 0) {
        array = ARRAY_HOURLY_PROBABILITY;
      } else if (strcmp(key, "uv_index") == 0) {
        array = ARRAY_HOURLY_UV;
      }
    } else if (section == SECTION_DAILY) {
      if (strcmp(key, "time") == 0) {
        array = ARRAY_DAILY_TIME;
      } else if (strcmp(key, "sunset") == 0) {
        array = ARRAY_DAILY_SUNSET;
      } else if (strcmp(key, "uv_index_max") == 0) {
        array = ARRAY_DAILY_UV;
      } else if (strcmp(key, "precipitation_hours") == 0) {
        array = ARRAY_DAILY_PRECIP_HOURS;
      }
    }
  }

  void onArrayEnd(int depth) override {
    if (depth == arrayDepth) {
      array = ARRAY_IGNORE;
      arrayDepth = -1;
      arrayIndex = 0;
    }
  }

  void onNumberValue(int, const char *key, int32_t value) override {
    if (section == SECTION_NONE && strcmp(key, "utc_offset_seconds") == 0) {
      utcOffsetSeconds = value;
      return;
    }
    if (section == SECTION_CURRENT) {
      setCurrent(key, value * 10);
      return;
    }
    setArrayValue(value, false);
  }

  void onDecimalValue(int, const char *key, int32_t valueX10) override {
    if (section == SECTION_CURRENT) {
      setCurrent(key, valueX10);
      return;
    }
    setArrayValue(valueX10, true);
  }

  int32_t currentTime = 0;
  int32_t utcOffsetSeconds = 0;
  int16_t currentTempX10 = 0;
  int16_t currentPrecipX10 = 0;
  int16_t currentWindX10 = 0;
  bool hasCurrent = false;
  int hourCount = 0;
  int dayCount = 0;

private:
  enum Section { SECTION_NONE, SECTION_CURRENT, SECTION_HOURLY, SECTION_DAILY };
  enum ArrayKind {
    ARRAY_IGNORE,
    ARRAY_HOURLY_TIME,
    ARRAY_HOURLY_TEMP,
    ARRAY_HOURLY_FEELS,
    ARRAY_HOURLY_PRECIP,
    ARRAY_HOURLY_PROBABILITY,
    ARRAY_HOURLY_UV,
    ARRAY_DAILY_TIME,
    ARRAY_DAILY_SUNSET,
    ARRAY_DAILY_UV,
    ARRAY_DAILY_PRECIP_HOURS,
  };

  WeatherApp::HourPoint *hours;
  WeatherApp::DayPoint *days;
  Section section = SECTION_NONE;
  ArrayKind array = ARRAY_IGNORE;
  int sectionDepth = -1;
  int arrayDepth = -1;
  int arrayIndex = 0;

  void setCurrent(const char *key, int32_t valueX10) {
    if (strcmp(key, "time") == 0) {
      currentTime = valueX10 / 10;
      hasCurrent = true;
    } else if (strcmp(key, "temperature_2m") == 0) {
      currentTempX10 = clamp16(valueX10);
      hasCurrent = true;
    } else if (strcmp(key, "precipitation") == 0) {
      currentPrecipX10 = clamp16(valueX10);
    } else if (strcmp(key, "wind_speed_10m") == 0) {
      currentWindX10 = clamp16(valueX10);
    }
  }

  void setArrayValue(int32_t value, bool scaled) {
    if (array == ARRAY_IGNORE) {
      return;
    }

    if (array >= ARRAY_HOURLY_TIME && array <= ARRAY_HOURLY_UV) {
      if (arrayIndex >= WeatherApp::MAX_HOURS) {
        return;
      }
      WeatherApp::HourPoint &hour = hours[arrayIndex];
      switch (array) {
      case ARRAY_HOURLY_TIME:
        hour.time = value;
        break;
      case ARRAY_HOURLY_TEMP:
        hour.tempX10 = clamp16(scaled ? value : value * 10);
        break;
      case ARRAY_HOURLY_FEELS:
        hour.apparentX10 = clamp16(scaled ? value : value * 10);
        break;
      case ARRAY_HOURLY_PRECIP:
        hour.precipX10 = clamp16(scaled ? value : value * 10);
        break;
      case ARRAY_HOURLY_PROBABILITY:
        hour.precipProbability = clamp16(scaled ? value / 10 : value);
        break;
      case ARRAY_HOURLY_UV:
        hour.uvX10 = clamp16(scaled ? value : value * 10);
        break;
      default:
        break;
      }
      arrayIndex++;
      if (arrayIndex > hourCount) {
        hourCount = arrayIndex;
      }
      return;
    }

    if (arrayIndex >= WeatherApp::MAX_DAYS) {
      return;
    }
    WeatherApp::DayPoint &day = days[arrayIndex];
    switch (array) {
    case ARRAY_DAILY_TIME:
      day.time = value;
      break;
    case ARRAY_DAILY_SUNSET:
      day.sunset = value;
      break;
    case ARRAY_DAILY_UV:
      day.uvX10 = clamp16(scaled ? value : value * 10);
      break;
    case ARRAY_DAILY_PRECIP_HOURS:
      day.precipHoursX10 = clamp16(scaled ? value : value * 10);
      break;
    default:
      break;
    }
    arrayIndex++;
    if (arrayIndex > dayCount) {
      dayCount = arrayIndex;
    }
  }

  static int16_t clamp16(int32_t value) {
    if (value > 32767) {
      return 32767;
    }
    if (value < -32768) {
      return -32768;
    }
    return static_cast<int16_t>(value);
  }
};

void WeatherApp::reset() {
  state = STATE_LOADING;
  page = PAGE_CURRENT;
  requested = false;
  status[0] = '\0';
  currentTime = 0;
  utcOffsetSeconds = 0;
  currentTempX10 = 0;
  currentPrecipX10 = 0;
  currentWindX10 = 0;
  hasCurrent = false;
  for (int i = 0; i < MAX_HOURS; i++) {
    hours[i] = HourPoint();
  }
  hourCount = 0;
  for (int i = 0; i < MAX_DAYS; i++) {
    days[i] = DayPoint();
  }
  dayCount = 0;
  nextUvPeakX10 = 0;
  todayUnsafeUv = UnsafeUvWindow();
  nextUnsafeUv = UnsafeUvWindow();
  nextHourSamples = 0;
}

bool WeatherApp::hasActiveSession() const {
  return state == STATE_LOADING || state == STATE_READY;
}

void WeatherApp::draw(Adafruit_GFX &gfx) {
  if (state == STATE_LOADING) {
    drawLoading(gfx);
  } else if (state == STATE_FAILED) {
    drawFailed(gfx);
  } else if (page == PAGE_CURRENT) {
    drawCurrent(gfx);
  } else if (page == PAGE_NEXT_24H) {
    drawNext24h(gfx);
  } else {
    drawWeek(gfx);
  }
}

bool WeatherApp::update() {
  if (requested) {
    return false;
  }
  requested = true;
  state = STATE_LOADING;
  bool ok = fetch();
  if (ok) {
    summarize();
  }
  state = ok ? STATE_READY : STATE_FAILED;
  return true;
}

bool WeatherApp::handleTouch(const TouchPoint &point) {
  if (state == STATE_FAILED) {
    reset();
    return true;
  }
  if (state != STATE_READY) {
    return false;
  }
  if (point.x < 80) {
    page = static_cast<Page>((page + PAGE_COUNT - 1) % PAGE_COUNT);
  } else if (point.x > 120) {
    page = static_cast<Page>((page + 1) % PAGE_COUNT);
  } else {
    page = static_cast<Page>((page + 1) % PAGE_COUNT);
  }
  return true;
}

bool WeatherApp::fetch() {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(FETCH_TIMEOUT_MS);
  if (!http.begin(client, WEATHER_URL)) {
    setStatus("HTTP setup fail");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setStatus("HTTP error");
    http.end();
    return false;
  }

  WeatherJsonListener listener(hours, days);
  LightweightJsonParser parser;
  bool ok = parser.parse(http.getStreamPtr(), http.getSize(), FETCH_TIMEOUT_MS,
                         MAX_JSON_BYTES, listener, status, sizeof(status));
  http.end();

  currentTime = listener.currentTime;
  utcOffsetSeconds = listener.utcOffsetSeconds;
  currentTempX10 = listener.currentTempX10;
  currentPrecipX10 = listener.currentPrecipX10;
  currentWindX10 = listener.currentWindX10;
  hasCurrent = listener.hasCurrent;
  hourCount = listener.hourCount;
  dayCount = listener.dayCount;

  if (!ok && !hasCurrent && hourCount == 0 && dayCount == 0) {
    return false;
  }
  if (!hasCurrent || hourCount == 0 || dayCount == 0) {
    setStatus("Weather incomplete");
    return false;
  }
  return true;
}

void WeatherApp::summarize() {
  nextHourSamples = 0;
  nextPrecipSumX10 = 0;
  nextPrecipProbabilityMax = 0;
  nextUvPeakX10 = 0;
  todayUnsafeUv = UnsafeUvWindow();
  nextUnsafeUv = UnsafeUvWindow();

  int32_t localTime = currentTime + utcOffsetSeconds;
  int32_t dayRemainder = localTime % kSecondsPerDay;
  if (dayRemainder < 0) {
    dayRemainder += kSecondsPerDay;
  }
  int32_t todayStart = localTime - dayRemainder - utcOffsetSeconds;
  int32_t todayEnd = todayStart + kSecondsPerDay;
  int32_t tomorrowStart = todayEnd;
  int32_t tomorrowEnd = tomorrowStart + kSecondsPerDay;

  summarizeUnsafeUv(todayStart, todayEnd, todayUnsafeUv);
  summarizeUnsafeUv(tomorrowStart, tomorrowEnd, nextUnsafeUv);

  for (int i = 0; i < hourCount; i++) {
    if (hours[i].time < tomorrowStart || hours[i].time >= tomorrowEnd) {
      continue;
    }
    if (nextHourSamples == 0) {
      nextTempMinX10 = hours[i].tempX10;
      nextTempMaxX10 = hours[i].tempX10;
      nextFeelsMinX10 = hours[i].apparentX10;
      nextFeelsMaxX10 = hours[i].apparentX10;
    } else {
      nextTempMinX10 =
          nextTempMinX10 < hours[i].tempX10 ? nextTempMinX10 : hours[i].tempX10;
      nextTempMaxX10 =
          nextTempMaxX10 > hours[i].tempX10 ? nextTempMaxX10 : hours[i].tempX10;
      nextFeelsMinX10 = nextFeelsMinX10 < hours[i].apparentX10
                            ? nextFeelsMinX10
                            : hours[i].apparentX10;
      nextFeelsMaxX10 = nextFeelsMaxX10 > hours[i].apparentX10
                            ? nextFeelsMaxX10
                            : hours[i].apparentX10;
    }
    nextPrecipSumX10 += hours[i].precipX10;
    nextPrecipProbabilityMax =
        nextPrecipProbabilityMax > hours[i].precipProbability
            ? nextPrecipProbabilityMax
            : hours[i].precipProbability;
    nextUvPeakX10 =
        nextUvPeakX10 > hours[i].uvX10 ? nextUvPeakX10 : hours[i].uvX10;
    nextHourSamples++;
  }
}

void WeatherApp::summarizeUnsafeUv(int32_t windowStart, int32_t windowEnd,
                                   UnsafeUvWindow &window) {
  for (int i = 0; i < hourCount; i++) {
    if (hours[i].time < windowStart || hours[i].time >= windowEnd) {
      continue;
    }
    if (hours[i].uvX10 < kUnsafeUvThresholdX10) {
      continue;
    }
    if (window.count == 0) {
      window.startTime = hours[i].time;
    }
    window.endTime = hours[i].time + kSecondsPerHour;
    window.count++;
  }
}

void WeatherApp::drawLoading(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  drawIcon(gfx, 'P', 88, 78);
  drawCentered(gfx, "Loading...", 120);
  drawCentered(gfx, "Open-Meteo forecast", 138);
}

void WeatherApp::drawFailed(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  drawIcon(gfx, 'F', 88, 54);
  gfx.setCursor(34, 100);
  gfx.print(status[0] ? status : "Weather failed");
  gfx.setCursor(44, 122);
  gfx.print("Touch to retry");
}

void WeatherApp::drawCurrent(Adafruit_GFX &gfx) {
  drawHeader(gfx, "WEATHER NOW");
  drawIcon(gfx, currentIcon(), 16, 75, 2);

  gfx.setFont();
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  gfx.setCursor(78, 52);
  printTemp(gfx, currentTempX10);

  gfx.setTextSize(1);
  char timeText[8];
  formatHour(currentTime, timeText, sizeof(timeText));
  gfx.setCursor(80, 82);
  gfx.print("at ");
  gfx.print(timeText);

  gfx.setCursor(kInfoLabelX, kInfoRowStartY);
  gfx.print("Rain now");
  gfx.setCursor(kInfoValueX, kInfoRowStartY);
  printDecimal(gfx, currentPrecipX10, "mm");

  gfx.setCursor(kInfoLabelX, kInfoRowStartY + kInfoRowStepY);
  gfx.print("Wind");
  gfx.setCursor(kInfoValueX, kInfoRowStartY + kInfoRowStepY);
  printDecimal(gfx, currentWindX10, "km/h");

  if (dayCount > 0) {
    char sunset[8];
    formatHour(days[0].sunset, sunset, sizeof(sunset));
    gfx.setCursor(kInfoLabelX, kInfoRowStartY + kInfoRowStepY * 2);
    gfx.print("Sunset");
    gfx.setCursor(kInfoValueX, kInfoRowStartY + kInfoRowStepY * 2);
    gfx.print(sunset);
  }
  char unsafeUvText[20];
  formatUnsafeUvLabel(dayCount > 0 ? days[0].uvX10 : 0, unsafeUvText,
                      sizeof(unsafeUvText));
  gfx.setCursor(kInfoLabelX, kInfoRowStartY + kInfoRowStepY * 3);
  gfx.print(unsafeUvText);
  gfx.setCursor(kInfoValueX, kInfoRowStartY + kInfoRowStepY * 3);
  formatUnsafeUvWindow(todayUnsafeUv, unsafeUvText, sizeof(unsafeUvText));
  gfx.print(unsafeUvText);
  drawFooter(gfx);
}

void WeatherApp::drawNext24h(Adafruit_GFX &gfx) {
  drawHeader(gfx, "NEXT DAY");
  drawIcon(gfx, nextPrecipSumX10 > 0 ? 'M' : 'C', 16, 75, 2);

  gfx.setFont();
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(76, 54);
  gfx.print("Temp ");
  printTemp(gfx, nextTempMinX10);
  gfx.print("..");
  printTemp(gfx, nextTempMaxX10);

  gfx.setCursor(76, 76);
  gfx.print("Feel ");
  printTemp(gfx, nextFeelsMinX10);
  gfx.print("..");
  printTemp(gfx, nextFeelsMaxX10);

  gfx.setCursor(kInfoLabelX, kInfoRowStartY);
  gfx.print("Rain chance");
  gfx.setCursor(kInfoValueX, kInfoRowStartY);
  gfx.print(nextPrecipProbabilityMax);
  gfx.print("%");

  gfx.setCursor(kInfoLabelX, kInfoRowStartY + kInfoRowStepY);
  gfx.print("Rain total");
  gfx.setCursor(kInfoValueX, kInfoRowStartY + kInfoRowStepY);
  printDecimal(gfx, nextPrecipSumX10, "mm");

  char unsafeUvText[20];
  formatUnsafeUvLabel(nextUvPeakX10, unsafeUvText, sizeof(unsafeUvText));
  gfx.setCursor(kInfoLabelX, kInfoRowStartY + kInfoRowStepY * 2);
  gfx.print(unsafeUvText);
  gfx.setCursor(kInfoValueX, kInfoRowStartY + kInfoRowStepY * 2);
  formatUnsafeUvWindow(nextUnsafeUv, unsafeUvText, sizeof(unsafeUvText));
  gfx.print(unsafeUvText);
  drawFooter(gfx);
}

void WeatherApp::drawWeek(Adafruit_GFX &gfx) {
  drawHeader(gfx, "7 DAY");
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setFont();
  gfx.setCursor(31, 31);
  gfx.print("Day");
  gfx.setCursor(82, 31);
  gfx.print("UV");
  gfx.setCursor(115, 31);
  gfx.print("RainH");
  gfx.setCursor(160, 31);
  gfx.print("Set");

  for (int i = 0; i < dayCount && i < MAX_DAYS; i++) {
    int y = 48 + i * 19;
    char day[8];
    char sunset[8];
    formatDay(days[i].time, day, sizeof(day));
    formatHour(days[i].sunset, sunset, sizeof(sunset));
    drawIcon(gfx, dayIcon(days[i]), 7, y + 14);
    gfx.setFont();
    gfx.setTextSize(1);
    gfx.setCursor(31, y);
    gfx.print(day);
    gfx.setCursor(82, y);
    printDecimal(gfx, days[i].uvX10, "");
    gfx.setCursor(118, y);
    printDecimal(gfx, days[i].precipHoursX10, "h");
    gfx.setCursor(160, y);
    gfx.print(sunset);
  }
  drawFooter(gfx);
}

void WeatherApp::drawHeader(Adafruit_GFX &gfx, const char *title) {
  gfx.setFont();
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(4, 6);
  gfx.print(title);
  gfx.drawLine(0, 22, 199, 22, 1);
  char pageText[8];
  snprintf(pageText, sizeof(pageText), "%d/%d", static_cast<int>(page) + 1,
           static_cast<int>(PAGE_COUNT));
  gfx.setCursor(174, 6);
  gfx.print(pageText);
}

void WeatherApp::drawFooter(Adafruit_GFX &gfx) {
  gfx.setFont();
  gfx.setTextSize(1);
  gfx.drawLine(0, 180, 199, 180, 1);
  gfx.setCursor(6, 188);
  gfx.print("<");
  gfx.setCursor(92, 188);
  gfx.print("tap");
  gfx.setCursor(188, 188);
  gfx.print(">");
}

void WeatherApp::drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor((200 - static_cast<int>(w)) / 2 - x1, y);
  gfx.print(text);
}

void WeatherApp::drawIcon(Adafruit_GFX &gfx, char icon, int16_t x, int16_t y,
                          uint8_t size) {
  gfx.setFont(&WeatherSymbols16pt7b);
  gfx.setTextSize(size);
  gfx.setCursor(x, y);
  gfx.print(icon);
  gfx.setFont();
  gfx.setTextSize(1);
}

char WeatherApp::currentIcon() const {
  if (currentPrecipX10 > 0) {
    return 'M';
  }
  if (currentTempX10 <= 0) {
    return 'E';
  }
  if (dayCount > 0 && days[0].sunset > 0 && currentTime > days[0].sunset) {
    return 'I';
  }
  if (dayCount > 0 && days[0].uvX10 >= 60) {
    return 'N';
  }
  return 'C';
}

char WeatherApp::dayIcon(const DayPoint &day) const {
  if (day.precipHoursX10 >= 20) {
    return 'M';
  }
  if (day.uvX10 >= 60) {
    return 'N';
  }
  return 'C';
}

void WeatherApp::printTemp(Adafruit_GFX &gfx, int16_t valueX10) {
  gfx.print(valueX10 / 10);
  gfx.print("C");
}

void WeatherApp::printDecimal(Adafruit_GFX &gfx, int16_t valueX10,
                              const char *unit) {
  gfx.print(valueX10 / 10);
  int decimal = abs(valueX10 % 10);
  if (decimal != 0) {
    gfx.print(".");
    gfx.print(decimal);
  }
  if (unit != nullptr && unit[0] != '\0') {
    gfx.print(unit);
  }
}

void WeatherApp::formatUnsafeUvLabel(int16_t peakX10, char *out,
                                     int outSize) const {
  if (outSize <= 0) {
    return;
  }
  if (peakX10 < 0) {
    peakX10 = 0;
  }
  int peakWhole = peakX10 / 10;
  int peakDecimal = abs(peakX10 % 10);
  if (peakDecimal == 0) {
    snprintf(out, outSize, "Unsafe UV (P%d):", peakWhole);
  } else {
    snprintf(out, outSize, "Unsafe UV (P%d.%d):", peakWhole, peakDecimal);
  }
}

void WeatherApp::formatUnsafeUvWindow(const UnsafeUvWindow &window, char *out,
                                      int outSize) const {
  if (outSize <= 0) {
    return;
  }

  if (window.count == 0) {
    snprintf(out, outSize, "none");
    return;
  }

  int32_t startLocal = (window.startTime + utcOffsetSeconds) % kSecondsPerDay;
  int32_t endLocal = (window.endTime - 1 + utcOffsetSeconds) % kSecondsPerDay;
  if (startLocal < 0) {
    startLocal += kSecondsPerDay;
  }
  if (endLocal < 0) {
    endLocal += kSecondsPerDay;
  }
  int startHour = startLocal / kSecondsPerHour;
  int endHour = endLocal / kSecondsPerHour;
  snprintf(out, outSize, "%d-%d", startHour, endHour);
}

void WeatherApp::formatHour(int32_t unixTime, char *out, int outSize) const {
  if (outSize <= 0) {
    return;
  }
  if (unixTime <= 0) {
    snprintf(out, outSize, "--:--");
    return;
  }
  int32_t daySeconds = (unixTime + utcOffsetSeconds) % kSecondsPerDay;
  if (daySeconds < 0) {
    daySeconds += kSecondsPerDay;
  }
  int hour = daySeconds / kSecondsPerHour;
  int minute = (daySeconds / 60) % 60;
  snprintf(out, outSize, "%02d:%02d", hour, minute);
}

void WeatherApp::formatDay(int32_t unixTime, char *out, int outSize) const {
  static const char *DAYS[] = {"Thu", "Fri", "Sat", "Sun",
                               "Mon", "Tue", "Wed"};
  if (outSize <= 0) {
    return;
  }
  if (unixTime <= 0) {
    snprintf(out, outSize, "---");
    return;
  }
  int index = ((unixTime + utcOffsetSeconds) / 86400L) % 7;
  if (index < 0) {
    index += 7;
  }
  snprintf(out, outSize, "%s", DAYS[index]);
}

void WeatherApp::setStatus(const char *text) {
  strncpy(status, text ? text : "", sizeof(status) - 1);
  status[sizeof(status) - 1] = '\0';
}

#endif
