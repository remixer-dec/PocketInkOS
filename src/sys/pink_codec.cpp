#include "sys/pink_codec.h"

#include "fs/file_provider.h"
#include "sys/debug_logging.h"
#include "sys/psram_allocator.h"
#include "sys/services/sd_storage.h"

#include <Arduino.h>
#include <cstddef>
#include <cstring>

namespace {

constexpr uint16_t kCodecAbiVersion = 1;
constexpr size_t kMaxCodecs = 16;
constexpr size_t kCodecPathCapacity = 192;

enum CodecStatus : int8_t {
  CodecError = -1,
  CodecUnsupported = -2,
  CodecEnd = 0,
  CodecOk = 1,
};

struct CodecSource {
  void *user;
  size_t (*read)(void *, uint8_t *, size_t);
  bool (*seek)(void *, uint32_t);
  uint32_t (*tell)(void *);
  uint32_t (*size)(void *);
};

struct CodecHost {
  uint16_t abiVersion;
  uint16_t reserved;
  size_t hostSize;
  void *(*psramAlloc)(size_t);
  void (*psramFree)(void *);
  void (*log)(const char *);
};

struct CodecInfo {
  uint32_t sampleRate;
  uint32_t durationMs;
  uint32_t dataBytes;
  uint8_t channels;
  uint8_t bitsPerSample;
  uint8_t reserved[2];
  char title[64];
  char artist[64];
};

struct CodecApi {
  uint16_t abiVersion;
  uint16_t reserved;
  size_t apiSize;
  int8_t (*open)(const CodecHost *, const CodecSource *, const char *, void **,
                 CodecInfo *);
  size_t (*readPcm16)(void *, int16_t *, size_t);
  bool (*seekPcmFrame)(void *, uint32_t, uint32_t *);
  void (*close)(void *);
};

using CodecEntryPoint = const CodecApi *(*)();

struct InstalledCodec {
  char path[kCodecPathCapacity] = {};
  PinkLibraryManifest manifest = {};
};

InstalledCodec *installed = nullptr;
size_t installedCount = 0;

void setError(char *out, size_t outSize, const char *message) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  strncpy(out, message != nullptr ? message : "", outSize - 1U);
  out[outSize - 1U] = '\0';
}

bool extensionMatches(const char *list, const char *extension) {
  if (list == nullptr || extension == nullptr || extension[0] == '\0') {
    return false;
  }
  const char *cursor = list;
  while (*cursor != '\0') {
    while (*cursor == ',' || *cursor == ' ') {
      cursor++;
    }
    const char *end = cursor;
    while (*end != '\0' && *end != ',') {
      end++;
    }
    size_t length = static_cast<size_t>(end - cursor);
    while (length > 0 && cursor[length - 1U] == ' ') {
      length--;
    }
    if (length == strlen(extension) &&
        strncasecmp(cursor, extension, length) == 0) {
      return true;
    }
    cursor = end;
  }
  return false;
}

bool pinkFilename(const char *name) {
  const char *dot = name != nullptr ? strrchr(name, '.') : nullptr;
  return dot != nullptr && strcasecmp(dot, ".pink") == 0;
}

bool joinPath(char *out, size_t outSize, const char *directory,
              const char *name) {
  const int written = snprintf(out, outSize, "%s/%s", directory, name);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool scanCodec(const FileProviderEntry &entry, void *) {
  if (entry.directory || !pinkFilename(entry.name) ||
      installedCount >= kMaxCodecs) {
    return true;
  }
  char path[kCodecPathCapacity];
  if (!joinPath(path, sizeof(path), "/bin/codecs", entry.name)) {
    return true;
  }
  PinkLibraryManifest manifest;
  if (!pinkLibraryReadManifest(path, manifest) ||
      manifest.type != PINK_LIBRARY_TYPE_AUDIO_CODEC) {
    return true;
  }
  InstalledCodec &codec = installed[installedCount++];
  strncpy(codec.path, path, sizeof(codec.path) - 1U);
  codec.manifest = manifest;
  return true;
}

void sortInstalled() {
  for (size_t i = 1; i < installedCount; i++) {
    InstalledCodec value = installed[i];
    size_t j = i;
    while (j > 0 && strcmp(installed[j - 1U].path, value.path) > 0) {
      installed[j] = installed[j - 1U];
      j--;
    }
    installed[j] = value;
  }
}

size_t sourceRead(void *user, uint8_t *out, size_t count) {
  File *file = static_cast<File *>(user);
  return file != nullptr && *file ? file->read(out, count) : 0;
}

bool sourceSeek(void *user, uint32_t offset) {
  File *file = static_cast<File *>(user);
  return file != nullptr && *file && file->seek(offset, SeekSet);
}

uint32_t sourceTell(void *user) {
  File *file = static_cast<File *>(user);
  return file != nullptr && *file ? static_cast<uint32_t>(file->position()) : 0;
}

uint32_t sourceSize(void *user) {
  File *file = static_cast<File *>(user);
  return file != nullptr && *file ? static_cast<uint32_t>(file->size()) : 0;
}

void *hostPsramAlloc(size_t bytes) { return psram::allocate(bytes); }
void hostPsramFree(void *memory) { psram::release(memory); }

void hostLog(const char *message) {
  if (debugLoggingEnabled() && message != nullptr) {
    Serial.print("[pink-codec] ");
    Serial.println(message);
  }
}

CodecHost makeHost() {
  return {kCodecAbiVersion, 0, sizeof(CodecHost), hostPsramAlloc,
          hostPsramFree, hostLog};
}

} // namespace

void pinkCodecRefresh() {
  psram::release(installed);
  installed = nullptr;
  installedCount = 0;
  if (!sdStorageMounted()) {
    return;
  }
  installed = static_cast<InstalledCodec *>(
      psram::allocate(sizeof(InstalledCodec) * kMaxCodecs));
  if (installed == nullptr) {
    return;
  }
  for (size_t i = 0; i < kMaxCodecs; i++) {
    installed[i] = InstalledCodec{};
  }
  listProviderDirectory(defaultFileProvider(), "/bin/codecs", scanCodec,
                        nullptr);
  sortInstalled();
  if (installedCount == 0) {
    psram::release(installed);
    installed = nullptr;
  }
}

bool pinkCodecSupportsExtension(const char *extension) {
  for (size_t i = 0; i < installedCount; i++) {
    if (extensionMatches(installed[i].manifest.extensions, extension)) {
      return true;
    }
  }
  return false;
}

PinkAudioCodec::~PinkAudioCodec() { close(); }

bool PinkAudioCodec::open(File &file, const char *extension,
                          PinkAudioCodecInfo &info, char *error,
                          size_t errorSize) {
  close();
  info = {};
  setError(error, errorSize, "codec unavailable");
  CodecSource source{&file, sourceRead, sourceSeek, sourceTell, sourceSize};
  CodecHost host = makeHost();

  for (size_t i = 0; i < installedCount; i++) {
    if (!extensionMatches(installed[i].manifest.extensions, extension)) {
      continue;
    }
    if (!library.load(installed[i].path, PINK_LIBRARY_TYPE_AUDIO_CODEC, error,
                      errorSize)) {
      continue;
    }
    CodecEntryPoint entry =
        reinterpret_cast<CodecEntryPoint>(library.entryPoint());
    const CodecApi *candidate = entry != nullptr ? entry() : nullptr;
    if (candidate == nullptr || candidate->abiVersion != kCodecAbiVersion ||
        candidate->apiSize < sizeof(CodecApi) || candidate->open == nullptr ||
        candidate->readPcm16 == nullptr || candidate->close == nullptr) {
      library.unload();
      setError(error, errorSize, "invalid codec API");
      continue;
    }
    CodecInfo opened = {};
    void *openedDecoder = nullptr;
    const int8_t status = candidate->open(&host, &source, extension,
                                          &openedDecoder, &opened);
    if (status != CodecOk || openedDecoder == nullptr ||
        opened.sampleRate == 0 || opened.channels == 0 ||
        opened.channels > 2 || opened.bitsPerSample != 16) {
      if (openedDecoder != nullptr) {
        candidate->close(openedDecoder);
      }
      library.unload();
      setError(error, errorSize, status == CodecUnsupported
                                     ? "codec rejected format"
                                     : "codec open failed");
      continue;
    }
    api = candidate;
    decoder = openedDecoder;
    info.sampleRate = opened.sampleRate;
    info.durationMs = opened.durationMs;
    info.dataBytes = opened.dataBytes;
    info.channels = opened.channels;
    info.bitsPerSample = opened.bitsPerSample;
    memcpy(info.title, opened.title, sizeof(info.title));
    info.title[sizeof(info.title) - 1U] = '\0';
    memcpy(info.artist, opened.artist, sizeof(info.artist));
    info.artist[sizeof(info.artist) - 1U] = '\0';
    setError(error, errorSize, "");
    return true;
  }
  return false;
}

size_t PinkAudioCodec::readPcm16(int16_t *frames, size_t frameCapacity) {
  const CodecApi *codec = static_cast<const CodecApi *>(api);
  return codec != nullptr && decoder != nullptr && frames != nullptr
             ? codec->readPcm16(decoder, frames, frameCapacity)
             : 0;
}

bool PinkAudioCodec::seekPcmFrame(uint32_t targetFrame,
                                  uint32_t *actualFrame) {
  const CodecApi *codec = static_cast<const CodecApi *>(api);
  return codec != nullptr && decoder != nullptr &&
         codec->seekPcmFrame != nullptr &&
         codec->seekPcmFrame(decoder, targetFrame, actualFrame);
}

void PinkAudioCodec::close() {
  const CodecApi *codec = static_cast<const CodecApi *>(api);
  if (codec != nullptr && decoder != nullptr && codec->close != nullptr) {
    codec->close(decoder);
  }
  decoder = nullptr;
  api = nullptr;
  library.unload();
}
