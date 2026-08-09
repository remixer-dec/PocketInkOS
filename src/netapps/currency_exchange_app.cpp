#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "netapps/currency_exchange_app.h"

#include "netapps/lightweight_json_parser.h"
#include "sys/global.h"
#include "sys/psram_allocator.h"
#include "sys/services/device_clock.h"
#include "ui/components/chart_component.h"
#include "ui/components/list_component.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr char kLatestRatesUrl[] =
    "https://api.frankfurter.dev/v2/rates?base=USD";
constexpr char kHistoryRatesUrl[] =
    "https://api.frankfurter.dev/v2/rates?base=USD&quotes=%s&from=%s&to=%s";
constexpr char kCryptoMarketsUrl[] =
    "https://api.coingecko.com/api/v3/coins/markets?vs_currency=USD&"
    "per_page=30&page=1&sparkline=false";
constexpr char kCryptoHistoryUrl[] =
    "https://api.coingecko.com/api/v3/coins/%s/market_chart?"
    "vs_currency=USD&days=%u";
constexpr int16_t kChoiceY = 54;
constexpr int16_t kChoiceRowHeight = 52;
constexpr int16_t kListY = 22;
constexpr int16_t kRateRowHeight = 22;
constexpr int16_t kListHeight = EPD_HEIGHT - kListY;
constexpr int16_t kListSplitX = 180;
constexpr int16_t kListValueRightX = 174;
constexpr int16_t kChartBottomFiat = 156;
constexpr int16_t kChartBottomCrypto = 148;
constexpr int16_t kXAxisLabelGap = 7;
constexpr int16_t kCryptoRangeButtonY = 168;
constexpr int16_t kCryptoRangeButtonWidth = 44;
constexpr int16_t kCryptoRangeButtonHeight = 26;
constexpr int16_t kCryptoRangeButtonXs[] = {26, 78, 130};
constexpr unsigned long kCryptoOneDayRefreshMs = 60000;

void copyText(char *out, size_t outSize, const char *value) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  strncpy(out, value ? value : "", outSize - 1);
  out[outSize - 1] = '\0';
}

void uppercaseAscii(char *text) {
  if (text == nullptr) {
    return;
  }
  for (; *text != '\0'; text++) {
    if (*text >= 'a' && *text <= 'z') {
      *text = static_cast<char>(*text - 'a' + 'A');
    }
  }
}

bool isLeapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(int year, int month) {
  static constexpr uint8_t kMonthDays[] = {31, 28, 31, 30, 31, 30,
                                           31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return month >= 1 && month <= 12 ? kMonthDays[month - 1] : 0;
}

bool dateDaysBefore(const char *date, uint8_t daysBefore, char *out,
                    size_t outSize) {
  int year = 0;
  int month = 0;
  int day = 0;
  char trailing = '\0';
  if (date == nullptr ||
      sscanf(date, "%4d-%2d-%2d%c", &year, &month, &day, &trailing) != 3 ||
      year < 1948 || month < 1 || month > 12 || day < 1 ||
      day > daysInMonth(year, month)) {
    return false;
  }

  while (daysBefore-- > 0) {
    if (--day > 0) {
      continue;
    }
    if (--month == 0) {
      month = 12;
      year--;
    }
    day = daysInMonth(year, month);
  }
  return snprintf(out, outSize, "%04d-%02d-%02d", year, month, day) == 10;
}

void civilFromUnixDays(int64_t days, int &year, unsigned &month,
                       unsigned &day) {
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(z - era * 146097);
  const unsigned yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 -
       dayOfEra / 146096) /
      365;
  year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
  const unsigned dayOfYear =
      dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPart = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthPart + 2) / 5 + 1;
  month = monthPart + (monthPart < 10 ? 3 : -9);
  year += month <= 2;
}

bool isoDateFromTimestamp(double timestamp, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0 || !std::isfinite(timestamp) ||
      timestamp < 0.0) {
    return false;
  }
  const int64_t unixSeconds =
      static_cast<int64_t>(timestamp / 1000.0) + deviceClock.utcOffsetSeconds();
  int64_t days = unixSeconds / 86400;
  if (unixSeconds < 0 && unixSeconds % 86400 != 0) {
    days--;
  }
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromUnixDays(days, year, month, day);
  return snprintf(out, outSize, "%04d-%02u-%02u", year, month, day) == 10;
}

void formatDateLabel(const char *isoDate, char *out, size_t outSize,
                     bool includeYear = false) {
  if (!DeviceClock::formatDateString(isoDate, out, outSize, includeYear)) {
    copyText(out, outSize, "--");
  }
}

} // namespace

class CurrencyRatesJsonListener : public JsonStreamListener {
public:
  CurrencyRatesJsonListener(CurrencyExchangeApp::RateEntry *target,
                            uint8_t capacity)
      : rates(target), maxRates(capacity) {}

  void onObjectStart(int depth, const char *) override {
    if (depth != 2) {
      return;
    }
    quote[0] = '\0';
    date[0] = '\0';
    rate = 0.0f;
    hasRate = false;
  }

  void onObjectEnd(int depth) override {
    if (depth != 2 || !hasRate || quote[0] == '\0' || count >= maxRates) {
      return;
    }
    CurrencyExchangeApp::RateEntry &entry = rates[count++];
    entry.rate = rate;
    copyText(entry.quote, sizeof(entry.quote), quote);
    copyText(entry.date, sizeof(entry.date), date);
    entry.valid = true;
  }

  void onStringValue(int depth, const char *key, const char *value) override {
    if (depth != 2) {
      return;
    }
    if (strcmp(key, "quote") == 0) {
      copyText(quote, sizeof(quote), value);
    } else if (strcmp(key, "date") == 0) {
      copyText(date, sizeof(date), value);
    }
  }

  void onNumericValue(int depth, const char *key, double value) override {
    if (depth == 2 && strcmp(key, "rate") == 0) {
      rate = static_cast<float>(value);
      hasRate = true;
    }
  }

  uint8_t count = 0;

private:
  CurrencyExchangeApp::RateEntry *rates;
  uint8_t maxRates;
  float rate = 0.0f;
  char date[11] = "";
  char quote[4] = "";
  bool hasRate = false;
};

class CurrencyHistoryJsonListener : public JsonStreamListener {
public:
  CurrencyHistoryJsonListener(float *target, uint16_t capacity)
      : values(target), maxValues(capacity) {}

  void onObjectStart(int depth, const char *) override {
    if (depth != 2) {
      return;
    }
    date[0] = '\0';
    rate = 0.0f;
    hasRate = false;
  }

  void onObjectEnd(int depth) override {
    if (depth != 2 || !hasRate || count >= maxValues) {
      return;
    }
    values[count++] = rate;
    if (firstDate[0] == '\0') {
      copyText(firstDate, sizeof(firstDate), date);
    }
    copyText(lastDate, sizeof(lastDate), date);
  }

  void onStringValue(int depth, const char *key, const char *value) override {
    if (depth == 2 && strcmp(key, "date") == 0) {
      copyText(date, sizeof(date), value);
    }
  }

  void onNumericValue(int depth, const char *key, double value) override {
    if (depth == 2 && strcmp(key, "rate") == 0) {
      rate = static_cast<float>(value);
      hasRate = true;
    }
  }

  uint16_t count = 0;
  char firstDate[11] = "";
  char lastDate[11] = "";

private:
  float *values;
  uint16_t maxValues;
  float rate = 0.0f;
  char date[11] = "";
  bool hasRate = false;
};

class CryptoMarketsJsonListener : public JsonStreamListener {
public:
  CryptoMarketsJsonListener(CurrencyExchangeApp::CryptoEntry *target,
                            uint8_t capacity)
      : entries(target), maxEntries(capacity) {}

  void onObjectStart(int depth, const char *) override {
    if (depth != 2) {
      return;
    }
    id[0] = '\0';
    symbol[0] = '\0';
    price = 0.0f;
    hasPrice = false;
  }

  void onObjectEnd(int depth) override {
    if (depth != 2 || count >= maxEntries || !hasPrice || id[0] == '\0' ||
        symbol[0] == '\0') {
      return;
    }
    CurrencyExchangeApp::CryptoEntry &entry = entries[count++];
    entry.price = price;
    copyText(entry.id, sizeof(entry.id), id);
    copyText(entry.symbol, sizeof(entry.symbol), symbol);
    uppercaseAscii(entry.symbol);
  }

  void onStringValue(int depth, const char *key, const char *value) override {
    if (depth != 2 || count >= maxEntries) {
      return;
    }
    if (strcmp(key, "id") == 0) {
      copyText(id, sizeof(id), value);
    } else if (strcmp(key, "symbol") == 0) {
      copyText(symbol, sizeof(symbol), value);
    }
  }

  void onNumericValue(int depth, const char *key, double value) override {
    if (depth == 2 && count < maxEntries &&
        strcmp(key, "current_price") == 0) {
      price = static_cast<float>(value);
      hasPrice = true;
    }
  }

  uint8_t count = 0;

private:
  CurrencyExchangeApp::CryptoEntry *entries;
  uint8_t maxEntries;
  float price = 0.0f;
  char id[48] = "";
  char symbol[12] = "";
  bool hasPrice = false;
};

class CryptoHistoryJsonListener : public JsonStreamListener {
public:
  CryptoHistoryJsonListener(float *target, uint16_t capacity, char *firstDate,
                            char *lastDate)
      : values(target), maxValues(capacity), firstDate(firstDate),
        lastDate(lastDate) {}

  void onArrayStart(int depth, const char *key) override {
    if (depth == 2) {
      // market_chart also returns caps and volumes; only retain price pairs.
      inPrices = strcmp(key, "prices") == 0;
      return;
    }
    if (depth == 3 && inPrices) {
      pairIndex = 0;
      timestamp = 0.0;
      hasTimestamp = false;
    }
  }

  void onArrayEnd(int depth) override {
    if (depth == 3 && inPrices) {
      pairIndex = 0;
    } else if (depth == 2) {
      inPrices = false;
    }
  }

  void onNumericValue(int depth, const char *, double value) override {
    if (depth != 3 || !inPrices) {
      return;
    }
    if (pairIndex == 0) {
      timestamp = value;
      hasTimestamp = true;
    } else if (pairIndex == 1 && count < maxValues) {
      values[count++] = static_cast<float>(value);
      char isoDate[11] = "";
      if (hasTimestamp && isoDateFromTimestamp(timestamp, isoDate,
                                               sizeof(isoDate))) {
        if (firstDate != nullptr && firstDate[0] == '\0') {
          copyText(firstDate, 11, isoDate);
        }
        if (lastDate != nullptr) {
          copyText(lastDate, 11, isoDate);
        }
      }
    }
    pairIndex++;
  }

  uint16_t count = 0;

private:
  float *values;
  uint16_t maxValues;
  char *firstDate;
  char *lastDate;
  uint8_t pairIndex = 0;
  bool inPrices = false;
  double timestamp = 0.0;
  bool hasTimestamp = false;
};

void CurrencyExchangeApp::reset() {
  state = State::Choice;
  failedRequest = State::Choice;
  requestStarted = false;
  inverseFiatRates = false;
  listOffset = 0;
  selectedRateSlot = -1;
  selectedCryptoIndex = -1;
  cryptoRange = CryptoRange::OneDay;
  cryptoRangeEnteredAt = 0;
  clearCryptoHistoryCaches();
  latestDate[0] = '\0';
  setStatus("");
  clearRates();
  clearCrypto();
  clearHistory();
}

void CurrencyExchangeApp::release() {
  if (rates != nullptr) {
    psram::release(rates);
    rates = nullptr;
  }
  if (crypto != nullptr) {
    psram::release(crypto);
    crypto = nullptr;
  }
  if (historyRates != nullptr) {
    psram::release(historyRates);
    historyRates = nullptr;
  }
  releaseCryptoHistoryCaches();
  rateCount = 0;
  cryptoCount = 0;
  historyCount = 0;
  state = State::Choice;
  failedRequest = State::Choice;
  requestStarted = false;
  selectedRateSlot = -1;
  selectedCryptoIndex = -1;
  cryptoRangeEnteredAt = 0;
}

bool CurrencyExchangeApp::hasActiveSession() const {
  return state != State::Failed;
}

bool CurrencyExchangeApp::ensureRateStorage() {
  if (rates == nullptr) {
    rates = static_cast<RateEntry *>(
        psram::allocate(sizeof(RateEntry) * MAX_RATE_COUNT));
  }
  return rates != nullptr;
}

bool CurrencyExchangeApp::ensureCryptoStorage() {
  if (crypto == nullptr) {
    crypto = static_cast<CryptoEntry *>(
        psram::allocate(sizeof(CryptoEntry) * MAX_CRYPTO_COUNT));
  }
  return crypto != nullptr;
}

bool CurrencyExchangeApp::ensureHistoryStorage() {
  if (historyRates == nullptr) {
    historyRates = static_cast<float *>(
        psram::allocate(sizeof(float) * MAX_HISTORY_POINTS));
  }
  return historyRates != nullptr;
}

bool CurrencyExchangeApp::ensureCryptoHistoryCache(CryptoRange range) {
  CryptoHistoryCache &cache = cryptoHistoryCaches[cryptoRangeIndex(range)];
  if (cache.values == nullptr) {
    cache.values = static_cast<float *>(
        psram::allocate(sizeof(float) * MAX_HISTORY_POINTS));
  }
  return cache.values != nullptr;
}

void CurrencyExchangeApp::clearRates() {
  if (rates != nullptr) {
    for (uint8_t i = 0; i < MAX_RATE_COUNT; i++) {
      rates[i] = RateEntry();
    }
  }
  rateCount = 0;
}

void CurrencyExchangeApp::clearCrypto() {
  if (crypto != nullptr) {
    for (uint8_t i = 0; i < MAX_CRYPTO_COUNT; i++) {
      crypto[i] = CryptoEntry();
    }
  }
  cryptoCount = 0;
}

void CurrencyExchangeApp::clearHistory() {
  if (historyRates != nullptr) {
    for (uint16_t i = 0; i < MAX_HISTORY_POINTS; i++) {
      historyRates[i] = 0.0f;
    }
  }
  historyCount = 0;
  historyFirstDate[0] = '\0';
  historyLastDate[0] = '\0';
}

void CurrencyExchangeApp::clearCryptoHistoryCaches() {
  for (CryptoHistoryCache &cache : cryptoHistoryCaches) {
    cache.count = 0;
    cache.firstDate[0] = '\0';
    cache.lastDate[0] = '\0';
    cache.fetchedAt = 0;
    cache.valid = false;
  }
}

void CurrencyExchangeApp::releaseCryptoHistoryCaches() {
  for (CryptoHistoryCache &cache : cryptoHistoryCaches) {
    if (cache.values != nullptr) {
      psram::release(cache.values);
      cache.values = nullptr;
    }
  }
  clearCryptoHistoryCaches();
}

void CurrencyExchangeApp::draw(Adafruit_GFX &gfx) {
  gfx.setFont();
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  switch (state) {
  case State::Choice:
    drawChoice(gfx);
    break;
  case State::LoadingFiatRates:
  case State::LoadingFiatHistory:
  case State::LoadingCryptoMarkets:
  case State::LoadingCryptoHistory:
    drawLoading(gfx);
    break;
  case State::FiatList:
    drawRateList(gfx);
    break;
  case State::FiatChart:
    drawFiatChart(gfx);
    break;
  case State::CryptoList:
    drawCryptoList(gfx);
    break;
  case State::CryptoChart:
    drawCryptoChart(gfx);
    break;
  case State::Failed:
    drawFailed(gfx);
    break;
  }
}

bool CurrencyExchangeApp::update() {
  if (state == State::CryptoChart && cryptoHistoryNeedsRefresh()) {
    state = State::LoadingCryptoHistory;
    requestStarted = false;
    return true;
  }

  const bool loading = state == State::LoadingFiatRates ||
                       state == State::LoadingFiatHistory ||
                       state == State::LoadingCryptoMarkets ||
                       state == State::LoadingCryptoHistory;
  if (requestStarted || !loading) {
    return false;
  }

  requestStarted = true;
  failedRequest = state;
  bool ok = false;
  State nextState = State::Failed;
  switch (state) {
  case State::LoadingFiatRates:
    ok = fetchRates();
    nextState = State::FiatList;
    break;
  case State::LoadingFiatHistory:
    ok = fetchFiatHistory();
    nextState = State::FiatChart;
    break;
  case State::LoadingCryptoMarkets:
    ok = fetchCryptoMarkets();
    nextState = State::CryptoList;
    break;
  case State::LoadingCryptoHistory:
    ok = fetchCryptoHistory();
    nextState = State::CryptoChart;
    break;
  default:
    break;
  }
  state = ok ? nextState : State::Failed;
  return true;
}

bool CurrencyExchangeApp::handleTouch(const TouchPoint &point) {
  if (state == State::Failed) {
    state = failedRequest;
    requestStarted = false;
    return true;
  }

  if (state == State::Choice) {
    uint16_t offset = 0;
    const UiListTouchResult result = uiListHandleTouch(
        point, 2, 2, kChoiceY, kChoiceRowHeight, kChoiceRowHeight * 2,
        EPD_WIDTH, offset);
    if (result.action != UiListTouchAction::Select) {
      return false;
    }
    listOffset = 0;
    selectedRateSlot = -1;
    selectedCryptoIndex = -1;
    requestStarted = false;
    state = result.index == 0 ? State::LoadingFiatRates
                              : State::LoadingCryptoMarkets;
    return true;
  }

  if (state == State::CryptoChart) {
    if (point.y < kCryptoRangeButtonY ||
        point.y >= kCryptoRangeButtonY + kCryptoRangeButtonHeight) {
      return false;
    }
    CryptoRange range;
    if (point.x >= kCryptoRangeButtonXs[0] &&
        point.x < kCryptoRangeButtonXs[0] + kCryptoRangeButtonWidth) {
      range = CryptoRange::OneDay;
    } else if (point.x >= kCryptoRangeButtonXs[1] &&
               point.x < kCryptoRangeButtonXs[1] + kCryptoRangeButtonWidth) {
      range = CryptoRange::SevenDays;
    } else if (point.x >= kCryptoRangeButtonXs[2] &&
               point.x < kCryptoRangeButtonXs[2] + kCryptoRangeButtonWidth) {
      range = CryptoRange::ThirtyDays;
    } else {
      return false;
    }
    if (range == cryptoRange) {
      return false;
    }
    setCryptoRange(range);
    return true;
  }

  const bool fiatList = state == State::FiatList;
  const bool cryptoList = state == State::CryptoList;
  if (!fiatList && !cryptoList) {
    return false;
  }

  uint16_t nextOffset = listOffset;
  const uint16_t itemCount = fiatList ? rateCount : cryptoCount;
  const UiListTouchResult result = uiListHandleTouch(
      point, itemCount, VISIBLE_RATE_COUNT, kListY, kRateRowHeight,
      kListHeight, kListSplitX, nextOffset);
  if (result.action == UiListTouchAction::Scroll) {
    const int8_t pageRemainder =
        static_cast<int8_t>(VISIBLE_RATE_COUNT - 1);
    uiListScrollBy(nextOffset, itemCount, VISIBLE_RATE_COUNT,
                   point.y < kListY + kListHeight / 2 ? -pageRemainder
                                                     : pageRemainder);
    listOffset = nextOffset;
    selectedRateSlot = -1;
    selectedCryptoIndex = -1;
    return true;
  }
  listOffset = nextOffset;
  if (result.action != UiListTouchAction::Select) {
    return false;
  }
  if (fiatList) {
    const int16_t slot = rateSlotAt(result.index);
    if (selectedRateSlot != slot) {
      selectedRateSlot = slot;
      setStatus("");
      return true;
    }
    openRate(result.index);
  } else {
    if (selectedCryptoIndex != static_cast<int8_t>(result.index)) {
      selectedCryptoIndex = static_cast<int8_t>(result.index);
      setStatus("");
      return true;
    }
    openCrypto(result.index);
  }
  return true;
}

bool CurrencyExchangeApp::handleMenuButton() {
  if (state == State::FiatChart || state == State::LoadingFiatHistory ||
      (state == State::Failed &&
       failedRequest == State::LoadingFiatHistory)) {
    state = State::FiatList;
    requestStarted = false;
    return true;
  }
  if (state == State::CryptoChart || state == State::LoadingCryptoHistory ||
      (state == State::Failed &&
       failedRequest == State::LoadingCryptoHistory)) {
    state = State::CryptoList;
    requestStarted = false;
    return true;
  }
  if (state == State::FiatList || state == State::LoadingFiatRates ||
      state == State::CryptoList || state == State::LoadingCryptoMarkets ||
      state == State::Failed) {
    state = State::Choice;
    failedRequest = State::Choice;
    requestStarted = false;
    listOffset = 0;
    return true;
  }
  return false;
}

bool CurrencyExchangeApp::handlePowerButton() {
  if (state != State::FiatList && state != State::FiatChart) {
    return false;
  }

  inverseFiatRates = !inverseFiatRates;
  if (state == State::FiatChart && historyRates != nullptr) {
    for (uint16_t i = 0; i < historyCount; i++) {
      if (historyRates[i] > 0.0f) {
        historyRates[i] = 1.0f / historyRates[i];
      }
    }
  }
  return true;
}

bool CurrencyExchangeApp::fetchRates() {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected");
    return false;
  }
  if (!ensureRateStorage()) {
    setStatus("PSRAM unavailable");
    return false;
  }
  latestDate[0] = '\0';
  clearRates();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  // getStreamPtr() exposes HTTP/1.1 chunk framing. Request connection-close
  // responses so the JSON parser receives only the response body.
  http.useHTTP10(true);
  http.setTimeout(FETCH_TIMEOUT_MS);
  if (!http.begin(client, kLatestRatesUrl)) {
    setStatus("HTTP setup failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setStatus("Rates request failed");
    http.end();
    return false;
  }

  CurrencyRatesJsonListener listener(rates, MAX_RATE_COUNT);
  LightweightJsonParser parser;
  const bool parsed =
      parser.parse(http.getStreamPtr(), http.getSize(), FETCH_TIMEOUT_MS,
                   MAX_RATES_JSON_BYTES, listener, status, sizeof(status));
  http.end();

  rateCount = listener.count;
  for (uint8_t i = 0; i < rateCount; i++) {
    if (latestDate[0] == '\0' || strcmp(rates[i].date, latestDate) > 0) {
      copyText(latestDate, sizeof(latestDate), rates[i].date);
    }
  }
  if (!parsed && rateCount == 0) {
    return false;
  }
  if (rateCount == 0 || latestDate[0] == '\0') {
    setStatus("No rates available");
    return false;
  }
  return true;
}

bool CurrencyExchangeApp::fetchFiatHistory() {
  const RateEntry *entry = selectedRate();
  if (entry == nullptr) {
    setStatus("Select a currency");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected");
    return false;
  }
  if (!ensureHistoryStorage()) {
    setStatus("PSRAM unavailable");
    return false;
  }
  clearHistory();

  char fromDate[11] = "";
  if (!dateDaysBefore(entry->date, 29, fromDate, sizeof(fromDate))) {
    setStatus("Invalid rate date");
    return false;
  }
  char url[192] = "";
  const int urlLength =
      snprintf(url, sizeof(url), kHistoryRatesUrl, entry->quote, fromDate,
               entry->date);
  if (urlLength < 0 || static_cast<size_t>(urlLength) >= sizeof(url)) {
    setStatus("History URL too long");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(FETCH_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    setStatus("HTTP setup failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setStatus("History request failed");
    http.end();
    return false;
  }

  CurrencyHistoryJsonListener listener(historyRates, MAX_HISTORY_POINTS);
  LightweightJsonParser parser;
  const bool parsed =
      parser.parse(http.getStreamPtr(), http.getSize(), FETCH_TIMEOUT_MS,
                   MAX_HISTORY_JSON_BYTES, listener, status, sizeof(status));
  http.end();

  historyCount = listener.count;
  copyText(historyFirstDate, sizeof(historyFirstDate), listener.firstDate);
  copyText(historyLastDate, sizeof(historyLastDate), listener.lastDate);
  if (inverseFiatRates) {
    for (uint16_t i = 0; i < historyCount; i++) {
      historyRates[i] = displayedFiatRate(historyRates[i]);
    }
  }
  if (!parsed && historyCount == 0) {
    return false;
  }
  if (historyCount == 0) {
    setStatus("No history available");
    return false;
  }
  return true;
}

bool CurrencyExchangeApp::fetchCryptoMarkets() {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected");
    return false;
  }
  if (!ensureCryptoStorage()) {
    setStatus("PSRAM unavailable");
    return false;
  }
  clearCrypto();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setUserAgent("PocketInk/1.0");
  http.setTimeout(FETCH_TIMEOUT_MS);
  if (!http.begin(client, kCryptoMarketsUrl)) {
    setStatus("HTTP setup failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setStatus(code == 429 ? "CoinGecko rate limit" : "Markets request failed");
    http.end();
    return false;
  }

  // Stream the full response, but retain only the leading market-cap entries.
  CryptoMarketsJsonListener listener(crypto, MAX_CRYPTO_COUNT);
  LightweightJsonParser parser;
  const bool parsed = parser.parse(
      http.getStreamPtr(), http.getSize(), FETCH_TIMEOUT_MS,
      MAX_CRYPTO_MARKETS_JSON_BYTES, listener, status, sizeof(status));
  http.end();

  cryptoCount = listener.count;
  if (!parsed && cryptoCount == 0) {
    return false;
  }
  if (cryptoCount == 0) {
    setStatus("No crypto markets");
    return false;
  }
  return true;
}

bool CurrencyExchangeApp::fetchCryptoHistory() {
  const CryptoEntry *entry = selectedCrypto();
  if (entry == nullptr) {
    setStatus("Select a coin");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected");
    return false;
  }
  if (!ensureHistoryStorage()) {
    setStatus("PSRAM unavailable");
    return false;
  }
  clearHistory();

  char url[192] = "";
  const unsigned days = static_cast<unsigned>(cryptoRange);
  const int urlLength =
      snprintf(url, sizeof(url), kCryptoHistoryUrl, entry->id, days);
  if (urlLength < 0 || static_cast<size_t>(urlLength) >= sizeof(url)) {
    setStatus("History URL too long");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setUserAgent("PocketInk/1.0");
  http.setTimeout(FETCH_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    setStatus("HTTP setup failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setStatus(code == 429 ? "CoinGecko rate limit" : "History request failed");
    http.end();
    return false;
  }

  CryptoHistoryJsonListener listener(historyRates, MAX_HISTORY_POINTS,
                                     historyFirstDate, historyLastDate);
  LightweightJsonParser parser;
  const bool parsed = parser.parse(
      http.getStreamPtr(), http.getSize(), FETCH_TIMEOUT_MS,
      MAX_CRYPTO_HISTORY_JSON_BYTES, listener, status, sizeof(status));
  http.end();

  historyCount = listener.count;
  if (!parsed && historyCount == 0) {
    return false;
  }
  if (historyCount == 0) {
    setStatus("No price history");
    return false;
  }
  cacheCryptoHistory(cryptoRange);
  return true;
}

void CurrencyExchangeApp::openRate(uint16_t visibleIndex) {
  const int16_t slot = rateSlotAt(visibleIndex);
  if (slot < 0) {
    return;
  }
  selectedRateSlot = slot;
  requestStarted = false;
  state = State::LoadingFiatHistory;
}

void CurrencyExchangeApp::openCrypto(uint16_t index) {
  if (crypto == nullptr || index >= cryptoCount) {
    return;
  }
  selectedCryptoIndex = static_cast<int8_t>(index);
  clearCryptoHistoryCaches();
  cryptoRange = CryptoRange::OneDay;
  cryptoRangeEnteredAt = millis();
  requestStarted = false;
  state = State::LoadingCryptoHistory;
}

void CurrencyExchangeApp::setCryptoRange(CryptoRange range) {
  if (selectedCrypto() == nullptr || range == cryptoRange) {
    return;
  }
  cryptoRange = range;
  cryptoRangeEnteredAt = millis();
  if (showCachedCryptoHistory(range)) {
    return;
  }
  requestStarted = false;
  state = State::LoadingCryptoHistory;
}

uint8_t CurrencyExchangeApp::cryptoRangeIndex(CryptoRange range) {
  switch (range) {
  case CryptoRange::SevenDays:
    return 1;
  case CryptoRange::ThirtyDays:
    return 2;
  case CryptoRange::OneDay:
  default:
    return 0;
  }
}

bool CurrencyExchangeApp::showCachedCryptoHistory(CryptoRange range) {
  const CryptoHistoryCache &cache = cryptoHistoryCaches[cryptoRangeIndex(range)];
  if (!cache.valid || cache.values == nullptr || cache.count == 0 ||
      !ensureHistoryStorage()) {
    return false;
  }
  memcpy(historyRates, cache.values, sizeof(float) * cache.count);
  historyCount = cache.count;
  copyText(historyFirstDate, sizeof(historyFirstDate), cache.firstDate);
  copyText(historyLastDate, sizeof(historyLastDate), cache.lastDate);
  if (range == CryptoRange::OneDay) {
    cryptoRangeEnteredAt = millis();
  }
  requestStarted = false;
  state = State::CryptoChart;
  return true;
}

void CurrencyExchangeApp::cacheCryptoHistory(CryptoRange range) {
  if (!ensureCryptoHistoryCache(range)) {
    return;
  }
  CryptoHistoryCache &cache = cryptoHistoryCaches[cryptoRangeIndex(range)];
  memcpy(cache.values, historyRates, sizeof(float) * historyCount);
  cache.count = historyCount;
  copyText(cache.firstDate, sizeof(cache.firstDate), historyFirstDate);
  copyText(cache.lastDate, sizeof(cache.lastDate), historyLastDate);
  cache.fetchedAt = millis();
  cache.valid = true;
  if (range == CryptoRange::OneDay) {
    cryptoRangeEnteredAt = cache.fetchedAt;
  }
}

bool CurrencyExchangeApp::cryptoHistoryNeedsRefresh() const {
  if (cryptoRange != CryptoRange::OneDay) {
    return false;
  }
  const CryptoHistoryCache &cache = cryptoHistoryCaches[0];
  return cache.valid && cryptoRangeEnteredAt != 0 &&
         static_cast<unsigned long>(millis() - cryptoRangeEnteredAt) >=
             kCryptoOneDayRefreshMs;
}

int16_t CurrencyExchangeApp::rateSlotAt(uint16_t visibleIndex) const {
  if (rates == nullptr || visibleIndex >= rateCount) {
    return -1;
  }
  return static_cast<int16_t>(visibleIndex);
}

const CurrencyExchangeApp::RateEntry *
CurrencyExchangeApp::selectedRate() const {
  if (rates == nullptr || selectedRateSlot < 0 ||
      selectedRateSlot >= static_cast<int16_t>(rateCount) ||
      !rates[selectedRateSlot].valid) {
    return nullptr;
  }
  return &rates[selectedRateSlot];
}

const CurrencyExchangeApp::CryptoEntry *
CurrencyExchangeApp::selectedCrypto() const {
  if (crypto == nullptr || selectedCryptoIndex < 0 ||
      selectedCryptoIndex >= static_cast<int8_t>(cryptoCount)) {
    return nullptr;
  }
  return &crypto[selectedCryptoIndex];
}

void CurrencyExchangeApp::drawChoice(Adafruit_GFX &gfx) const {
  drawHeader(gfx, "MARKETS", "");
  gfx.drawRect(20, kChoiceY, EPD_WIDTH - 40, 42, 1);
  drawCentered(gfx, "CURRENCY", kChoiceY + 17);
  gfx.drawRect(20, kChoiceY + kChoiceRowHeight, EPD_WIDTH - 40, 42, 1);
  drawCentered(gfx, "CRYPTO", kChoiceY + kChoiceRowHeight + 17);
}

void CurrencyExchangeApp::drawLoading(Adafruit_GFX &gfx) const {
  char text[28] = "";
  if (state == State::LoadingFiatRates) {
    drawHeader(gfx, "USD RATES", "");
    copyText(text, sizeof(text), "Loading rates...");
  } else if (state == State::LoadingFiatHistory) {
    const RateEntry *entry = selectedRate();
    char latestDateText[20] = "";
    formatDateLabel(latestDate, latestDateText, sizeof(latestDateText), true);
    drawHeader(gfx, "USD RATES", latestDateText);
    if (inverseFiatRates) {
      snprintf(text, sizeof(text), "Loading %s/USD...",
               entry != nullptr ? entry->quote : "---");
    } else {
      snprintf(text, sizeof(text), "Loading USD/%s...",
               entry != nullptr ? entry->quote : "---");
    }
  } else if (state == State::LoadingCryptoMarkets) {
    drawHeader(gfx, "CRYPTO USD", "TOP 30");
    copyText(text, sizeof(text), "Loading markets...");
  } else {
    const CryptoEntry *entry = selectedCrypto();
    const char *rangeLabel =
        cryptoRange == CryptoRange::OneDay
            ? "1D"
            : cryptoRange == CryptoRange::SevenDays ? "7D" : "30D";
    drawHeader(gfx, "CRYPTO USD", rangeLabel);
    snprintf(text, sizeof(text), "Loading %s prices...",
             entry != nullptr ? entry->symbol : "---");
  }
  drawCentered(gfx, text, 94);
}

void CurrencyExchangeApp::drawFailed(Adafruit_GFX &gfx) const {
  const bool cryptoRequest =
      failedRequest == State::LoadingCryptoMarkets ||
      failedRequest == State::LoadingCryptoHistory;
  drawHeader(gfx, cryptoRequest ? "CRYPTO USD" : "USD RATES", "");
  drawCentered(gfx, status[0] ? status : "Request failed", 82);
  drawCentered(gfx, "Touch to retry", 104);
  drawCentered(gfx,
               failedRequest == State::LoadingFiatHistory ||
                       failedRequest == State::LoadingCryptoHistory
                   ? "Menu: list"
                   : "Menu: markets",
               126);
}

void CurrencyExchangeApp::drawRateList(Adafruit_GFX &gfx) const {
  char latestDateText[20] = "";
  formatDateLabel(latestDate, latestDateText, sizeof(latestDateText), true);
  drawHeader(gfx, inverseFiatRates ? "1 UNIT = USD" : "1 USD =",
             latestDateText);
  if (rates == nullptr || rateCount == 0) {
    drawCentered(gfx, "No rates", 94);
    return;
  }

  const uint8_t visible =
      rateCount - listOffset < VISIBLE_RATE_COUNT
          ? static_cast<uint8_t>(rateCount - listOffset)
          : VISIBLE_RATE_COUNT;
  for (uint8_t row = 0; row < visible; row++) {
    const int16_t slot = rateSlotAt(listOffset + row);
    if (slot < 0) {
      continue;
    }
    const RateEntry &entry = rates[slot];
    const int16_t y = static_cast<int16_t>(kListY + row * kRateRowHeight);
    const bool selected = slot == selectedRateSlot;
    if (selected) {
      gfx.fillRect(2, y - 3, kListSplitX - 6, kRateRowHeight - 1, 1);
      gfx.setTextColor(0);
    }
    gfx.setCursor(8, y + 7);
    gfx.print(entry.quote);

    char rateText[16] = "";
    formatRate(displayedFiatRate(entry.rate), rateText, sizeof(rateText));
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    gfx.getTextBounds(rateText, 0, y + 7, &x1, &y1, &width, &height);
    gfx.setCursor(kListValueRightX - static_cast<int16_t>(width) - x1,
                  y + 7);
    gfx.print(rateText);
    if (selected) {
      gfx.setTextColor(1);
    }
    if (row + 1 < visible) {
      gfx.drawLine(4, y + kRateRowHeight - 1, kListSplitX - 5,
                   y + kRateRowHeight - 1, 1);
    }
  }
  drawListScrollbar(gfx, rateCount);
}

void CurrencyExchangeApp::drawFiatChart(Adafruit_GFX &gfx) const {
  const RateEntry *entry = selectedRate();
  if (entry == nullptr || historyRates == nullptr || historyCount == 0) {
    drawCentered(gfx, "No chart data", 94);
    return;
  }

  char title[12] = "";
  if (inverseFiatRates) {
    snprintf(title, sizeof(title), "%s/USD", entry->quote);
  } else {
    snprintf(title, sizeof(title), "USD/%s", entry->quote);
  }
  char rateText[16] = "";
  formatRate(displayedFiatRate(entry->rate), rateText, sizeof(rateText));
  drawPriceChart(gfx, title, rateText, kChartBottomFiat);
  drawXAxisLabels(gfx, kChartBottomFiat + kXAxisLabelGap);
}

void CurrencyExchangeApp::drawCryptoList(Adafruit_GFX &gfx) const {
  drawHeader(gfx, "CRYPTO USD", "TOP 30");
  if (crypto == nullptr || cryptoCount == 0) {
    drawCentered(gfx, "No crypto markets", 94);
    return;
  }

  const uint8_t visible =
      cryptoCount - listOffset < VISIBLE_RATE_COUNT
          ? static_cast<uint8_t>(cryptoCount - listOffset)
          : VISIBLE_RATE_COUNT;
  for (uint8_t row = 0; row < visible; row++) {
    const uint16_t index = listOffset + row;
    const CryptoEntry &entry = crypto[index];
    const int16_t y = static_cast<int16_t>(kListY + row * kRateRowHeight);
    const bool selected =
        static_cast<int8_t>(index) == selectedCryptoIndex;
    if (selected) {
      gfx.fillRect(2, y - 3, kListSplitX - 6, kRateRowHeight - 1, 1);
      gfx.setTextColor(0);
    }
    gfx.setCursor(5, y + 7);
    gfx.print(static_cast<int>(index + 1));
    gfx.setCursor(28, y + 7);
    gfx.print(entry.symbol);

    char priceText[18] = "";
    formatCryptoPrice(entry.price, priceText, sizeof(priceText));
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    gfx.getTextBounds(priceText, 0, y + 7, &x1, &y1, &width, &height);
    gfx.setCursor(kListValueRightX - static_cast<int16_t>(width) - x1,
                  y + 7);
    gfx.print(priceText);
    if (selected) {
      gfx.setTextColor(1);
    }
    if (row + 1 < visible) {
      gfx.drawLine(4, y + kRateRowHeight - 1, kListSplitX - 5,
                   y + kRateRowHeight - 1, 1);
    }
  }

  drawListScrollbar(gfx, cryptoCount);
}

void CurrencyExchangeApp::drawListScrollbar(Adafruit_GFX &gfx,
                                             uint16_t itemCount) const {
  if (itemCount == 0) {
    return;
  }

  const int16_t trackTop = kListY + 4;
  const int16_t trackHeight = kListHeight - 8;
  const uint16_t maxOffset =
      uiListMaxOffset(itemCount, VISIBLE_RATE_COUNT);
  int16_t thumbHeight = itemCount <= VISIBLE_RATE_COUNT
                            ? trackHeight
                            : static_cast<int16_t>(
                                  static_cast<int32_t>(trackHeight) *
                                  VISIBLE_RATE_COUNT / itemCount);
  if (thumbHeight < 8) {
    thumbHeight = 8;
  }
  const int16_t thumbTravel = trackHeight - thumbHeight;
  const int16_t thumbTop =
      maxOffset == 0
          ? trackTop
          : static_cast<int16_t>(
                trackTop + static_cast<int32_t>(thumbTravel) * listOffset /
                               maxOffset);
  gfx.drawLine(kListSplitX, kListY, kListSplitX, EPD_HEIGHT - 1, 1);
  gfx.drawLine(190, trackTop, 190, trackTop + trackHeight - 1, 1);
  gfx.fillRect(188, thumbTop, 5, thumbHeight, 1);
}

void CurrencyExchangeApp::drawCryptoChart(Adafruit_GFX &gfx) const {
  const CryptoEntry *entry = selectedCrypto();
  if (entry == nullptr || historyRates == nullptr || historyCount == 0) {
    drawCentered(gfx, "No chart data", 94);
    return;
  }

  char title[20] = "";
  snprintf(title, sizeof(title), "%s/USD", entry->symbol);
  char priceText[18] = "";
  formatCryptoPrice(entry->price, priceText, sizeof(priceText));
  drawPriceChart(gfx, title, priceText, kChartBottomCrypto);
  drawXAxisLabels(gfx, kChartBottomCrypto + kXAxisLabelGap);

  const bool oneDay = cryptoRange == CryptoRange::OneDay;
  const bool sevenDays = cryptoRange == CryptoRange::SevenDays;
  const int8_t selectedButton = oneDay ? 0 : sevenDays ? 1 : 2;
  for (int8_t button = 0; button < 3; button++) {
    const int16_t x = kCryptoRangeButtonXs[button];
    gfx.drawRect(x, kCryptoRangeButtonY, kCryptoRangeButtonWidth,
                 kCryptoRangeButtonHeight, 1);
    if (button == selectedButton) {
      gfx.fillRect(x, kCryptoRangeButtonY, kCryptoRangeButtonWidth,
                   kCryptoRangeButtonHeight, 1);
    }
  }
  const char *labels[] = {"1D", "7D", "30D"};
  for (int8_t button = 0; button < 3; button++) {
    gfx.setTextColor(button == selectedButton ? 0 : 1);
    drawCenteredInRect(gfx, labels[button], kCryptoRangeButtonXs[button],
                       kCryptoRangeButtonY, kCryptoRangeButtonWidth,
                       kCryptoRangeButtonHeight);
  }
  gfx.setTextColor(1);
}

void CurrencyExchangeApp::drawPriceChart(Adafruit_GFX &gfx,
                                         const char *title,
                                         const char *priceText,
                                         int16_t bottom) const {
  drawHeader(gfx, title, priceText);
  const UiChartSeries series = {
      historyRates,
      historyCount,
      UiChartTexture::Diagonal,
  };
  float minimum = 0.0f;
  float maximum = 0.0f;
  if (ChartComponent::valueRange(UiChartType::Area, &series, 1, minimum,
                                 maximum)) {
    char maximumText[12] = "";
    char minimumText[12] = "";
    formatAxisRate(maximum, maximumText, sizeof(maximumText));
    formatAxisRate(minimum, minimumText, sizeof(minimumText));
    gfx.setCursor(4, 27);
    gfx.print(maximumText);
    gfx.setCursor(4, bottom - 11);
    gfx.print(minimumText);
  }

  ChartComponent::draw(
      gfx, {45, 25, 151, static_cast<int16_t>(bottom - 25)},
      UiChartType::Area, &series, 1);
}

void CurrencyExchangeApp::drawXAxisLabels(Adafruit_GFX &gfx, int16_t y) const {
  char firstDate[20] = "";
  char lastDate[20] = "";
  formatDateLabel(historyFirstDate, firstDate, sizeof(firstDate));
  formatDateLabel(historyLastDate, lastDate, sizeof(lastDate));
  gfx.setCursor(45, y);
  gfx.print(firstDate);
  drawRightText(gfx, lastDate, y);
}

void CurrencyExchangeApp::drawHeader(Adafruit_GFX &gfx, const char *left,
                                     const char *right) {
  gfx.setCursor(4, 6);
  gfx.print(left);
  drawRightText(gfx, right, 6);
  gfx.drawLine(4, 18, EPD_WIDTH - 4, 18, 1);
}

void CurrencyExchangeApp::drawCentered(Adafruit_GFX &gfx, const char *text,
                                       int16_t y) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(width)) / 2 - x1, y);
  gfx.print(text);
}

void CurrencyExchangeApp::drawCenteredInRect(Adafruit_GFX &gfx,
                                             const char *text, int16_t x,
                                             int16_t y, int16_t width,
                                             int16_t height) {
  int16_t textX = 0;
  int16_t textY = 0;
  uint16_t textWidth = 0;
  uint16_t textHeight = 0;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textWidth, &textHeight);
  gfx.setCursor(x + (width - static_cast<int16_t>(textWidth)) / 2 - textX,
                y + (height - static_cast<int16_t>(textHeight)) / 2 - textY);
  gfx.print(text);
}

void CurrencyExchangeApp::drawRightText(Adafruit_GFX &gfx, const char *text,
                                        int16_t y) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  gfx.setCursor(EPD_WIDTH - 4 - static_cast<int16_t>(width) - x1, y);
  gfx.print(text);
}

void CurrencyExchangeApp::formatRate(float value, char *out, size_t outSize) {
  if (value >= 100.0f) {
    snprintf(out, outSize, "%.2f", value);
  } else if (value >= 10.0f) {
    snprintf(out, outSize, "%.3f", value);
  } else if (value >= 1.0f) {
    snprintf(out, outSize, "%.4f", value);
  } else {
    snprintf(out, outSize, "%.5f", value);
  }
}

void CurrencyExchangeApp::formatCryptoPrice(float value, char *out,
                                            size_t outSize) {
  if (value >= 100000.0f) {
    snprintf(out, outSize, "%.0f", value);
  } else if (value >= 1000.0f) {
    snprintf(out, outSize, "%.1f", value);
  } else if (value >= 1.0f) {
    snprintf(out, outSize, "%.2f", value);
  } else if (value >= 0.01f) {
    snprintf(out, outSize, "%.4f", value);
  } else {
    snprintf(out, outSize, "%.3g", static_cast<double>(value));
  }
}

void CurrencyExchangeApp::formatAxisRate(float value, char *out,
                                         size_t outSize) {
  if (value >= 1000.0f) {
    snprintf(out, outSize, "%.0f", value);
  } else if (value >= 100.0f) {
    snprintf(out, outSize, "%.1f", value);
  } else if (value >= 10.0f) {
    snprintf(out, outSize, "%.2f", value);
  } else if (value >= 1.0f) {
    snprintf(out, outSize, "%.3f", value);
  } else if (value >= 0.01f) {
    snprintf(out, outSize, "%.4f", value);
  } else {
    snprintf(out, outSize, "%.3g", static_cast<double>(value));
  }
}

float CurrencyExchangeApp::displayedFiatRate(float rate) const {
  return inverseFiatRates && rate > 0.0f ? 1.0f / rate : rate;
}

void CurrencyExchangeApp::setStatus(const char *text) {
  copyText(status, sizeof(status), text);
}

#endif
