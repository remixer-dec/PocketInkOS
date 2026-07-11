#ifndef APP_API_FILE_SERVICE_H
#define APP_API_FILE_SERVICE_H

#include "fs/file_provider.h"
#include "sys/app_api_services.h"
#include "sys/services/app_api/service_result.h"
#include "sys/services/app_api/permission_service.h"

namespace app_api {

class FileService {
public:
  explicit FileService(PermissionService &permissions);

  ServiceResult read(const char *path, size_t offset, uint8_t *out,
                     size_t capacity, size_t *outRead, size_t *outFileSize);
  ServiceResult list(const char *path, AppApiDirectoryEntryHandler handler,
                     void *context);
  ServiceResult write(const char *path, const uint8_t *data, size_t size,
                      bool append);
  ServiceResult remove(const char *path);
  ServiceResult openRead(const char *path, File &out);
  const char *providerId() const;

private:
  PermissionService &permissions;
  const FileProvider *provider = nullptr;
};

} // namespace app_api

#endif
