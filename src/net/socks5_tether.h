/// Transparent, hotspot-bound SOCKS5 and HTTP(S) proxy tether adapter.
#ifndef POCKETINK_NET_SOCKS5_TETHER_H
#define POCKETINK_NET_SOCKS5_TETHER_H

#include <stdint.h>

namespace net {

struct Socks5TetherImpl;

class Socks5Tether {
public:
  Socks5Tether() = default;
  ~Socks5Tether();

  Socks5Tether(const Socks5Tether &) = delete;
  Socks5Tether &operator=(const Socks5Tether &) = delete;

  bool start();
  void update(uint32_t nowMs);
  void stop();
  bool running() const;

private:
  Socks5TetherImpl *impl = nullptr;
};

} // namespace net

#endif
