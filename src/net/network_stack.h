/// Lightweight, process-wide ownership layer for IPv4 sockets and listening
/// ports. Handles include a generation so stale app/server handles cannot act
/// on a descriptor that the OS has since reused.
#ifndef POCKETINK_NET_NETWORK_STACK_H
#define POCKETINK_NET_NETWORK_STACK_H

#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace net {

enum class Transport : uint8_t { Tcp, Udp };

enum class SocketError : int8_t {
  Ok = 0,
  WouldBlock = 1,
  InvalidArgument = -1,
  NoMemory = -2,
  NoResources = -3,
  PortInUse = -4,
  NotFound = -5,
  Io = -6,
};

struct SocketResult {
  SocketError error = SocketError::Ok;
  int systemError = 0;

  bool ok() const { return error == SocketError::Ok; }
  bool wouldBlock() const { return error == SocketError::WouldBlock; }
};

using SocketHandle = uint32_t;
static constexpr SocketHandle kInvalidSocket = 0;

struct PortInfo {
  char owner[24] = {};
  uint16_t port = 0;
  Transport transport = Transport::Tcp;
  bool listening = false;
};

/// Allocates socket records lazily in PSRAM and serializes descriptor lifetime.
class NetworkStack {
public:
  struct Impl;

  /// Opens a non-blocking TCP listener, optionally bound to one local IPv4.
  SocketResult openTcpServer(const char *owner, uint16_t port,
                             uint8_t backlog, SocketHandle &out,
                             const char *localIpv4 = nullptr);
  /// Opens a non-blocking UDP socket, optionally bound to one local IPv4.
  SocketResult openUdp(const char *owner, uint16_t port, SocketHandle &out,
                       const char *localIpv4 = nullptr);
  /// Starts a non-blocking IPv4 TCP connection.
  SocketResult openTcpClient(const char *owner, const char *ipv4,
                             uint16_t port, SocketHandle &out);

  /// Accepts one client from a TCP listener.
  SocketResult accept(SocketHandle server, SocketHandle &out,
                      char *remoteIpv4 = nullptr,
                      size_t remoteIpv4Size = 0,
                      uint16_t *remotePort = nullptr);
  /// Reads or writes at most size bytes. A successful zero-byte read is EOF.
  SocketResult receive(SocketHandle socket, uint8_t *out, size_t size,
                       size_t &received);
  SocketResult send(SocketHandle socket, const uint8_t *data, size_t size,
                    size_t &sent);
  SocketResult receiveFrom(SocketHandle socket, uint8_t *out, size_t size,
                           size_t &received, char *remoteIpv4,
                           size_t remoteIpv4Size, uint16_t &remotePort);
  SocketResult sendTo(SocketHandle socket, const char *remoteIpv4,
                      uint16_t remotePort, const uint8_t *data, size_t size,
                      size_t &sent);

  /// Idempotently closes a handle. In-flight operations finish before the file
  /// descriptor is actually released.
  void close(SocketHandle socket);
  /// Closes every socket owned by the exact owner id.
  void closeOwner(const char *owner);

  /// Returns a point-in-time copy of active bound ports.
  size_t portCount() const;
  bool portAt(size_t index, PortInfo &out) const;

  /// Test/diagnostic helper that reports the port assigned to a bound socket.
  bool boundPort(SocketHandle socket, uint16_t &out) const;

private:
  std::atomic<Impl *> impl{nullptr};

  bool ensureImpl();
  Impl *state() const { return impl.load(std::memory_order_acquire); }
};

/// Shared OS networking subsystem. It owns only a small pointer until used.
NetworkStack &networkStack();

} // namespace net

#endif
