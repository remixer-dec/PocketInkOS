#include "net/socks5_protocol.h"

#include <cstring>

namespace net::socks5 {
namespace {

constexpr uint8_t kVersion = 5;
constexpr uint8_t kNoAuth = 0;
constexpr uint8_t kPasswordAuth = 2;
constexpr uint8_t kIpv4 = 1;

size_t checkedLength(const char *text) {
  return text != nullptr ? strnlen(text, 256) : 0;
}

} // namespace

size_t buildGreeting(bool passwordAuth, uint8_t *out, size_t outSize) {
  if (out == nullptr || outSize < 3) {
    return 0;
  }
  out[0] = kVersion;
  out[1] = 1;
  out[2] = passwordAuth ? kPasswordAuth : kNoAuth;
  return 3;
}

ParseState parseMethodReply(const uint8_t *data, size_t size,
                            bool passwordAuth) {
  if (data == nullptr || size < 2) {
    return ParseState::Incomplete;
  }
  if (data[0] != kVersion) {
    return ParseState::Malformed;
  }
  return data[1] == (passwordAuth ? kPasswordAuth : kNoAuth)
             ? ParseState::Accepted
             : ParseState::Rejected;
}

size_t buildPasswordAuth(const char *username, const char *password,
                         uint8_t *out, size_t outSize) {
  const size_t usernameLength = checkedLength(username);
  const size_t passwordLength = checkedLength(password);
  const size_t required = 3U + usernameLength + passwordLength;
  if (out == nullptr || usernameLength == 0 || usernameLength > 255 ||
      passwordLength == 0 || passwordLength > 255 || outSize < required) {
    return 0;
  }
  out[0] = 1;
  out[1] = static_cast<uint8_t>(usernameLength);
  memcpy(out + 2, username, usernameLength);
  out[2 + usernameLength] = static_cast<uint8_t>(passwordLength);
  memcpy(out + 3 + usernameLength, password, passwordLength);
  return required;
}

ParseState parsePasswordReply(const uint8_t *data, size_t size) {
  if (data == nullptr || size < 2) {
    return ParseState::Incomplete;
  }
  if (data[0] != 1) {
    return ParseState::Malformed;
  }
  return data[1] == 0 ? ParseState::Accepted : ParseState::Rejected;
}

size_t buildIpv4Request(Command command, const Ipv4Endpoint &target,
                        uint8_t *out, size_t outSize) {
  const bool supported = command == Command::Connect ||
                         command == Command::UdpAssociate;
  if (out == nullptr || outSize < 10U || !supported ||
      (command == Command::Connect && target.port == 0)) {
    return 0;
  }
  out[0] = kVersion;
  out[1] = static_cast<uint8_t>(command);
  out[2] = 0;
  out[3] = kIpv4;
  memcpy(out + 4, target.address, sizeof(target.address));
  out[8] = static_cast<uint8_t>(target.port >> 8U);
  out[9] = static_cast<uint8_t>(target.port);
  return 10;
}

ParseState parseCommandReply(const uint8_t *data, size_t size,
                             Ipv4Endpoint &bound, size_t &consumed) {
  bound = {};
  consumed = 0;
  if (data == nullptr || size < 4) {
    return ParseState::Incomplete;
  }
  if (data[0] != kVersion || data[2] != 0) {
    return ParseState::Malformed;
  }
  if (data[1] != 0) {
    return ParseState::Rejected;
  }
  size_t addressOffset = 4;
  size_t addressLength = 0;
  if (data[3] == kIpv4) {
    addressLength = 4;
  } else if (data[3] == 3) {
    if (size < 5) {
      return ParseState::Incomplete;
    }
    addressOffset = 5;
    addressLength = data[4];
  } else if (data[3] == 4) {
    addressLength = 16;
  } else {
    return ParseState::Malformed;
  }
  const size_t required = addressOffset + addressLength + 2U;
  if (size < required) {
    return ParseState::Incomplete;
  }
  consumed = required;
  if (data[3] != kIpv4) {
    return ParseState::Rejected;
  }
  memcpy(bound.address, data + addressOffset, sizeof(bound.address));
  bound.port = static_cast<uint16_t>(data[addressOffset + addressLength])
                   << 8U |
               data[addressOffset + addressLength + 1U];
  return ParseState::Accepted;
}

size_t buildUdpDatagram(const Ipv4Endpoint &target, const uint8_t *payload,
                        size_t payloadSize, uint8_t *out, size_t outSize) {
  if (payloadSize > SIZE_MAX - 10U) {
    return 0;
  }
  const size_t required = 10U + payloadSize;
  if (out == nullptr || target.port == 0 ||
      (payloadSize > 0 && payload == nullptr) ||
      outSize < required) {
    return 0;
  }
  out[0] = 0;
  out[1] = 0;
  out[2] = 0;
  out[3] = kIpv4;
  memcpy(out + 4, target.address, sizeof(target.address));
  out[8] = static_cast<uint8_t>(target.port >> 8U);
  out[9] = static_cast<uint8_t>(target.port);
  if (payloadSize > 0) {
    memcpy(out + 10, payload, payloadSize);
  }
  return required;
}

ParseState parseUdpDatagram(const uint8_t *data, size_t size,
                            Ipv4Endpoint &source, const uint8_t *&payload,
                            size_t &payloadSize) {
  source = {};
  payload = nullptr;
  payloadSize = 0;
  if (data == nullptr || size < 4) {
    return ParseState::Incomplete;
  }
  if (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != kIpv4) {
    return ParseState::Rejected;
  }
  if (size < 10) {
    return ParseState::Incomplete;
  }
  memcpy(source.address, data + 4, sizeof(source.address));
  source.port = static_cast<uint16_t>(data[8]) << 8U | data[9];
  payload = data + 10;
  payloadSize = size - 10U;
  return ParseState::Accepted;
}

} // namespace net::socks5
