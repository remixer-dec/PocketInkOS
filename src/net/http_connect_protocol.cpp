#include "net/http_connect_protocol.h"

#include <cstdio>
#include <cstring>

namespace net::http_connect {
namespace {

constexpr size_t kUsernameCapacity = 65;
constexpr size_t kPasswordCapacity = 97;

bool validCredentialText(const char *text, size_t capacity,
                         bool rejectColon, size_t &length) {
  if (text == nullptr) {
    return false;
  }
  length = strnlen(text, capacity);
  if (length >= capacity) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const uint8_t byte = static_cast<uint8_t>(text[i]);
    if (byte < 0x20U || byte == 0x7fU || (rejectColon && byte == ':')) {
      return false;
    }
  }
  return true;
}

bool append(uint8_t *out, size_t capacity, size_t &size, const char *text,
            size_t length) {
  if (out == nullptr || text == nullptr || size > capacity ||
      length > capacity - size) {
    return false;
  }
  memcpy(out + size, text, length);
  size += length;
  return true;
}

size_t base64(const uint8_t *input, size_t inputSize, char *out,
              size_t capacity) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t groups = inputSize / 3U + (inputSize % 3U != 0 ? 1U : 0U);
  if (groups > (SIZE_MAX - 1U) / 4U) {
    return 0;
  }
  const size_t required = groups * 4U;
  if ((inputSize > 0 && input == nullptr) || out == nullptr ||
      required >= capacity) {
    return 0;
  }
  size_t source = 0;
  size_t target = 0;
  while (source < inputSize) {
    const size_t remaining = inputSize - source;
    const uint32_t first = input[source++];
    const uint32_t second = remaining > 1 ? input[source++] : 0;
    const uint32_t third = remaining > 2 ? input[source++] : 0;
    const uint32_t group = first << 16U | second << 8U | third;
    out[target++] = kAlphabet[(group >> 18U) & 0x3fU];
    out[target++] = kAlphabet[(group >> 12U) & 0x3fU];
    out[target++] = remaining > 1 ? kAlphabet[(group >> 6U) & 0x3fU] : '=';
    out[target++] = remaining > 2 ? kAlphabet[group & 0x3fU] : '=';
  }
  out[target] = '\0';
  return target;
}

bool headerNameByte(uint8_t byte) {
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') || byte == '!' || byte == '#' ||
         byte == '$' || byte == '%' || byte == '&' || byte == '\'' ||
         byte == '*' || byte == '+' || byte == '-' || byte == '.' ||
         byte == '^' || byte == '_' || byte == '`' || byte == '|' ||
         byte == '~';
}

bool validHeaders(const uint8_t *data, size_t start, size_t end) {
  size_t line = start;
  while (line < end) {
    size_t lineEnd = line;
    while (lineEnd + 1U < end &&
           !(data[lineEnd] == '\r' && data[lineEnd + 1U] == '\n')) {
      lineEnd++;
    }
    if (lineEnd + 1U >= end) {
      return false;
    }
    if (lineEnd == line) {
      return lineEnd + 2U == end;
    }
    size_t colon = line;
    while (colon < lineEnd && data[colon] != ':') {
      if (!headerNameByte(data[colon])) {
        return false;
      }
      colon++;
    }
    if (colon == line || colon == lineEnd) {
      return false;
    }
    for (size_t i = colon + 1U; i < lineEnd; i++) {
      if ((data[i] < 0x20U && data[i] != '\t') || data[i] == 0x7fU) {
        return false;
      }
    }
    line = lineEnd + 2U;
  }
  return false;
}

} // namespace

bool validCredentials(const char *username, const char *password) {
  size_t usernameLength = 0;
  size_t passwordLength = 0;
  if (!validCredentialText(username, kUsernameCapacity, true,
                           usernameLength) ||
      !validCredentialText(password, kPasswordCapacity, false,
                           passwordLength)) {
    return false;
  }
  return (usernameLength == 0) == (passwordLength == 0);
}

size_t buildIpv4Request(const uint8_t address[4], uint16_t port,
                        const char *username, const char *password,
                        uint8_t *out, size_t capacity) {
  if (address == nullptr || port == 0 || out == nullptr ||
      !validCredentials(username, password)) {
    return 0;
  }
  char authority[22] = {};
  const int authoritySize =
      snprintf(authority, sizeof(authority), "%u.%u.%u.%u:%u", address[0],
               address[1], address[2], address[3], port);
  if (authoritySize <= 0 ||
      static_cast<size_t>(authoritySize) >= sizeof(authority)) {
    return 0;
  }

  size_t size = 0;
  const size_t authorityLength = static_cast<size_t>(authoritySize);
  if (!append(out, capacity, size, "CONNECT ", 8) ||
      !append(out, capacity, size, authority, authorityLength) ||
      !append(out, capacity, size, " HTTP/1.1\r\nHost: ", 17) ||
      !append(out, capacity, size, authority, authorityLength) ||
      !append(out, capacity, size, "\r\n", 2)) {
    memset(out, 0, size);
    return 0;
  }

  if (username[0] != '\0') {
    uint8_t credentials[kUsernameCapacity + kPasswordCapacity] = {};
    char encoded[((sizeof(credentials) + 2U) / 3U) * 4U + 1U] = {};
    const size_t usernameLength = strlen(username);
    const size_t passwordLength = strlen(password);
    memcpy(credentials, username, usernameLength);
    credentials[usernameLength] = ':';
    memcpy(credentials + usernameLength + 1U, password, passwordLength);
    const size_t encodedSize =
        base64(credentials, usernameLength + passwordLength + 1U, encoded,
               sizeof(encoded));
    memset(credentials, 0, sizeof(credentials));
    const bool added =
        encodedSize != 0 &&
        append(out, capacity, size, "Proxy-Authorization: Basic ", 27) &&
        append(out, capacity, size, encoded, encodedSize) &&
        append(out, capacity, size, "\r\n", 2);
    memset(encoded, 0, sizeof(encoded));
    if (!added) {
      memset(out, 0, size);
      return 0;
    }
  }
  if (!append(out, capacity, size, "\r\n", 2)) {
    memset(out, 0, size);
    return 0;
  }
  return size;
}

ParseState parseResponse(const uint8_t *data, size_t size, size_t &consumed) {
  consumed = 0;
  if (data == nullptr) {
    return ParseState::Malformed;
  }
  size_t headerEnd = 0;
  for (size_t i = 0; i + 3U < size; i++) {
    if (data[i] == '\r' && data[i + 1U] == '\n' &&
        data[i + 2U] == '\r' && data[i + 3U] == '\n') {
      headerEnd = i + 4U;
      break;
    }
  }
  if (headerEnd == 0) {
    return size >= kMaximumResponseHeaderSize ? ParseState::Malformed
                                               : ParseState::Incomplete;
  }
  if (headerEnd > kMaximumResponseHeaderSize) {
    return ParseState::Malformed;
  }

  size_t statusEnd = 0;
  for (size_t i = 0; i + 1U < headerEnd; i++) {
    if (data[i] == '\r' && data[i + 1U] == '\n') {
      statusEnd = i;
      break;
    }
  }
  constexpr size_t kStatusPrefixSize = 9;
  if (statusEnd < 12U ||
      (memcmp(data, "HTTP/1.1 ", kStatusPrefixSize) != 0 &&
       memcmp(data, "HTTP/1.0 ", kStatusPrefixSize) != 0) ||
      data[9] < '0' || data[9] > '9' || data[10] < '0' || data[10] > '9' ||
      data[11] < '0' || data[11] > '9' ||
      (statusEnd > 12U && data[12] != ' ')) {
    return ParseState::Malformed;
  }
  for (size_t i = 12; i < statusEnd; i++) {
    if ((data[i] < 0x20U && data[i] != '\t') || data[i] == 0x7fU) {
      return ParseState::Malformed;
    }
  }
  if (!validHeaders(data, statusEnd + 2U, headerEnd)) {
    return ParseState::Malformed;
  }
  consumed = headerEnd;
  const unsigned status = static_cast<unsigned>(data[9] - '0') * 100U +
                          static_cast<unsigned>(data[10] - '0') * 10U +
                          static_cast<unsigned>(data[11] - '0');
  return status >= 200U && status < 300U ? ParseState::Accepted
                                         : ParseState::Rejected;
}

} // namespace net::http_connect
