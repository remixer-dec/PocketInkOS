/// Shared validation for files exposed by the hotspot HTTP and FTP services.
#ifndef POCKETINK_NET_SHARE_PATHS_H
#define POCKETINK_NET_SHARE_PATHS_H

#include <stddef.h>
#include <stdint.h>

namespace net {

static constexpr const char *kShareRoot = "/share";
static constexpr size_t kShareNameCapacity = 65;

enum class ShareUploadKind : uint8_t { Ftp, Http };

/// Accepts one flat, visible ASCII filename with a passive file extension.
bool validShareName(const char *name);
/// Produces `/share/<name>` after applying the same validation as FTP/HTTP.
bool sharePath(const char *name, char *out, size_t outSize);
/// Reserves an unpredictable hidden staging name. The caller still owns file
/// creation and must remove the staging file after any failed transfer.
bool temporaryShareUploadPath(ShareUploadKind kind, char *out, size_t outSize);
bool ensureShareRoot();

} // namespace net

#endif
