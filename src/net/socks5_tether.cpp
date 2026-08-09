#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#include "net/socks5_tether.h"

#include "net/http_connect_protocol.h"
#include "net/ipv4_address.h"
#include "net/network_config.h"
#include "net/network_interface.h"
#include "net/network_stack.h"
#include "net/socks5_protocol.h"
#include "net/transparent_proxy_packet.h"
#include "sys/psram_allocator.h"

#if ENABLE_NETWORK_APPS
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/err.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#endif

#include <atomic>
#include <cstring>
#include <netdb.h>
#include <new>

namespace net {
namespace {

constexpr char kOwner[] = "proxy-tether";
constexpr char kApAddress[] = "192.168.4.1";
constexpr uint8_t kApAddressBytes[4] = {192, 168, 4, 1};
constexpr uint16_t kTcpRedirectPort = 61080;
constexpr uint16_t kUdpRedirectFirstPort = 61081;
constexpr uint16_t kUdpUpstreamPort = 61085;
constexpr uint8_t kTcpFlowCount = 2;
// One UDP association preserves an unambiguous client/remote mapping. SOCKS5
// UDP replies contain no client identifier, so sharing one association across
// hotspot clients would risk cross-delivery.
constexpr uint8_t kUdpFlowCount = 1;
constexpr size_t kRelayBufferSize = 1436;
constexpr size_t kUdpPayloadSize = 1400;
constexpr uint32_t kHandshakeTimeoutMs = 15000;
constexpr uint32_t kTlsHandshakeTimeoutMs = 5000;
#if ENABLE_NETWORK_APPS
constexpr uint32_t kTlsWorkerStackBytes = 8192;
constexpr UBaseType_t kTlsWorkerPriority = 1;
constexpr BaseType_t kTlsWorkerCore = 0;
#endif
constexpr uint32_t kTcpIdleMs = 300000;
constexpr uint32_t kTcpClosedMs = 30000;
constexpr uint32_t kUdpIdleMs = 60000;
constexpr uint32_t kUdpRetryMs = 5000;
constexpr size_t kMaximumCertificateBundleSize = 1024U * 1024U;

bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

bool sameAddress(const uint8_t left[4], const uint8_t right[4]) {
  return memcmp(left, right, 4) == 0;
}

bool apClientAddress(const uint8_t address[4]) {
  return address[0] == 192 && address[1] == 168 && address[2] == 4 &&
         address[3] >= 2 && address[3] < 255;
}

bool localOrSpecialDestination(const uint8_t address[4]) {
  return (address[0] == 192 && address[1] == 168 && address[2] == 4) ||
         address[0] == 0 || address[0] >= 224 ||
         (address[0] == 255 && address[1] == 255 && address[2] == 255 &&
          address[3] == 255);
}

bool safeProxyAddress(const uint8_t address[4]) {
  return address[0] != 0 && address[0] != 127 && address[0] < 224 &&
         !(address[0] == 169 && address[1] == 254) &&
         !(address[0] == 192 && address[1] == 168 && address[2] == 4) &&
         !(address[0] == 255 && address[1] == 255 && address[2] == 255 &&
           address[3] == 255);
}

bool parseAddress(const char *text, uint8_t out[4]) {
  return parseIpv4Address(text, out);
}

bool printAddress(const uint8_t address[4], char out[16]) {
  return formatIpv4Address(address, out, 16U);
}

bool resolveProxy(const char *host, uint8_t out[4], char printable[16]) {
  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *result = nullptr;
  if (host == nullptr || getaddrinfo(host, nullptr, &hints, &result) != 0 ||
      result == nullptr) {
    return false;
  }
  bool resolved = false;
  for (addrinfo *item = result; item != nullptr; item = item->ai_next) {
    if (item->ai_family != AF_INET || item->ai_addr == nullptr ||
        item->ai_addrlen < sizeof(sockaddr_in)) {
      continue;
    }
    const auto *address = reinterpret_cast<const sockaddr_in *>(item->ai_addr);
    memcpy(out, &address->sin_addr.s_addr, 4);
    if (safeProxyAddress(out) && printAddress(out, printable)) {
      resolved = true;
      break;
    }
  }
  freeaddrinfo(result);
  return resolved;
}

class FlowGuard {
public:
  explicit FlowGuard(std::atomic_flag &lock) : lock(lock) {
    while (lock.test_and_set(std::memory_order_acquire)) {
    }
  }
  ~FlowGuard() { lock.clear(std::memory_order_release); }

private:
  std::atomic_flag &lock;
};

enum class NegotiationState : uint8_t { Progress, Ready, Failed };

struct ConnectionIo {
  void *context = nullptr;
  SocketResult (*send)(void *, const uint8_t *, size_t, size_t &) = nullptr;
  SocketResult (*receive)(void *, uint8_t *, size_t, size_t &) = nullptr;
};

class Negotiation {
public:
  void begin(socks5::Command requestedCommand,
             const socks5::Ipv4Endpoint &requestedTarget,
             bool usePasswordAuth, uint32_t now) {
    clear();
    command = requestedCommand;
    target = requestedTarget;
    passwordAuth = usePasswordAuth;
    deadline = now + kHandshakeTimeoutMs;
    txSize = socks5::buildGreeting(passwordAuth, tx, sizeof(tx));
    stage = txSize != 0 ? Stage::SendGreeting : Stage::Failed;
  }

  NegotiationState update(SocketHandle socket, const char *username,
                          const char *password, uint32_t now) {
    if (stage == Stage::Ready) {
      return NegotiationState::Ready;
    }
    if (stage == Stage::Failed || due(now, deadline)) {
      stage = Stage::Failed;
      return NegotiationState::Failed;
    }
    if (sending()) {
      size_t sent = 0;
      const SocketResult result = networkStack().send(
          socket, tx + txOffset, txSize - txOffset, sent);
      if (!result.ok()) {
        return result.wouldBlock() ? NegotiationState::Progress
                                   : fail();
      }
      if (sent == 0 || sent > txSize - txOffset) {
        return fail();
      }
      txOffset += sent;
      if (txOffset < txSize) {
        return NegotiationState::Progress;
      }
      memset(tx, 0, sizeof(tx));
      txOffset = 0;
      txSize = 0;
      switch (stage) {
      case Stage::SendGreeting:
        stage = Stage::ReadGreeting;
        break;
      case Stage::SendAuth:
        stage = Stage::ReadAuth;
        break;
      case Stage::SendCommand:
        stage = Stage::ReadCommand;
        break;
      default:
        return fail();
      }
      return NegotiationState::Progress;
    }

    size_t capacity = sizeof(rx) - rxSize;
    if (stage == Stage::ReadGreeting || stage == Stage::ReadAuth) {
      capacity = rxSize < 2 ? 2U - rxSize : 0;
    }
    if (capacity == 0) {
      return fail();
    }
    size_t received = 0;
    const SocketResult result =
        networkStack().receive(socket, rx + rxSize, capacity, received);
    if (!result.ok()) {
      return result.wouldBlock() ? NegotiationState::Progress : fail();
    }
    if (received == 0 || received > capacity) {
      return fail();
    }
    rxSize += received;
    socks5::ParseState parsed = socks5::ParseState::Incomplete;
    if (stage == Stage::ReadGreeting) {
      parsed = socks5::parseMethodReply(rx, rxSize, passwordAuth);
      if (parsed == socks5::ParseState::Accepted) {
        rxSize = 0;
        if (passwordAuth) {
          txSize = socks5::buildPasswordAuth(username, password, tx,
                                              sizeof(tx));
          stage = txSize != 0 ? Stage::SendAuth : Stage::Failed;
        } else {
          buildCommand();
        }
      }
    } else if (stage == Stage::ReadAuth) {
      parsed = socks5::parsePasswordReply(rx, rxSize);
      if (parsed == socks5::ParseState::Accepted) {
        rxSize = 0;
        buildCommand();
      }
    } else if (stage == Stage::ReadCommand) {
      size_t consumed = 0;
      parsed = socks5::parseCommandReply(rx, rxSize, bound, consumed);
      if (parsed == socks5::ParseState::Accepted) {
        if (consumed > rxSize) {
          return fail();
        }
        tunnelOffset = consumed;
        stage = Stage::Ready;
        return NegotiationState::Ready;
      }
    }
    if (parsed == socks5::ParseState::Rejected ||
        parsed == socks5::ParseState::Malformed || stage == Stage::Failed) {
      return fail();
    }
    return NegotiationState::Progress;
  }

  const socks5::Ipv4Endpoint &boundEndpoint() const { return bound; }

  bool takeTunnelBytes(uint8_t *out, size_t capacity, size_t &outSize) {
    outSize = 0;
    if (stage != Stage::Ready || tunnelOffset > rxSize) {
      return false;
    }
    const size_t available = rxSize - tunnelOffset;
    if (available > capacity || (available > 0 && out == nullptr)) {
      return false;
    }
    if (available > 0) {
      memcpy(out, rx + tunnelOffset, available);
    }
    outSize = available;
    memset(rx, 0, sizeof(rx));
    rxSize = 0;
    tunnelOffset = 0;
    return true;
  }

  void clear() {
    *this = Negotiation{};
  }

private:
  enum class Stage : uint8_t {
    Idle,
    SendGreeting,
    ReadGreeting,
    SendAuth,
    ReadAuth,
    SendCommand,
    ReadCommand,
    Ready,
    Failed,
  };

  bool sending() const {
    return stage == Stage::SendGreeting || stage == Stage::SendAuth ||
           stage == Stage::SendCommand;
  }

  void buildCommand() {
    memset(tx, 0, sizeof(tx));
    txOffset = 0;
    txSize = socks5::buildIpv4Request(command, target, tx, sizeof(tx));
    stage = txSize != 0 ? Stage::SendCommand : Stage::Failed;
  }

  NegotiationState fail() {
    stage = Stage::Failed;
    return NegotiationState::Failed;
  }

  Stage stage = Stage::Idle;
  socks5::Command command = socks5::Command::Connect;
  socks5::Ipv4Endpoint target;
  socks5::Ipv4Endpoint bound;
  uint8_t tx[260] = {};
  uint8_t rx[262] = {};
  size_t txOffset = 0;
  size_t txSize = 0;
  size_t rxSize = 0;
  size_t tunnelOffset = 0;
  uint32_t deadline = 0;
  bool passwordAuth = false;
};

class HttpNegotiation {
public:
  void begin(const socks5::Ipv4Endpoint &target, const char *username,
             const char *password, uint32_t now) {
    clear();
    deadline = now + kHandshakeTimeoutMs;
    txSize = http_connect::buildIpv4Request(
        target.address, target.port, username, password, tx, sizeof(tx));
    stage = txSize != 0 ? Stage::SendRequest : Stage::Failed;
  }

  NegotiationState update(const ConnectionIo &io, uint32_t now) {
    if (stage == Stage::Ready) {
      return NegotiationState::Ready;
    }
    if (stage == Stage::Failed || due(now, deadline) || io.send == nullptr ||
        io.receive == nullptr) {
      return fail();
    }
    if (stage == Stage::SendRequest) {
      size_t sent = 0;
      const SocketResult result = io.send(
          io.context, tx + txOffset, txSize - txOffset, sent);
      if (!result.ok()) {
        return result.wouldBlock() ? NegotiationState::Progress : fail();
      }
      if (sent == 0 || sent > txSize - txOffset) {
        return fail();
      }
      txOffset += sent;
      if (txOffset < txSize) {
        return NegotiationState::Progress;
      }
      memset(tx, 0, sizeof(tx));
      txOffset = 0;
      txSize = 0;
      stage = Stage::ReadResponse;
      return NegotiationState::Progress;
    }

    if (rxSize == sizeof(rx)) {
      return fail();
    }
    size_t received = 0;
    const SocketResult result = io.receive(
        io.context, rx + rxSize, sizeof(rx) - rxSize, received);
    if (!result.ok()) {
      return result.wouldBlock() ? NegotiationState::Progress : fail();
    }
    if (received == 0 || received > sizeof(rx) - rxSize) {
      return fail();
    }
    rxSize += received;
    size_t consumed = 0;
    const http_connect::ParseState parsed =
        http_connect::parseResponse(rx, rxSize, consumed);
    if (parsed == http_connect::ParseState::Incomplete) {
      return NegotiationState::Progress;
    }
    if (parsed != http_connect::ParseState::Accepted || consumed > rxSize) {
      return fail();
    }
    tunnelOffset = consumed;
    stage = Stage::Ready;
    return NegotiationState::Ready;
  }

  bool takeTunnelBytes(uint8_t *out, size_t capacity, size_t &outSize) {
    outSize = 0;
    if (stage != Stage::Ready || tunnelOffset > rxSize) {
      return false;
    }
    const size_t available = rxSize - tunnelOffset;
    if (available > capacity || (available > 0 && out == nullptr)) {
      return false;
    }
    if (available != 0) {
      memcpy(out, rx + tunnelOffset, available);
    }
    outSize = available;
    memset(rx, 0, sizeof(rx));
    rxSize = 0;
    tunnelOffset = 0;
    return true;
  }

  void clear() {
    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    stage = Stage::Idle;
    txOffset = 0;
    txSize = 0;
    rxSize = 0;
    tunnelOffset = 0;
    deadline = 0;
  }

private:
  enum class Stage : uint8_t { Idle, SendRequest, ReadResponse, Ready, Failed };

  NegotiationState fail() {
    memset(tx, 0, sizeof(tx));
    stage = Stage::Failed;
    return NegotiationState::Failed;
  }

  Stage stage = Stage::Idle;
  uint8_t tx[512] = {};
  uint8_t rx[http_connect::kMaximumResponseHeaderSize] = {};
  size_t txOffset = 0;
  size_t txSize = 0;
  size_t rxSize = 0;
  size_t tunnelOffset = 0;
  uint32_t deadline = 0;
};

struct Flow {
  uint8_t clientAddress[4] = {};
  uint8_t remoteAddress[4] = {};
  uint16_t clientPort = 0;
  uint16_t remotePort = 0;
  uint16_t redirectPort = 0;
  uint32_t lastAt = 0;
  bool used = false;
  bool accepted = false;
  bool closed = false;
};

struct RelayBuffer {
  uint8_t data[kRelayBufferSize] = {};
  size_t offset = 0;
  size_t size = 0;

  void clear() {
    offset = 0;
    size = 0;
  }
};

struct TcpSession {
  SocketHandle client = kInvalidSocket;
  SocketHandle proxy = kInvalidSocket;
  socks5::Ipv4Endpoint target;
  Negotiation negotiation;
  HttpNegotiation httpNegotiation;
  RelayBuffer toProxy;
  RelayBuffer toClient;
  uint8_t clientAddress[4] = {};
  uint16_t clientPort = 0;
  uint32_t lastAt = 0;
  bool used = false;
#if ENABLE_NETWORK_APPS
  WiFiClientSecure *secureProxy = nullptr;
  bool tlsPending = false;
  bool tlsFailed = false;
#endif
};

SocketResult rawSend(void *context, const uint8_t *data, size_t size,
                     size_t &sent) {
  if (context == nullptr) {
    sent = 0;
    return {SocketError::InvalidArgument, 0};
  }
  return networkStack().send(*static_cast<SocketHandle *>(context), data,
                             size, sent);
}

SocketResult rawReceive(void *context, uint8_t *out, size_t size,
                        size_t &received) {
  if (context == nullptr) {
    received = 0;
    return {SocketError::InvalidArgument, 0};
  }
  return networkStack().receive(*static_cast<SocketHandle *>(context), out,
                                size, received);
}

#if ENABLE_NETWORK_APPS
extern const uint8_t x509BundleStart[]
    asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509BundleEnd[] asm("_binary_x509_crt_bundle_end");

SocketResult secureSend(void *context, const uint8_t *data, size_t size,
                        size_t &sent) {
  sent = 0;
  auto *client = static_cast<WiFiClientSecure *>(context);
  if (client == nullptr || data == nullptr || size == 0 ||
      !client->connected()) {
    return {SocketError::Io, 0};
  }
  sent = client->write(data, size);
  return sent > 0 && sent <= size ? SocketResult{}
                                  : SocketResult{SocketError::Io, 0};
}

SocketResult secureReceive(void *context, uint8_t *out, size_t size,
                           size_t &received) {
  received = 0;
  auto *client = static_cast<WiFiClientSecure *>(context);
  if (client == nullptr || out == nullptr || size == 0) {
    return {SocketError::InvalidArgument, 0};
  }
  const int available = client->available();
  if (available <= 0) {
    return client->connected() ? SocketResult{SocketError::WouldBlock, 0}
                               : SocketResult{};
  }
  const size_t amount = static_cast<size_t>(available) < size
                            ? static_cast<size_t>(available)
                            : size;
  const int result = client->read(out, amount);
  if (result <= 0 || static_cast<size_t>(result) > amount) {
    return {SocketError::Io, 0};
  }
  received = static_cast<size_t>(result);
  return {};
}

bool certificateBundleSize(size_t &size) {
  size = 0;
  const uintptr_t start = reinterpret_cast<uintptr_t>(x509BundleStart);
  const uintptr_t end = reinterpret_cast<uintptr_t>(x509BundleEnd);
  if (end <= start || end - start > kMaximumCertificateBundleSize) {
    return false;
  }
  size = static_cast<size_t>(end - start);
  return true;
}

struct TlsConnectJob {
  TcpSession *session = nullptr;
  WiFiClientSecure *client = nullptr;
  char host[96] = {};
  uint8_t address[4] = {};
  uint16_t port = 0;
  bool allowUntrusted = false;
  bool succeeded = false;
};

void releaseTlsJob(TlsConnectJob *job) {
  if (job == nullptr) {
    return;
  }
  if (job->client != nullptr) {
    job->client->stop();
    job->client->~WiFiClientSecure();
    psram::release(job->client);
  }
  memset(job->host, 0, sizeof(job->host));
  memset(job->address, 0, sizeof(job->address));
  job->session = nullptr;
  job->client = nullptr;
  job->~TlsConnectJob();
  psram::release(job);
}

struct TlsWorker {
  std::atomic<TlsConnectJob *> request{nullptr};
  std::atomic<TlsConnectJob *> completion{nullptr};
  std::atomic<bool> occupied{false};
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> exited{false};
  bool started = false;

  static void taskEntry(void *raw) {
    TlsWorker *worker = static_cast<TlsWorker *>(raw);
    if (worker != nullptr) {
      worker->taskLoop();
    }
    vTaskDelete(nullptr);
  }

  void taskLoop() {
    while (!stopRequested.load(std::memory_order_acquire)) {
      TlsConnectJob *job = request.exchange(nullptr,
                                             std::memory_order_acq_rel);
      if (job == nullptr) {
        delay(2);
        continue;
      }
      size_t bundleSize = 0;
      void *memory = psram::allocate(sizeof(WiFiClientSecure));
      if (memory != nullptr && job->port != 0 && validProxyHost(job->host) &&
          (job->allowUntrusted || certificateBundleSize(bundleSize))) {
        job->client = new (memory) WiFiClientSecure();
        job->client->setTimeout(kTlsHandshakeTimeoutMs);
        job->client->setHandshakeTimeout(kTlsHandshakeTimeoutMs / 1000U);
        if (job->allowUntrusted) {
          job->client->setInsecure();
        } else {
          job->client->setCACertBundle(x509BundleStart, bundleSize);
        }
        const IPAddress resolved(job->address[0], job->address[1],
                                 job->address[2], job->address[3]);
        job->succeeded =
            job->client->connect(resolved, job->port, job->host, nullptr,
                                 nullptr, nullptr) != 0;
      } else if (memory != nullptr) {
        psram::release(memory);
      }
      if (!job->succeeded && job->client != nullptr) {
        job->client->stop();
        job->client->~WiFiClientSecure();
        psram::release(job->client);
        job->client = nullptr;
      }
      if (stopRequested.load(std::memory_order_acquire)) {
        releaseTlsJob(job);
        occupied.store(false, std::memory_order_release);
        break;
      }
      completion.store(job, std::memory_order_release);
    }
    releaseTlsJob(request.exchange(nullptr, std::memory_order_acq_rel));
    occupied.store(false, std::memory_order_release);
    exited.store(true, std::memory_order_release);
  }

  bool start() {
    if (started) {
      return true;
    }
    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCore(taskEntry, "https-proxy", kTlsWorkerStackBytes,
                                this, kTlsWorkerPriority, &task,
                                kTlsWorkerCore) != pdPASS) {
      return false;
    }
    started = true;
    return true;
  }

  void stop() {
    if (!started) {
      return;
    }
    stopRequested.store(true, std::memory_order_release);
    while (!exited.load(std::memory_order_acquire)) {
      delay(2);
    }
    releaseTlsJob(completion.exchange(nullptr, std::memory_order_acq_rel));
    releaseTlsJob(request.exchange(nullptr, std::memory_order_acq_rel));
    occupied.store(false, std::memory_order_release);
    started = false;
  }

  bool enqueue(TcpSession &session, const char *host,
               const uint8_t address[4], uint16_t port,
               bool allowUntrusted) {
    if (!started || host == nullptr || address == nullptr || port == 0) {
      return false;
    }
    void *memory = psram::allocate(sizeof(TlsConnectJob));
    if (memory == nullptr) {
      return false;
    }
    TlsConnectJob *job = new (memory) TlsConnectJob();
    const size_t hostLength = strnlen(host, sizeof(job->host));
    if (hostLength == 0 || hostLength >= sizeof(job->host)) {
      releaseTlsJob(job);
      return false;
    }
    memcpy(job->host, host, hostLength + 1U);
    memcpy(job->address, address, sizeof(job->address));
    job->session = &session;
    job->port = port;
    job->allowUntrusted = allowUntrusted;
    bool expected = false;
    if (!occupied.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      releaseTlsJob(job);
      return false;
    }
    request.store(job, std::memory_order_release);
    return true;
  }
};
#endif

ConnectionIo proxyIo(TcpSession &session) {
#if ENABLE_NETWORK_APPS
  if (session.secureProxy != nullptr) {
    return {session.secureProxy, secureSend, secureReceive};
  }
#endif
  return {&session.proxy, rawSend, rawReceive};
}

#if ENABLE_NETWORK_APPS
std::atomic<Socks5TetherImpl *> hookedImpl{nullptr};
std::atomic<uint32_t> hookUsers{0};
netif *hookedNetif = nullptr;
netif_input_fn originalInput = nullptr;
netif_output_fn originalOutput = nullptr;

struct HookChange {
  Socks5TetherImpl *impl = nullptr;
  netif *interface = nullptr;
  bool install = false;
  bool succeeded = false;
};

err_t tetherInputHook(pbuf *packet, netif *interface);
err_t tetherOutputHook(netif *interface, pbuf *packet,
                       const ip4_addr_t *address);

esp_err_t changeHooks(void *raw) {
  HookChange *change = static_cast<HookChange *>(raw);
  if (change == nullptr || change->impl == nullptr) {
    return -1;
  }
  if (change->install) {
    if (change->interface != nullptr && change->interface->input != nullptr &&
        change->interface->output != nullptr &&
        hookedImpl.load(std::memory_order_acquire) == nullptr) {
      hookedNetif = change->interface;
      originalInput = change->interface->input;
      originalOutput = change->interface->output;
      hookedImpl.store(change->impl, std::memory_order_release);
      change->interface->input = tetherInputHook;
      change->interface->output = tetherOutputHook;
      change->succeeded = true;
    }
  } else {
    Socks5TetherImpl *current =
        hookedImpl.load(std::memory_order_acquire);
    if (current == change->impl) {
      hookedImpl.store(nullptr, std::memory_order_release);
      if (hookedNetif != nullptr) {
        if (hookedNetif->input == tetherInputHook) {
          hookedNetif->input = originalInput;
        }
        if (hookedNetif->output == tetherOutputHook) {
          hookedNetif->output = originalOutput;
        }
      }
    }
    // If another owner replaced the global pointer, this object is already
    // unreachable from the hook and can be released safely.
    change->succeeded = true;
  }
  return ESP_OK;
}

bool runHookChange(HookChange &change, bool retry = false) {
  constexpr unsigned kMaximumAttempts = 50;
  esp_err_t result = -1;
  unsigned attempts = 0;
  do {
    result = esp_netif_tcpip_exec(changeHooks, &change);
    attempts++;
    if (result != ESP_OK && retry && attempts < kMaximumAttempts) {
      delay(1);
    }
  } while (result != ESP_OK && retry && attempts < kMaximumAttempts);
  return result == ESP_OK && change.succeeded;
}
#endif

} // namespace

struct Socks5TetherImpl {
  char proxyHost[96] = {};
  char proxyAddress[16] = {};
  char username[65] = {};
  char password[97] = {};
  uint8_t proxyAddressBytes[4] = {};
  uint16_t proxyPort = 0;
  SocketHandle tcpListener = kInvalidSocket;
  SocketHandle udpLocal[kUdpFlowCount] = {};
  SocketHandle udpUpstream = kInvalidSocket;
  SocketHandle udpControl = kInvalidSocket;
  Negotiation udpNegotiation;
  socks5::Ipv4Endpoint udpRelay;
  TcpSession tcpSessions[kTcpFlowCount];
  Flow tcpFlows[kTcpFlowCount];
  Flow udpFlows[kUdpFlowCount];
  std::atomic_flag flowLock = ATOMIC_FLAG_INIT;
#if ENABLE_NETWORK_APPS
  TlsWorker tlsWorker;
#endif
  uint8_t udpOutbound[10 + kUdpPayloadSize] = {};
  uint8_t udpInbound[11 + kUdpPayloadSize] = {};
  uint8_t udpPayload[1 + kUdpPayloadSize] = {};
  size_t udpOutboundSize = 0;
  size_t udpInboundSize = 0;
  uint8_t udpInboundFlow = 0xff;
  uint32_t udpRetryAt = 0;
  uint32_t enableRetryAt = 0;
  bool passwordAuth = false;
  bool allowUntrustedTls = false;
  TetherRoute route = TetherRoute::Direct;
  bool active = false;
  std::atomic<bool> relayReady{false};
  bool hooksInstalled = false;

  bool loadProfile() {
    char host[96] = {};
    char untrusted[8] = {};
    configValue("network.proxy.host", "", host, sizeof(host));
    configValue("network.proxy.username", "", username, sizeof(username));
    configValue("network.proxy.password", "", password, sizeof(password));
    configValue("network.proxy.allowUntrusted", "false", untrusted,
                sizeof(untrusted));
    proxyPort = configPort("network.proxy.port", 0);
    const size_t usernameLength = strnlen(username, sizeof(username));
    const size_t passwordLength = strnlen(password, sizeof(password));
    if (!validProxyHost(host) || proxyPort == 0 ||
        usernameLength >= sizeof(username) ||
        passwordLength >= sizeof(password) ||
        ((usernameLength == 0) != (passwordLength == 0))) {
      return false;
    }
    if (route != TetherRoute::Socks5 &&
        !http_connect::validCredentials(username, password)) {
      return false;
    }
    memcpy(proxyHost, host, sizeof(proxyHost));
    passwordAuth = usernameLength != 0;
    allowUntrustedTls = strcmp(untrusted, "true") == 0;
    return resolveProxy(host, proxyAddressBytes, proxyAddress);
  }

  bool openSockets() {
    NetworkStack &stack = networkStack();
    if (!stack.openTcpServer(kOwner, kTcpRedirectPort, kTcpFlowCount,
                             tcpListener, kApAddress)
             .ok()) {
      return false;
    }
    if (route != TetherRoute::Socks5) {
      return true;
    }
    for (uint8_t i = 0; i < kUdpFlowCount; i++) {
      if (!stack.openUdp(kOwner, kUdpRedirectFirstPort + i, udpLocal[i],
                         kApAddress)
               .ok()) {
        return false;
      }
    }
    return stack.openUdp(kOwner, kUdpUpstreamPort, udpUpstream).ok();
  }

  void resetSocketHandles() {
    tcpListener = kInvalidSocket;
    udpUpstream = kInvalidSocket;
    udpControl = kInvalidSocket;
    for (SocketHandle &socket : udpLocal) {
      socket = kInvalidSocket;
    }
  }

  bool tryEnableRelay(uint32_t now) {
    if (relayReady) {
      return true;
    }
    if (!due(now, enableRetryAt)) {
      return false;
    }
    enableRetryAt = now + kUdpRetryMs;
    memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));
    memset(proxyHost, 0, sizeof(proxyHost));
    memset(proxyAddress, 0, sizeof(proxyAddress));
    memset(proxyAddressBytes, 0, sizeof(proxyAddressBytes));
    proxyPort = 0;
    if (configuredDnsMode() == DnsMode::Doh || !configuredProxyReady() ||
        !loadProfile() || !openSockets()) {
      networkStack().closeOwner(kOwner);
      resetSocketHandles();
      return false;
    }
    relayReady = true;
    setTetherProxyRoutingActive(true);
    return true;
  }

  bool installHooks() {
#if ENABLE_NETWORK_APPS
    esp_netif_t *espInterface =
        esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (espInterface == nullptr) {
      return false;
    }
    auto *interface = static_cast<netif *>(
        esp_netif_get_netif_impl(espInterface));
    HookChange change;
    change.impl = this;
    change.interface = interface;
    change.install = true;
    hooksInstalled = runHookChange(change);
    return hooksInstalled;
#else
    return false;
#endif
  }

  bool removeHooks() {
#if ENABLE_NETWORK_APPS
    if (!hooksInstalled) {
      return true;
    }
    HookChange change;
    change.impl = this;
    change.install = false;
    if (!runHookChange(change, true)) {
      // Keep this object alive and inactive while lwIP may still reference it.
      return false;
    }
    while (hookUsers.load(std::memory_order_acquire) != 0) {
      delay(1);
    }
    hookedNetif = nullptr;
    originalInput = nullptr;
    originalOutput = nullptr;
    hooksInstalled = false;
#endif
    return true;
  }

  uint16_t inputRedirectPort(const transparent::PacketView &packet,
                             uint32_t now) {
    FlowGuard guard(flowLock);
    if (packet.sourcePort == 0 || packet.destinationPort == 0) {
      return 0;
    }
    if (route != TetherRoute::Socks5 &&
        packet.protocol != transparent::Protocol::Tcp) {
      return 0;
    }
    Flow *flows = packet.protocol == transparent::Protocol::Tcp ? tcpFlows
                                                                : udpFlows;
    const uint8_t count = packet.protocol == transparent::Protocol::Tcp
                              ? kTcpFlowCount
                              : kUdpFlowCount;
    const uint32_t idle = packet.protocol == transparent::Protocol::Tcp
                              ? kTcpClosedMs
                              : kUdpIdleMs;
    for (uint8_t i = 0; i < count; i++) {
      Flow &flow = flows[i];
      if (flow.used && sameAddress(flow.clientAddress, packet.sourceAddress) &&
          sameAddress(flow.remoteAddress, packet.destinationAddress) &&
          flow.clientPort == packet.sourcePort &&
          flow.remotePort == packet.destinationPort) {
        if (packet.protocol == transparent::Protocol::Tcp && flow.closed &&
            (packet.tcpFlags & 0x02U) != 0) {
          flow.accepted = false;
          flow.closed = false;
        }
        flow.lastAt = now;
        return flow.redirectPort;
      }
    }
    if (packet.protocol == transparent::Protocol::Tcp) {
      // Rewriting collapses the original destination out of lwIP's tuple.
      // A second destination using the same client IP/port would otherwise be
      // indistinguishable and could be attached to the wrong proxy target.
      for (const Flow &flow : tcpFlows) {
        if (flow.used && !flow.closed &&
            flow.clientPort == packet.sourcePort &&
            sameAddress(flow.clientAddress, packet.sourceAddress)) {
          return 0;
        }
      }
    }
    if (packet.protocol == transparent::Protocol::Tcp &&
        (packet.tcpFlags & 0x3fU) != 0x02U) {
      return 0;
    }
    int available = -1;
    for (uint8_t i = 0; i < count; i++) {
      Flow &flow = flows[i];
      if (!flow.used ||
          (packet.protocol == transparent::Protocol::Tcp && flow.closed) ||
          (packet.protocol == transparent::Protocol::Tcp && !flow.accepted &&
           due(now, flow.lastAt + kTcpClosedMs)) ||
          (packet.protocol == transparent::Protocol::Udp &&
           due(now, flow.lastAt + idle))) {
        available = i;
        break;
      }
    }
    if (available < 0) {
      return 0;
    }
    Flow &flow = flows[available];
    flow = {};
    memcpy(flow.clientAddress, packet.sourceAddress, 4);
    memcpy(flow.remoteAddress, packet.destinationAddress, 4);
    flow.clientPort = packet.sourcePort;
    flow.remotePort = packet.destinationPort;
    flow.redirectPort = packet.protocol == transparent::Protocol::Tcp
                            ? kTcpRedirectPort
                            : kUdpRedirectFirstPort + available;
    flow.lastAt = now;
    flow.used = true;
    return flow.redirectPort;
  }

  bool outputFlow(const transparent::PacketView &packet,
                  uint8_t remoteAddress[4], uint16_t &remotePort) {
    FlowGuard guard(flowLock);
    Flow *flows = packet.protocol == transparent::Protocol::Tcp ? tcpFlows
                                                                : udpFlows;
    const uint8_t count = packet.protocol == transparent::Protocol::Tcp
                              ? kTcpFlowCount
                              : kUdpFlowCount;
    for (uint8_t i = 0; i < count; i++) {
      Flow &flow = flows[i];
      if (flow.used && flow.redirectPort == packet.sourcePort &&
          flow.clientPort == packet.destinationPort &&
          sameAddress(flow.clientAddress, packet.destinationAddress)) {
        memcpy(remoteAddress, flow.remoteAddress, 4);
        remotePort = flow.remotePort;
        return true;
      }
    }
    return false;
  }

  bool acceptTarget(const uint8_t clientAddress[4], uint16_t clientPort,
                    socks5::Ipv4Endpoint &target) {
    FlowGuard guard(flowLock);
    for (Flow &flow : tcpFlows) {
      if (flow.used && !flow.accepted && !flow.closed &&
          flow.clientPort == clientPort &&
          sameAddress(flow.clientAddress, clientAddress)) {
        memcpy(target.address, flow.remoteAddress, 4);
        target.port = flow.remotePort;
        flow.accepted = true;
        return true;
      }
    }
    return false;
  }

  void closeFlow(const uint8_t clientAddress[4], uint16_t clientPort,
                 uint32_t now) {
    FlowGuard guard(flowLock);
    for (Flow &flow : tcpFlows) {
      if (flow.used && flow.clientPort == clientPort &&
          sameAddress(flow.clientAddress, clientAddress)) {
        flow.closed = true;
        flow.lastAt = now;
        return;
      }
    }
  }

  void expireFlows(uint32_t now) {
    FlowGuard guard(flowLock);
    for (Flow &flow : tcpFlows) {
      if (flow.used &&
          ((flow.closed && due(now, flow.lastAt + kTcpClosedMs)) ||
           (!flow.accepted && due(now, flow.lastAt + kTcpClosedMs)))) {
        flow = {};
      }
    }
    for (Flow &flow : udpFlows) {
      if (flow.used && due(now, flow.lastAt + kUdpIdleMs)) {
        flow = {};
      }
    }
  }

  bool openProxy(TcpSession &session) {
    if (route != TetherRoute::HttpsConnect) {
      return networkStack()
          .openTcpClient(kOwner, proxyAddress, proxyPort, session.proxy)
          .ok();
    }
#if ENABLE_NETWORK_APPS
    if (!tlsWorker.enqueue(session, proxyHost, proxyAddressBytes, proxyPort,
                           allowUntrustedTls)) {
      return false;
    }
    session.tlsPending = true;
    return true;
#else
    return false;
#endif
  }

  void closeProxy(TcpSession &session) {
    networkStack().close(session.proxy);
    session.proxy = kInvalidSocket;
#if ENABLE_NETWORK_APPS
    if (session.secureProxy != nullptr) {
      session.secureProxy->stop();
      session.secureProxy->~WiFiClientSecure();
      psram::release(session.secureProxy);
      session.secureProxy = nullptr;
    }
#endif
  }

  void closeSession(TcpSession &session, uint32_t now) {
    NetworkStack &stack = networkStack();
    stack.close(session.client);
    closeProxy(session);
    closeFlow(session.clientAddress, session.clientPort, now);
    session.negotiation.clear();
    session.httpNegotiation.clear();
    session = TcpSession{};
  }

  void collectTlsCompletion() {
#if ENABLE_NETWORK_APPS
    TlsConnectJob *job = tlsWorker.completion.exchange(
        nullptr, std::memory_order_acq_rel);
    if (job == nullptr) {
      return;
    }
    TcpSession *target = nullptr;
    for (TcpSession &session : tcpSessions) {
      if (&session == job->session) {
        target = &session;
        break;
      }
    }
    if (target != nullptr && target->used && target->tlsPending) {
      target->tlsPending = false;
      target->tlsFailed = !job->succeeded || job->client == nullptr;
      if (!target->tlsFailed) {
        target->secureProxy = job->client;
        job->client = nullptr;
      }
    }
    releaseTlsJob(job);
    tlsWorker.occupied.store(false, std::memory_order_release);
#endif
  }

  void acceptTcp(uint32_t now) {
    TcpSession *available = nullptr;
    for (TcpSession &session : tcpSessions) {
      if (!session.used) {
        available = &session;
        break;
      }
    }
    if (available == nullptr) {
      return;
    }
    char clientText[16] = {};
    uint16_t clientPort = 0;
    SocketHandle client = kInvalidSocket;
    const SocketResult accepted = networkStack().accept(
        tcpListener, client, clientText, sizeof(clientText), &clientPort);
    if (!accepted.ok()) {
      return;
    }
    uint8_t clientAddress[4] = {};
    socks5::Ipv4Endpoint target;
    if (!parseAddress(clientText, clientAddress) ||
        !acceptTarget(clientAddress, clientPort, target)) {
      networkStack().close(client);
      return;
    }
    available->proxy = kInvalidSocket;
    if (!openProxy(*available)) {
      networkStack().close(client);
      closeFlow(clientAddress, clientPort, now);
      return;
    }
    available->client = client;
    available->target = target;
    memcpy(available->clientAddress, clientAddress, 4);
    available->clientPort = clientPort;
    available->lastAt = now;
    available->used = true;
    if (route == TetherRoute::Socks5) {
      available->negotiation.begin(socks5::Command::Connect, target,
                                   passwordAuth, now);
    } else {
      available->httpNegotiation.begin(target, username, password, now);
    }
  }

  bool flushIo(const ConnectionIo &io, RelayBuffer &buffer, uint32_t &lastAt,
               uint32_t now) {
    if (buffer.offset > sizeof(buffer.data) ||
        buffer.size > sizeof(buffer.data) - buffer.offset) {
      return false;
    }
    if (buffer.size == 0) {
      buffer.clear();
      return true;
    }
    size_t sent = 0;
    if (io.send == nullptr) {
      return false;
    }
    const SocketResult result = io.send(
        io.context, buffer.data + buffer.offset, buffer.size, sent);
    if (!result.ok()) {
      return result.wouldBlock();
    }
    if (sent == 0 || sent > buffer.size) {
      return false;
    }
    buffer.offset += sent;
    buffer.size -= sent;
    lastAt = now;
    if (buffer.size == 0) {
      buffer.clear();
    }
    return true;
  }

  bool flush(SocketHandle socket, RelayBuffer &buffer, uint32_t &lastAt,
             uint32_t now) {
    return flushIo({&socket, rawSend, rawReceive}, buffer, lastAt, now);
  }

  bool fillIo(const ConnectionIo &io, RelayBuffer &buffer, uint32_t &lastAt,
              uint32_t now) {
    if (buffer.offset > sizeof(buffer.data) ||
        buffer.size > sizeof(buffer.data) - buffer.offset) {
      return false;
    }
    if (buffer.size != 0) {
      return true;
    }
    buffer.clear();
    size_t received = 0;
    if (io.receive == nullptr) {
      return false;
    }
    const SocketResult result =
        io.receive(io.context, buffer.data, sizeof(buffer.data), received);
    if (!result.ok()) {
      return result.wouldBlock();
    }
    if (received == 0 || received > sizeof(buffer.data)) {
      return false;
    }
    buffer.offset = 0;
    buffer.size = received;
    lastAt = now;
    return true;
  }

  bool fill(SocketHandle socket, RelayBuffer &buffer, uint32_t &lastAt,
            uint32_t now) {
    return fillIo({&socket, rawSend, rawReceive}, buffer, lastAt, now);
  }

  void updateTcp(uint32_t now) {
    collectTlsCompletion();
    acceptTcp(now);
    for (TcpSession &session : tcpSessions) {
      if (!session.used) {
        continue;
      }
#if ENABLE_NETWORK_APPS
      if (route == TetherRoute::HttpsConnect && session.tlsPending) {
        continue;
      }
      if (route == TetherRoute::HttpsConnect &&
          (session.tlsFailed || session.secureProxy == nullptr)) {
        closeSession(session, now);
        continue;
      }
#endif
      const ConnectionIo upstream = proxyIo(session);
      const NegotiationState state =
          route == TetherRoute::Socks5
              ? session.negotiation.update(session.proxy, username, password,
                                           now)
              : session.httpNegotiation.update(upstream, now);
      if (state == NegotiationState::Failed) {
        closeSession(session, now);
        continue;
      }
      if (state != NegotiationState::Ready) {
        continue;
      }
      if (session.toClient.size == 0) {
        size_t tunneled = 0;
        const bool taken = route == TetherRoute::Socks5
                               ? session.negotiation.takeTunnelBytes(
                                     session.toClient.data,
                                     sizeof(session.toClient.data), tunneled)
                               : session.httpNegotiation.takeTunnelBytes(
                                     session.toClient.data,
                                     sizeof(session.toClient.data), tunneled);
        if (!taken) {
          closeSession(session, now);
          continue;
        }
        session.toClient.size = tunneled;
      }
      if (!flushIo(upstream, session.toProxy, session.lastAt, now) ||
          !flush(session.client, session.toClient, session.lastAt, now) ||
          !fill(session.client, session.toProxy, session.lastAt, now) ||
          !fillIo(upstream, session.toClient, session.lastAt, now) ||
          due(now, session.lastAt + kTcpIdleMs)) {
        closeSession(session, now);
      }
    }
  }

  void closeUdpControl() {
    networkStack().close(udpControl);
    udpControl = kInvalidSocket;
    udpNegotiation.clear();
    udpRelay = {};
  }

  void startUdpControl(uint32_t now) {
    if (udpControl != kInvalidSocket || !due(now, udpRetryAt)) {
      return;
    }
    if (!networkStack()
             .openTcpClient(kOwner, proxyAddress, proxyPort, udpControl)
             .ok()) {
      udpControl = kInvalidSocket;
      udpRetryAt = now + kUdpRetryMs;
      return;
    }
    socks5::Ipv4Endpoint any;
    udpNegotiation.begin(socks5::Command::UdpAssociate, any, passwordAuth,
                         now);
  }

  int udpFlowByRemote(const socks5::Ipv4Endpoint &remote) {
    FlowGuard guard(flowLock);
    for (uint8_t i = 0; i < kUdpFlowCount; i++) {
      if (udpFlows[i].used && udpFlows[i].remotePort == remote.port &&
          sameAddress(udpFlows[i].remoteAddress, remote.address)) {
        return i;
      }
    }
    return -1;
  }

  bool sendPendingUdpOutbound() {
    if (udpOutboundSize == 0) {
      return true;
    }
    char relayText[16] = {};
    if (!printAddress(udpRelay.address, relayText)) {
      return false;
    }
    size_t sent = 0;
    const SocketResult result = networkStack().sendTo(
        udpUpstream, relayText, udpRelay.port, udpOutbound, udpOutboundSize,
        sent);
    if (!result.ok()) {
      return result.wouldBlock();
    }
    if (sent != udpOutboundSize) {
      return false;
    }
    udpOutboundSize = 0;
    return true;
  }

  void receiveLocalUdp() {
    if (udpOutboundSize != 0) {
      return;
    }
    for (uint8_t i = 0; i < kUdpFlowCount; i++) {
      Flow snapshot;
      {
        FlowGuard guard(flowLock);
        if (!udpFlows[i].used) {
          continue;
        }
        snapshot = udpFlows[i];
      }
      char clientText[16] = {};
      uint16_t clientPort = 0;
      size_t received = 0;
      const SocketResult result = networkStack().receiveFrom(
          udpLocal[i], udpPayload, sizeof(udpPayload), received, clientText,
          sizeof(clientText), clientPort);
      if (!result.ok()) {
        continue;
      }
      uint8_t clientAddress[4] = {};
      if (received == 0 || received > kUdpPayloadSize ||
          !parseAddress(clientText, clientAddress) ||
          clientPort != snapshot.clientPort ||
          !sameAddress(clientAddress, snapshot.clientAddress)) {
        return;
      }
      socks5::Ipv4Endpoint target;
      memcpy(target.address, snapshot.remoteAddress, 4);
      target.port = snapshot.remotePort;
      udpOutboundSize = socks5::buildUdpDatagram(
          target, udpPayload, received, udpOutbound, sizeof(udpOutbound));
      return;
    }
  }

  bool sendPendingUdpInbound() {
    if (udpInboundSize == 0 || udpInboundFlow >= kUdpFlowCount) {
      return true;
    }
    Flow snapshot;
    {
      FlowGuard guard(flowLock);
      if (!udpFlows[udpInboundFlow].used) {
        udpInboundSize = 0;
        udpInboundFlow = 0xff;
        return true;
      }
      snapshot = udpFlows[udpInboundFlow];
    }
    char clientText[16] = {};
    if (!printAddress(snapshot.clientAddress, clientText)) {
      return false;
    }
    size_t sent = 0;
    const SocketResult result = networkStack().sendTo(
        udpLocal[udpInboundFlow], clientText, snapshot.clientPort, udpInbound,
        udpInboundSize, sent);
    if (!result.ok()) {
      return result.wouldBlock();
    }
    if (sent != udpInboundSize) {
      return false;
    }
    udpInboundSize = 0;
    udpInboundFlow = 0xff;
    return true;
  }

  void receiveUpstreamUdp() {
    if (udpInboundSize != 0) {
      return;
    }
    char sourceText[16] = {};
    uint16_t sourcePort = 0;
    size_t received = 0;
    const SocketResult result = networkStack().receiveFrom(
        udpUpstream, udpInbound, sizeof(udpInbound), received, sourceText,
        sizeof(sourceText), sourcePort);
    if (!result.ok() || received == 0 ||
        received > 10U + kUdpPayloadSize) {
      return;
    }
    uint8_t sourceAddress[4] = {};
    if (!parseAddress(sourceText, sourceAddress) ||
        sourcePort != udpRelay.port ||
        !sameAddress(sourceAddress, udpRelay.address)) {
      udpInboundSize = 0;
      return;
    }
    socks5::Ipv4Endpoint remote;
    const uint8_t *payload = nullptr;
    size_t payloadSize = 0;
    if (socks5::parseUdpDatagram(udpInbound, received, remote, payload,
                                 payloadSize) !=
        socks5::ParseState::Accepted ||
        payloadSize > kUdpPayloadSize) {
      udpInboundSize = 0;
      return;
    }
    const int flow = udpFlowByRemote(remote);
    if (flow < 0) {
      udpInboundSize = 0;
      return;
    }
    memmove(udpInbound, payload, payloadSize);
    udpInboundSize = payloadSize;
    udpInboundFlow = static_cast<uint8_t>(flow);
  }

  void updateUdp(uint32_t now) {
    startUdpControl(now);
    if (udpControl == kInvalidSocket) {
      return;
    }
    const NegotiationState state = udpNegotiation.update(
        udpControl, username, password, now);
    if (state == NegotiationState::Failed) {
      closeUdpControl();
      udpRetryAt = now + kUdpRetryMs;
      return;
    }
    if (state != NegotiationState::Ready) {
      return;
    }
    if (udpRelay.port == 0) {
      size_t unexpectedSize = 0;
      if (!udpNegotiation.takeTunnelBytes(nullptr, 0, unexpectedSize)) {
        closeUdpControl();
        udpRetryAt = now + kUdpRetryMs;
        return;
      }
      udpRelay = udpNegotiation.boundEndpoint();
      const uint8_t any[4] = {};
      if (sameAddress(udpRelay.address, any)) {
        memcpy(udpRelay.address, proxyAddressBytes, 4);
      }
      if (!safeProxyAddress(udpRelay.address) || udpRelay.port == 0) {
        closeUdpControl();
        udpRetryAt = now + kUdpRetryMs;
        return;
      }
    }
    uint8_t unexpected = 0;
    size_t controlBytes = 0;
    const SocketResult control =
        networkStack().receive(udpControl, &unexpected, 1, controlBytes);
    if (control.ok() || (!control.ok() && !control.wouldBlock())) {
      closeUdpControl();
      udpRetryAt = now + kUdpRetryMs;
      return;
    }
    if (!sendPendingUdpOutbound() || !sendPendingUdpInbound()) {
      closeUdpControl();
      udpRetryAt = now + kUdpRetryMs;
      return;
    }
    receiveLocalUdp();
    sendPendingUdpOutbound();
    receiveUpstreamUdp();
    sendPendingUdpInbound();
  }

  void update(uint32_t now) {
    if (!tryEnableRelay(now)) {
      return;
    }
    expireFlows(now);
    updateTcp(now);
    if (route == TetherRoute::Socks5) {
      updateUdp(now);
    }
  }

  bool shutdown(uint32_t now) {
    active = false;
    relayReady = false;
    setTetherProxyRoutingActive(false);
    const bool hooksRemoved = removeHooks();
#if ENABLE_NETWORK_APPS
    tlsWorker.stop();
#endif
    for (TcpSession &session : tcpSessions) {
      if (session.used) {
        closeSession(session, now);
      }
    }
    closeUdpControl();
    networkStack().closeOwner(kOwner);
    resetSocketHandles();
    memset(password, 0, sizeof(password));
    return hooksRemoved;
  }
};

#if ENABLE_NETWORK_APPS
namespace {

class HookUse {
public:
  HookUse() {
    hookUsers.fetch_add(1, std::memory_order_acq_rel);
    impl = hookedImpl.load(std::memory_order_acquire);
  }
  ~HookUse() { hookUsers.fetch_sub(1, std::memory_order_acq_rel); }

  Socks5TetherImpl *get() const { return impl; }

private:
  Socks5TetherImpl *impl = nullptr;
};

pbuf *copyPacket(const pbuf *packet, pbuf_layer layer) {
  if (packet == nullptr || packet->tot_len == 0) {
    return nullptr;
  }
  pbuf *copy = pbuf_alloc(layer, packet->tot_len, PBUF_RAM);
  if (copy == nullptr || copy->len != copy->tot_len ||
      pbuf_copy(copy, packet) != ERR_OK) {
    if (copy != nullptr) {
      pbuf_free(copy);
    }
    return nullptr;
  }
  return copy;
}

bool reservedLocalPort(const transparent::PacketView &view) {
  if (!sameAddress(view.destinationAddress, kApAddressBytes)) {
    return false;
  }
  if (view.protocol == transparent::Protocol::Tcp) {
    return view.destinationPort == kTcpRedirectPort;
  }
  return view.destinationPort >= kUdpRedirectFirstPort &&
         view.destinationPort < kUdpRedirectFirstPort + kUdpFlowCount;
}

bool reservedRedirectSource(const transparent::PacketView &view) {
  if (!sameAddress(view.sourceAddress, kApAddressBytes)) {
    return false;
  }
  if (view.protocol == transparent::Protocol::Tcp) {
    return view.sourcePort == kTcpRedirectPort;
  }
  return view.sourcePort >= kUdpRedirectFirstPort &&
         view.sourcePort < kUdpRedirectFirstPort + kUdpFlowCount;
}

err_t tetherInputHook(pbuf *packet, netif *interface) {
  HookUse use;
  Socks5TetherImpl *current = use.get();
  if (current == nullptr || originalInput == nullptr) {
    if (packet != nullptr) {
      pbuf_free(packet);
    }
    return ERR_OK;
  }
  uint8_t ethernetHeader[14] = {};
  if (packet == nullptr ||
      pbuf_copy_partial(packet, ethernetHeader, sizeof(ethernetHeader), 0) !=
          sizeof(ethernetHeader)) {
    pbuf_free(packet);
    return ERR_OK;
  }
  const uint16_t etherType =
      static_cast<uint16_t>(ethernetHeader[12]) << 8U | ethernetHeader[13];
  if (etherType == 0x86ddU) {
    pbuf_free(packet);
    return ERR_OK;
  }
  if (etherType != 0x0800U) {
    return originalInput(packet, interface);
  }
  pbuf *copy = copyPacket(packet, PBUF_RAW);
  if (copy == nullptr) {
    pbuf_free(packet);
    return ERR_OK;
  }
  auto *bytes = static_cast<uint8_t *>(copy->payload);
  transparent::Ipv4HeaderView ipHeader;
  if (!transparent::inspectIpv4Header(bytes, copy->tot_len, true, ipHeader)) {
    pbuf_free(copy);
    pbuf_free(packet);
    return ERR_OK;
  }
  if (!apClientAddress(ipHeader.sourceAddress)) {
    pbuf_free(copy);
    return originalInput(packet, interface);
  }
  const bool local = localOrSpecialDestination(ipHeader.destinationAddress);
  transparent::PacketView view;
  if (!transparent::inspectPacket(bytes, copy->tot_len, true, view)) {
    pbuf_free(copy);
    if (local) {
      return originalInput(packet, interface);
    }
    pbuf_free(packet);
    return ERR_OK;
  }
  if (reservedLocalPort(view)) {
    pbuf_free(copy);
    pbuf_free(packet);
    return ERR_OK;
  }
  if (local) {
    pbuf_free(copy);
    return originalInput(packet, interface);
  }
  if (!transparent::checksumsValid(bytes, copy->tot_len, view)) {
    pbuf_free(copy);
    pbuf_free(packet);
    return ERR_OK;
  }
  if (!current->relayReady) {
    pbuf_free(copy);
    pbuf_free(packet);
    return ERR_OK;
  }
  const uint16_t redirectPort = current->inputRedirectPort(view, millis());
  if (view.fragmented || redirectPort == 0 ||
      !transparent::rewriteDestination(bytes, copy->tot_len, view,
                                       kApAddressBytes, redirectPort)) {
    pbuf_free(copy);
    pbuf_free(packet);
    return ERR_OK;
  }
  pbuf_free(packet);
  return originalInput(copy, interface);
}

err_t tetherOutputHook(netif *interface, pbuf *packet,
                       const ip4_addr_t *address) {
  HookUse use;
  Socks5TetherImpl *current = use.get();
  if (current == nullptr || originalOutput == nullptr) {
    return ERR_IF;
  }
  // Preserve link-layer headroom because the original AP output is
  // etharp_output(), which prepends an Ethernet header.
  pbuf *copy = copyPacket(packet, PBUF_LINK);
  if (copy == nullptr) {
    // A redirect response must never escape with its private listener port
    // exposed. Under memory pressure the safe behavior is to drop output.
    return ERR_IF;
  }
  auto *bytes = static_cast<uint8_t *>(copy->payload);
  transparent::PacketView view;
  uint8_t remoteAddress[4] = {};
  uint16_t remotePort = 0;
  if (!transparent::inspectPacket(bytes, copy->tot_len, false, view)) {
    pbuf_free(copy);
    return ERR_IF;
  }
  if (!sameAddress(view.sourceAddress, kApAddressBytes)) {
    pbuf_free(copy);
    return originalOutput(interface, packet, address);
  }
  if (!current->outputFlow(view, remoteAddress, remotePort)) {
    const bool privateRedirect = reservedRedirectSource(view);
    pbuf_free(copy);
    return privateRedirect ? ERR_IF
                           : originalOutput(interface, packet, address);
  }
  if (!transparent::rewriteSource(bytes, copy->tot_len, view, remoteAddress,
                                  remotePort)) {
    pbuf_free(copy);
    return ERR_IF;
  }
  const err_t result = originalOutput(interface, copy, address);
  pbuf_free(copy);
  return result;
}

} // namespace
#endif

Socks5Tether::~Socks5Tether() { stop(); }

bool Socks5Tether::start() {
#if ENABLE_NETWORK_APPS
  if (impl != nullptr) {
    return impl->active;
  }
  const TetherRoute route = configuredTetherRoute();
  if (!tetheringActive() || WiFi.status() != WL_CONNECTED ||
      (route != TetherRoute::Socks5 && route != TetherRoute::HttpConnect &&
       route != TetherRoute::HttpsConnect)) {
    return false;
  }
  void *memory = psram::allocate(sizeof(Socks5TetherImpl));
  if (memory == nullptr) {
    return false;
  }
  Socks5TetherImpl *created = new (memory) Socks5TetherImpl();
  created->route = route;
#if ENABLE_NETWORK_APPS
  if (route == TetherRoute::HttpsConnect && !created->tlsWorker.start()) {
    created->~Socks5TetherImpl();
    psram::release(created);
    return false;
  }
#endif
  if (!created->installHooks()) {
#if ENABLE_NETWORK_APPS
    created->tlsWorker.stop();
#endif
    created->~Socks5TetherImpl();
    psram::release(created);
    return false;
  }
  created->active = true;
  impl = created;
  created->tryEnableRelay(millis());
  return true;
#else
  return false;
#endif
}

void Socks5Tether::update(uint32_t nowMs) {
  if (impl != nullptr && impl->active) {
    impl->update(nowMs);
  }
}

void Socks5Tether::stop() {
  if (impl == nullptr) {
    return;
  }
#if ENABLE_NETWORK_APPS
  const bool canRelease = impl->shutdown(millis());
#else
  const bool canRelease = impl->shutdown(0);
#endif
  if (!canRelease) {
    // Quarantine the inactive object instead of freeing memory still reachable
    // from an lwIP hook. A later stop() call retries removal.
    return;
  }
  impl->~Socks5TetherImpl();
  psram::release(impl);
  impl = nullptr;
  setTetherProxyRoutingActive(false);
}

bool Socks5Tether::running() const {
  return impl != nullptr && impl->active;
}

} // namespace net
