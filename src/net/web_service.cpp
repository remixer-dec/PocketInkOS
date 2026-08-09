#include "net/web_service.h"

#include "net/network_interface.h"
#include "net/network_config.h"
#include "net/share_paths.h"
#include "sys/psram_allocator.h"
#include "ui/remote_text_input.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <cstdio>
#include <cstring>
#include <new>

namespace net {
namespace {

constexpr char kApAddress[] = "192.168.4.1";
constexpr uint8_t kHistoryCapacity = 10;
constexpr size_t kMessageCapacity = 321;
constexpr size_t kJsonCapacity = 8192;
constexpr size_t kUploadLimit = 512U * 1024U;

struct StaticFileContext {
  File file;
};

bool streamRead(void *raw, uint8_t *out, size_t capacity, size_t &outRead) {
  outRead = 0;
  StaticFileContext *context = static_cast<StaticFileContext *>(raw);
  if (context == nullptr || (capacity > 0 && out == nullptr)) {
    return false;
  }
  outRead = capacity > 0 ? context->file.read(out, capacity) : 0;
  return outRead == capacity;
}

void streamClose(void *raw) {
  StaticFileContext *context = static_cast<StaticFileContext *>(raw);
  if (context == nullptr) {
    return;
  }
  context->file.close();
  context->~StaticFileContext();
  psram::release(context);
}

bool openFile(const char *path, http::StaticFile &out) {
  File file = SD_MMC.open(path);
  if (!file || file.isDirectory()) {
    file.close();
    return false;
  }
  void *memory = psram::allocate(sizeof(StaticFileContext));
  if (memory == nullptr) {
    file.close();
    return false;
  }
  StaticFileContext *context = new (memory) StaticFileContext{file};
  out.body.context = context;
  out.body.length = file.size();
  out.body.read = streamRead;
  out.body.close = streamClose;
  return true;
}

void error(http::Response &response, const char *message, int status) {
  if (!response.text(message, status)) {
    response.noContent(status);
  }
}

void noStore(http::Response &response) {
  response.setHeader("Cache-Control", "no-store");
}

bool validApiPost(const http::Request &request) {
  const char *marker = request.header("x-pocketink");
  return marker != nullptr && strcmp(marker, "1") == 0;
}

bool validUtf8Text(const uint8_t *body, size_t length, size_t maximum) {
  if (body == nullptr || length == 0 || length > maximum) {
    return false;
  }
  // Reject controls and malformed UTF-8. Newlines and tabs remain useful in
  // chat while NUL is never admitted into the fixed message record.
  size_t i = 0;
  while (i < length) {
    const uint8_t c = body[i++];
    if (c == 0 || c == 0x7f || (c < 0x20 && c != '\n' && c != '\t')) {
      return false;
    }
    if (c < 0x80) {
      continue;
    }
    uint8_t extra = 0;
    uint32_t codepoint = 0;
    if ((c & 0xe0U) == 0xc0U) {
      extra = 1;
      codepoint = c & 0x1fU;
    } else if ((c & 0xf0U) == 0xe0U) {
      extra = 2;
      codepoint = c & 0x0fU;
    } else if ((c & 0xf8U) == 0xf0U) {
      extra = 3;
      codepoint = c & 0x07U;
    } else {
      return false;
    }
    if (i + extra > length) {
      return false;
    }
    for (uint8_t j = 0; j < extra; j++) {
      const uint8_t continuation = body[i++];
      if ((continuation & 0xc0U) != 0x80U) {
        return false;
      }
      codepoint = (codepoint << 6U) | (continuation & 0x3fU);
    }
    if ((extra == 1 && codepoint < 0x80U) ||
        (extra == 2 && codepoint < 0x800U) ||
        (extra == 3 && codepoint < 0x10000U) ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU) ||
        codepoint > 0x10ffffU) {
      return false;
    }
  }
  return true;
}

bool appendJson(char *out, size_t capacity, size_t &used, const char *text) {
  if (out == nullptr || text == nullptr || capacity == 0 || used >= capacity) {
    return false;
  }
  for (const unsigned char *at =
           reinterpret_cast<const unsigned char *>(text);
       *at != 0; at++) {
    const char *escaped = nullptr;
    char unicode[7] = {};
    char plain[2] = {static_cast<char>(*at), '\0'};
    switch (*at) {
    case '"': escaped = "\\\""; break;
    case '\\': escaped = "\\\\"; break;
    case '\n': escaped = "\\n"; break;
    case '\r': escaped = "\\r"; break;
    case '\t': escaped = "\\t"; break;
    default:
      if (*at < 0x20U) {
        snprintf(unicode, sizeof(unicode), "\\u%04x",
                 static_cast<unsigned>(*at));
        escaped = unicode;
      } else {
        escaped = plain;
      }
    }
    const size_t amount = strlen(escaped);
    if (amount >= capacity - used) {
      return false;
    }
    memcpy(out + used, escaped, amount);
    used += amount;
  }
  out[used] = '\0';
  return true;
}

bool appendText(char *out, size_t capacity, size_t &used, const char *text) {
  if (out == nullptr || text == nullptr || capacity == 0 || used >= capacity) {
    return false;
  }
  const size_t amount = strlen(text);
  if (amount >= capacity - used) {
    return false;
  }
  memcpy(out + used, text, amount);
  used += amount;
  out[used] = '\0';
  return true;
}

const char *baseName(const char *path) {
  const char *slash = path != nullptr ? strrchr(path, '/') : nullptr;
  return slash != nullptr ? slash + 1 : path;
}

bool validServerCredential(const char *text, size_t minimum, size_t maximum,
                           bool rejectColon) {
  if (text == nullptr) {
    return false;
  }
  const size_t length = strnlen(text, maximum + 1U);
  if (length < minimum || length > maximum) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const unsigned char byte = static_cast<unsigned char>(text[i]);
    if (byte < 0x20U || byte == 0x7fU ||
        (rejectColon && byte == ':')) {
      return false;
    }
  }
  return true;
}

} // namespace

struct WebService::Message {
  char peer[16] = {};
  char kind[6] = "text";
  char text[kMessageCapacity] = {};
  uint32_t id = 0;
};

http::ServerConfig WebService::serverConfig() {
  http::ServerConfig config;
  config.parser.bodyBytes = kUploadLimit;
  config.maxConnections = 2;
  config.streamChunkBytes = 2048;
  return config;
}

WebService::WebService() : server(router, serverConfig()) {}
WebService::~WebService() { stop(); }

bool WebService::configureRoutes() {
  if (routesConfigured) {
    return true;
  }
  const bool ok =
      router.get("/", root, this) &&
      router.get("/api/status", status, this) &&
      router.get("/api/messages", listMessages, this) &&
      router.post("/api/messages", addMessage, this) &&
      router.post("/api/upload", upload, this) &&
      router.post("/api/input", input, this) &&
      router.staticFiles("/web", "/web", openWebFile, this) &&
      router.staticFiles("/files", kShareRoot, openShareFile, this);
  routesConfigured = ok;
  return ok;
}

bool WebService::start(uint16_t requestedPort) {
  if (server.running()) {
    return server.port() == requestedPort;
  }
  char username[33] = {};
  char password[65] = {};
  configValue("network.server.username", "pocketink", username,
              sizeof(username));
  configValue("network.server.password", "", password, sizeof(password));
  const bool passwordSet = password[0] != '\0';
  const bool authenticationReady =
      !passwordSet
          ? router.basicAuth("", "", "PocketInk")
          : validServerCredential(username, 1U, 32U, true) &&
                validServerCredential(password, 8U, 64U, false) &&
                router.basicAuth(username, password, "PocketInk");
  memset(password, 0, sizeof(password));
  if (!tetheringActive() || requestedPort == 0 || !authenticationReady ||
      !configureRoutes() || !ensureShareRoot()) {
    return false;
  }
  void *memory = psram::allocate(sizeof(Message) * kHistoryCapacity);
  if (memory == nullptr) {
    return false;
  }
  messages = static_cast<Message *>(memory);
  for (uint8_t i = 0; i < kHistoryCapacity; i++) {
    new (&messages[i]) Message();
  }
  if (!server.start("hotspot-http", requestedPort, kApAddress)) {
    clearMessages();
    return false;
  }
  return true;
}

void WebService::clearMessages() {
  if (messages != nullptr) {
    for (uint8_t i = 0; i < kHistoryCapacity; i++) {
      messages[i].~Message();
    }
    psram::release(messages);
  }
  messages = nullptr;
  messageStart = 0;
  messageCount = 0;
  nextMessageId = 1;
}

void WebService::stop() {
  server.stop();
  clearMessages();
  uploadsEnabled = false;
}

void WebService::update(uint32_t nowMs) { server.update(nowMs); }
bool WebService::running() const { return server.running(); }
uint16_t WebService::port() const { return server.port(); }
void WebService::setUploadsEnabled(bool enabled) { uploadsEnabled = enabled; }

http::DispatchResult WebService::root(const http::RequestContext &,
                                      http::Response &response, void *) {
  http::StaticFile file;
  if (openFile("/web/index.html", file)) {
    if (response.stream(file.body, "text/html; charset=utf-8")) {
      response.setHeader("Content-Security-Policy",
                         "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
      return http::DispatchResult::Responded;
    }
    file.body.close(file.body.context);
  }
  static constexpr char fallback[] =
      "<!doctype html><meta name=viewport content='width=device-width'>"
      "<title>PocketInk</title><h1>PocketInk</h1><p>Copy the bundled web "
      "folder to <code>/web</code> on the SD card.</p>";
  response.text(fallback, 200, "text/html; charset=utf-8");
  response.setHeader("Content-Security-Policy",
                     "default-src 'none'; style-src 'unsafe-inline'");
  return http::DispatchResult::Responded;
}

http::DispatchResult WebService::status(const http::RequestContext &,
                                        http::Response &response,
                                        void *raw) {
  noStore(response);
  WebService *self = static_cast<WebService *>(raw);
  char json[160] = {};
  snprintf(json, sizeof(json),
           "{\"http\":true,\"ftp\":%s,\"input\":%s,\"inputCapacity\":%lu}",
           self->uploadsEnabled ? "true" : "false",
           remoteTextInputActive() ? "true" : "false",
           static_cast<unsigned long>(remoteTextInputCapacity()));
  response.json(json);
  return http::DispatchResult::Responded;
}

http::DispatchResult WebService::listMessages(
    const http::RequestContext &, http::Response &response, void *raw) {
  noStore(response);
  WebService *self = static_cast<WebService *>(raw);
  char *json = static_cast<char *>(psram::allocate(kJsonCapacity));
  if (json == nullptr) {
    error(response, "No memory\n", 503);
    return http::DispatchResult::Responded;
  }
  size_t used = 0;
  if (self->messages == nullptr && self->messageCount != 0) {
    error(response, "Service unavailable\n", 503);
    psram::release(json);
    return http::DispatchResult::Responded;
  }
  bool ok = appendText(json, kJsonCapacity, used, "{\"messages\":[");
  for (uint8_t i = 0; ok && i < self->messageCount; i++) {
    const Message &message =
        self->messages[(self->messageStart + i) % kHistoryCapacity];
    char prefix[96] = {};
    snprintf(prefix, sizeof(prefix),
             "%s{\"id\":%lu,\"uid\":\"", i > 0 ? "," : "",
             static_cast<unsigned long>(message.id));
    ok = appendText(json, kJsonCapacity, used, prefix) &&
         appendJson(json, kJsonCapacity, used, message.peer) &&
         appendText(json, kJsonCapacity, used, "\",\"kind\":\"") &&
         appendJson(json, kJsonCapacity, used, message.kind) &&
         appendText(json, kJsonCapacity, used, "\",\"text\":\"") &&
         appendJson(json, kJsonCapacity, used, message.text) &&
         appendText(json, kJsonCapacity, used, "\"}");
  }
  ok = ok && appendText(json, kJsonCapacity, used, "]}");
  if (!ok || !response.json(json)) {
    error(response, "Response too large\n", 503);
  }
  psram::release(json);
  return http::DispatchResult::Responded;
}

http::DispatchResult WebService::addMessage(
    const http::RequestContext &context, http::Response &response, void *raw) {
  WebService *self = static_cast<WebService *>(raw);
  noStore(response);
  const http::Request &request = context.request;
  if (!validApiPost(request)) {
    error(response, "Missing API marker\n", 403);
    return http::DispatchResult::Responded;
  }
  const char *kind = request.header("x-message-kind");
  if (kind == nullptr) {
    kind = "text";
  }
  const bool textKind = strcmp(kind, "text") == 0;
  const bool fileKind = strcmp(kind, "file") == 0;
  const bool photoKind = strcmp(kind, "photo") == 0;
  if ((!textKind && !fileKind && !photoKind) ||
      !validUtf8Text(request.body, request.bodySize,
                     textKind ? kMessageCapacity - 1U
                              : kShareNameCapacity - 1U)) {
    error(response, "Invalid message\n", 400);
    return http::DispatchResult::Responded;
  }
  char value[kMessageCapacity] = {};
  memcpy(value, request.body, request.bodySize);
  if (!textKind) {
    char path[96] = {};
    File shared;
    if (sharePath(value, path, sizeof(path))) {
      shared = SD_MMC.open(path);
    }
    const bool regular = shared && !shared.isDirectory();
    shared.close();
    if (!regular) {
      error(response, "Unknown shared file\n", 404);
      return http::DispatchResult::Responded;
    }
  }
  if (self->messages == nullptr) {
    error(response, "Service unavailable\n", 503);
    return http::DispatchResult::Responded;
  }
  uint8_t index = 0;
  if (self->messageCount < kHistoryCapacity) {
    index = (self->messageStart + self->messageCount) % kHistoryCapacity;
    self->messageCount++;
  } else {
    index = self->messageStart;
    self->messageStart = (self->messageStart + 1U) % kHistoryCapacity;
  }
  Message &message = self->messages[index];
  message = {};
  strncpy(message.peer, context.peerIpv4 != nullptr ? context.peerIpv4 : "?",
          sizeof(message.peer) - 1U);
  strncpy(message.kind, kind, sizeof(message.kind) - 1U);
  memcpy(message.text, value, request.bodySize + 1U);
  message.id = self->nextMessageId++;
  if (self->nextMessageId == 0) {
    self->nextMessageId = 1;
  }
  char json[48] = {};
  snprintf(json, sizeof(json), "{\"id\":%lu}",
           static_cast<unsigned long>(message.id));
  response.json(json, 201);
  return http::DispatchResult::Responded;
}

http::DispatchResult WebService::upload(const http::RequestContext &context,
                                        http::Response &response, void *raw) {
  WebService *self = static_cast<WebService *>(raw);
  noStore(response);
  const http::Request &request = context.request;
  if (!validApiPost(request)) {
    error(response, "Missing API marker\n", 403);
    return http::DispatchResult::Responded;
  }
  const char *name = request.header("x-file-name");
  char path[96] = {};
  char temporary[96] = {};
  if (!self->uploadsEnabled) {
    error(response, "FTP and HTTP must both be active\n", 409);
  } else if (!sharePath(name, path, sizeof(path)) || request.body == nullptr ||
             request.bodySize == 0 || request.bodySize > kUploadLimit) {
    error(response, "Invalid upload\n", 400);
  } else if (!ensureShareRoot()) {
    error(response, "SD unavailable\n", 503);
  } else if (SD_MMC.exists(path)) {
    error(response, "File exists\n", 409);
  } else if (!temporaryShareUploadPath(ShareUploadKind::Http, temporary,
                                       sizeof(temporary))) {
    error(response, "Cannot reserve upload\n", 503);
  } else {
    File file = SD_MMC.open(temporary, FILE_WRITE);
    const size_t written = file ? file.write(request.body, request.bodySize) : 0;
    if (file) {
      file.flush();
    }
    file.close();
    File staged = SD_MMC.open(temporary);
    const bool complete = staged && !staged.isDirectory() &&
                          staged.size() == request.bodySize;
    staged.close();
    const bool committed = written == request.bodySize &&
                           complete &&
                           !SD_MMC.exists(path) &&
                           SD_MMC.rename(temporary, path);
    if (!committed) {
      SD_MMC.remove(temporary);
      error(response, "Write failed\n", 507);
    } else {
      char json[128] = {};
      snprintf(json, sizeof(json), "{\"name\":\"%s\",\"url\":\"/files/%s\"}",
               name, name);
      response.json(json, 201);
    }
  }
  return http::DispatchResult::Responded;
}

http::DispatchResult WebService::input(const http::RequestContext &context,
                                       http::Response &response, void *) {
  noStore(response);
  const http::Request &request = context.request;
  if (!validApiPost(request)) {
    error(response, "Missing API marker\n", 403);
    return http::DispatchResult::Responded;
  }
  if (request.body == nullptr || request.bodySize == 0 ||
      !validUtf8Text(request.body, request.bodySize,
                     remoteTextInputCapacity()) ||
      !remoteTextInputReplace(reinterpret_cast<const char *>(request.body),
                              request.bodySize)) {
    error(response, remoteTextInputActive() ? "Invalid input\n"
                                            : "No text popup active\n",
          remoteTextInputActive() ? 400 : 409);
  } else {
    response.noContent();
  }
  return http::DispatchResult::Responded;
}

bool WebService::openWebFile(const char *path, http::StaticFile &out, void *) {
  return path != nullptr && strncmp(path, "/web/", 5) == 0 && openFile(path, out);
}

bool WebService::openShareFile(const char *path, http::StaticFile &out,
                               void *) {
  if (path == nullptr || strncmp(path, "/share/", 7) != 0) {
    return false;
  }
  const char *name = path + 7U;
  if (strchr(name, '/') != nullptr || !validShareName(name) ||
      baseName(path) != name) {
    return false;
  }
  return openFile(path, out);
}

} // namespace net
