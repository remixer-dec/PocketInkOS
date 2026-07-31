/// Small HTTP/1 framework: exact routes, JSON/text helpers, static mounts,
/// Basic authentication, CORS preflight, deferred app responses, and
/// round-robin streaming across bounded parallel clients.
#ifndef POCKETINK_NET_HTTP_SERVER_H
#define POCKETINK_NET_HTTP_SERVER_H

#include "net/http/http_parser.h"
#include "net/network_stack.h"

#include <stddef.h>
#include <stdint.h>

namespace net::http {

struct ResponseHeader {
  char name[32] = {};
  char value[160] = {};
};

struct StreamBody {
  void *context = nullptr;
  size_t length = 0;
  bool (*read)(void *context, uint8_t *out, size_t capacity,
               size_t &outRead) = nullptr;
  void (*close)(void *context) = nullptr;
};

class Response {
public:
  static constexpr uint8_t kHeaderCapacity = 8;

  Response() = default;
  ~Response();
  Response(const Response &) = delete;
  Response &operator=(const Response &) = delete;
  Response(Response &&other) noexcept;
  Response &operator=(Response &&other) noexcept;

  int status() const { return statusCode; }
  size_t contentLength() const;
  const char *contentType() const { return type; }
  const ResponseHeader *headers() const { return extraHeaders; }
  uint8_t headerCount() const { return extraHeaderCount; }
  bool bodySuppressed() const { return suppressBody; }

  bool setStatus(int code);
  bool setHeader(const char *name, const char *value);
  bool text(const char *text, int status = 200,
            const char *contentType = "text/plain; charset=utf-8");
  /// Copies already-serialized JSON into PSRAM and sets application/json.
  bool json(const char *json, int status = 200);
  bool bytes(const uint8_t *data, size_t size, const char *contentType,
             int status = 200);
  bool stream(StreamBody source, const char *contentType, int status = 200);
  void noContent(int status = 204);

private:
  friend class Router;
  friend class Server;

  enum class BodyKind : uint8_t { Empty, Memory, Stream };

  uint8_t *memory = nullptr;
  StreamBody source;
  size_t memorySize = 0;
  int statusCode = 200;
  char type[64] = "text/plain; charset=utf-8";
  ResponseHeader extraHeaders[kHeaderCapacity] = {};
  uint8_t extraHeaderCount = 0;
  BodyKind bodyKind = BodyKind::Empty;
  bool suppressBody = false;

  void release();
};

struct RequestContext {
  const Request &request;
  const char *path = nullptr;
  uint32_t requestId = 0;
};

enum class DispatchResult : uint8_t { Responded, Deferred };
using RouteHandler = DispatchResult (*)(const RequestContext &request,
                                        Response &response, void *context);

struct StaticFile {
  StreamBody body;
};
using StaticOpen = bool (*)(const char *absolutePath, StaticFile &out,
                            void *context);

struct CorsPolicy {
  bool enabled = false;
  /// Exact allowed origin. "*" is permitted, but arbitrary origins are never
  /// reflected back.
  const char *allowOrigin = "*";
  const char *allowHeaders = "Content-Type, Authorization";
  uint32_t maxAgeSeconds = 600;
};

class Router {
public:
  struct Impl;

  Router();
  ~Router();
  Router(const Router &) = delete;
  Router &operator=(const Router &) = delete;

  /// Registers one exact normalized path. Duplicate method/path pairs fail.
  bool route(Method method, const char *path, RouteHandler handler,
             void *context = nullptr);
  bool get(const char *path, RouteHandler handler, void *context = nullptr) {
    return route(Method::Get, path, handler, context);
  }
  bool post(const char *path, RouteHandler handler, void *context = nullptr) {
    return route(Method::Post, path, handler, context);
  }
  bool put(const char *path, RouteHandler handler, void *context = nullptr) {
    return route(Method::Put, path, handler, context);
  }
  bool remove(const char *path, RouteHandler handler, void *context = nullptr) {
    return route(Method::Delete, path, handler, context);
  }

  /// Mounts one URL prefix to one absolute storage directory.
  bool staticFiles(const char *prefix, const char *directory, StaticOpen open,
                   void *context = nullptr);

  /// Empty username disables authentication. Credentials are session-owned and
  /// compared in constant time after strict Base64 decoding.
  bool basicAuth(const char *username, const char *password,
                 const char *realm = "PocketInk");
  bool cors(const CorsPolicy &policy);

  DispatchResult dispatch(const Request &request, uint32_t requestId,
                          Response &response);
  void decorate(const Request &request, Response &response) const;

private:
  Impl *impl = nullptr;

  bool ensureImpl();
};

struct ServerConfig {
  ParserLimits parser;
  uint32_t requestTimeoutMs = 10000;
  uint32_t deferredTimeoutMs = 10000;
  uint32_t sendTimeoutMs = 10000;
  uint8_t maxConnections = 4;
  uint16_t streamChunkBytes = 2048;
};

class Server {
public:
  struct Impl;

  explicit Server(Router &router, ServerConfig config = {});
  ~Server();
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  bool start(const char *owner, uint16_t port);
  void stop();
  bool running() const;
  uint16_t port() const;

  /// Advances accept/read/dispatch/write work without blocking. Multiple
  /// connections are progressed every call for parallel browser asset loads.
  void update(uint32_t nowMs);

  /// Completes a handler that returned Deferred. Unknown/repeated ids fail.
  bool respond(uint32_t requestId, Response &&response);
  bool awaitingResponse(uint32_t requestId) const;

private:
  Router &router;
  ServerConfig config;
  Impl *impl = nullptr;
  mutable std::atomic_flag lifecycleLock = ATOMIC_FLAG_INIT;
};

} // namespace net::http

#endif
