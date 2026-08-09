#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#include "net/dns_forwarder.h"

#include "net/dns_message.h"
#include "net/ipv4_address.h"
#include "net/network_config.h"
#include "sys/psram_allocator.h"

#if ENABLE_NETWORK_APPS
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <new>
#include <strings.h>

namespace net {
namespace {

constexpr size_t kMaxDnsQuery = dns::kMaximumQuerySize;
constexpr size_t kMaxDnsResponse = dns::kMaximumResponseSize;
constexpr unsigned long kDohTimeoutMs = 3500;
constexpr unsigned long kDohBodyDeadlineMs = 7000;
constexpr size_t kMaximumCertificateBundleSize = 1024U * 1024U;
#if ENABLE_NETWORK_APPS
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr UBaseType_t kWorkerPriority = 1;
constexpr BaseType_t kWorkerCore = 0;
#endif

bool apClient(const char *text) {
  uint8_t address[4] = {};
  if (!parseIpv4Address(text, address)) {
    return false;
  }
  return address[0] == 192U && address[1] == 168U && address[2] == 4U &&
         address[3] >= 2U && address[3] < 255U;
}

#if ENABLE_NETWORK_APPS
extern const uint8_t x509BundleStart[]
    asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509BundleEnd[] asm("_binary_x509_crt_bundle_end");

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

bool dnsContentType(const char *value) {
  static constexpr char expected[] = "application/dns-message";
  if (value == nullptr || strncasecmp(value, expected, sizeof(expected) - 1U) !=
                              0) {
    return false;
  }
  const char *suffix = value + sizeof(expected) - 1U;
  while (*suffix == ' ' || *suffix == '\t') {
    suffix++;
  }
  return *suffix == '\0' || *suffix == ';';
}

bool readHttpBody(HTTPClient &http, uint8_t *out, size_t capacity,
                  size_t &outSize) {
  outSize = 0;
  const int declared = http.getSize();
  if (declared <= 0 || static_cast<size_t>(declared) > capacity) {
    return false;
  }
  Stream *stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }
  const size_t expected = static_cast<size_t>(declared);
  const unsigned long started = millis();
  unsigned long lastProgress = millis();
  while (outSize < expected && millis() - lastProgress < kDohTimeoutMs &&
         millis() - started < kDohBodyDeadlineMs) {
    const int available = stream->available();
    if (available <= 0) {
      delay(1);
      continue;
    }
    size_t amount = static_cast<size_t>(available);
    if (amount > expected - outSize) {
      amount = expected - outSize;
    }
    size_t received = 0;
    while (received < amount) {
      const int byte = stream->read();
      if (byte < 0) {
        break;
      }
      out[outSize + received++] = static_cast<uint8_t>(byte);
    }
    if (received == 0) {
      delay(1);
      continue;
    }
    outSize += received;
    lastProgress = millis();
  }
  return outSize == expected && outSize >= 12;
}

bool queryDoh(const char *endpoint, const uint8_t *query, size_t querySize,
              uint8_t *response, size_t responseCapacity,
              size_t &responseSize) {
  size_t bundleSize = 0;
  if (!validDohEndpoint(endpoint) ||
      !certificateBundleSize(bundleSize)) {
    return false;
  }
  WiFiClientSecure client;
  client.setCACertBundle(x509BundleStart, bundleSize);
  HTTPClient http;
  http.setTimeout(kDohTimeoutMs);
  http.useHTTP10(true);
  if (!http.begin(client, endpoint)) {
    return false;
  }
  http.addHeader("Accept", "application/dns-message");
  http.addHeader("Content-Type", "application/dns-message");
  static const char *responseHeaders[] = {"Content-Type"};
  http.collectHeaders(responseHeaders, 1);
  const int status = http.sendRequest(
      "POST", const_cast<uint8_t *>(query), querySize);
  const String responseType = http.header("Content-Type");
  const bool ok = status == HTTP_CODE_OK && dnsContentType(responseType.c_str()) &&
                  readHttpBody(http, response, responseCapacity, responseSize);
  http.end();
  return ok;
}
#endif

} // namespace

#if ENABLE_NETWORK_APPS
struct DnsJob {
  uint8_t query[kMaxDnsQuery] = {};
  uint8_t response[kMaxDnsResponse] = {};
  char endpoint[96] = {};
  char remote[16] = {};
  size_t querySize = 0;
  size_t responseSize = 0;
  uint16_t remotePort = 0;
};

struct DnsForwarderImpl {
  std::atomic<DnsJob *> request{nullptr};
  std::atomic<DnsJob *> completion{nullptr};
  std::atomic<bool> occupied{false};
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> exited{false};
};

namespace {

void releaseJob(DnsJob *job) {
  if (job == nullptr) {
    return;
  }
  memset(job->query, 0, sizeof(job->query));
  memset(job->response, 0, sizeof(job->response));
  memset(job->endpoint, 0, sizeof(job->endpoint));
  memset(job->remote, 0, sizeof(job->remote));
  job->~DnsJob();
  psram::release(job);
}

void dnsWorker(void *raw) {
  DnsForwarderImpl *worker = static_cast<DnsForwarderImpl *>(raw);
  if (worker != nullptr) {
    while (!worker->stopRequested.load(std::memory_order_acquire)) {
      DnsJob *job = worker->request.exchange(nullptr,
                                             std::memory_order_acq_rel);
      if (job == nullptr) {
        delay(2);
        continue;
      }
      if (!queryDoh(job->endpoint, job->query, job->querySize, job->response,
                    sizeof(job->response), job->responseSize) ||
          !dns::validResponse(job->query, job->querySize, job->response,
                              job->responseSize)) {
        job->responseSize = dns::serviceFailure(
            job->query, job->querySize, job->response, sizeof(job->response));
      }
      if (worker->stopRequested.load(std::memory_order_acquire)) {
        releaseJob(job);
        worker->occupied.store(false, std::memory_order_release);
        break;
      }
      worker->completion.store(job, std::memory_order_release);
    }
    DnsJob *queued = worker->request.exchange(nullptr,
                                               std::memory_order_acq_rel);
    releaseJob(queued);
    worker->occupied.store(false, std::memory_order_release);
    worker->exited.store(true, std::memory_order_release);
  }
  vTaskDelete(nullptr);
}

} // namespace
#else
struct DnsForwarderImpl {};
#endif

DnsForwarder::~DnsForwarder() { stop(); }

bool DnsForwarder::start() {
#if ENABLE_NETWORK_APPS
  if (socket != kInvalidSocket && impl != nullptr) {
    return true;
  }
  SocketHandle opened = kInvalidSocket;
  if (!networkStack()
           .openUdp("dns-forwarder", 53, opened, "192.168.4.1")
           .ok()) {
    return false;
  }
  void *memory = psram::allocate(sizeof(DnsForwarderImpl));
  if (memory == nullptr) {
    networkStack().close(opened);
    return false;
  }
  DnsForwarderImpl *created = new (memory) DnsForwarderImpl();
  TaskHandle_t task = nullptr;
  if (xTaskCreatePinnedToCore(dnsWorker, "doh-forward", kWorkerStackBytes,
                              created, kWorkerPriority, &task,
                              kWorkerCore) != pdPASS) {
    created->~DnsForwarderImpl();
    psram::release(created);
    networkStack().close(opened);
    return false;
  }
  impl = created;
  socket = opened;
  return true;
#else
  return false;
#endif
}

void DnsForwarder::stop() {
  networkStack().close(socket);
  socket = kInvalidSocket;
#if ENABLE_NETWORK_APPS
  DnsForwarderImpl *worker = impl;
  impl = nullptr;
  if (worker == nullptr) {
    return;
  }
  worker->stopRequested.store(true, std::memory_order_release);
  while (!worker->exited.load(std::memory_order_acquire)) {
    delay(2);
  }
  releaseJob(worker->completion.exchange(nullptr,
                                          std::memory_order_acq_rel));
  releaseJob(worker->request.exchange(nullptr, std::memory_order_acq_rel));
  worker->~DnsForwarderImpl();
  psram::release(worker);
#endif
}

bool DnsForwarder::running() const {
  return socket != kInvalidSocket && impl != nullptr;
}

void DnsForwarder::update() {
#if ENABLE_NETWORK_APPS
  if (socket == kInvalidSocket || impl == nullptr) {
    return;
  }
  DnsJob *completed = impl->completion.exchange(nullptr,
                                                 std::memory_order_acq_rel);
  if (completed != nullptr) {
    if (completed->responseSize > 0 &&
        completed->responseSize <= sizeof(completed->response)) {
      size_t sent = 0;
      (void)networkStack().sendTo(
          socket, completed->remote, completed->remotePort,
          completed->response, completed->responseSize, sent);
    }
    releaseJob(completed);
    impl->occupied.store(false, std::memory_order_release);
  }
  if (impl->occupied.load(std::memory_order_acquire)) {
    return;
  }
  uint8_t query[kMaxDnsQuery] = {};
  char remote[16] = {};
  uint16_t port = 0;
  size_t querySize = 0;
  const SocketResult received = networkStack().receiveFrom(
      socket, query, sizeof(query), querySize, remote, sizeof(remote), port);
  if (!received.ok() || port == 0 || !apClient(remote) ||
      !dns::validQuery(query, querySize)) {
    return;
  }

  char endpoint[96] = {};
  configValue("network.dns.dohUrl",
              "https://cloudflare-dns.com/dns-query", endpoint,
              sizeof(endpoint));
  if (!validDohEndpoint(endpoint)) {
    return;
  }
  void *memory = psram::allocate(sizeof(DnsJob));
  if (memory == nullptr) {
    uint8_t failure[kMaxDnsQuery] = {};
    const size_t failureSize =
        dns::serviceFailure(query, querySize, failure, sizeof(failure));
    size_t sent = 0;
    if (failureSize > 0) {
      (void)networkStack().sendTo(socket, remote, port, failure, failureSize,
                                  sent);
    }
    return;
  }
  DnsJob *job = new (memory) DnsJob();
  memcpy(job->query, query, querySize);
  memcpy(job->endpoint, endpoint, sizeof(job->endpoint));
  memcpy(job->remote, remote, sizeof(job->remote));
  job->querySize = querySize;
  job->remotePort = port;
  bool expected = false;
  if (!impl->occupied.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel,
          std::memory_order_acquire)) {
    releaseJob(job);
    return;
  }
  impl->request.store(job, std::memory_order_release);
#endif
}

} // namespace net
