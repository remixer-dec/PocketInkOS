#ifndef WEATHER_APP_H
#define WEATHER_APP_H

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

class WeatherJsonListener;

enum class WeatherIcon {
  CloudLightning,
  RainDrops,
  PartlyCloudyDay,
  Snowflake,
  ThermometerSnow,
  FogCloud,
  PartlyCloudyNight,
  Thermometer,
  CrescentMoon,
  Umbrella,
  ThunderstormRain,
  CloudDrizzleDots,
  CloudRain,
  Sun,
  Cloudy,
  Cloud
};

class WeatherApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;

private:
  friend class WeatherJsonListener;

  enum State { STATE_LOADING, STATE_READY, STATE_FAILED };
  enum Page { PAGE_CURRENT, PAGE_NEXT_24H, PAGE_WEEK, PAGE_COUNT };

  static const unsigned long FETCH_TIMEOUT_MS = 10000;
  static const int MAX_JSON_BYTES = 38000;
  static const int MAX_HOURS = 48;
  static const int MAX_DAYS = 7;

  struct HourPoint {
    int32_t time = 0;
    int16_t tempX10 = 0;
    int16_t apparentX10 = 0;
    int16_t precipX10 = 0;
    int16_t precipProbability = 0;
    int16_t uvX10 = 0;
  };

  struct DayPoint {
    int32_t time = 0;
    int32_t sunset = 0;
    int16_t uvX10 = 0;
    int16_t precipHoursX10 = 0;
  };

  struct UnsafeUvWindow {
    int32_t startTime = 0;
    int32_t endTime = 0;
    int count = 0;
  };

  State state = STATE_LOADING;
  Page page = PAGE_CURRENT;
  bool requested = false;
  char status[32] = "";

  int32_t currentTime = 0;
  int32_t utcOffsetSeconds = 0;
  int16_t currentTempX10 = 0;
  int16_t currentPrecipX10 = 0;
  int16_t currentWindX10 = 0;
  bool hasCurrent = false;

  HourPoint hours[MAX_HOURS];
  int hourCount = 0;
  DayPoint days[MAX_DAYS];
  int dayCount = 0;

  int16_t nextTempMinX10 = 0;
  int16_t nextTempMaxX10 = 0;
  int16_t nextFeelsMinX10 = 0;
  int16_t nextFeelsMaxX10 = 0;
  int16_t nextPrecipSumX10 = 0;
  int16_t nextPrecipProbabilityMax = 0;
  int16_t nextUvPeakX10 = 0;
  UnsafeUvWindow todayUnsafeUv;
  UnsafeUvWindow nextUnsafeUv;
  int nextHourSamples = 0;

  bool fetch();
  void summarize();
  void summarizeUnsafeUv(int32_t windowStart, int32_t windowEnd,
                         UnsafeUvWindow &window);
  void formatUnsafeUvLabel(int16_t peakX10, char *out, int outSize) const;
  void formatUnsafeUvWindow(const UnsafeUvWindow &window, char *out,
                            int outSize) const;
  void drawLoading(Adafruit_GFX &gfx);
  void drawFailed(Adafruit_GFX &gfx);
  void drawCurrent(Adafruit_GFX &gfx);
  void drawNext24h(Adafruit_GFX &gfx);
  void drawWeek(Adafruit_GFX &gfx);
  void drawHeader(Adafruit_GFX &gfx, const char *title);
  void drawFooter(Adafruit_GFX &gfx);
  void drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y);
  void drawIcon(Adafruit_GFX &gfx, char icon, int16_t x, int16_t y,
                uint8_t size = 1);
  char currentIcon() const;
  char dayIcon(const DayPoint &day) const;
  void printTemp(Adafruit_GFX &gfx, int16_t valueX10);
  void printDecimal(Adafruit_GFX &gfx, int16_t valueX10, const char *unit);
  void formatHour(int32_t unixTime, char *out, int outSize) const;
  void formatDay(int32_t unixTime, char *out, int outSize) const;
  void setStatus(const char *text);
};

#endif
#endif
