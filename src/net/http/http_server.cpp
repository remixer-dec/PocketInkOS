#include "net/http/http_server.h"

#include "sys/psram_allocator.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <strings.h>

namespace net::http {
namespace {

constexpr uint8_t kRouteCapacity = 24;
constexpr uint8_t kMountCapacity = 4;
constexpr size_t kRoutePathCapacity = 192;
constexpr size_t kStaticPathCapacity = 288;
constexpr size_t kAuthCapacity = 192;
constexpr size_t kHeaderOutputCapacity = 1840;

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

bool copyText(char *out, size_t capacity, const char *text) {
  if (out == nullptr || capacity == 0 || text == nullptr) {
    return false;
  }
  const size_t length = strnlen(text, capacity);
  if (length >= capacity) {
    return false;
  }
  memcpy(out, text, length + 1U);
  return true;
}

bool responseHeaderName(const char *name, size_t capacity) {
  if (name == nullptr || capacity == 0) {
    return false;
  }
  const size_t length = strnlen(name, capacity);
  if (length == 0 || length >= capacity) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-')) {
      return false;
    }
  }
  return true;
}

bool responseHeaderValue(const char *value, size_t capacity) {
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

const char *reasonPhrase(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 202:
    return "Accepted";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 408:
    return "Request Timeout";
  case 413:
    return "Payload Too Large";
  case 414:
    return "URI Too Long";
  case 415:
    return "Unsupported Media Type";
  case 431:
    return "Request Header Fields Too Large";
  case 500:
    return "Internal Server Error";
  case 503:
    return "Service Unavailable";
  case 504:
    return "Gateway Timeout";
  case 505:
    return "HTTP Version Not Supported";
  default:
    return "Response";
  }
}

const char *mimeType(const char *path) {
  const char *dot = path != nullptr ? strrchr(path, '.') : nullptr;
  if (dot == nullptr) {
    return "application/octet-stream";
  }
  if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) {
    return "text/html; charset=utf-8";
  }
  if (strcmp(dot, ".css") == 0) {
    return "text/css; charset=utf-8";
  }
  if (strcmp(dot, ".js") == 0 || strcmp(dot, ".mjs") == 0) {
    return "text/javascript; charset=utf-8";
  }
  if (strcmp(dot, ".json") == 0) {
    return "application/json; charset=utf-8";
  }
  if (strcmp(dot, ".svg") == 0) {
    return "image/svg+xml";
  }
  if (strcmp(dot, ".png") == 0) {
    return "image/png";
  }
  if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) {
    return "image/jpeg";
  }
  if (strcmp(dot, ".webp") == 0) {
    return "image/webp";
  }
  if (strcmp(dot, ".ico") == 0) {
    return "image/x-icon";
  }
  if (strcmp(dot, ".txt") == 0) {
    return "text/plain; charset=utf-8";
  }
  if (strcmp(dot, ".woff2") == 0) {
    return "font/woff2";
  }
  return "application/octet-stream";
}

bool appendIndex(char *path, size_t capacity) {
  const size_t length = strlen(path);
  const bool hasSlash = length > 0 && path[length - 1U] == '/';
  static constexpr char slashSuffix[] = "/index.html";
  static constexpr char plainSuffix[] = "index.html";
  const char *suffix = hasSlash ? plainSuffix : slashSuffix;
  const size_t suffixBytes =
      hasSlash ? sizeof(plainSuffix) : sizeof(slashSuffix);
  if (length + suffixBytes > capacity) {
    return false;
  }
  memcpy(path + length, suffix, suffixBytes);
  return true;
}

int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return -1;
}

bool decodeBasic(const char *header, uint8_t *out, size_t capacity,
                 size_t &outSize) {
  outSize = 0;
  if (header == nullptr || strncmp(header, "Basic ", 6) != 0) {
    return false;
  }
  const char *encoded = header + 6;
  const size_t length = strlen(encoded);
  if (length == 0 || (length & 3U) != 0 ||
      length > ((capacity + 2U) / 3U) * 4U) {
    return false;
  }
  for (size_t at = 0; at < length; at += 4U) {
    int values[4] = {};
    uint8_t padding = 0;
    for (uint8_t i = 0; i < 4; i++) {
      const char c = encoded[at + i];
      if (c == '=') {
        if (i < 2 || at + 4U != length) {
          return false;
        }
        values[i] = 0;
        padding++;
      } else {
        if (padding != 0) {
          return false;
        }
        values[i] = base64Value(c);
        if (values[i] < 0) {
          return false;
        }
      }
    }
    if (padding > 2 || (padding == 1 && (values[2] & 0x03) != 0) ||
        (padding == 2 && (values[1] & 0x0f) != 0)) {
      return false;
    }
    const uint32_t bits =
        (static_cast<uint32_t>(values[0]) << 18U) |
        (static_cast<uint32_t>(values[1]) << 12U) |
        (static_cast<uint32_t>(values[2]) << 6U) |
        static_cast<uint32_t>(values[3]);
    const uint8_t bytes = static_cast<uint8_t>(3U - padding);
    if (outSize + bytes > capacity) {
      return false;
    }
    if (bytes > 0) {
      out[outSize++] = static_cast<uint8_t>(bits >> 16U);
    }
    if (bytes > 1) {
      out[outSize++] = static_cast<uint8_t>(bits >> 8U);
    }
    if (bytes > 2) {
      out[outSize++] = static_cast<uint8_t>(bits);
    }
  }
  return true;
}

bool constantTimeEqual(const uint8_t *left, size_t leftSize,
                       const uint8_t *right, size_t rightSize) {
  const size_t count = leftSize > rightSize ? leftSize : rightSize;
  uint8_t difference = static_cast<uint8_t>(leftSize ^ rightSize);
  for (size_t i = 0; i < count; i++) {
    const uint8_t a = i < leftSize ? left[i] : 0;
    const uint8_t b = i < rightSize ? right[i] : 0;
    difference |= static_cast<uint8_t>(a ^ b);
  }
  return difference == 0;
}

int statusForParseError(ParseError error) {
  switch (error) {
  case ParseError::HeaderTooLarge:
  case ParseError::TooManyHeaders:
    return 431;
  case ParseError::BodyTooLarge:
    return 413;
  case ParseError::InvalidTarget:
    return 414;
  case ParseError::UnsupportedVersion:
    return 505;
  case ParseError::UnsupportedTransferEncoding:
    return 415;
  case ParseError::NoMemory:
    return 503;
  default:
    return 400;
  }
}

void textOrEmpty(Response &response, const char *text, int status) {
  if (!response.text(text, status)) {
    response.noContent(status);
  }
}

} // namespace

Response::~Response() { release(); }

Response::Response(Response &&other) noexcept { *this = static_cast<Response &&>(other); }

Response &Response::operator=(Response &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  release();
  memory = other.memory;
  source = other.source;
  memorySize = other.memorySize;
  statusCode = other.statusCode;
  memcpy(type, other.type, sizeof(type));
  memcpy(extraHeaders, other.extraHeaders, sizeof(extraHeaders));
  extraHeaderCount = other.extraHeaderCount;
  bodyKind = other.bodyKind;
  suppressBody = other.suppressBody;
  other.memory = nullptr;
  other.source = {};
  other.memorySize = 0;
  other.extraHeaderCount = 0;
  other.bodyKind = BodyKind::Empty;
  other.suppressBody = false;
  return *this;
}

void Response::release() {
  if (memory != nullptr && memorySize > 0) {
    memset(memory, 0, memorySize);
  }
  psram::release(memory);
  memory = nullptr;
  memorySize = 0;
  if (bodyKind == BodyKind::Stream && source.close != nullptr) {
    source.close(source.context);
  }
  source = {};
  bodyKind = BodyKind::Empty;
}

size_t Response::contentLength() const {
  if (bodyKind == BodyKind::Memory) {
    return memorySize;
  }
  if (bodyKind == BodyKind::Stream) {
    return source.length;
  }
  return 0;
}

bool Response::setStatus(int code) {
  if (code < 100 || code > 599) {
    return false;
  }
  statusCode = code;
  return true;
}

bool Response::setHeader(const char *name, const char *value) {
  if (extraHeaderCount >= kHeaderCapacity ||
      !responseHeaderName(name, sizeof(ResponseHeader::name)) ||
      !responseHeaderValue(value, sizeof(ResponseHeader::value))) {
    return false;
  }
  if (strcasecmp(name, "Content-Length") == 0 ||
      strcasecmp(name, "Transfer-Encoding") == 0 ||
      strcasecmp(name, "Connection") == 0 ||
      strcasecmp(name, "Content-Type") == 0 ||
      strcasecmp(name, "Server") == 0) {
    return false;
  }
  for (uint8_t i = 0; i < extraHeaderCount; i++) {
    if (strcasecmp(extraHeaders[i].name, name) == 0) {
      return false;
    }
  }
  ResponseHeader &header = extraHeaders[extraHeaderCount];
  if (!copyText(header.name, sizeof(header.name), name) ||
      !copyText(header.value, sizeof(header.value), value)) {
    header = {};
    return false;
  }
  extraHeaderCount++;
  return true;
}

bool Response::bytes(const uint8_t *data, size_t size, const char *contentType,
                     int status) {
  char checkedType[sizeof(type)] = {};
  if ((size > 0 && data == nullptr) || status < 100 || status > 599 ||
      !copyText(checkedType, sizeof(checkedType), contentType) ||
      !responseHeaderValue(checkedType, sizeof(checkedType))) {
    return false;
  }
  uint8_t *copy = nullptr;
  if (size > 0) {
    copy = static_cast<uint8_t *>(psram::allocate(size));
    if (copy == nullptr) {
      return false;
    }
    memcpy(copy, data, size);
  }
  release();
  memory = copy;
  memorySize = size;
  statusCode = status;
  memcpy(type, checkedType, sizeof(type));
  bodyKind = BodyKind::Memory;
  return true;
}

bool Response::text(const char *text, int status, const char *contentType) {
  return text != nullptr &&
         bytes(reinterpret_cast<const uint8_t *>(text), strlen(text),
               contentType, status);
}

bool Response::json(const char *json, int status) {
  return text(json, status, "application/json; charset=utf-8");
}

bool Response::stream(StreamBody next, const char *contentType, int status) {
  char checkedType[sizeof(type)] = {};
  if ((next.length > 0 && next.read == nullptr) || status < 100 ||
      status > 599 ||
      !copyText(checkedType, sizeof(checkedType), contentType) ||
      !responseHeaderValue(checkedType, sizeof(checkedType))) {
    return false;
  }
  release();
  source = next;
  statusCode = status;
  memcpy(type, checkedType, sizeof(type));
  bodyKind = BodyKind::Stream;
  return true;
}

void Response::noContent(int status) {
  release();
  statusCode = status >= 100 && status <= 599 ? status : 500;
  type[0] = '\0';
}

struct Router::Impl {
  struct Route {
    RouteHandler handler = nullptr;
    void *context = nullptr;
    Method method = Method::Unknown;
    char path[kRoutePathCapacity] = {};
  };
  struct Mount {
    StaticOpen open = nullptr;
    void *context = nullptr;
    char directory[kStaticPathCapacity] = {};
    char prefix[kRoutePathCapacity] = {};
  };

  mutable std::atomic_flag lock = ATOMIC_FLAG_INIT;
  Route routes[kRouteCapacity];
  Mount mounts[kMountCapacity];
  uint8_t routeCount = 0;
  uint8_t mountCount = 0;
  char auth[kAuthCapacity] = {};
  size_t authLength = 0;
  char realm[40] = "PocketInk";
  char allowOrigin[96] = "*";
  char allowHeaders[160] = "Content-Type, Authorization";
  uint32_t corsMaxAge = 600;
  bool corsEnabled = false;

  bool authorized(const Request &request) {
    if (request.method == Method::Options) {
      return true;
    }
    uint8_t expected[kAuthCapacity] = {};
    size_t expectedSize = 0;
    {
      SpinGuard guard(lock);
      if (authLength == 0) {
        return true;
      }
      expectedSize = authLength;
      memcpy(expected, auth, expectedSize);
    }
    uint8_t decoded[kAuthCapacity] = {};
    size_t decodedSize = 0;
    const bool matches =
        decodeBasic(request.header("authorization"), decoded, sizeof(decoded),
                    decodedSize) &&
        constantTimeEqual(decoded, decodedSize, expected, expectedSize);
    memset(decoded, 0, sizeof(decoded));
    memset(expected, 0, sizeof(expected));
    return matches;
  }
};

Router::Router() = default;

Router::~Router() {
  if (impl != nullptr) {
    memset(impl->auth, 0, sizeof(impl->auth));
    impl->~Impl();
    psram::release(impl);
  }
}

bool Router::ensureImpl() {
  if (impl != nullptr) {
    return true;
  }
  void *memory = psram::allocate(sizeof(Impl));
  if (memory == nullptr) {
    return false;
  }
  impl = new (memory) Impl();
  return true;
}

bool Router::route(Method method, const char *path, RouteHandler handler,
                   void *context) {
  if (method == Method::Unknown || method == Method::Options ||
      handler == nullptr || !ensureImpl()) {
    return false;
  }
  char normalized[kRoutePathCapacity] = {};
  if (!normalizeUrlPath(path, normalized, sizeof(normalized)) ||
      strchr(path, '?') != nullptr) {
    return false;
  }
  SpinGuard guard(impl->lock);
  for (uint8_t i = 0; i < impl->routeCount; i++) {
    if (impl->routes[i].method == method &&
        strcmp(impl->routes[i].path, normalized) == 0) {
      return false;
    }
  }
  if (impl->routeCount >= kRouteCapacity) {
    return false;
  }
  Impl::Route &route = impl->routes[impl->routeCount];
  if (!copyText(route.path, sizeof(route.path), normalized)) {
    return false;
  }
  route.method = method;
  route.handler = handler;
  route.context = context;
  impl->routeCount++;
  return true;
}

bool Router::staticFiles(const char *prefix, const char *directory,
                         StaticOpen open, void *context) {
  if (open == nullptr || !ensureImpl()) {
    return false;
  }
  char normalizedPrefix[kRoutePathCapacity] = {};
  char normalizedDirectory[kStaticPathCapacity] = {};
  if (!normalizeUrlPath(prefix, normalizedPrefix, sizeof(normalizedPrefix)) ||
      !normalizeUrlPath(directory, normalizedDirectory,
                        sizeof(normalizedDirectory)) ||
      strchr(prefix, '?') != nullptr || strchr(directory, '?') != nullptr) {
    return false;
  }
  SpinGuard guard(impl->lock);
  for (uint8_t i = 0; i < impl->mountCount; i++) {
    if (strcmp(impl->mounts[i].prefix, normalizedPrefix) == 0) {
      return false;
    }
  }
  if (impl->mountCount >= kMountCapacity) {
    return false;
  }
  Impl::Mount &mount = impl->mounts[impl->mountCount];
  if (!copyText(mount.prefix, sizeof(mount.prefix), normalizedPrefix) ||
      !copyText(mount.directory, sizeof(mount.directory),
                normalizedDirectory)) {
    mount = {};
    return false;
  }
  mount.open = open;
  mount.context = context;
  impl->mountCount++;
  return true;
}

bool Router::basicAuth(const char *username, const char *password,
                       const char *realm) {
  if (!ensureImpl() || username == nullptr || password == nullptr ||
      realm == nullptr) {
    return false;
  }
  const size_t userLength = strnlen(username, sizeof(impl->auth));
  const size_t passwordLength = strnlen(password, sizeof(impl->auth));
  const size_t realmLength = strnlen(realm, sizeof(impl->realm));
  if (userLength >= sizeof(impl->auth) ||
      passwordLength >= sizeof(impl->auth) ||
      realmLength >= sizeof(impl->realm) ||
      memchr(realm, '"', realmLength) != nullptr ||
      !responseHeaderValue(realm, sizeof(impl->realm))) {
    return false;
  }
  if (userLength == 0) {
    SpinGuard guard(impl->lock);
    memset(impl->auth, 0, sizeof(impl->auth));
    impl->authLength = 0;
    return true;
  }
  if (userLength + 1U + passwordLength >= sizeof(impl->auth) ||
      memchr(username, ':', userLength) != nullptr) {
    return false;
  }
  SpinGuard guard(impl->lock);
  memset(impl->auth, 0, sizeof(impl->auth));
  memcpy(impl->auth, username, userLength);
  impl->auth[userLength] = ':';
  memcpy(impl->auth + userLength + 1U, password, passwordLength);
  impl->authLength = userLength + 1U + passwordLength;
  impl->auth[impl->authLength] = '\0';
  return copyText(impl->realm, sizeof(impl->realm), realm);
}

bool Router::cors(const CorsPolicy &policy) {
  if (!ensureImpl()) {
    return false;
  }
  char checkedOrigin[sizeof(impl->allowOrigin)] = {};
  char checkedHeaders[sizeof(impl->allowHeaders)] = {};
  if (policy.enabled &&
      (!responseHeaderValue(policy.allowOrigin, sizeof(checkedOrigin)) ||
       !responseHeaderValue(policy.allowHeaders, sizeof(checkedHeaders)) ||
       !copyText(checkedOrigin, sizeof(checkedOrigin), policy.allowOrigin) ||
       !copyText(checkedHeaders, sizeof(checkedHeaders),
                 policy.allowHeaders))) {
    return false;
  }
  SpinGuard guard(impl->lock);
  if (policy.enabled) {
    memcpy(impl->allowOrigin, checkedOrigin, sizeof(impl->allowOrigin));
    memcpy(impl->allowHeaders, checkedHeaders, sizeof(impl->allowHeaders));
  }
  impl->corsEnabled = policy.enabled;
  impl->corsMaxAge = policy.maxAgeSeconds;
  return true;
}

void Router::decorate(const Request &, Response &response) const {
  if (impl == nullptr) {
    return;
  }
  char origin[sizeof(impl->allowOrigin)] = {};
  {
    SpinGuard guard(impl->lock);
    if (!impl->corsEnabled) {
      return;
    }
    memcpy(origin, impl->allowOrigin, sizeof(origin));
  }
  response.setHeader("Access-Control-Allow-Origin", origin);
}

DispatchResult Router::dispatch(const Request &request, uint32_t requestId,
                                Response &response) {
  char path[kRoutePathCapacity] = {};
  if (request.method == Method::Options && strcmp(request.target, "*") == 0) {
    memcpy(path, "/", 2);
  } else if (!normalizeUrlPath(request.target, path, sizeof(path))) {
    textOrEmpty(response, "Bad request\n", 400);
    return DispatchResult::Responded;
  }
  if (!ensureImpl()) {
    textOrEmpty(response, "Service unavailable\n", 503);
    return DispatchResult::Responded;
  }
  if (!impl->authorized(request)) {
    textOrEmpty(response, "Unauthorized\n", 401);
    char realm[sizeof(impl->realm)] = {};
    {
      SpinGuard guard(impl->lock);
      memcpy(realm, impl->realm, sizeof(realm));
    }
    char challenge[96] = {};
    snprintf(challenge, sizeof(challenge), "Basic realm=\"%s\"",
             realm);
    response.setHeader("WWW-Authenticate", challenge);
    decorate(request, response);
    return DispatchResult::Responded;
  }
  if (request.method == Method::Options) {
    response.noContent();
    response.setHeader("Allow", "GET, HEAD, POST, PUT, DELETE, OPTIONS");
    char allowOrigin[sizeof(impl->allowOrigin)] = {};
    char allowHeaders[sizeof(impl->allowHeaders)] = {};
    uint32_t maxAgeSeconds = 0;
    bool corsEnabled = false;
    {
      SpinGuard guard(impl->lock);
      corsEnabled = impl->corsEnabled;
      maxAgeSeconds = impl->corsMaxAge;
      memcpy(allowOrigin, impl->allowOrigin, sizeof(allowOrigin));
      memcpy(allowHeaders, impl->allowHeaders, sizeof(allowHeaders));
    }
    if (corsEnabled) {
      response.setHeader("Access-Control-Allow-Origin", allowOrigin);
      response.setHeader("Access-Control-Allow-Methods",
                         "GET, HEAD, POST, PUT, DELETE, OPTIONS");
      response.setHeader("Access-Control-Allow-Headers", allowHeaders);
      char maxAge[16] = {};
      snprintf(maxAge, sizeof(maxAge), "%lu",
               static_cast<unsigned long>(maxAgeSeconds));
      response.setHeader("Access-Control-Max-Age", maxAge);
    }
    return DispatchResult::Responded;
  }

  RouteHandler handler = nullptr;
  void *handlerContext = nullptr;
  bool pathExists = false;
  {
    SpinGuard guard(impl->lock);
    const Method routeMethod =
        request.method == Method::Head ? Method::Get : request.method;
    for (uint8_t i = 0; i < impl->routeCount; i++) {
      if (strcmp(impl->routes[i].path, path) != 0) {
        continue;
      }
      pathExists = true;
      if (impl->routes[i].method == routeMethod) {
        handler = impl->routes[i].handler;
        handlerContext = impl->routes[i].context;
        break;
      }
    }
  }
  if (handler != nullptr) {
    const DispatchResult result =
        handler({request, path, requestId}, response, handlerContext);
    if (result == DispatchResult::Responded) {
      response.suppressBody = request.method == Method::Head;
      decorate(request, response);
    }
    return result;
  }
  if (pathExists) {
    textOrEmpty(response, "Method not allowed\n", 405);
    response.setHeader("Allow", "GET, HEAD, POST, PUT, DELETE, OPTIONS");
    decorate(request, response);
    return DispatchResult::Responded;
  }

  if (request.method == Method::Get || request.method == Method::Head) {
    uint8_t mountCount = 0;
    {
      SpinGuard guard(impl->lock);
      mountCount = impl->mountCount;
    }
    for (uint8_t i = 0; i < mountCount; i++) {
      Impl::Mount mount;
      {
        SpinGuard guard(impl->lock);
        mount = impl->mounts[i];
      }
      char filePath[kStaticPathCapacity] = {};
      if (!staticFilePath(mount.directory, mount.prefix, path, filePath,
                          sizeof(filePath))) {
        continue;
      }
      const bool mountRoot =
          strcmp(path, mount.prefix) == 0;
      if (mountRoot && !appendIndex(filePath, sizeof(filePath))) {
        break;
      }
      StaticFile file;
      if (!mount.open(filePath, file, mount.context) ||
          (file.body.length > 0 && file.body.read == nullptr)) {
        if (file.body.close != nullptr) {
          file.body.close(file.body.context);
        }
        continue;
      }
      if (!response.stream(file.body, mimeType(filePath))) {
        if (file.body.close != nullptr) {
          file.body.close(file.body.context);
        }
        textOrEmpty(response, "Service unavailable\n", 503);
      }
      response.suppressBody = request.method == Method::Head;
      decorate(request, response);
      return DispatchResult::Responded;
    }
  }
  textOrEmpty(response, "Not found\n", 404);
  decorate(request, response);
  return DispatchResult::Responded;
}

struct Server::Impl {
  enum class ClientState : uint8_t {
    Free,
    Receiving,
    Deferred,
    SendingHeaders,
    SendingBody,
  };

  struct Client {
    explicit Client(ParserLimits limits) : parser(limits) {}

    RequestParser parser;
    Response response;
    SocketHandle socket = kInvalidSocket;
    uint8_t *chunk = nullptr;
    size_t chunkSize = 0;
    size_t chunkSent = 0;
    size_t bodySent = 0;
    size_t headerSize = 0;
    size_t headerSent = 0;
    uint32_t requestId = 0;
    uint32_t deadline = 0;
    ClientState state = ClientState::Free;
    char header[kHeaderOutputCapacity] = {};
  };

  Client *clients = nullptr;
  SocketHandle listener = kInvalidSocket;
  uint32_t nextRequestId = 1;
  uint32_t nowMs = 0;
  uint16_t port = 0;
  uint8_t clientCount = 0;
  std::atomic_flag lock = ATOMIC_FLAG_INIT;
};

Server::Server(Router &router, ServerConfig config)
    : router(router), config(config) {
  if (this->config.maxConnections == 0) {
    this->config.maxConnections = 1;
  }
  if (this->config.maxConnections > 6) {
    this->config.maxConnections = 6;
  }
  if (this->config.streamChunkBytes < 256) {
    this->config.streamChunkBytes = 256;
  }
  if (this->config.streamChunkBytes > 4096) {
    this->config.streamChunkBytes = 4096;
  }
  constexpr uint32_t kMaximumTimeout = 0x7fffffffUL;
  if (this->config.requestTimeoutMs > kMaximumTimeout) {
    this->config.requestTimeoutMs = kMaximumTimeout;
  }
  if (this->config.deferredTimeoutMs > kMaximumTimeout) {
    this->config.deferredTimeoutMs = kMaximumTimeout;
  }
  if (this->config.sendTimeoutMs > kMaximumTimeout) {
    this->config.sendTimeoutMs = kMaximumTimeout;
  }
}

Server::~Server() { stop(); }

static void resetClient(Server::Impl::Client &client) {
  networkStack().close(client.socket);
  psram::release(client.chunk);
  client.chunk = nullptr;
  client.response = {};
  client.parser.reset();
  client.socket = kInvalidSocket;
  client.chunkSize = 0;
  client.chunkSent = 0;
  client.bodySent = 0;
  client.headerSize = 0;
  client.headerSent = 0;
  client.requestId = 0;
  client.deadline = 0;
  client.state = Server::Impl::ClientState::Free;
}

bool Server::start(const char *owner, uint16_t port) {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (impl != nullptr && impl->listener != kInvalidSocket) {
    return impl->port == port;
  }
  if (owner == nullptr || port == 0) {
    return false;
  }
  void *memory = psram::allocate(sizeof(Impl));
  if (memory == nullptr) {
    return false;
  }
  Impl *created = new (memory) Impl();
  const size_t clientsBytes =
      static_cast<size_t>(config.maxConnections) * sizeof(Impl::Client);
  created->clients =
      static_cast<Impl::Client *>(psram::allocate(clientsBytes));
  if (created->clients == nullptr) {
    created->~Impl();
    psram::release(created);
    return false;
  }
  for (uint8_t i = 0; i < config.maxConnections; i++) {
    new (&created->clients[i]) Impl::Client(config.parser);
  }
  SocketHandle listener = kInvalidSocket;
  const SocketResult opened =
      networkStack().openTcpServer(owner, port, config.maxConnections, listener);
  if (!opened.ok()) {
    for (uint8_t i = 0; i < config.maxConnections; i++) {
      created->clients[i].~Client();
    }
    psram::release(created->clients);
    created->~Impl();
    psram::release(created);
    return false;
  }
  created->listener = listener;
  created->port = port;
  created->clientCount = config.maxConnections;
  impl = created;
  return true;
}

void Server::stop() {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (impl == nullptr) {
    return;
  }
  networkStack().close(impl->listener);
  for (uint8_t i = 0; i < impl->clientCount; i++) {
    resetClient(impl->clients[i]);
    impl->clients[i].~Client();
  }
  psram::release(impl->clients);
  impl->~Impl();
  psram::release(impl);
  impl = nullptr;
}

bool Server::running() const {
  SpinGuard lifecycleGuard(lifecycleLock);
  return impl != nullptr && impl->listener != kInvalidSocket;
}

uint16_t Server::port() const {
  SpinGuard lifecycleGuard(lifecycleLock);
  return impl != nullptr ? impl->port : 0;
}

static bool prepareHeaders(Server::Impl::Client &client) {
  const Response &response = client.response;
  const int written = snprintf(
      client.header, sizeof(client.header),
      "HTTP/1.1 %d %s\r\n"
      "Server: PocketInk\r\n"
      "Connection: close\r\n"
      "Content-Length: %lu\r\n",
      response.status(), reasonPhrase(response.status()),
      static_cast<unsigned long>(response.contentLength()));
  if (written < 0 || static_cast<size_t>(written) >= sizeof(client.header)) {
    return false;
  }
  size_t used = static_cast<size_t>(written);
  if (response.contentType()[0] != '\0') {
    const int amount =
        snprintf(client.header + used, sizeof(client.header) - used,
                 "Content-Type: %s\r\n", response.contentType());
    if (amount < 0 || static_cast<size_t>(amount) >=
                          sizeof(client.header) - used) {
      return false;
    }
    used += static_cast<size_t>(amount);
  }
  static constexpr char security[] = "X-Content-Type-Options: nosniff\r\n";
  if (sizeof(security) - 1U >= sizeof(client.header) - used) {
    return false;
  }
  memcpy(client.header + used, security, sizeof(security) - 1U);
  used += sizeof(security) - 1U;
  for (uint8_t i = 0; i < response.headerCount(); i++) {
    const ResponseHeader &header = response.headers()[i];
    const int amount =
        snprintf(client.header + used, sizeof(client.header) - used,
                 "%s: %s\r\n", header.name, header.value);
    if (amount < 0 || static_cast<size_t>(amount) >=
                          sizeof(client.header) - used) {
      return false;
    }
    used += static_cast<size_t>(amount);
  }
  if (used + 2U > sizeof(client.header)) {
    return false;
  }
  client.header[used++] = '\r';
  client.header[used++] = '\n';
  client.headerSize = used;
  client.headerSent = 0;
  client.state = Server::Impl::ClientState::SendingHeaders;
  return true;
}

static void errorResponse(Server::Impl::Client &client, int status,
                          uint32_t deadline) {
  char body[48] = {};
  snprintf(body, sizeof(body), "%d %s\n", status, reasonPhrase(status));
  textOrEmpty(client.response, body, status);
  client.deadline = deadline;
  if (!prepareHeaders(client)) {
    resetClient(client);
  }
}

void Server::update(uint32_t nowMs) {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (impl == nullptr) {
    return;
  }
  SpinGuard serverGuard(impl->lock);
  impl->nowMs = nowMs;

  while (true) {
    Impl::Client *slot = nullptr;
    for (uint8_t i = 0; i < impl->clientCount; i++) {
      if (impl->clients[i].state == Impl::ClientState::Free) {
        slot = &impl->clients[i];
        break;
      }
    }
    if (slot == nullptr) {
      break;
    }
    SocketHandle accepted = kInvalidSocket;
    const SocketResult result = networkStack().accept(impl->listener, accepted);
    if (!result.ok()) {
      break;
    }
    slot->socket = accepted;
    slot->deadline = nowMs + config.requestTimeoutMs;
    slot->state = Impl::ClientState::Receiving;
  }

  for (uint8_t i = 0; i < impl->clientCount; i++) {
    Impl::Client &client = impl->clients[i];
    if (client.state == Impl::ClientState::Free) {
      continue;
    }
    if (static_cast<int32_t>(nowMs - client.deadline) >= 0 &&
        (client.state == Impl::ClientState::Receiving ||
         client.state == Impl::ClientState::Deferred)) {
      errorResponse(client,
                    client.state == Impl::ClientState::Deferred ? 504 : 408,
                    nowMs + config.sendTimeoutMs);
    } else if (static_cast<int32_t>(nowMs - client.deadline) >= 0 &&
               (client.state == Impl::ClientState::SendingHeaders ||
                client.state == Impl::ClientState::SendingBody)) {
      resetClient(client);
      continue;
    }

    if (client.state == Impl::ClientState::Receiving) {
      uint8_t input[768];
      size_t received = 0;
      const SocketResult read =
          networkStack().receive(client.socket, input, sizeof(input), received);
      if (read.ok() && received == 0) {
        resetClient(client);
        continue;
      }
      if (read.ok()) {
        size_t offset = 0;
        while (offset < received &&
               client.parser.state() == ParseState::Incomplete) {
          const ParseResult parsed =
              client.parser.feed(input + offset, received - offset);
          offset += parsed.consumed;
          if (parsed.state == ParseState::Failed) {
            errorResponse(client, statusForParseError(parsed.error),
                          nowMs + config.sendTimeoutMs);
            break;
          }
          if (parsed.consumed == 0) {
            break;
          }
        }
        if (client.parser.state() == ParseState::Complete) {
          bool collision = false;
          do {
            collision = false;
            client.requestId = impl->nextRequestId++;
            if (impl->nextRequestId == 0) {
              impl->nextRequestId = 1;
            }
            for (uint8_t other = 0; other < impl->clientCount; other++) {
              if (&impl->clients[other] != &client &&
                  impl->clients[other].state != Impl::ClientState::Free &&
                  impl->clients[other].requestId == client.requestId) {
                collision = true;
                break;
              }
            }
          } while (client.requestId == 0 || collision);
          const DispatchResult dispatched = router.dispatch(
              client.parser.request(), client.requestId, client.response);
          if (dispatched == DispatchResult::Deferred) {
            client.deadline = nowMs + config.deferredTimeoutMs;
            client.state = Impl::ClientState::Deferred;
          } else {
            client.deadline = nowMs + config.sendTimeoutMs;
            if (!prepareHeaders(client)) {
              errorResponse(client, 500, nowMs + config.sendTimeoutMs);
            }
          }
        }
      } else if (!read.wouldBlock()) {
        resetClient(client);
        continue;
      }
    }

    if (client.state == Impl::ClientState::SendingHeaders) {
      size_t sent = 0;
      const SocketResult result = networkStack().send(
          client.socket,
          reinterpret_cast<const uint8_t *>(client.header) + client.headerSent,
          client.headerSize - client.headerSent, sent);
      if (!result.ok() && !result.wouldBlock()) {
        resetClient(client);
        continue;
      }
      client.headerSent += sent;
      if (sent > 0) {
        client.deadline = nowMs + config.sendTimeoutMs;
      }
      if (client.headerSent == client.headerSize) {
        if (client.response.bodySuppressed() ||
            client.response.contentLength() == 0) {
          resetClient(client);
          continue;
        }
        client.state = Impl::ClientState::SendingBody;
      }
    }

    if (client.state != Impl::ClientState::SendingBody) {
      continue;
    }
    const size_t total = client.response.contentLength();
    if (client.response.bodyKind == Response::BodyKind::Memory) {
      size_t sent = 0;
      const SocketResult result = networkStack().send(
          client.socket, client.response.memory + client.bodySent,
          total - client.bodySent, sent);
      if (!result.ok() && !result.wouldBlock()) {
        resetClient(client);
        continue;
      }
      client.bodySent += sent;
      if (sent > 0) {
        client.deadline = nowMs + config.sendTimeoutMs;
      }
    } else {
      if (client.chunk == nullptr) {
        client.chunk =
            static_cast<uint8_t *>(psram::allocate(config.streamChunkBytes));
        if (client.chunk == nullptr) {
          resetClient(client);
          continue;
        }
      }
      if (client.chunkSent == client.chunkSize && client.bodySent < total) {
        client.chunkSize = 0;
        client.chunkSent = 0;
        const size_t remaining = total - client.bodySent;
        const size_t capacity =
            remaining < config.streamChunkBytes ? remaining
                                                : config.streamChunkBytes;
        if (!client.response.source.read(client.response.source.context,
                                         client.chunk, capacity,
                                         client.chunkSize) ||
            client.chunkSize == 0 || client.chunkSize > capacity) {
          resetClient(client);
          continue;
        }
      }
      if (client.chunkSent < client.chunkSize) {
        size_t sent = 0;
        const SocketResult result = networkStack().send(
            client.socket, client.chunk + client.chunkSent,
            client.chunkSize - client.chunkSent, sent);
        if (!result.ok() && !result.wouldBlock()) {
          resetClient(client);
          continue;
        }
        client.chunkSent += sent;
        client.bodySent += sent;
        if (sent > 0) {
          client.deadline = nowMs + config.sendTimeoutMs;
        }
      }
    }
    if (client.bodySent == total) {
      resetClient(client);
    }
  }
}

bool Server::respond(uint32_t requestId, Response &&response) {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (impl == nullptr || requestId == 0) {
    return false;
  }
  SpinGuard guard(impl->lock);
  for (uint8_t i = 0; i < impl->clientCount; i++) {
    Impl::Client &client = impl->clients[i];
    if (client.state != Impl::ClientState::Deferred ||
        client.requestId != requestId) {
      continue;
    }
    router.decorate(client.parser.request(), response);
    response.suppressBody =
        client.parser.request().method == Method::Head;
    client.response = static_cast<Response &&>(response);
    client.deadline = impl->nowMs + config.sendTimeoutMs;
    return prepareHeaders(client);
  }
  return false;
}

bool Server::awaitingResponse(uint32_t requestId) const {
  SpinGuard lifecycleGuard(lifecycleLock);
  if (impl == nullptr || requestId == 0) {
    return false;
  }
  SpinGuard guard(impl->lock);
  for (uint8_t i = 0; i < impl->clientCount; i++) {
    const Impl::Client &client = impl->clients[i];
    if (client.state == Impl::ClientState::Deferred &&
        client.requestId == requestId) {
      return true;
    }
  }
  return false;
}

} // namespace net::http
