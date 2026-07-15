#include "sys/agentfiesta/openai_transport.h"

#include "sys/debug_logging.h"
#include "sys/psram_allocator.h"

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#include <Arduino.h>
#include <cstring>
#include <esp_rom_sys.h>
#include <new>
#include <cstdio>
#include <cstdlib>

#if ENABLE_NETWORK_APPS
#include <WiFiClientSecure.h>
#endif

namespace agentfiesta {
namespace {

constexpr size_t kHostCapacity = 96;
constexpr size_t kPathCapacity = 160;
constexpr size_t kErrorCapacity = 96;
constexpr size_t kErrorBodyCapacity = 512;
constexpr uint32_t kHeaderTimeoutMs = 5000;

void copyText(char *out, size_t outSize, const char *text) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const char *source = text != nullptr ? text : "";
  strncpy(out, source, outSize - 1U);
  out[outSize - 1U] = '\0';
}

[[maybe_unused]] bool startsWith(const char *text, const char *prefix) {
  return text != nullptr && prefix != nullptr &&
         strncmp(text, prefix, strlen(prefix)) == 0;
}

[[maybe_unused]] bool containsIgnoreCase(const char *text,
                                         const char *needle) {
  if (text == nullptr || needle == nullptr || needle[0] == '\0') {
    return false;
  }
  for (size_t i = 0; text[i] != '\0'; i++) {
    size_t j = 0;
    while (needle[j] != '\0' && text[i + j] != '\0') {
      char left = text[i + j];
      char right = needle[j];
      if (left >= 'A' && left <= 'Z') {
        left = static_cast<char>(left - 'A' + 'a');
      }
      if (right >= 'A' && right <= 'Z') {
        right = static_cast<char>(right - 'A' + 'a');
      }
      if (left != right) {
        break;
      }
      j++;
    }
    if (needle[j] == '\0') {
      return true;
    }
  }
  return false;
}

[[maybe_unused]] bool parseUnsigned(const char *text, size_t &value) {
  if (text == nullptr) {
    return false;
  }
  while (*text == ' ' || *text == '\t') {
    text++;
  }
  if (*text < '0' || *text > '9') {
    return false;
  }
  size_t parsed = 0;
  while (*text >= '0' && *text <= '9') {
    const size_t digit = static_cast<size_t>(*text - '0');
    if (parsed > (SIZE_MAX - digit) / 10U) {
      return false;
    }
    parsed = parsed * 10U + digit;
    text++;
  }
  value = parsed;
  return true;
}

[[maybe_unused]] bool extractErrorMessage(const char *body, char *out,
                                          size_t outSize) {
  if (body == nullptr || out == nullptr || outSize < 2U) {
    return false;
  }
  const char *cursor = strstr(body, "\"message\"");
  if (cursor == nullptr) {
    return false;
  }
  cursor = strchr(cursor + strlen("\"message\""), ':');
  if (cursor == nullptr) {
    return false;
  }
  cursor++;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
         *cursor == '\n') {
    cursor++;
  }
  if (*cursor++ != '"') {
    return false;
  }

  size_t used = 0;
  bool escaped = false;
  while (*cursor != '\0') {
    char value = *cursor++;
    if (!escaped && value == '"') {
      out[used] = '\0';
      return used > 0;
    }
    if (!escaped && value == '\\') {
      escaped = true;
      continue;
    }
    if (escaped) {
      if (value == 'n' || value == 'r' || value == 't') {
        value = value == 'n' ? '\n' : value == 'r' ? '\r' : '\t';
      } else if (value == 'u') {
        for (uint8_t i = 0; i < 4U && *cursor != '\0'; i++) {
          cursor++;
        }
        value = '?';
      }
      escaped = false;
    }
    if (used + 1U < outSize) {
      out[used++] = value;
    }
  }
  return false;
}

[[maybe_unused]] int parseChunkSize(const char *text) {
  if (text == nullptr) {
    return -1;
  }
  int value = 0;
  bool found = false;
  for (; *text != '\0' && *text != ';'; text++) {
    int digit = -1;
    if (*text >= '0' && *text <= '9') {
      digit = *text - '0';
    } else if (*text >= 'a' && *text <= 'f') {
      digit = *text - 'a' + 10;
    } else if (*text >= 'A' && *text <= 'F') {
      digit = *text - 'A' + 10;
    } else {
      return -1;
    }
    if (value > 0x0fffffff) {
      return -1;
    }
    value = value * 16 + digit;
    found = true;
  }
  return found ? value : -1;
}

[[maybe_unused]] bool parseBaseUrl(const char *url, char *host,
                                   size_t hostSize, uint16_t &port, char *path,
                                   size_t pathSize) {
  if (url == nullptr || url[0] == '\0' || host == nullptr || hostSize < 2U ||
      path == nullptr || pathSize < 2U) {
    return false;
  }
  const char *start = url;
  if (strncmp(start, "https://", 8) == 0) {
    start += 8;
    port = 443;
  } else if (strncmp(start, "http://", 7) == 0) {
    start += 7;
    port = 80;
  } else {
    port = 443;
  }

  const char *slash = strchr(start, '/');
  size_t hostLength =
      slash != nullptr ? static_cast<size_t>(slash - start) : strlen(start);
  const char *colon = static_cast<const char *>(memchr(start, ':', hostLength));
  if (colon != nullptr) {
    char portText[8] = {};
    const size_t portLength =
        hostLength - static_cast<size_t>(colon - start) - 1U;
    if (portLength == 0 || portLength >= sizeof(portText)) {
      return false;
    }
    memcpy(portText, colon + 1, portLength);
    const long parsed = strtol(portText, nullptr, 10);
    if (parsed <= 0 || parsed > 65535) {
      return false;
    }
    port = static_cast<uint16_t>(parsed);
    hostLength = static_cast<size_t>(colon - start);
  }
  if (hostLength == 0 || hostLength >= hostSize) {
    return false;
  }
  memcpy(host, start, hostLength);
  host[hostLength] = '\0';

  const char *basePath =
      slash != nullptr && slash[1] != '\0' ? slash : "/v1";
  copyText(path, pathSize, basePath);
  const size_t length = strlen(path);
  if (length > 1U && path[length - 1U] == '/') {
    path[length - 1U] = '\0';
  }
  return true;
}

} // namespace

struct OpenAiTransport::Impl {
#if ENABLE_NETWORK_APPS
  WiFiClientSecure client;
#endif
  uint32_t lastActivityMs = 0;
  size_t contentRemaining = 0;
  int chunkRemaining = 0;
  uint8_t chunkTrailerBytes = 0;
  bool chunked = false;
  bool contentLengthKnown = false;
  bool headersReady = false;
  bool complete = false;
  char chunkLine[20] = {};
  uint8_t chunkLineLength = 0;
  char error[kErrorCapacity] = {};
};

OpenAiTransport::~OpenAiTransport() { stop(); }

TransportStatus OpenAiTransport::begin(const char *baseUrl,
                                       const char *apiKey,
                                       const char *endpoint,
                                       size_t contentLength,
                                       const char *userAgent) {
  stop();
#if !ENABLE_NETWORK_APPS
  (void)baseUrl;
  (void)apiKey;
  (void)endpoint;
  (void)contentLength;
  (void)userAgent;
  return TransportStatus::Unsupported;
#else
  if (baseUrl == nullptr || baseUrl[0] == '\0' || apiKey == nullptr ||
      apiKey[0] == '\0' || endpoint == nullptr || endpoint[0] != '/') {
    return TransportStatus::Error;
  }
  void *memory = psram::allocateOrInternal(sizeof(Impl));
  if (memory == nullptr) {
    return TransportStatus::Error;
  }
  impl = new (memory) Impl();

  char host[kHostCapacity] = {};
  char basePath[kPathCapacity] = {};
  uint16_t port = 443;
  if (!parseBaseUrl(baseUrl, host, sizeof(host), port, basePath,
                    sizeof(basePath))) {
    copyText(impl->error, sizeof(impl->error), "invalid AI base URL");
    return TransportStatus::Error;
  }
  char requestPath[kPathCapacity] = {};
  const int pathLength = snprintf(requestPath, sizeof(requestPath), "%s%s",
                                  basePath, endpoint);
  if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(requestPath)) {
    copyText(impl->error, sizeof(impl->error), "AI endpoint too long");
    return TransportStatus::Error;
  }

  impl->client.setInsecure();
  if (!impl->client.connect(host, port)) {
    copyText(impl->error, sizeof(impl->error), "AI connect failed");
    return TransportStatus::Error;
  }
  char header[640] = {};
  const int headerLength = snprintf(
      header, sizeof(header),
      "POST %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Authorization: Bearer %s\r\n"
      "Content-Type: application/json\r\n"
      "Accept: application/json, text/event-stream\r\n"
      "Connection: close\r\n"
      "User-Agent: %s\r\n"
      "Content-Length: %lu\r\n\r\n",
      requestPath, host, apiKey,
      userAgent != nullptr ? userAgent : "PocketInk/AgentFiesta",
      static_cast<unsigned long>(contentLength));
  if (headerLength <= 0 ||
      static_cast<size_t>(headerLength) >= sizeof(header) ||
      !writeAll(reinterpret_cast<const uint8_t *>(header),
                static_cast<size_t>(headerLength))) {
    copyText(impl->error, sizeof(impl->error), "AI header write failed");
    return TransportStatus::Error;
  }
  impl->lastActivityMs = millis();
  return TransportStatus::Ok;
#endif
}

bool OpenAiTransport::writeAll(const uint8_t *data, size_t length) {
#if !ENABLE_NETWORK_APPS
  (void)data;
  (void)length;
  return false;
#else
  if (impl == nullptr || (data == nullptr && length > 0)) {
    return false;
  }
  size_t sent = 0;
  while (sent < length) {
    const size_t written = impl->client.write(data + sent, length - sent);
    if (written == 0) {
      return false;
    }
    sent += written;
    impl->lastActivityMs = millis();
  }
  return true;
#endif
}

bool OpenAiTransport::writeAll(const char *text) {
  return text != nullptr &&
         writeAll(reinterpret_cast<const uint8_t *>(text), strlen(text));
}

TransportStatus OpenAiTransport::finishRequest() {
#if !ENABLE_NETWORK_APPS
  return TransportStatus::Unsupported;
#else
  if (impl == nullptr) {
    return TransportStatus::Error;
  }
  auto readLine = [this](char *out, size_t outSize) {
    if (out == nullptr || outSize < 2U) {
      return false;
    }
    const uint32_t started = millis();
    size_t used = 0;
    while (millis() - started < kHeaderTimeoutMs) {
      if (impl->client.available() <= 0) {
        delay(1);
        continue;
      }
      const int raw = impl->client.read();
      if (raw < 0) {
        continue;
      }
      impl->lastActivityMs = millis();
      const char value = static_cast<char>(raw);
      if (value == '\r') {
        continue;
      }
      if (value == '\n') {
        out[used] = '\0';
        return true;
      }
      if (used + 1U < outSize) {
        out[used++] = value;
      }
    }
    out[used] = '\0';
    return false;
  };

  char line[256] = {};
  if (!readLine(line, sizeof(line))) {
    copyText(impl->error, sizeof(impl->error), "AI response timeout");
    return TransportStatus::Error;
  }
  const bool responseOk = startsWith(line, "HTTP/1.1 200") ||
                          startsWith(line, "HTTP/1.0 200");
  char statusLine[kErrorCapacity] = {};
  copyText(statusLine, sizeof(statusLine), line);
  bool headersComplete = false;
  while (readLine(line, sizeof(line))) {
    if (line[0] == '\0') {
      impl->headersReady = true;
      impl->lastActivityMs = millis();
      headersComplete = true;
      break;
    }
    if (containsIgnoreCase(line, "transfer-encoding:") &&
        containsIgnoreCase(line, "chunked")) {
      impl->chunked = true;
    } else if (containsIgnoreCase(line, "content-length:")) {
      const char *colon = strchr(line, ':');
      size_t length = 0;
      if (colon != nullptr && parseUnsigned(colon + 1, length)) {
        impl->contentLengthKnown = true;
        impl->contentRemaining = length;
        impl->complete = length == 0;
      }
    }
  }
  if (!headersComplete) {
    copyText(impl->error, sizeof(impl->error), "AI response headers failed");
    return TransportStatus::Error;
  }
  if (responseOk) {
    return TransportStatus::Ok;
  }

  char body[kErrorBodyCapacity] = {};
  size_t used = 0;
  const uint32_t started = millis();
  while (used + 1U < sizeof(body) &&
         millis() - started < kHeaderTimeoutMs) {
    bool readAny = false;
    char value = 0;
    while (used + 1U < sizeof(body) && readBodyByte(value)) {
      body[used++] = value;
      readAny = true;
    }
    const TransportStatus status = responseStatus();
    if (status == TransportStatus::End || status == TransportStatus::Error) {
      break;
    }
    if (!readAny) {
      delay(1);
    }
  }
  body[used] = '\0';

  if (debugLoggingEnabled()) {
    esp_rom_printf("[ai-http] %s\n", statusLine);
    if (used > 0) {
      esp_rom_printf("[ai-http] %s\n", body);
    }
  }

  char message[kErrorCapacity] = {};
  if (extractErrorMessage(body, message, sizeof(message))) {
    copyText(impl->error, sizeof(impl->error), message);
  } else if (impl->error[0] == '\0') {
    copyText(impl->error, sizeof(impl->error), statusLine);
  }
  return TransportStatus::Error;
#endif
}

bool OpenAiTransport::readBodyByte(char &out) {
#if !ENABLE_NETWORK_APPS
  (void)out;
  return false;
#else
  if (impl == nullptr || !impl->headersReady || impl->complete) {
    return false;
  }
  if (impl->contentLengthKnown && impl->contentRemaining == 0) {
    impl->complete = true;
    return false;
  }

  if (impl->chunked) {
    while (impl->chunkTrailerBytes > 0 && impl->client.available() > 0) {
      impl->client.read();
      impl->chunkTrailerBytes--;
      impl->lastActivityMs = millis();
    }
    if (impl->chunkTrailerBytes > 0) {
      return false;
    }
    while (impl->chunkRemaining == 0 && impl->client.available() > 0) {
      const int raw = impl->client.read();
      if (raw < 0) {
        return false;
      }
      impl->lastActivityMs = millis();
      const char value = static_cast<char>(raw);
      if (value == '\r') {
        continue;
      }
      if (value == '\n') {
        impl->chunkLine[impl->chunkLineLength] = '\0';
        if (impl->chunkLineLength == 0) {
          continue;
        }
        const int parsed = parseChunkSize(impl->chunkLine);
        impl->chunkLineLength = 0;
        impl->chunkLine[0] = '\0';
        if (parsed < 0) {
          copyText(impl->error, sizeof(impl->error), "bad chunk header");
          impl->complete = true;
          return false;
        }
        if (parsed == 0) {
          impl->complete = true;
          return false;
        }
        impl->chunkRemaining = parsed;
        break;
      }
      if (impl->chunkLineLength + 1U >= sizeof(impl->chunkLine)) {
        copyText(impl->error, sizeof(impl->error), "chunk header too long");
        impl->complete = true;
        return false;
      }
      impl->chunkLine[impl->chunkLineLength++] = value;
      impl->chunkLine[impl->chunkLineLength] = '\0';
    }
    if (impl->chunkRemaining == 0) {
      return false;
    }
  }

  if (impl->client.available() <= 0) {
    return false;
  }
  const int raw = impl->client.read();
  if (raw < 0) {
    return false;
  }
  impl->lastActivityMs = millis();
  out = static_cast<char>(raw);
  if (impl->chunked) {
    impl->chunkRemaining--;
    if (impl->chunkRemaining == 0) {
      impl->chunkTrailerBytes = 2;
    }
  } else if (impl->contentLengthKnown) {
    impl->contentRemaining--;
    if (impl->contentRemaining == 0) {
      impl->complete = true;
    }
  }
  return true;
#endif
}

TransportStatus OpenAiTransport::responseStatus() {
#if !ENABLE_NETWORK_APPS
  return TransportStatus::Unsupported;
#else
  if (impl == nullptr) {
    return TransportStatus::Error;
  }
  if (impl->complete) {
    return impl->error[0] == '\0' ? TransportStatus::End
                                  : TransportStatus::Error;
  }
  if (impl->headersReady && !impl->client.connected() &&
      impl->client.available() <= 0) {
    impl->complete = true;
    return TransportStatus::End;
  }
  return TransportStatus::Pending;
#endif
}

bool OpenAiTransport::connected() const {
#if ENABLE_NETWORK_APPS
  return impl != nullptr && impl->client.connected();
#else
  return false;
#endif
}

bool OpenAiTransport::timedOut(uint32_t timeoutMs) const {
  return impl != nullptr && timeoutMs > 0 &&
         millis() - impl->lastActivityMs > timeoutMs;
}

const char *OpenAiTransport::error() const {
  return impl != nullptr ? impl->error : "AI transport unavailable";
}

void OpenAiTransport::stop() {
  if (impl == nullptr) {
    return;
  }
#if ENABLE_NETWORK_APPS
  impl->client.stop();
#endif
  impl->~Impl();
  psram::release(impl);
  impl = nullptr;
}

size_t base64EncodedSize(size_t inputBytes) {
  if (inputBytes > (SIZE_MAX / 4U) * 3U - 2U) {
    return 0;
  }
  return ((inputBytes + 2U) / 3U) * 4U;
}

bool base64Encode(const uint8_t *input, size_t inputBytes, char *out,
                  size_t outSize) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t encodedBytes = base64EncodedSize(inputBytes);
  if ((input == nullptr && inputBytes > 0) || out == nullptr ||
      (encodedBytes == 0 && inputBytes > 0) || outSize <= encodedBytes) {
    return false;
  }
  size_t source = 0;
  size_t target = 0;
  while (source < inputBytes) {
    const uint32_t a = input[source++];
    const bool haveB = source < inputBytes;
    const uint32_t b = haveB ? input[source++] : 0;
    const bool haveC = source < inputBytes;
    const uint32_t c = haveC ? input[source++] : 0;
    const uint32_t packed = (a << 16U) | (b << 8U) | c;
    out[target++] = kAlphabet[(packed >> 18U) & 0x3fU];
    out[target++] = kAlphabet[(packed >> 12U) & 0x3fU];
    out[target++] = haveB ? kAlphabet[(packed >> 6U) & 0x3fU] : '=';
    out[target++] = haveC ? kAlphabet[packed & 0x3fU] : '=';
  }
  out[target] = '\0';
  return true;
}

} // namespace agentfiesta
