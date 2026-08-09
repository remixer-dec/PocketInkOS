/// Hotspot-only built-in web app and bounded device APIs.
#ifndef POCKETINK_NET_WEB_SERVICE_H
#define POCKETINK_NET_WEB_SERVICE_H

#include "net/http/http_server.h"

#include <stdint.h>

namespace net {

class WebService {
public:
  WebService();
  ~WebService();

  bool start(uint16_t port);
  void stop();
  void update(uint32_t nowMs);
  bool running() const;
  uint16_t port() const;
  void setUploadsEnabled(bool enabled);

private:
  struct Message;

  http::Router router;
  http::Server server;
  Message *messages = nullptr;
  uint32_t nextMessageId = 1;
  uint8_t messageStart = 0;
  uint8_t messageCount = 0;
  bool routesConfigured = false;
  bool uploadsEnabled = false;

  static http::ServerConfig serverConfig();
  bool configureRoutes();
  void clearMessages();

  static http::DispatchResult root(const http::RequestContext &request,
                                   http::Response &response, void *context);
  static http::DispatchResult status(const http::RequestContext &request,
                                     http::Response &response, void *context);
  static http::DispatchResult listMessages(
      const http::RequestContext &request, http::Response &response,
      void *context);
  static http::DispatchResult addMessage(
      const http::RequestContext &request, http::Response &response,
      void *context);
  static http::DispatchResult upload(const http::RequestContext &request,
                                     http::Response &response, void *context);
  static http::DispatchResult input(const http::RequestContext &request,
                                    http::Response &response, void *context);
  static bool openWebFile(const char *path, http::StaticFile &out,
                          void *context);
  static bool openShareFile(const char *path, http::StaticFile &out,
                            void *context);
};

} // namespace net

#endif
