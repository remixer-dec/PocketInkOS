#ifndef CURRENCY_EXCHANGE_APP_H
#define CURRENCY_EXCHANGE_APP_H

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "sys/services/touch_input.h"
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

class CurrencyRatesJsonListener;
class CurrencyHistoryJsonListener;
class CryptoMarketsJsonListener;
class CryptoHistoryJsonListener;

class CurrencyExchangeApp {
public:
  void reset();
  void release();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handlePowerButton();
  bool hasActiveSession() const;

private:
  friend class CurrencyRatesJsonListener;
  friend class CurrencyHistoryJsonListener;
  friend class CryptoMarketsJsonListener;
  friend class CryptoHistoryJsonListener;

  enum class State : uint8_t {
    Choice,
    LoadingFiatRates,
    FiatList,
    LoadingFiatHistory,
    FiatChart,
    LoadingCryptoMarkets,
    CryptoList,
    LoadingCryptoHistory,
    CryptoChart,
    Failed,
  };

  enum class CryptoRange : uint8_t {
    OneDay = 1,
    SevenDays = 7,
    ThirtyDays = 30,
  };

  static const unsigned long FETCH_TIMEOUT_MS = 10000;
  static const uint8_t MAX_RATE_COUNT = 180;
  static const uint8_t VISIBLE_RATE_COUNT = 8;
  static const uint8_t MAX_CRYPTO_COUNT = 30;
  static const uint16_t MAX_HISTORY_POINTS = 300;
  static const int MAX_RATES_JSON_BYTES = 64000;
  static const int MAX_HISTORY_JSON_BYTES = 24000;
  static const int MAX_CRYPTO_MARKETS_JSON_BYTES = 128000;
  static const int MAX_CRYPTO_HISTORY_JSON_BYTES = 96000;

  struct RateEntry {
    float rate = 0.0f;
    char date[11] = "";
    char quote[4] = "";
    bool valid = false;
  };

  struct CryptoEntry {
    float price = 0.0f;
    char id[48] = "";
    char symbol[12] = "";
  };
  static_assert(sizeof(CryptoEntry) == 64,
                "Crypto entries should span exactly two cache lines");

  struct CryptoHistoryCache {
    float *values = nullptr;
    uint16_t count = 0;
    char firstDate[11] = "";
    char lastDate[11] = "";
    unsigned long fetchedAt = 0;
    bool valid = false;
  };

  State state = State::Choice;
  State failedRequest = State::Choice;
  bool requestStarted = false;
  bool inverseFiatRates = false;
  uint8_t rateCount = 0;
  uint8_t cryptoCount = 0;
  uint16_t listOffset = 0;
  int16_t selectedRateSlot = -1;
  int8_t selectedCryptoIndex = -1;
  uint16_t historyCount = 0;
  CryptoRange cryptoRange = CryptoRange::OneDay;
  unsigned long cryptoRangeEnteredAt = 0;
  RateEntry *rates = nullptr;
  CryptoEntry *crypto = nullptr;
  CryptoHistoryCache cryptoHistoryCaches[3];
  float *historyRates = nullptr;
  char latestDate[11] = "";
  char historyFirstDate[11] = "";
  char historyLastDate[11] = "";
  char status[32] = "";

  bool ensureRateStorage();
  bool ensureCryptoStorage();
  bool ensureHistoryStorage();
  bool ensureCryptoHistoryCache(CryptoRange range);
  void clearRates();
  void clearCrypto();
  void clearHistory();
  void clearCryptoHistoryCaches();
  void releaseCryptoHistoryCaches();
  bool fetchRates();
  bool fetchFiatHistory();
  bool fetchCryptoMarkets();
  bool fetchCryptoHistory();
  void openRate(uint16_t visibleIndex);
  void openCrypto(uint16_t index);
  void setCryptoRange(CryptoRange range);
  bool showCachedCryptoHistory(CryptoRange range);
  void cacheCryptoHistory(CryptoRange range);
  static uint8_t cryptoRangeIndex(CryptoRange range);
  bool cryptoHistoryNeedsRefresh() const;
  int16_t rateSlotAt(uint16_t visibleIndex) const;
  const RateEntry *selectedRate() const;
  const CryptoEntry *selectedCrypto() const;
  void drawChoice(Adafruit_GFX &gfx) const;
  void drawLoading(Adafruit_GFX &gfx) const;
  void drawFailed(Adafruit_GFX &gfx) const;
  void drawRateList(Adafruit_GFX &gfx) const;
  void drawFiatChart(Adafruit_GFX &gfx) const;
  void drawCryptoList(Adafruit_GFX &gfx) const;
  void drawCryptoChart(Adafruit_GFX &gfx) const;
  void drawListScrollbar(Adafruit_GFX &gfx, uint16_t itemCount) const;
  void drawPriceChart(Adafruit_GFX &gfx, const char *title,
                      const char *priceText, int16_t bottom) const;
  void drawXAxisLabels(Adafruit_GFX &gfx, int16_t y) const;
  static void drawHeader(Adafruit_GFX &gfx, const char *left,
                         const char *right);
  static void drawCentered(Adafruit_GFX &gfx, const char *text, int16_t y);
  static void drawCenteredInRect(Adafruit_GFX &gfx, const char *text,
                                 int16_t x, int16_t y, int16_t width,
                                 int16_t height);
  static void drawRightText(Adafruit_GFX &gfx, const char *text, int16_t y);
  static void formatRate(float value, char *out, size_t outSize);
  static void formatCryptoPrice(float value, char *out, size_t outSize);
  static void formatAxisRate(float value, char *out, size_t outSize);
  float displayedFiatRate(float rate) const;
  void setStatus(const char *text);
};

#endif
#endif
