#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "lightweight_json_parser.h"

#include <Arduino.h>
#include <cstring>

bool LightweightJsonParser::parse(Stream *stream, int expectedSize,
                                  unsigned long timeoutMs, int maxBytes,
                                  JsonStreamListener &listener, char *error,
                                  int errorSize) {
  if (stream == nullptr) {
    setError(error, errorSize, "No stream");
    return false;
  }

  bool inString = false;
  bool escape = false;
  bool stringIsValue = false;
  bool hasPendingKey = false;
  bool expectingValue = false;
  bool hasPendingRaw = false;
  int pendingRaw = 0;
  int unicodeRemaining = 0;
  int depth = 0;
  int bytes = 0;
  int tokenLen = 0;
  char token[TOKEN_SIZE] = "";
  char currentKey[KEY_SIZE] = "";
  unsigned long startedAt = millis();
  unsigned long lastReadAt = startedAt;

  while (millis() - startedAt < timeoutMs && bytes < maxBytes) {
    if (expectedSize >= 0 && bytes >= expectedSize && !hasPendingRaw) {
      break;
    }

    int raw = -1;
    if (hasPendingRaw) {
      raw = pendingRaw;
      hasPendingRaw = false;
    } else {
      if (stream->available() <= 0) {
        if (expectedSize < 0 && bytes > 0 && millis() - lastReadAt > 750) {
          break;
        }
        delay(1);
        continue;
      }
      raw = stream->read();
      if (raw < 0) {
        continue;
      }
      bytes++;
      lastReadAt = millis();
    }
    char c = static_cast<char>(raw);

    if (inString) {
      if (unicodeRemaining > 0) {
        unicodeRemaining--;
        continue;
      }
      if (escape) {
        escape = false;
        if (c == 'u') {
          appendSanitized(token, sizeof(token), tokenLen, '?');
          unicodeRemaining = 4;
        } else if (c == 'n' || c == 'r' || c == 't') {
          appendSanitized(token, sizeof(token), tokenLen, ' ');
        } else {
          appendSanitized(token, sizeof(token), tokenLen, c);
        }
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (c == '"') {
        inString = false;
        token[tokenLen] = '\0';
        if (stringIsValue) {
          listener.onStringValue(depth, currentKey, token);
          expectingValue = false;
          currentKey[0] = '\0';
        } else {
          hasPendingKey = true;
        }
        continue;
      }
      appendSanitized(token, sizeof(token), tokenLen, c);
      continue;
    }

    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      continue;
    }

    if (hasPendingKey) {
      if (c == ':') {
        copyText(currentKey, sizeof(currentKey), token);
        expectingValue = true;
      }
      hasPendingKey = false;
      continue;
    }

    if (c == '"') {
      inString = true;
      escape = false;
      stringIsValue = expectingValue;
      unicodeRemaining = 0;
      tokenLen = 0;
      token[0] = '\0';
      continue;
    }

    if (c == '-' || (c >= '0' && c <= '9')) {
      bool negative = c == '-';
      bool decimal = false;
      bool haveDecimalDigit = false;
      int32_t value = negative ? 0 : c - '0';
      int32_t tenth = 0;
      while (millis() - startedAt < timeoutMs && bytes < maxBytes) {
        if (stream->available() <= 0) {
          delay(1);
          continue;
        }
        int next = stream->read();
        if (next < 0) {
          continue;
        }
        bytes++;
        char digit = static_cast<char>(next);
        if (digit == '.' && !decimal) {
          decimal = true;
          continue;
        }
        if (digit < '0' || digit > '9') {
          pendingRaw = next;
          hasPendingRaw = true;
          break;
        }
        if (decimal) {
          if (!haveDecimalDigit) {
            tenth = digit - '0';
            haveDecimalDigit = true;
          }
          continue;
        }
        if (value < 100000000) {
          value = value * 10 + (digit - '0');
        }
      }
      int32_t signedValue = negative ? -value : value;
      if (decimal) {
        int32_t valueX10 = value * 10 + tenth;
        listener.onDecimalValue(depth, currentKey, negative ? -valueX10 : valueX10);
      } else {
        listener.onNumberValue(depth, currentKey, signedValue);
      }
      expectingValue = false;
      currentKey[0] = '\0';
      continue;
    }

    if (c == '{') {
      listener.onObjectStart(depth + 1, expectingValue ? currentKey : "");
      depth++;
      expectingValue = false;
      currentKey[0] = '\0';
      continue;
    }

    if (c == '[') {
      listener.onArrayStart(depth + 1, expectingValue ? currentKey : "");
      depth++;
      expectingValue = false;
      currentKey[0] = '\0';
      continue;
    }

    if (c == '}') {
      listener.onObjectEnd(depth);
      if (depth > 0) {
        depth--;
      }
      expectingValue = false;
      currentKey[0] = '\0';
      continue;
    }

    if (c == ']') {
      listener.onArrayEnd(depth);
      if (depth > 0) {
        depth--;
      }
      expectingValue = false;
      currentKey[0] = '\0';
      continue;
    }

    if (c == ',' || c == 'n' || c == 't' || c == 'f') {
      expectingValue = false;
      currentKey[0] = '\0';
    }
  }

  if (bytes >= maxBytes) {
    setError(error, errorSize, "JSON clipped");
    return false;
  }
  if (millis() - startedAt >= timeoutMs) {
    setError(error, errorSize, "JSON timeout");
    return false;
  }
  return true;
}

void LightweightJsonParser::appendSanitized(char *out, int outSize, int &len,
                                            char value) {
  if (len + 1 >= outSize) {
    return;
  }
  unsigned char byte = static_cast<unsigned char>(value);
  if (byte < 32) {
    value = ' ';
  } else if (byte >= 127) {
    value = '?';
  }
  out[len++] = value;
  out[len] = '\0';
}

void LightweightJsonParser::copyText(char *out, int outSize,
                                     const char *value) {
  if (outSize <= 0) {
    return;
  }
  strncpy(out, value ? value : "", outSize - 1);
  out[outSize - 1] = '\0';
}

void LightweightJsonParser::setError(char *error, int errorSize,
                                     const char *text) {
  if (error == nullptr || errorSize <= 0) {
    return;
  }
  strncpy(error, text ? text : "", errorSize - 1);
  error[errorSize - 1] = '\0';
}

#endif
