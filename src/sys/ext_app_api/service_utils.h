/// Shared size limits and defensive text/path validation for external-app
/// services. Keeping these rules here makes every service apply the same ABI
/// buffer and storage-sandbox assumptions.
#ifndef EXT_APP_API_SERVICE_UTILS_H
#define EXT_APP_API_SERVICE_UTILS_H

#include <stddef.h>

namespace ext_app_api {

/// Maximum app identifier size, including the terminating null byte.
constexpr size_t kAppIdCapacity = 24;
/// Maximum service path size, including the terminating null byte.
constexpr size_t kPathCapacity = 288;

/// Copies null-terminated text without truncation or unterminated output.
bool copyText(char *out, size_t outSize, const char *text);
/// Accepts only absolute, normalized paths that cannot escape app storage rules.
bool safePath(const char *path);

} // namespace ext_app_api

#endif
