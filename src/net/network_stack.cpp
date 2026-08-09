#include "net/network_stack.h"

#include "net/ipv4_address.h"
#include "sys/psram_allocator.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <new>

#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {
namespace {

constexpr uint8_t kSocketCapacity = 24;
constexpr size_t kOwnerCapacity = sizeof(PortInfo::owner);
constexpr uint32_t kMaxGeneration = 0x00ffffffUL;

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

bool copyOwner(char *out, const char *owner) {
  if (out == nullptr || owner == nullptr || owner[0] == '\0') {
    return false;
  }
  const size_t length = strnlen(owner, kOwnerCapacity);
  if (length == 0 || length >= kOwnerCapacity) {
    return false;
  }
  memcpy(out, owner, length + 1U);
  return true;
}

SocketResult socketFailure(SocketError error) {
  return {error, errno};
}

bool retryableError(int value) {
  return value == EAGAIN || value == EWOULDBLOCK || value == EINTR ||
         value == EINPROGRESS || value == EALREADY;
}

bool setNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace

struct NetworkStack::Impl {
  struct Record {
    char owner[kOwnerCapacity] = {};
    int fd = -1;
    uint32_t generation = 1;
    uint16_t port = 0;
    uint8_t users = 0;
    Transport transport = Transport::Tcp;
    bool active = false;
    bool opening = false;
    bool closing = false;
    bool listening = false;
  };

  mutable std::atomic_flag lock = ATOMIC_FLAG_INIT;
  Record records[kSocketCapacity];

  SocketHandle handleFor(uint8_t index) const {
    return (records[index].generation << 8U) |
           static_cast<uint32_t>(index + 1U);
  }

  bool indexFor(SocketHandle handle, uint8_t &index) const {
    const uint32_t low = handle & 0xffU;
    if (low == 0 || low > kSocketCapacity) {
      return false;
    }
    index = static_cast<uint8_t>(low - 1U);
    return records[index].generation == (handle >> 8U);
  }

  int finalizeLocked(uint8_t index) {
    Record &record = records[index];
    if (!record.active || !record.closing || record.users != 0) {
      return -1;
    }
    const int fd = record.fd;
    const uint32_t nextGeneration =
        record.generation == kMaxGeneration
            ? 1U
            : record.generation + 1U;
    record = {};
    record.fd = -1;
    record.generation = nextGeneration;
    return fd;
  }

  bool begin(SocketHandle handle, int &fd) {
    SpinGuard guard(lock);
    uint8_t index = 0;
    if (!indexFor(handle, index)) {
      return false;
    }
    Record &record = records[index];
    if (!record.active || record.opening || record.closing || record.fd < 0 ||
        record.users == UINT8_MAX) {
      return false;
    }
    record.users++;
    fd = record.fd;
    return true;
  }

  void end(SocketHandle handle) {
    int fdToClose = -1;
    {
      SpinGuard guard(lock);
      uint8_t index = 0;
      if (!indexFor(handle, index)) {
        return;
      }
      Record &record = records[index];
      if (record.users > 0) {
        record.users--;
      }
      fdToClose = finalizeLocked(index);
    }
    if (fdToClose >= 0) {
      ::close(fdToClose);
    }
  }
};

NetworkStack &networkStack() {
  static NetworkStack instance;
  return instance;
}

bool NetworkStack::ensureImpl() {
  if (state() != nullptr) {
    return true;
  }
  void *memory = psram::allocate(sizeof(Impl));
  if (memory == nullptr) {
    return false;
  }
  Impl *created = new (memory) Impl();
  Impl *expected = nullptr;
  if (impl.compare_exchange_strong(expected, created,
                                   std::memory_order_release,
                                   std::memory_order_acquire)) {
    return true;
  }
  created->~Impl();
  psram::release(created);
  return true;
}

static SocketResult reserveBoundRecord(NetworkStack::Impl &impl,
                                       const char *owner, uint16_t port,
                                       Transport transport, bool listening,
                                       uint8_t &index, SocketHandle &handle) {
  char checkedOwner[kOwnerCapacity] = {};
  if (port == 0 || !copyOwner(checkedOwner, owner)) {
    return {SocketError::InvalidArgument, 0};
  }
  SpinGuard guard(impl.lock);
  int freeIndex = -1;
  for (uint8_t i = 0; i < kSocketCapacity; i++) {
    const NetworkStack::Impl::Record &record = impl.records[i];
    if (record.active && record.port == port &&
        record.transport == transport) {
      return {SocketError::PortInUse, 0};
    }
    if (!record.active && freeIndex < 0) {
      freeIndex = i;
    }
  }
  if (freeIndex < 0) {
    return {SocketError::NoResources, 0};
  }
  index = static_cast<uint8_t>(freeIndex);
  NetworkStack::Impl::Record &record = impl.records[index];
  memcpy(record.owner, checkedOwner, sizeof(record.owner));
  record.port = port;
  record.transport = transport;
  record.active = true;
  record.opening = true;
  record.listening = listening;
  handle = impl.handleFor(index);
  return {};
}

static void abandonOpening(NetworkStack::Impl &impl, uint8_t index,
                           SocketHandle handle) {
  SpinGuard guard(impl.lock);
  if (impl.handleFor(index) != handle || !impl.records[index].active ||
      !impl.records[index].opening) {
    return;
  }
  NetworkStack::Impl::Record &record = impl.records[index];
  const uint32_t generation = record.generation;
  record = {};
  record.fd = -1;
  record.generation = generation;
}

static SocketResult commitOpening(NetworkStack::Impl &impl, uint8_t index,
                                  SocketHandle handle, int fd) {
  SpinGuard guard(impl.lock);
  NetworkStack::Impl::Record &record = impl.records[index];
  if (impl.handleFor(index) != handle || !record.active || !record.opening ||
      record.closing) {
    return {SocketError::NotFound, 0};
  }
  record.fd = fd;
  record.opening = false;
  return {};
}

SocketResult NetworkStack::openTcpServer(const char *owner, uint16_t port,
                                         uint8_t backlog, SocketHandle &out,
                                         const char *localIpv4) {
  out = kInvalidSocket;
  in_addr bindAddress = {};
  if (localIpv4 != nullptr &&
      !parseIpv4Address(
          localIpv4, reinterpret_cast<uint8_t *>(&bindAddress.s_addr))) {
    return {SocketError::InvalidArgument, 0};
  }
  if (backlog == 0 || !ensureImpl()) {
    return {backlog == 0 ? SocketError::InvalidArgument
                         : SocketError::NoMemory,
            0};
  }
  Impl *current = state();
  uint8_t index = 0;
  SocketHandle handle = kInvalidSocket;
  SocketResult reserved =
      reserveBoundRecord(*current, owner, port, Transport::Tcp, true, index,
                         handle);
  if (!reserved.ok()) {
    return reserved;
  }

  const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    SocketResult failure = socketFailure(SocketError::Io);
    abandonOpening(*current, index, handle);
    return failure;
  }
  const int reuse = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr =
      localIpv4 != nullptr ? bindAddress.s_addr : htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      ::listen(fd, backlog) != 0 || !setNonBlocking(fd)) {
    SocketResult failure = socketFailure(SocketError::Io);
    ::close(fd);
    abandonOpening(*current, index, handle);
    return failure;
  }
  SocketResult committed = commitOpening(*current, index, handle, fd);
  if (!committed.ok()) {
    ::close(fd);
    abandonOpening(*current, index, handle);
    return committed;
  }
  out = handle;
  return {};
}

SocketResult NetworkStack::openUdp(const char *owner, uint16_t port,
                                   SocketHandle &out,
                                   const char *localIpv4) {
  out = kInvalidSocket;
  in_addr bindAddress = {};
  if (localIpv4 != nullptr &&
      !parseIpv4Address(
          localIpv4, reinterpret_cast<uint8_t *>(&bindAddress.s_addr))) {
    return {SocketError::InvalidArgument, 0};
  }
  if (!ensureImpl()) {
    return {SocketError::NoMemory, 0};
  }
  Impl *current = state();
  uint8_t index = 0;
  SocketHandle handle = kInvalidSocket;
  SocketResult reserved =
      reserveBoundRecord(*current, owner, port, Transport::Udp, false, index,
                         handle);
  if (!reserved.ok()) {
    return reserved;
  }
  const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr =
      localIpv4 != nullptr ? bindAddress.s_addr : htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (fd < 0 ||
      ::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      !setNonBlocking(fd)) {
    SocketResult failure = socketFailure(SocketError::Io);
    if (fd >= 0) {
      ::close(fd);
    }
    abandonOpening(*current, index, handle);
    return failure;
  }
  SocketResult committed = commitOpening(*current, index, handle, fd);
  if (!committed.ok()) {
    ::close(fd);
    abandonOpening(*current, index, handle);
    return committed;
  }
  out = handle;
  return {};
}

SocketResult NetworkStack::openTcpClient(const char *owner, const char *ipv4,
                                         uint16_t port, SocketHandle &out) {
  out = kInvalidSocket;
  char checkedOwner[kOwnerCapacity] = {};
  in_addr parsed = {};
  if (port == 0 || ipv4 == nullptr || !copyOwner(checkedOwner, owner) ||
      !parseIpv4Address(ipv4,
                        reinterpret_cast<uint8_t *>(&parsed.s_addr))) {
    return {SocketError::InvalidArgument, 0};
  }
  if (!ensureImpl()) {
    return {SocketError::NoMemory, 0};
  }
  Impl *current = state();
  uint8_t index = 0;
  SocketHandle handle = kInvalidSocket;
  {
    SpinGuard guard(current->lock);
    int freeIndex = -1;
    for (uint8_t i = 0; i < kSocketCapacity; i++) {
      if (!current->records[i].active) {
        freeIndex = i;
        break;
      }
    }
    if (freeIndex < 0) {
      return {SocketError::NoResources, 0};
    }
    index = static_cast<uint8_t>(freeIndex);
    Impl::Record &record = current->records[index];
    memcpy(record.owner, checkedOwner, sizeof(record.owner));
    record.transport = Transport::Tcp;
    record.active = true;
    record.opening = true;
    handle = current->handleFor(index);
  }

  const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr = parsed;
  address.sin_port = htons(port);
  if (fd < 0 || !setNonBlocking(fd)) {
    SocketResult failure = socketFailure(SocketError::Io);
    if (fd >= 0) {
      ::close(fd);
    }
    abandonOpening(*current, index, handle);
    return failure;
  }
  const int connected =
      ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  const int connectError = errno;
  if (connected != 0 && !retryableError(connectError)) {
    ::close(fd);
    abandonOpening(*current, index, handle);
    return {SocketError::Io, connectError};
  }
  SocketResult committed = commitOpening(*current, index, handle, fd);
  if (!committed.ok()) {
    ::close(fd);
    abandonOpening(*current, index, handle);
    return committed;
  }
  out = handle;
  return {};
}

SocketResult NetworkStack::accept(SocketHandle server, SocketHandle &out,
                                  char *remoteIpv4,
                                  size_t remoteIpv4Size,
                                  uint16_t *remotePort) {
  out = kInvalidSocket;
  if (remotePort != nullptr) {
    *remotePort = 0;
  }
  if (remoteIpv4 == nullptr && remoteIpv4Size != 0) {
    return {SocketError::InvalidArgument, 0};
  }
  if (remoteIpv4 != nullptr) {
    if (remoteIpv4Size < INET_ADDRSTRLEN) {
      return {SocketError::InvalidArgument, 0};
    }
    remoteIpv4[0] = '\0';
  }
  Impl *current = state();
  if (current == nullptr) {
    return {SocketError::NotFound, 0};
  }
  int serverFd = -1;
  if (!current->begin(server, serverFd)) {
    return {SocketError::NotFound, 0};
  }
  sockaddr_in address = {};
  socklen_t length = sizeof(address);
  const int clientFd =
      ::accept(serverFd, reinterpret_cast<sockaddr *>(&address), &length);
  const int acceptedError = errno;
  current->end(server);
  if (clientFd < 0) {
    errno = acceptedError;
    return retryableError(acceptedError)
               ? SocketResult{SocketError::WouldBlock, acceptedError}
               : socketFailure(SocketError::Io);
  }
  if (!setNonBlocking(clientFd)) {
    SocketResult failure = socketFailure(SocketError::Io);
    ::close(clientFd);
    return failure;
  }
  if (length < sizeof(sockaddr_in) || address.sin_family != AF_INET) {
    ::close(clientFd);
    return {SocketError::Io, 0};
  }
  if (remoteIpv4 != nullptr &&
      !formatIpv4Address(
          reinterpret_cast<const uint8_t *>(&address.sin_addr.s_addr),
          remoteIpv4, remoteIpv4Size)) {
    ::close(clientFd);
    return {SocketError::Io, errno};
  }
  if (remotePort != nullptr) {
    *remotePort = ntohs(address.sin_port);
  }

  SpinGuard guard(current->lock);
  uint8_t serverIndex = 0;
  if (!current->indexFor(server, serverIndex) ||
      !current->records[serverIndex].active ||
      !current->records[serverIndex].listening ||
      current->records[serverIndex].transport != Transport::Tcp) {
    ::close(clientFd);
    return {SocketError::NotFound, 0};
  }
  int freeIndex = -1;
  for (uint8_t i = 0; i < kSocketCapacity; i++) {
    if (!current->records[i].active) {
      freeIndex = i;
      break;
    }
  }
  if (freeIndex < 0) {
    ::close(clientFd);
    return {SocketError::NoResources, 0};
  }
  Impl::Record &record = current->records[freeIndex];
  memcpy(record.owner, current->records[serverIndex].owner,
         sizeof(record.owner));
  record.fd = clientFd;
  record.transport = Transport::Tcp;
  record.active = true;
  out = current->handleFor(static_cast<uint8_t>(freeIndex));
  return {};
}

SocketResult NetworkStack::receive(SocketHandle socket, uint8_t *out,
                                   size_t size, size_t &received) {
  received = 0;
  Impl *current = state();
  if (out == nullptr || size == 0 || size > static_cast<size_t>(SSIZE_MAX) ||
      current == nullptr) {
    return {SocketError::InvalidArgument, 0};
  }
  int fd = -1;
  if (!current->begin(socket, fd)) {
    return {SocketError::NotFound, 0};
  }
  const ssize_t result = ::recv(fd, out, size, 0);
  const int receivedError = errno;
  current->end(socket);
  if (result < 0) {
    return retryableError(receivedError)
               ? SocketResult{SocketError::WouldBlock, receivedError}
               : SocketResult{SocketError::Io, receivedError};
  }
  received = static_cast<size_t>(result);
  return {};
}

SocketResult NetworkStack::send(SocketHandle socket, const uint8_t *data,
                                size_t size, size_t &sent) {
  sent = 0;
  Impl *current = state();
  if (data == nullptr || size == 0 || size > static_cast<size_t>(SSIZE_MAX) ||
      current == nullptr) {
    return {SocketError::InvalidArgument, 0};
  }
  int fd = -1;
  if (!current->begin(socket, fd)) {
    return {SocketError::NotFound, 0};
  }
#ifdef MSG_NOSIGNAL
  const ssize_t result = ::send(fd, data, size, MSG_NOSIGNAL);
#else
  const ssize_t result = ::send(fd, data, size, 0);
#endif
  const int sentError = errno;
  current->end(socket);
  if (result < 0) {
    return retryableError(sentError)
               ? SocketResult{SocketError::WouldBlock, sentError}
               : SocketResult{SocketError::Io, sentError};
  }
  sent = static_cast<size_t>(result);
  return {};
}

SocketResult NetworkStack::receiveFrom(SocketHandle socket, uint8_t *out,
                                       size_t size, size_t &received,
                                       char *remoteIpv4,
                                       size_t remoteIpv4Size,
                                       uint16_t &remotePort) {
  received = 0;
  remotePort = 0;
  if (out == nullptr || size == 0 || size > static_cast<size_t>(SSIZE_MAX) ||
      remoteIpv4 == nullptr ||
      remoteIpv4Size < INET_ADDRSTRLEN) {
    return {SocketError::InvalidArgument, 0};
  }
  remoteIpv4[0] = '\0';
  Impl *current = state();
  if (current == nullptr) {
    return {SocketError::NotFound, 0};
  }
  int fd = -1;
  if (!current->begin(socket, fd)) {
    return {SocketError::NotFound, 0};
  }
  sockaddr_in remote = {};
  socklen_t remoteSize = sizeof(remote);
  const ssize_t result =
      ::recvfrom(fd, out, size, 0, reinterpret_cast<sockaddr *>(&remote),
                 &remoteSize);
  const int receivedError = errno;
  current->end(socket);
  if (result < 0) {
    return retryableError(receivedError)
               ? SocketResult{SocketError::WouldBlock, receivedError}
               : SocketResult{SocketError::Io, receivedError};
  }
  if (remote.sin_family != AF_INET ||
      !formatIpv4Address(
          reinterpret_cast<const uint8_t *>(&remote.sin_addr.s_addr),
          remoteIpv4, remoteIpv4Size)) {
    return {SocketError::Io, 0};
  }
  received = static_cast<size_t>(result);
  remotePort = ntohs(remote.sin_port);
  return {};
}

SocketResult NetworkStack::sendTo(SocketHandle socket, const char *remoteIpv4,
                                  uint16_t remotePort, const uint8_t *data,
                                  size_t size, size_t &sent) {
  sent = 0;
  in_addr parsed = {};
  if (remoteIpv4 == nullptr || remotePort == 0 ||
      data == nullptr || size == 0 || size > static_cast<size_t>(SSIZE_MAX) ||
      !parseIpv4Address(
          remoteIpv4, reinterpret_cast<uint8_t *>(&parsed.s_addr))) {
    return {SocketError::InvalidArgument, 0};
  }
  Impl *current = state();
  if (current == nullptr) {
    return {SocketError::NotFound, 0};
  }
  int fd = -1;
  if (!current->begin(socket, fd)) {
    return {SocketError::NotFound, 0};
  }
  sockaddr_in remote = {};
  remote.sin_family = AF_INET;
  remote.sin_addr = parsed;
  remote.sin_port = htons(remotePort);
#ifdef MSG_NOSIGNAL
  const int flags = MSG_NOSIGNAL;
#else
  const int flags = 0;
#endif
  const ssize_t result =
      ::sendto(fd, data, size, flags, reinterpret_cast<sockaddr *>(&remote),
               sizeof(remote));
  const int sentError = errno;
  current->end(socket);
  if (result < 0) {
    return retryableError(sentError)
               ? SocketResult{SocketError::WouldBlock, sentError}
               : SocketResult{SocketError::Io, sentError};
  }
  sent = static_cast<size_t>(result);
  return {};
}

void NetworkStack::close(SocketHandle socket) {
  Impl *current = state();
  if (current == nullptr || socket == kInvalidSocket) {
    return;
  }
  int fdToClose = -1;
  {
    SpinGuard guard(current->lock);
    uint8_t index = 0;
    if (!current->indexFor(socket, index) ||
        !current->records[index].active) {
      return;
    }
    current->records[index].closing = true;
    fdToClose = current->finalizeLocked(index);
  }
  if (fdToClose >= 0) {
    ::close(fdToClose);
  }
}

void NetworkStack::closeOwner(const char *owner) {
  Impl *current = state();
  char checkedOwner[kOwnerCapacity] = {};
  if (current == nullptr || !copyOwner(checkedOwner, owner)) {
    return;
  }
  SocketHandle handles[kSocketCapacity] = {};
  size_t count = 0;
  {
    SpinGuard guard(current->lock);
    for (uint8_t i = 0; i < kSocketCapacity; i++) {
      if (current->records[i].active &&
          strcmp(current->records[i].owner, checkedOwner) == 0) {
        handles[count++] = current->handleFor(i);
      }
    }
  }
  for (size_t i = 0; i < count; i++) {
    close(handles[i]);
  }
}

size_t NetworkStack::portCount() const {
  Impl *current = state();
  if (current == nullptr) {
    return 0;
  }
  SpinGuard guard(current->lock);
  size_t count = 0;
  for (const Impl::Record &record : current->records) {
    if (record.active && record.port != 0) {
      count++;
    }
  }
  return count;
}

bool NetworkStack::portAt(size_t index, PortInfo &out) const {
  out = {};
  Impl *current = state();
  if (current == nullptr) {
    return false;
  }
  SpinGuard guard(current->lock);
  size_t at = 0;
  for (const Impl::Record &record : current->records) {
    if (!record.active || record.port == 0) {
      continue;
    }
    if (at++ == index) {
      memcpy(out.owner, record.owner, sizeof(out.owner));
      out.port = record.port;
      out.transport = record.transport;
      out.listening = record.listening;
      return true;
    }
  }
  return false;
}

bool NetworkStack::boundPort(SocketHandle socket, uint16_t &out) const {
  out = 0;
  Impl *current = state();
  if (current == nullptr) {
    return false;
  }
  SpinGuard guard(current->lock);
  uint8_t index = 0;
  if (!current->indexFor(socket, index) ||
      !current->records[index].active) {
    return false;
  }
  out = current->records[index].port;
  return out != 0;
}

} // namespace net
