#include "net/dns_message.h"

#include <cstring>

namespace net::dns {
namespace {

uint16_t read16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) << 8U | data[1];
}

bool skipName(const uint8_t *data, size_t size, size_t &offset,
              bool allowCompression) {
  size_t wireLength = 0;
  while (offset < size && wireLength <= 255U) {
    const uint8_t length = data[offset++];
    wireLength++;
    if (length == 0) {
      return true;
    }
    if ((length & 0xc0U) == 0xc0U) {
      if (!allowCompression || offset >= size) {
        return false;
      }
      const uint16_t pointer =
          static_cast<uint16_t>(length & 0x3fU) << 8U | data[offset++];
      return pointer >= 12U && pointer < size;
    }
    if ((length & 0xc0U) != 0 || length > 63U ||
        static_cast<size_t>(length) > size - offset ||
        wireLength + length > 255U) {
      return false;
    }
    offset += length;
    wireLength += length;
  }
  return false;
}

bool skipQuestion(const uint8_t *data, size_t size, size_t &offset,
                  bool allowCompression) {
  if (!skipName(data, size, offset, allowCompression) || offset > size ||
      size - offset < 4U) {
    return false;
  }
  offset += 4U;
  return true;
}

bool skipRecord(const uint8_t *data, size_t size, size_t &offset) {
  if (!skipName(data, size, offset, true) || offset > size ||
      size - offset < 10U) {
    return false;
  }
  const size_t payloadSize = read16(data + offset + 8U);
  offset += 10U;
  if (payloadSize > size - offset) {
    return false;
  }
  offset += payloadSize;
  return true;
}

} // namespace

bool validQuery(const uint8_t *data, size_t size) {
  if (data == nullptr || size < 12U || size > kMaximumQuerySize) {
    return false;
  }
  const uint16_t flags = read16(data + 2U);
  const uint16_t questions = read16(data + 4U);
  const uint16_t answers = read16(data + 6U);
  const uint16_t authorities = read16(data + 8U);
  const uint16_t additional = read16(data + 10U);
  if ((flags & 0x8000U) != 0 || (flags & 0x7800U) != 0 || questions != 1U ||
      answers != 0 || authorities != 0 || additional > 1U) {
    return false;
  }
  size_t offset = 12U;
  if (!skipQuestion(data, size, offset, false)) {
    return false;
  }
  for (uint16_t i = 0; i < additional; i++) {
    if (!skipRecord(data, size, offset)) {
      return false;
    }
  }
  return offset == size;
}

bool validResponse(const uint8_t *query, size_t querySize,
                   const uint8_t *response, size_t responseSize) {
  if (query == nullptr || response == nullptr || querySize < 12U ||
      responseSize < 12U || responseSize > kMaximumResponseSize ||
      response[0] != query[0] || response[1] != query[1]) {
    return false;
  }
  const uint16_t flags = read16(response + 2U);
  const uint16_t questions = read16(response + 4U);
  const uint16_t answers = read16(response + 6U);
  const uint16_t authorities = read16(response + 8U);
  const uint16_t additional = read16(response + 10U);
  const uint32_t records =
      static_cast<uint32_t>(answers) + authorities + additional;
  if ((flags & 0x8000U) == 0 || (flags & 0x7800U) != 0 || questions != 1U ||
      records > 64U) {
    return false;
  }
  size_t queryQuestionEnd = 12U;
  size_t responseQuestionEnd = 12U;
  if (!skipQuestion(query, querySize, queryQuestionEnd, false) ||
      !skipQuestion(response, responseSize, responseQuestionEnd, false) ||
      queryQuestionEnd - 12U != responseQuestionEnd - 12U ||
      memcmp(query + 12U, response + 12U, queryQuestionEnd - 12U) != 0) {
    return false;
  }
  size_t offset = responseQuestionEnd;
  for (uint32_t i = 0; i < records; i++) {
    if (!skipRecord(response, responseSize, offset)) {
      return false;
    }
  }
  return offset == responseSize;
}

size_t serviceFailure(const uint8_t *query, size_t querySize, uint8_t *out,
                      size_t capacity) {
  if (query == nullptr || out == nullptr || querySize < 12U) {
    return 0;
  }
  size_t questionEnd = 12U;
  if (!skipQuestion(query, querySize, questionEnd, false) ||
      questionEnd > capacity) {
    return 0;
  }
  memcpy(out, query, questionEnd);
  out[2] = static_cast<uint8_t>((out[2] & 0x01U) | 0x80U);
  out[3] = static_cast<uint8_t>((out[3] & 0xf0U) | 0x02U);
  memset(out + 6U, 0, 6U);
  return questionEnd;
}

} // namespace net::dns
