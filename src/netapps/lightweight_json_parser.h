#ifndef LIGHTWEIGHT_JSON_PARSER_H
#define LIGHTWEIGHT_JSON_PARSER_H

#include <Stream.h>
#include <stdint.h>

class JsonStreamListener {
public:
  virtual ~JsonStreamListener() = default;
  virtual void onObjectStart(int, const char *) {}
  virtual void onObjectEnd(int) {}
  virtual void onArrayStart(int, const char *) {}
  virtual void onArrayEnd(int) {}
  virtual void onStringValue(int, const char *, const char *) {}
  virtual void onNumberValue(int, const char *, int32_t) {}
  virtual void onDecimalValue(int depth, const char *key, int32_t valueX10) {
    onNumberValue(depth, key, valueX10 / 10);
  }
  virtual void onNumericValue(int depth, const char *key, double value) {
    if (value < static_cast<double>(INT32_MIN)) {
      onNumberValue(depth, key, INT32_MIN);
      return;
    }
    if (value > static_cast<double>(INT32_MAX)) {
      onNumberValue(depth, key, INT32_MAX);
      return;
    }
    const int32_t integer = static_cast<int32_t>(value);
    if (value == static_cast<double>(integer)) {
      onNumberValue(depth, key, integer);
    } else {
      const double scaled = value * 10.0;
      onDecimalValue(depth, key,
                     scaled < static_cast<double>(INT32_MIN)
                         ? INT32_MIN
                         : (scaled > static_cast<double>(INT32_MAX)
                                ? INT32_MAX
                                : static_cast<int32_t>(scaled)));
    }
  }
  virtual void onBooleanValue(int, const char *, bool) {}
  virtual void onNullValue(int, const char *) {}
};

class LightweightJsonParser {
public:
  bool parse(Stream *stream, int expectedSize, unsigned long timeoutMs,
             int maxBytes, JsonStreamListener &listener, char *error,
             int errorSize);

private:
  static const int KEY_SIZE = 32;
  static const int TOKEN_SIZE = 768;

  static void appendSanitized(char *out, int outSize, int &len, char value);
  static void copyText(char *out, int outSize, const char *value);
  static void setError(char *error, int errorSize, const char *text);
};

#endif
