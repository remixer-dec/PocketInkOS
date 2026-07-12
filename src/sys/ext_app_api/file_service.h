/// File-provider facade for an external-app session. Reads and listings are
/// exposed through one provider, while mutations are routed through the shared
/// permission service before reaching storage.
#ifndef EXT_APP_API_FILE_SERVICE_H
#define EXT_APP_API_FILE_SERVICE_H

#include "fs/file_provider.h"
#include "sys/ext_app_api/app_api.h"
#include "sys/ext_app_api/service_result.h"
#include "sys/ext_app_api/permission_service.h"

namespace ext_app_api {

/// Validates and permissions external-app operations on the active file provider.
class FileService {
public:
  /// Binds file operations to one session's permission decisions.
  explicit FileService(PermissionService &permissions);

  /// Reads a bounded range and reports both bytes read and complete file size.
  ServiceResult read(const char *path, size_t offset, uint8_t *out,
                     size_t capacity, size_t *outRead, size_t *outFileSize);
  /// Enumerates one directory until exhausted or the callback stops iteration.
  ServiceResult list(const char *path, AppApiDirectoryEntryHandler handler,
                     void *context);
  /// Writes or appends bytes after obtaining path-specific permission.
  ServiceResult write(const char *path, const uint8_t *data, size_t size,
                      bool append);
  /// Deletes one file after obtaining path-specific permission.
  ServiceResult remove(const char *path);
  /// Opens a validated path for internal service-side streaming reads.
  ServiceResult openRead(const char *path, File &out);
  /// Returns the storage provider selected for the current app session.
  const char *providerId() const;

private:
  PermissionService &permissions;
  const FileProvider *provider = nullptr;
};

} // namespace ext_app_api

#endif
