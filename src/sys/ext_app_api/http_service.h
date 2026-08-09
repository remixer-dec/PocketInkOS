/// Session-owned LAN HTTP service for sandboxed apps. Dynamic requests are
/// copied into a bounded PSRAM queue and completed from the app's normal update
/// context; networking code never calls Pink or JavaScript directly.
#ifndef EXT_APP_API_HTTP_SERVICE_H
#define EXT_APP_API_HTTP_SERVICE_H

#include "net/http/http_server.h"
#include "sys/ext_app_api/app_api.h"
#include "sys/ext_app_api/file_service.h"
#include "sys/ext_app_api/permission_service.h"
#include "sys/ext_app_api/service_result.h"

#include <atomic>

namespace ext_app_api {

class HttpService {
public:
  HttpService(FileService &files, PermissionService &permissions);
  ~HttpService();

  ServiceResult start(uint16_t port, const char *staticDirectory,
                      const char *username, const char *password);
  ServiceResult route(AppApiHttpMethod method, const char *path);
  ServiceResult next(AppApiHttpRequest &out);
  ServiceResult respond(uint32_t requestId, int status,
                        const char *contentType, const uint8_t *body,
                        size_t bodySize);
  void update(uint32_t nowMs);
  void release();

private:
  static constexpr uint8_t kPendingCapacity = 4;
  static constexpr size_t kExternalBodyLimit = 16U * 1024U;

  struct Pending {
    uint8_t *body = nullptr;
    size_t bodySize = 0;
    uint32_t requestId = 0;
    AppApiHttpMethod method = AppApiHttpMethod::Get;
    char path[192] = {};
    char query[384] = {};
    char contentType[96] = {};
    bool delivered = false;
    bool responding = false;
  };

  FileService &files;
  PermissionService &permissions;
  net::http::Router router;
  net::http::Server server;
  Pending pending[kPendingCapacity];
  mutable std::atomic_flag lifecycleLock = ATOMIC_FLAG_INIT;
  mutable std::atomic_flag pendingLock = ATOMIC_FLAG_INIT;
  uint16_t activePort = 0;
  bool corsConfigured = false;
  bool staticMounted = false;

  static net::http::DispatchResult handleRequest(
      const net::http::RequestContext &request,
      net::http::Response &response, void *context);
  static net::http::ServerConfig serverConfig();
  static bool openStatic(const char *path, net::http::StaticFile &out,
                         void *context);
  bool enqueue(const net::http::RequestContext &request,
               net::http::Response &response);
  void clearPending(Pending &entry);
};

} // namespace ext_app_api

#endif
