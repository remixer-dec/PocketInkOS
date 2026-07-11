#ifndef APP_API_SERVICE_UTILS_H
#define APP_API_SERVICE_UTILS_H

#include <stddef.h>

namespace app_api {

constexpr size_t kAppIdCapacity = 24;
constexpr size_t kPathCapacity = 288;

bool copyText(char *out, size_t outSize, const char *text);
bool safePath(const char *path);

} // namespace app_api

#endif
