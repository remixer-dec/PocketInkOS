#include "sys/ext_app_api/file_service.h"

#include "sys/ext_app_api/service_utils.h"

namespace ext_app_api {
namespace {

struct DirectoryContext {
  AppApiDirectoryEntryHandler handler = nullptr;
  void *user = nullptr;
  bool stopped = false;
};

bool forwardEntry(const FileProviderEntry &entry, void *raw) {
  DirectoryContext *context = static_cast<DirectoryContext *>(raw);
  if (context == nullptr || context->handler == nullptr) {
    return false;
  }
  if (!context->handler(entry.name, entry.directory, entry.size,
                        context->user)) {
    context->stopped = true;
    return false;
  }
  return true;
}

} // namespace

FileService::FileService(PermissionService &permissions)
    : permissions(permissions), provider(defaultFileProvider()) {}

const char *FileService::providerId() const {
  return provider != nullptr ? provider->id : nullptr;
}

ServiceResult FileService::openRead(const char *path, File &out) {
  if (!safePath(path)) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid file path");
  }
  if (provider == nullptr) {
    return ServiceResult::fail(ServiceError::Unsupported,
                               "no storage provider");
  }
  out = openProviderPath(provider, path);
  if (!out || out.isDirectory()) {
    return ServiceResult::fail(ServiceError::NotFound, "file not found");
  }
  return ServiceResult::ok();
}

ServiceResult FileService::read(const char *path, size_t offset, uint8_t *out,
                                size_t capacity, size_t *outRead,
                                size_t *outFileSize) {
  if (outRead != nullptr) {
    *outRead = 0;
  }
  if (outFileSize != nullptr) {
    *outFileSize = 0;
  }
  if (capacity > 0 && out == nullptr) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "missing read buffer");
  }
  File file;
  ServiceResult opened = openRead(path, file);
  if (opened.state == ServiceState::Failed) {
    return opened;
  }
  const size_t fileSize = file.size();
  if (outFileSize != nullptr) {
    *outFileSize = fileSize;
  }
  if (offset >= fileSize || capacity == 0) {
    return offset >= fileSize ? ServiceResult::end() : ServiceResult::ok();
  }
  if (offset > UINT32_MAX || !file.seek(static_cast<uint32_t>(offset))) {
    return ServiceResult::fail(ServiceError::Io, "file seek failed");
  }
  const size_t available = fileSize - offset;
  const size_t amount = capacity < available ? capacity : available;
  const size_t amountRead = file.read(out, amount);
  if (outRead != nullptr) {
    *outRead = amountRead;
  }
  if (amountRead != amount) {
    return ServiceResult::fail(ServiceError::Io, "short file read");
  }
  return offset + amountRead >= fileSize ? ServiceResult::end()
                                         : ServiceResult::ok();
}

ServiceResult FileService::list(const char *path,
                                AppApiDirectoryEntryHandler handler,
                                void *context) {
  if (!safePath(path) || handler == nullptr) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid directory request");
  }
  if (provider == nullptr) {
    return ServiceResult::fail(ServiceError::Unsupported,
                               "no storage provider");
  }
  DirectoryContext forwarding{handler, context, false};
  const bool listed =
      listProviderDirectory(provider, path, forwardEntry, &forwarding);
  return listed || forwarding.stopped
             ? ServiceResult::ok()
             : ServiceResult::fail(ServiceError::Io,
                                   "directory listing failed");
}

ServiceResult FileService::write(const char *path, const uint8_t *data,
                                 size_t size, bool append) {
  if (!safePath(path) || (size > 0 && data == nullptr)) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid write request");
  }
  ServiceResult permission = permissions.request(PermissionKind::FileWrite,
                                                 path);
  if (permission.state != ServiceState::Ok) {
    return permission;
  }
  File file = openProviderPathForWrite(provider, path, append);
  if (!file) {
    return ServiceResult::fail(ServiceError::Io, "file open failed");
  }
  const size_t written = size > 0 ? file.write(data, size) : 0;
  file.close();
  return written == size
             ? ServiceResult::ok()
             : ServiceResult::fail(ServiceError::Io, "short file write");
}

ServiceResult FileService::remove(const char *path) {
  if (!safePath(path)) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid delete path");
  }
  ServiceResult permission = permissions.request(PermissionKind::FileDelete,
                                                 path);
  if (permission.state != ServiceState::Ok) {
    return permission;
  }
  return removeProviderPath(provider, path)
             ? ServiceResult::ok()
             : ServiceResult::fail(ServiceError::Io, "file delete failed");
}

} // namespace ext_app_api
