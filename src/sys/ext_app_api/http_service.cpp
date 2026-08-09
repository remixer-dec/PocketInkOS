#include "sys/ext_app_api/http_service.h"

#include "sys/ext_app_api/service_utils.h"
#include "sys/psram_allocator.h"

#include <FS.h>
#include <cstdio>
#include <cstring>
#include <new>

namespace ext_app_api {
namespace {

class SpinGuard {
public:
  explicit SpinGuard(std::atomic_flag &flag) : flag(flag) {
    while (flag.test_and_set(std::memory_order_acquire)) {
    }
  }
  ~SpinGuard() { flag.clear(std::memory_order_release); }

private:
  std::atomic_flag &flag;
};

struct StaticFileContext {
  File file;
};

net::http::Method frameworkMethod(AppApiHttpMethod method) {
  switch (method) {
  case AppApiHttpMethod::Get:
    return net::http::Method::Get;
  case AppApiHttpMethod::Post:
    return net::http::Method::Post;
  case AppApiHttpMethod::Put:
    return net::http::Method::Put;
  case AppApiHttpMethod::Delete:
    return net::http::Method::Delete;
  }
  return net::http::Method::Unknown;
}

AppApiHttpMethod appMethod(net::http::Method method) {
  switch (method) {
  case net::http::Method::Post:
    return AppApiHttpMethod::Post;
  case net::http::Method::Put:
    return AppApiHttpMethod::Put;
  case net::http::Method::Delete:
    return AppApiHttpMethod::Delete;
  default:
    return AppApiHttpMethod::Get;
  }
}

bool copyBounded(char *out, size_t capacity, const char *text,
                 size_t length = SIZE_MAX) {
  if (out == nullptr || capacity == 0 || text == nullptr) {
    return false;
  }
  const size_t actual =
      length == SIZE_MAX ? strnlen(text, capacity) : length;
  if (actual >= capacity) {
    return false;
  }
  memcpy(out, text, actual);
  out[actual] = '\0';
  return true;
}

bool validContentType(const char *value, size_t capacity) {
  if (value == nullptr || capacity == 0) {
    return false;
  }
  const size_t length = strnlen(value, capacity);
  if (length >= capacity) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (c < 0x20U || c == 0x7fU) {
      return false;
    }
  }
  return true;
}

void errorText(net::http::Response &response, const char *text, int status) {
  if (!response.text(text, status)) {
    response.noContent(status);
  }
}

bool readStatic(void *raw, uint8_t *out, size_t capacity, size_t &outRead) {
  outRead = 0;
  StaticFileContext *context = static_cast<StaticFileContext *>(raw);
  if (context == nullptr || (capacity > 0 && out == nullptr)) {
    return false;
  }
  outRead = capacity > 0 ? context->file.read(out, capacity) : 0;
  return outRead == capacity;
}

void closeStatic(void *raw) {
  StaticFileContext *context = static_cast<StaticFileContext *>(raw);
  if (context == nullptr) {
    return;
  }
  context->file.close();
  context->~StaticFileContext();
  psram::release(context);
}

} // namespace

HttpService::HttpService(FileService &files, PermissionService &permissions)
    : files(files), permissions(permissions), server(router, serverConfig()) {}

HttpService::~HttpService() { release(); }

net::http::ServerConfig HttpService::serverConfig() {
  net::http::ServerConfig config;
  config.parser.bodyBytes = kExternalBodyLimit;
  config.maxConnections = kPendingCapacity;
  return config;
}

ServiceResult HttpService::start(uint16_t port, const char *staticDirectory,
                                 const char *username,
                                 const char *password) {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (port < 1024 || staticDirectory == nullptr || username == nullptr ||
      password == nullptr || strnlen(staticDirectory, 288) >= 288 ||
      strnlen(username, 192) >= 192 || strnlen(password, 192) >= 192 ||
      (staticDirectory[0] != '\0' && !safePath(staticDirectory)) ||
      strcmp(staticDirectory, "/") == 0 ||
      (username[0] == '\0' && password[0] != '\0')) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid HTTP server options");
  }
  if (server.running()) {
    return ServiceResult::fail(ServiceError::Busy,
                               "HTTP server already running");
  }
  char resource[24] = {};
  snprintf(resource, sizeof(resource), "tcp:%u",
           static_cast<unsigned>(port));
  ServiceResult permission =
      permissions.request(PermissionKind::LanServer, resource);
  if (permission.state != ServiceState::Ok) {
    return permission;
  }
  if (!corsConfigured) {
    net::http::CorsPolicy cors;
    cors.enabled = true;
    if (!router.cors(cors)) {
      return ServiceResult::fail(ServiceError::OutOfMemory,
                                 "HTTP router allocation failed");
    }
    corsConfigured = true;
  }
  if (username[0] != '\0' &&
      !router.basicAuth(username, password, "PocketInk app")) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid basic auth");
  }
  if (staticDirectory[0] != '\0' && strcmp(staticDirectory, "/") != 0 &&
      !staticMounted) {
    if (!router.staticFiles("/", staticDirectory, openStatic, this)) {
      return ServiceResult::fail(ServiceError::InvalidArgument,
                                 "invalid static directory");
    }
    staticMounted = true;
  }
  if (!server.start("external-http", port)) {
    return ServiceResult::fail(ServiceError::Io,
                               "HTTP listen failed");
  }
  activePort = port;
  return ServiceResult::ok();
}

ServiceResult HttpService::route(AppApiHttpMethod method, const char *path) {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (path == nullptr || path[0] != '/' ||
      frameworkMethod(method) == net::http::Method::Unknown) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid HTTP route");
  }
  return router.route(frameworkMethod(method), path, handleRequest, this)
             ? ServiceResult::ok()
             : ServiceResult::fail(ServiceError::Busy,
                                   "duplicate or full HTTP route table");
}

net::http::DispatchResult HttpService::handleRequest(
    const net::http::RequestContext &request, net::http::Response &response,
    void *context) {
  HttpService *service = static_cast<HttpService *>(context);
  return service != nullptr && service->enqueue(request, response)
             ? net::http::DispatchResult::Deferred
             : net::http::DispatchResult::Responded;
}

bool HttpService::enqueue(const net::http::RequestContext &request,
                          net::http::Response &response) {
  if (request.request.bodySize > kExternalBodyLimit) {
    errorText(response, "Payload too large\n", 413);
    return false;
  }
  SpinGuard guard(pendingLock);
  Pending *slot = nullptr;
  for (Pending &entry : pending) {
    if (entry.requestId == 0) {
      slot = &entry;
      break;
    }
  }
  if (slot == nullptr) {
    errorText(response, "Server busy\n", 503);
    return false;
  }
  const char *query = strchr(request.request.target, '?');
  const size_t pathLength = strlen(request.path);
  const size_t queryLength = query != nullptr ? strlen(query + 1U) : 0;
  if (!copyBounded(slot->path, sizeof(slot->path), request.path, pathLength) ||
      !copyBounded(slot->query, sizeof(slot->query),
                   query != nullptr ? query + 1U : "", queryLength)) {
    errorText(response, "URI too long\n", 414);
    clearPending(*slot);
    return false;
  }
  const char *contentType = request.request.header("content-type");
  if (contentType != nullptr &&
      !copyBounded(slot->contentType, sizeof(slot->contentType),
                   contentType)) {
    errorText(response, "Header too large\n", 431);
    clearPending(*slot);
    return false;
  }
  if (request.request.bodySize > 0) {
    slot->body =
        static_cast<uint8_t *>(psram::allocate(request.request.bodySize));
    if (slot->body == nullptr) {
      errorText(response, "Service unavailable\n", 503);
      clearPending(*slot);
      return false;
    }
    memcpy(slot->body, request.request.body, request.request.bodySize);
  }
  slot->bodySize = request.request.bodySize;
  slot->requestId = request.requestId;
  slot->method = appMethod(request.request.method);
  slot->delivered = false;
  return true;
}

ServiceResult HttpService::next(AppApiHttpRequest &out) {
  SpinGuard lifecycleGuard(lifecycleLock);
  out = {};
  SpinGuard guard(pendingLock);
  for (Pending &entry : pending) {
    if (entry.requestId == 0 || entry.delivered) {
      continue;
    }
    out.requestId = entry.requestId;
    out.method = entry.method;
    out.body = entry.body;
    out.bodySize = entry.bodySize;
    memcpy(out.path, entry.path, sizeof(out.path));
    memcpy(out.query, entry.query, sizeof(out.query));
    memcpy(out.contentType, entry.contentType, sizeof(out.contentType));
    entry.delivered = true;
    return ServiceResult::ok();
  }
  return ServiceResult::end();
}

ServiceResult HttpService::respond(uint32_t requestId, int status,
                                   const char *contentType,
                                   const uint8_t *body, size_t bodySize) {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (requestId == 0 || contentType == nullptr ||
      (bodySize > 0 && body == nullptr) || bodySize > 64U * 1024U) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid HTTP response");
  }
  if (status < 100 || status > 599 || !validContentType(contentType, 64)) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid HTTP response");
  }
  Pending *entry = nullptr;
  {
    SpinGuard guard(pendingLock);
    for (Pending &candidate : pending) {
      if (candidate.requestId == requestId) {
        entry = &candidate;
        break;
      }
    }
    if (entry == nullptr || !entry->delivered || entry->responding) {
      return ServiceResult::fail(ServiceError::NotFound,
                                 "unknown HTTP request");
    }
    entry->responding = true;
  }
  net::http::Response response;
  if (!response.bytes(body, bodySize, contentType, status)) {
    SpinGuard guard(pendingLock);
    if (entry->requestId == requestId) {
      entry->responding = false;
    }
    return ServiceResult::fail(ServiceError::OutOfMemory,
                               "HTTP response allocation failed");
  }
  const bool completed =
      server.respond(requestId, static_cast<net::http::Response &&>(response));
  {
    SpinGuard guard(pendingLock);
    if (entry->requestId == requestId) {
      clearPending(*entry);
    }
  }
  return completed
             ? ServiceResult::ok()
             : ServiceResult::fail(ServiceError::NotFound,
                                   "HTTP request expired");
}

void HttpService::update(uint32_t nowMs) {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (server.running()) {
    server.update(nowMs);
    for (uint8_t i = 0; i < kPendingCapacity; i++) {
      uint32_t requestId = 0;
      {
        SpinGuard guard(pendingLock);
        if (!pending[i].responding) {
          requestId = pending[i].requestId;
        }
      }
      if (requestId == 0 || server.awaitingResponse(requestId)) {
        continue;
      }
      SpinGuard guard(pendingLock);
      if (pending[i].requestId == requestId && !pending[i].responding) {
        clearPending(pending[i]);
      }
    }
  }
}

bool HttpService::openStatic(const char *path, net::http::StaticFile &out,
                             void *context) {
  out = {};
  HttpService *service = static_cast<HttpService *>(context);
  if (service == nullptr) {
    return false;
  }
  File file;
  const ServiceResult opened = service->files.openRead(path, file);
  if (opened.state != ServiceState::Ok) {
    return false;
  }
  void *memory = psram::allocate(sizeof(StaticFileContext));
  if (memory == nullptr) {
    file.close();
    return false;
  }
  StaticFileContext *source = new (memory) StaticFileContext();
  source->file = file;
  out.body.context = source;
  out.body.length = file.size();
  out.body.read = readStatic;
  out.body.close = closeStatic;
  return true;
}

void HttpService::clearPending(Pending &entry) {
  if (entry.body != nullptr && entry.bodySize > 0) {
    memset(entry.body, 0, entry.bodySize);
  }
  psram::release(entry.body);
  entry = {};
}

void HttpService::release() {
  SpinGuard lifecycleGuard(lifecycleLock);
  server.stop();
  {
    SpinGuard guard(pendingLock);
    for (Pending &entry : pending) {
      clearPending(entry);
    }
  }
  router.reset();
  activePort = 0;
  corsConfigured = false;
  staticMounted = false;
}

} // namespace ext_app_api
