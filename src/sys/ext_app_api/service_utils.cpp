#include "sys/ext_app_api/service_utils.h"

#include <cstring>

namespace ext_app_api {

bool copyText(char *out, size_t outSize, const char *text) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  const char *source = text != nullptr ? text : "";
  const size_t length = strlen(source);
  const size_t amount = length < outSize ? length : outSize - 1U;
  if (amount > 0) {
    memcpy(out, source, amount);
  }
  out[amount] = '\0';
  return length < outSize;
}

bool safePath(const char *path) {
  if (path == nullptr || path[0] != '/' || strlen(path) >= kPathCapacity) {
    return false;
  }
  const char *cursor = path;
  while ((cursor = strstr(cursor, "..")) != nullptr) {
    const bool startsComponent = cursor == path || cursor[-1] == '/';
    const bool endsComponent = cursor[2] == '\0' || cursor[2] == '/';
    if (startsComponent && endsComponent) {
      return false;
    }
    cursor += 2;
  }
  return true;
}

} // namespace ext_app_api
