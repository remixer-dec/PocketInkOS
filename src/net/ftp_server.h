/// Small hotspot-only, authenticated, passive FTP server for `/share`.
#ifndef POCKETINK_NET_FTP_SERVER_H
#define POCKETINK_NET_FTP_SERVER_H

#include <stdint.h>

namespace net {

class FtpServer {
public:
  struct Impl;

  FtpServer() = default;
  ~FtpServer();

  bool start(uint16_t controlPort, uint16_t dataPort, const char *username,
             const char *password);
  void stop();
  void update(uint32_t nowMs);
  bool running() const;
  uint16_t port() const;

private:
  Impl *impl = nullptr;
};

} // namespace net

#endif
