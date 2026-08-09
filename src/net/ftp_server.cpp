#include "net/ftp_server.h"

#include "net/network_interface.h"
#include "net/network_stack.h"
#include "net/share_paths.h"
#include "sys/psram_allocator.h"

#include <FS.h>
#include <SD_MMC.h>
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <new>

namespace net {
namespace {

constexpr char kApAddress[] = "192.168.4.1";
constexpr uint32_t kControlTimeoutMs = 120000;
constexpr uint32_t kLoginTimeoutMs = 15000;
constexpr uint32_t kDataTimeoutMs = 15000;
constexpr size_t kTransferBufferSize = 2048;
constexpr size_t kListCapacity = 4096;
constexpr size_t kMaximumUpload = 8U * 1024U * 1024U;

bool expired(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

bool constantTimeEqual(const char *left, const char *right) {
  const size_t a = left != nullptr ? strnlen(left, 65) : 0;
  const size_t b = right != nullptr ? strnlen(right, 65) : 0;
  unsigned difference = static_cast<unsigned>(a ^ b);
  const size_t count = a > b ? a : b;
  for (size_t i = 0; i < count; i++) {
    const unsigned char x = i < a ? static_cast<unsigned char>(left[i]) : 0;
    const unsigned char y = i < b ? static_cast<unsigned char>(right[i]) : 0;
    difference |= x ^ y;
  }
  return difference == 0;
}

bool validCredential(const char *text, size_t minimum, size_t maximum) {
  if (text == nullptr) {
    return false;
  }
  const size_t length = strnlen(text, maximum + 1U);
  if (length < minimum || length > maximum || text[0] == ' ') {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const unsigned char byte = static_cast<unsigned char>(text[i]);
    if (byte < 0x20U || byte == 0x7fU) {
      return false;
    }
  }
  return true;
}

const char *flatArgument(const char *argument) {
  if (argument == nullptr) {
    return nullptr;
  }
  while (*argument == ' ') {
    argument++;
  }
  return argument[0] == '/' ? argument + 1 : argument;
}

const char *baseName(const char *path) {
  const char *slash = path != nullptr ? strrchr(path, '/') : nullptr;
  return slash != nullptr ? slash + 1 : path;
}

} // namespace

struct FtpServer::Impl {
  enum class Operation : uint8_t { None, List, Retrieve, Store };

  SocketHandle listener = kInvalidSocket;
  SocketHandle control = kInvalidSocket;
  SocketHandle passive = kInvalidSocket;
  SocketHandle data = kInvalidSocket;
  File transferFile;
  uint8_t *transferBuffer = nullptr;
  char *listing = nullptr;
  char username[33] = {};
  char password[65] = {};
  char controlPeer[16] = {};
  char uploadPath[96] = {};
  char uploadTempPath[96] = {};
  char line[256] = {};
  char reply[256] = {};
  size_t lineUsed = 0;
  size_t replySize = 0;
  size_t replySent = 0;
  size_t transferSize = 0;
  size_t transferOffset = 0;
  size_t transferred = 0;
  uint32_t controlDeadline = 0;
  uint32_t dataDeadline = 0;
  uint32_t acceptAfter = 0;
  uint16_t controlPort = 0;
  uint16_t dataPort = 0;
  uint8_t failedPasswords = 0;
  uint8_t consecutiveAuthFailures = 0;
  Operation operation = Operation::None;
  bool usernameAccepted = false;
  bool authenticated = false;
  bool closeAfterReply = false;
  bool sourceFinished = false;
};

namespace {

void queueReply(FtpServer::Impl &state, const char *text) {
  if (state.replySent < state.replySize || text == nullptr) {
    return;
  }
  const size_t length = strnlen(text, sizeof(state.reply));
  if (length >= sizeof(state.reply)) {
    return;
  }
  memcpy(state.reply, text, length + 1U);
  state.replySize = length;
  state.replySent = 0;
}

void closeData(FtpServer::Impl &state, bool preserveUpload = false) {
  networkStack().close(state.data);
  networkStack().close(state.passive);
  state.data = kInvalidSocket;
  state.passive = kInvalidSocket;
  state.transferFile.close();
  state.transferFile = File();
  psram::release(state.listing);
  state.listing = nullptr;
  if (!preserveUpload && state.operation == FtpServer::Impl::Operation::Store &&
      state.uploadTempPath[0] != '\0') {
    SD_MMC.remove(state.uploadTempPath);
  }
  state.uploadPath[0] = '\0';
  state.uploadTempPath[0] = '\0';
  state.operation = FtpServer::Impl::Operation::None;
  state.transferSize = 0;
  state.transferOffset = 0;
  state.transferred = 0;
  state.sourceFinished = false;
}

void closeControl(FtpServer::Impl &state) {
  closeData(state);
  networkStack().close(state.control);
  state.control = kInvalidSocket;
  state.controlPeer[0] = '\0';
  memset(state.line, 0, sizeof(state.line));
  state.lineUsed = 0;
  state.replySize = 0;
  state.replySent = 0;
  state.failedPasswords = 0;
  state.usernameAccepted = false;
  state.authenticated = false;
  state.closeAfterReply = false;
}

bool openPassive(FtpServer::Impl &state) {
  closeData(state);
  return networkStack()
      .openTcpServer("hotspot-ftp-data", state.dataPort, 1, state.passive,
                     kApAddress)
      .ok();
}

bool buildListing(FtpServer::Impl &state) {
  state.listing = static_cast<char *>(psram::allocate(kListCapacity));
  if (state.listing == nullptr) {
    return false;
  }
  state.listing[0] = '\0';
  size_t used = 0;
  File directory = SD_MMC.open(kShareRoot);
  if (!directory || !directory.isDirectory()) {
    directory.close();
    return false;
  }
  for (File entry = directory.openNextFile(); entry;
       entry = directory.openNextFile()) {
    const char *name = baseName(entry.name());
    if (!entry.isDirectory() && validShareName(name)) {
      const int written = snprintf(
          state.listing + used, kListCapacity - used,
          "-rw-r--r-- 1 pocketink pocketink %lu Jan 01 00:00 %s\r\n",
          static_cast<unsigned long>(entry.size()), name);
      if (written < 0 || static_cast<size_t>(written) >=
                             kListCapacity - used) {
        entry.close();
        directory.close();
        return false;
      }
      used += static_cast<size_t>(written);
    }
    entry.close();
  }
  directory.close();
  state.transferSize = used;
  state.transferOffset = 0;
  return true;
}

void beginTransfer(FtpServer::Impl &state, FtpServer::Impl::Operation operation,
                   uint32_t now) {
  state.operation = operation;
  state.dataDeadline = now + kDataTimeoutMs;
  state.transferOffset = 0;
  state.transferred = 0;
  state.sourceFinished = false;
  queueReply(state, "150 Opening passive data connection.\r\n");
}

void processCommand(FtpServer::Impl &state, uint32_t now) {
  char *argument = strchr(state.line, ' ');
  if (argument != nullptr) {
    *argument++ = '\0';
    while (*argument == ' ') {
      argument++;
    }
  }
  for (char *at = state.line; *at != '\0'; at++) {
    if (*at >= 'a' && *at <= 'z') {
      *at = static_cast<char>(*at - 'a' + 'A');
    }
  }
  state.controlDeadline =
      now + (state.authenticated ? kControlTimeoutMs : kLoginTimeoutMs);

  if (strcmp(state.line, "QUIT") == 0) {
    queueReply(state, "221 Goodbye.\r\n");
    state.closeAfterReply = true;
    return;
  }
  if (strcmp(state.line, "NOOP") == 0) {
    queueReply(state, "200 OK.\r\n");
    return;
  }
  if (strcmp(state.line, "SYST") == 0) {
    queueReply(state, "215 UNIX Type: L8\r\n");
    return;
  }
  if (strcmp(state.line, "FEAT") == 0) {
    queueReply(state, "211-Features\r\n EPSV\r\n PASV\r\n SIZE\r\n UTF8\r\n211 End\r\n");
    return;
  }
  if (strcmp(state.line, "OPTS") == 0 && argument != nullptr &&
      strcmp(argument, "UTF8 ON") == 0) {
    queueReply(state, "200 UTF8 enabled.\r\n");
    return;
  }
  if (strcmp(state.line, "AUTH") == 0 || strcmp(state.line, "PORT") == 0 ||
      strcmp(state.line, "EPRT") == 0) {
    queueReply(state, "502 Command disabled.\r\n");
    return;
  }
  if (strcmp(state.line, "USER") == 0) {
    state.authenticated = false;
    state.usernameAccepted =
        argument != nullptr && constantTimeEqual(argument, state.username);
    queueReply(state, "331 Password required.\r\n");
    return;
  }
  if (strcmp(state.line, "PASS") == 0) {
    const bool accepted = state.usernameAccepted && argument != nullptr &&
                          constantTimeEqual(argument, state.password);
    state.authenticated = accepted;
    if (accepted) {
      state.failedPasswords = 0;
      state.consecutiveAuthFailures = 0;
      state.controlDeadline = now + kControlTimeoutMs;
      queueReply(state, "230 Login successful.\r\n");
    } else {
      if (state.failedPasswords < UINT8_MAX) {
        state.failedPasswords++;
      }
      if (state.consecutiveAuthFailures < UINT8_MAX) {
        state.consecutiveAuthFailures++;
      }
      queueReply(state, "530 Login incorrect.\r\n");
      if (state.failedPasswords >= 3) {
        state.acceptAfter = now + 5000U;
        state.closeAfterReply = true;
      }
    }
    return;
  }
  if (!state.authenticated) {
    queueReply(state, "530 Please login.\r\n");
    return;
  }
  if (strcmp(state.line, "PWD") == 0) {
    queueReply(state, "257 \"/\" is the current directory.\r\n");
    return;
  }
  if (strcmp(state.line, "CWD") == 0) {
    if (argument == nullptr || strcmp(argument, "/") == 0 ||
        strcmp(argument, ".") == 0) {
      queueReply(state, "250 Directory changed.\r\n");
    } else {
      queueReply(state, "550 Flat share only.\r\n");
    }
    return;
  }
  if (strcmp(state.line, "TYPE") == 0) {
    queueReply(state, "200 Type set.\r\n");
    return;
  }
  if (strcmp(state.line, "PASV") == 0 || strcmp(state.line, "EPSV") == 0) {
    if (!openPassive(state)) {
      queueReply(state, "425 Cannot open passive listener.\r\n");
      return;
    }
    char reply[96] = {};
    if (strcmp(state.line, "EPSV") == 0) {
      snprintf(reply, sizeof(reply), "229 Entering Extended Passive Mode (|||%u|).\r\n",
               static_cast<unsigned>(state.dataPort));
    } else {
      snprintf(reply, sizeof(reply),
               "227 Entering Passive Mode (192,168,4,1,%u,%u).\r\n",
               static_cast<unsigned>(state.dataPort >> 8U),
               static_cast<unsigned>(state.dataPort & 0xffU));
    }
    queueReply(state, reply);
    state.dataDeadline = now + kDataTimeoutMs;
    return;
  }
  if (strcmp(state.line, "SIZE") == 0) {
    char path[96] = {};
    if (!sharePath(flatArgument(argument), path, sizeof(path))) {
      queueReply(state, "550 Invalid name.\r\n");
      return;
    }
    File file = SD_MMC.open(path);
    if (!file || file.isDirectory()) {
      file.close();
      queueReply(state, "550 File unavailable.\r\n");
      return;
    }
    char reply[48] = {};
    snprintf(reply, sizeof(reply), "213 %lu\r\n",
             static_cast<unsigned long>(file.size()));
    file.close();
    queueReply(state, reply);
    return;
  }
  if (state.passive == kInvalidSocket) {
    queueReply(state, "425 Use PASV or EPSV first.\r\n");
    return;
  }
  if (strcmp(state.line, "LIST") == 0) {
    if (!buildListing(state)) {
      closeData(state);
      queueReply(state, "451 Cannot list share.\r\n");
      return;
    }
    beginTransfer(state, FtpServer::Impl::Operation::List, now);
    return;
  }
  if (strcmp(state.line, "RETR") == 0) {
    char path[96] = {};
    if (!sharePath(flatArgument(argument), path, sizeof(path))) {
      queueReply(state, "550 Invalid name.\r\n");
      return;
    }
    state.transferFile = SD_MMC.open(path);
    if (!state.transferFile || state.transferFile.isDirectory()) {
      state.transferFile.close();
      state.transferFile = File();
      queueReply(state, "550 File unavailable.\r\n");
      return;
    }
    beginTransfer(state, FtpServer::Impl::Operation::Retrieve, now);
    return;
  }
  if (strcmp(state.line, "STOR") == 0) {
    char path[96] = {};
    char temporary[96] = {};
    if (!sharePath(flatArgument(argument), path, sizeof(path))) {
      queueReply(state, "550 Invalid name.\r\n");
      return;
    }
    if (SD_MMC.exists(path)) {
      queueReply(state, "553 Refusing overwrite.\r\n");
      return;
    }
    if (!temporaryShareUploadPath(ShareUploadKind::Ftp, temporary,
                                  sizeof(temporary))) {
      queueReply(state, "451 Cannot reserve upload.\r\n");
      return;
    }
    state.transferFile = SD_MMC.open(temporary, FILE_WRITE);
    if (!state.transferFile) {
      SD_MMC.remove(temporary);
      queueReply(state, "451 Cannot create file.\r\n");
      return;
    }
    strncpy(state.uploadPath, path, sizeof(state.uploadPath) - 1U);
    strncpy(state.uploadTempPath, temporary,
            sizeof(state.uploadTempPath) - 1U);
    beginTransfer(state, FtpServer::Impl::Operation::Store, now);
    return;
  }
  queueReply(state, "502 Command not implemented.\r\n");
}

void finishTransfer(FtpServer::Impl &state) {
  const bool stored = state.operation == FtpServer::Impl::Operation::Store;
  bool committed = true;
  if (stored) {
    state.transferFile.flush();
    state.transferFile.close();
    File staged = state.uploadTempPath[0] != '\0'
                      ? SD_MMC.open(state.uploadTempPath)
                      : File();
    const bool complete = staged && !staged.isDirectory() &&
                          staged.size() == state.transferred;
    staged.close();
    committed = state.uploadPath[0] != '\0' &&
                state.uploadTempPath[0] != '\0' &&
                complete &&
                !SD_MMC.exists(state.uploadPath) &&
                SD_MMC.rename(state.uploadTempPath, state.uploadPath);
    if (!committed && state.uploadTempPath[0] != '\0') {
      SD_MMC.remove(state.uploadTempPath);
    }
  }
  closeData(state, stored && committed);
  queueReply(state, committed ? "226 Transfer complete.\r\n"
                              : "451 Cannot commit upload.\r\n");
}

void failTransfer(FtpServer::Impl &state, const char *reply) {
  closeData(state);
  queueReply(state, reply);
}

void updateTransfer(FtpServer::Impl &state, uint32_t now) {
  if (state.operation == FtpServer::Impl::Operation::None) {
    if (state.passive != kInvalidSocket && expired(now, state.dataDeadline)) {
      closeData(state);
    }
    return;
  }
  if (expired(now, state.dataDeadline)) {
    failTransfer(state, "426 Data connection timed out.\r\n");
    return;
  }
  if (state.data == kInvalidSocket) {
    char peer[16] = {};
    SocketHandle accepted = kInvalidSocket;
    const SocketResult result =
        networkStack().accept(state.passive, accepted, peer, sizeof(peer));
    if (result.wouldBlock()) {
      return;
    }
    if (!result.ok()) {
      failTransfer(state, "425 Data connection failed.\r\n");
      return;
    }
    if (strcmp(peer, state.controlPeer) != 0) {
      networkStack().close(accepted);
      return;
    }
    state.data = accepted;
    networkStack().close(state.passive);
    state.passive = kInvalidSocket;
    state.dataDeadline = now + kDataTimeoutMs;
  }

  if (state.operation == FtpServer::Impl::Operation::Store) {
    size_t received = 0;
    const SocketResult result = networkStack().receive(
        state.data, state.transferBuffer, kTransferBufferSize, received);
    if (result.wouldBlock()) {
      return;
    }
    if (!result.ok()) {
      failTransfer(state, "426 Upload failed.\r\n");
      return;
    }
    if (received == 0) {
      finishTransfer(state);
      return;
    }
    if (received > kTransferBufferSize ||
        state.transferred > kMaximumUpload ||
        received > kMaximumUpload - state.transferred ||
        state.transferFile.write(state.transferBuffer, received) != received) {
      failTransfer(state, "552 Upload limit or storage failure.\r\n");
      return;
    }
    state.transferred += received;
    state.dataDeadline = now + kDataTimeoutMs;
    return;
  }

  if (state.transferOffset == state.transferSize && !state.sourceFinished) {
    state.transferOffset = 0;
    if (state.operation == FtpServer::Impl::Operation::List) {
      state.sourceFinished = true;
    } else {
      state.transferSize =
          state.transferFile.read(state.transferBuffer, kTransferBufferSize);
      state.sourceFinished = state.transferSize == 0;
    }
  }
  if (state.sourceFinished && state.transferOffset == state.transferSize) {
    finishTransfer(state);
    return;
  }
  const uint8_t *source =
      state.operation == FtpServer::Impl::Operation::List
          ? reinterpret_cast<const uint8_t *>(state.listing)
          : state.transferBuffer;
  if (source == nullptr || state.transferOffset > state.transferSize ||
      state.transferSize >
          (state.operation == FtpServer::Impl::Operation::List
               ? kListCapacity
               : kTransferBufferSize)) {
    failTransfer(state, "426 Invalid transfer state.\r\n");
    return;
  }
  const size_t remaining = state.transferSize - state.transferOffset;
  size_t sent = 0;
  const SocketResult result = networkStack().send(
      state.data, source + state.transferOffset,
      remaining, sent);
  if (result.wouldBlock()) {
    return;
  }
  if (!result.ok() || sent == 0 || sent > remaining) {
    failTransfer(state, "426 Download failed.\r\n");
    return;
  }
  state.transferOffset += sent;
  state.dataDeadline = now + kDataTimeoutMs;
}

} // namespace

FtpServer::~FtpServer() { stop(); }

bool FtpServer::start(uint16_t controlPort, uint16_t dataPort,
                      const char *username, const char *password) {
  if (impl != nullptr) {
    return impl->controlPort == controlPort && impl->dataPort == dataPort;
  }
  const size_t userLength = username != nullptr ? strnlen(username, 33) : 0;
  const size_t passwordLength = password != nullptr ? strnlen(password, 65) : 0;
  if (!tetheringActive() || controlPort == 0 || dataPort == 0 ||
      controlPort == dataPort || !validCredential(username, 1U, 32U) ||
      !validCredential(password, 8U, 64U) || !ensureShareRoot()) {
    return false;
  }
  void *memory = psram::allocate(sizeof(Impl));
  if (memory == nullptr) {
    return false;
  }
  Impl *created = new (memory) Impl();
  created->transferBuffer =
      static_cast<uint8_t *>(psram::allocate(kTransferBufferSize));
  if (created->transferBuffer == nullptr ||
      !networkStack()
           .openTcpServer("hotspot-ftp", controlPort, 1, created->listener,
                          kApAddress)
           .ok()) {
    psram::release(created->transferBuffer);
    created->~Impl();
    psram::release(created);
    return false;
  }
  memcpy(created->username, username, userLength + 1U);
  memcpy(created->password, password, passwordLength + 1U);
  created->controlPort = controlPort;
  created->dataPort = dataPort;
  impl = created;
  return true;
}

void FtpServer::stop() {
  if (impl == nullptr) {
    return;
  }
  closeControl(*impl);
  networkStack().close(impl->listener);
  memset(impl->password, 0, sizeof(impl->password));
  memset(impl->username, 0, sizeof(impl->username));
  memset(impl->transferBuffer, 0, kTransferBufferSize);
  psram::release(impl->transferBuffer);
  impl->~Impl();
  psram::release(impl);
  impl = nullptr;
}

void FtpServer::update(uint32_t nowMs) {
  if (impl == nullptr) {
    return;
  }
  Impl &state = *impl;
  if (state.control == kInvalidSocket) {
    if (!expired(nowMs, state.acceptAfter)) {
      return;
    }
    SocketHandle accepted = kInvalidSocket;
    char peer[16] = {};
    const SocketResult result = networkStack().accept(
        state.listener, accepted, peer, sizeof(peer));
    if (result.ok()) {
      state.control = accepted;
      memcpy(state.controlPeer, peer, sizeof(state.controlPeer));
      state.controlDeadline = nowMs + kLoginTimeoutMs;
      queueReply(state, "220 PocketInk FTP ready.\r\n");
    }
    return;
  }

  // Refuse parallel control sessions; one authenticated client bounds all
  // state and prevents competing writes to the shared SD root.
  SocketHandle extra = kInvalidSocket;
  if (networkStack().accept(state.listener, extra).ok()) {
    networkStack().close(extra);
  }

  if (state.replySent < state.replySize) {
    if (state.replySize > sizeof(state.reply) ||
        state.replySent > state.replySize) {
      closeControl(state);
      return;
    }
    const size_t remaining = state.replySize - state.replySent;
    size_t sent = 0;
    const SocketResult result = networkStack().send(
        state.control,
        reinterpret_cast<const uint8_t *>(state.reply) + state.replySent,
        remaining, sent);
    if ((!result.ok() && !result.wouldBlock()) || sent > remaining) {
      closeControl(state);
      return;
    }
    state.replySent += sent;
    if (state.replySent == state.replySize && state.closeAfterReply) {
      closeControl(state);
      return;
    }
    if (state.replySent < state.replySize) {
      return;
    }
  }

  updateTransfer(state, nowMs);
  if (state.operation != Impl::Operation::None ||
      state.replySent < state.replySize) {
    return;
  }
  if (expired(nowMs, state.controlDeadline)) {
    closeControl(state);
    return;
  }

  // Read at most one command per update so every response is serialized before
  // the next command mutates session state.
  for (uint16_t i = 0; i < sizeof(state.line); i++) {
    uint8_t byte = 0;
    size_t received = 0;
    const SocketResult result =
        networkStack().receive(state.control, &byte, 1, received);
    if (result.wouldBlock()) {
      return;
    }
    if (!result.ok() || received == 0) {
      closeControl(state);
      return;
    }
    if (byte == '\n') {
      if (state.lineUsed > 0 && state.line[state.lineUsed - 1U] == '\r') {
        state.lineUsed--;
      }
      state.line[state.lineUsed] = '\0';
      if (state.lineUsed > 0) {
        processCommand(state, nowMs);
      }
      memset(state.line, 0, sizeof(state.line));
      state.lineUsed = 0;
      return;
    }
    if (byte == 0 || (byte < 0x20 && byte != '\r') || byte == 0x7f ||
        state.lineUsed >= sizeof(state.line) - 1U) {
      queueReply(state, "500 Invalid command.\r\n");
      state.closeAfterReply = true;
      return;
    }
    state.line[state.lineUsed++] = static_cast<char>(byte);
  }
}

bool FtpServer::running() const {
  return impl != nullptr && impl->listener != kInvalidSocket;
}

uint16_t FtpServer::port() const { return impl != nullptr ? impl->controlPort : 0; }

} // namespace net
