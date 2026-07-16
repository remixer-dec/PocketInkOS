#include "sys/video/avi_encoder.h"

#include "sys/psram_allocator.h"

#include <cstring>

namespace {

constexpr uint32_t kHeaderBytes = 232;
constexpr uint32_t kRiffSizeOffset = 4;
constexpr uint32_t kTotalFramesOffset = 48;
constexpr uint32_t kStreamLengthOffset = 140;
constexpr uint32_t kMoviSizeOffset = 224;
constexpr uint32_t kIndexEntryBytes = 16;
constexpr uint32_t kIndexBatchEntries = 32;
constexpr uint32_t kInitialFrameSizeCapacity = 64;
constexpr size_t kMsrleWriteBatchBytes = 512;

void writeLe16(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8U);
}

void writeLe32(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8U);
  out[2] = static_cast<uint8_t>(value >> 16U);
  out[3] = static_cast<uint8_t>(value >> 24U);
}

void writeFourcc(uint8_t *out, const char *fourcc) {
  memcpy(out, fourcc, 4);
}

bool writeBytes(File &file, const uint8_t *bytes, size_t count) {
  return bytes != nullptr && file.write(bytes, count) == count;
}

uint32_t alignToWord(uint32_t bytes) { return (bytes + 1U) & ~1U; }

class MsrleWriter final {
public:
  explicit MsrleWriter(File *nextFile) : file(nextFile) {}

  void put(uint8_t value) {
    total++;
    if (file == nullptr || !ok) {
      return;
    }
    buffer[used++] = value;
    if (used == sizeof(buffer)) {
      flush();
    }
  }

  bool finish() {
    flush();
    return ok;
  }

  uint32_t size() const { return total; }

private:
  File *file = nullptr;
  uint8_t buffer[kMsrleWriteBatchBytes];
  uint32_t total = 0;
  size_t used = 0;
  bool ok = true;

  void flush() {
    if (file == nullptr || used == 0 || !ok) {
      return;
    }
    ok = writeBytes(*file, buffer, used);
    used = 0;
  }
};

uint8_t pixelAt(const uint8_t *row, uint32_t x) {
  return static_cast<uint8_t>((row[x >> 3U] >> (7U - (x & 7U))) & 1U);
}

uint32_t patternLength(const uint8_t *row, uint32_t x, uint32_t width) {
  const uint8_t first = pixelAt(row, x);
  const uint8_t second = x + 1U < width ? pixelAt(row, x + 1U) : first;
  uint32_t end = x + 1U;
  while (end < width &&
         pixelAt(row, end) == (((end - x) & 1U) != 0 ? second : first)) {
    end++;
  }
  return end - x;
}

void writePattern(MsrleWriter &writer, const uint8_t *row, uint32_t x,
                  uint32_t count) {
  writer.put(static_cast<uint8_t>(count));
  const uint8_t first = pixelAt(row, x);
  const uint8_t second = count > 1U ? pixelAt(row, x + 1U) : first;
  writer.put(static_cast<uint8_t>((first << 4U) | second));
}

void encodeMsrleRow(MsrleWriter &writer, const uint8_t *row,
                    uint32_t width) {
  uint32_t x = 0;
  while (x < width) {
    uint32_t pattern = patternLength(row, x, width);
    if (pattern >= 3U) {
      while (pattern != 0) {
        const uint32_t count = pattern < 255U ? pattern : 255U;
        writePattern(writer, row, x, count);
        x += count;
        pattern -= count;
      }
      continue;
    }

    const uint32_t literalStart = x;
    uint32_t literals = 0;
    while (x < width && literals < 255U) {
      pattern = patternLength(row, x, width);
      if (pattern >= 3U) {
        break;
      }
      const uint32_t room = 255U - literals;
      const uint32_t count = pattern < room ? pattern : room;
      x += count;
      literals += count;
    }

    if (literals < 3U) {
      writePattern(writer, row, literalStart, literals);
      continue;
    }

    writer.put(0);
    writer.put(static_cast<uint8_t>(literals));
    for (uint32_t i = 0; i < literals; i += 2U) {
      const uint8_t first = pixelAt(row, literalStart + i);
      const uint8_t second = i + 1U < literals
                                 ? pixelAt(row, literalStart + i + 1U)
                                 : 0;
      writer.put(static_cast<uint8_t>((first << 4U) | second));
    }
    if ((((literals + 1U) / 2U) & 1U) != 0) {
      writer.put(0);
    }
  }

  writer.put(0); // end of line
  writer.put(0);
}

bool encodeMsrleFrame(File *file, const uint8_t *framebuffer,
                      size_t sourceStrideBytes, uint16_t width,
                      uint16_t height, uint32_t &encodedBytes) {
  MsrleWriter writer(file);
  for (uint16_t outputRow = 0; outputRow < height; outputRow++) {
    const uint16_t sourceRow = height - 1U - outputRow;
    encodeMsrleRow(writer,
                   framebuffer + static_cast<size_t>(sourceRow) *
                                     sourceStrideBytes,
                   width);
  }
  writer.put(0); // end of bitmap
  writer.put(1);
  encodedBytes = writer.size();
  return writer.finish();
}

bool patchLe32(File &file, uint32_t offset, uint32_t value) {
  uint8_t bytes[4];
  writeLe32(bytes, value);
  return file.seek(offset) && writeBytes(file, bytes, sizeof(bytes));
}

} // namespace

namespace avi {

Encoder::~Encoder() { reset(); }

bool Encoder::begin(File &file, const Config &nextConfig) {
  if (open || !file || nextConfig.width == 0 || nextConfig.height == 0 ||
      nextConfig.framesPerSecond == 0 || nextConfig.width > INT16_MAX ||
      nextConfig.height > INT16_MAX ||
      (nextConfig.compression != Compression::Uncompressed &&
       nextConfig.compression != Compression::Msrle4)) {
    return fail(Error::InvalidArgument);
  }

  reset();
  config = nextConfig;
  packedRowBytes = (static_cast<uint32_t>(config.width) + 7U) / 8U;
  dibStride = ((static_cast<uint32_t>(config.width) + 31U) / 32U) * 4U;
  const bool msrle = config.compression == Compression::Msrle4;
  const uint64_t frameBytes64 = msrle
      ? (static_cast<uint64_t>(config.width) * 2U + 2U) * config.height + 2U
      : static_cast<uint64_t>(dibStride) * config.height;
  const uint64_t bytesPerSecond64 = frameBytes64 * config.framesPerSecond;
  if (frameBytes64 > UINT32_MAX || bytesPerSecond64 > UINT32_MAX) {
    return fail(Error::InvalidArgument);
  }
  maxFrameBytes = static_cast<uint32_t>(frameBytes64);

  if (!msrle) {
    // This is strict PSRAM: a missing external allocation must never consume
    // the small internal heap used by WiFi, TLS, and the display worker.
    stagingFrame = static_cast<uint8_t *>(psram::allocate(maxFrameBytes));
    if (stagingFrame == nullptr) {
      reset();
      return fail(Error::NoMemory);
    }
  }

  uint8_t header[kHeaderBytes] = {};
  writeFourcc(header, "RIFF");
  writeFourcc(header + 8, "AVI ");

  writeFourcc(header + 12, "LIST");
  writeLe32(header + 16, 200U);
  writeFourcc(header + 20, "hdrl");

  writeFourcc(header + 24, "avih");
  writeLe32(header + 28, 56U);
  writeLe32(header + 32, 1000000U / config.framesPerSecond);
  writeLe32(header + 36, static_cast<uint32_t>(bytesPerSecond64));
  writeLe32(header + 44, 0x10U); // AVIF_HASINDEX
  writeLe32(header + 56, 1U);    // one stream
  writeLe32(header + 60, maxFrameBytes);
  writeLe32(header + 64, config.width);
  writeLe32(header + 68, config.height);

  writeFourcc(header + 88, "LIST");
  writeLe32(header + 92, 124U);
  writeFourcc(header + 96, "strl");

  writeFourcc(header + 100, "strh");
  writeLe32(header + 104, 56U);
  writeFourcc(header + 108, "vids");
  writeFourcc(header + 112, msrle ? "MRLE" : "DIB ");
  writeLe32(header + 128, 1U); // dwScale
  writeLe32(header + 132, config.framesPerSecond); // dwRate
  writeLe32(header + 144, maxFrameBytes);
  writeLe32(header + 148, UINT32_MAX); // default quality
  writeLe16(header + 160, config.width);
  writeLe16(header + 162, config.height);

  writeFourcc(header + 164, "strf");
  writeLe32(header + 168, 48U);
  writeLe32(header + 172, 40U); // BITMAPINFOHEADER
  writeLe32(header + 176, config.width);
  writeLe32(header + 180, config.height); // positive: bottom-up rows
  writeLe16(header + 184, 1U);            // planes
  writeLe16(header + 186, msrle ? 4U : 1U); // bits per pixel
  writeLe32(header + 188, msrle ? 2U : 0U); // BI_RLE4 or BI_RGB
  writeLe32(header + 192, maxFrameBytes);
  writeLe32(header + 204, 2U);
  writeLe32(header + 208, 2U);
  writeLe32(header + 216, 0x00FFFFFFU); // palette: black, white

  writeFourcc(header + 220, "LIST");
  writeFourcc(header + 228, "movi");

  if (!writeBytes(file, header, sizeof(header))) {
    reset();
    return fail(Error::WriteFailed);
  }

  frames = 0;
  open = true;
  error = Error::None;
  return true;
}

bool Encoder::writeFrame(File &file, const uint8_t *framebuffer,
                         size_t framebufferSize,
                         size_t sourceStrideBytes) {
  if (!open) {
    return fail(Error::NotOpen);
  }
  if (!file || framebuffer == nullptr ||
      sourceStrideBytes < packedRowBytes) {
    return fail(Error::InvalidArgument);
  }
  const uint64_t requiredBytes =
      static_cast<uint64_t>(sourceStrideBytes) * config.height;
  if (requiredBytes > framebufferSize) {
    return fail(Error::FramebufferTooSmall);
  }

  uint32_t payloadBytes = maxFrameBytes;
  if (config.compression == Compression::Msrle4 &&
      !encodeMsrleFrame(nullptr, framebuffer, sourceStrideBytes,
                        config.width, config.height, payloadBytes)) {
    return fail(Error::WriteFailed);
  }

  const uint64_t recordBytes =
      static_cast<uint64_t>(alignToWord(payloadBytes)) + 8U;
  const uint64_t nextFrames = static_cast<uint64_t>(frames) + 1U;
  const uint64_t indexBytes = nextFrames * kIndexEntryBytes;
  const uint64_t nextMoviRecordBytes = moviRecordBytes + recordBytes;
  const uint64_t fileBytes =
      kHeaderBytes + nextMoviRecordBytes + 8U + indexBytes;
  if (nextFrames > UINT32_MAX || indexBytes > UINT32_MAX ||
      nextMoviRecordBytes + 4U > UINT32_MAX ||
      fileBytes - 8U > UINT32_MAX) {
    return fail(Error::SizeLimit);
  }

  if (config.compression == Compression::Msrle4 && !reserveFrameSize()) {
    return false;
  }

  uint8_t chunkHeader[8];
  writeFourcc(chunkHeader, config.compression == Compression::Msrle4
                               ? "00dc"
                               : "00db");
  writeLe32(chunkHeader + 4, payloadBytes);
  if (!writeBytes(file, chunkHeader, sizeof(chunkHeader))) {
    return fail(Error::WriteFailed);
  }

  if (config.compression == Compression::Msrle4) {
    uint32_t writtenBytes = 0;
    if (!encodeMsrleFrame(&file, framebuffer, sourceStrideBytes,
                          config.width, config.height, writtenBytes) ||
        writtenBytes != payloadBytes) {
      return fail(Error::WriteFailed);
    }
  } else {
    memset(stagingFrame, 0, maxFrameBytes);
    const uint8_t trailingBits = static_cast<uint8_t>(config.width & 7U);
    const uint8_t trailingMask = trailingBits == 0
                                     ? 0xFFU
                                     : static_cast<uint8_t>(
                                           0xFFU << (8U - trailingBits));
    for (uint16_t outputRow = 0; outputRow < config.height; outputRow++) {
      const uint16_t sourceRow = config.height - 1U - outputRow;
      uint8_t *target = stagingFrame +
                        static_cast<size_t>(outputRow) * dibStride;
      const uint8_t *source = framebuffer +
                              static_cast<size_t>(sourceRow) *
                                  sourceStrideBytes;
      memcpy(target, source, packedRowBytes);
      target[packedRowBytes - 1U] &= trailingMask;
    }
    if (!writeBytes(file, stagingFrame, maxFrameBytes)) {
      return fail(Error::WriteFailed);
    }
  }

  if ((payloadBytes & 1U) != 0) {
    const uint8_t padding = 0;
    if (!writeBytes(file, &padding, 1)) {
      return fail(Error::WriteFailed);
    }
  }

  if (frameSizes != nullptr) {
    frameSizes[frames] = payloadBytes;
  }
  moviRecordBytes = static_cast<uint32_t>(nextMoviRecordBytes);
  frames++;
  error = Error::None;
  return true;
}

bool Encoder::finish(File &file) {
  if (!open) {
    return fail(Error::NotOpen);
  }

  bool ok = static_cast<bool>(file);
  const uint64_t moviBytes64 = 4U + moviRecordBytes;
  const uint64_t indexBytes64 =
      static_cast<uint64_t>(frames) * kIndexEntryBytes;
  const uint64_t fileBytes64 =
      kHeaderBytes + moviRecordBytes + 8U + indexBytes64;
  if (moviBytes64 > UINT32_MAX || indexBytes64 > UINT32_MAX ||
      fileBytes64 - 8U > UINT32_MAX) {
    ok = false;
    error = Error::SizeLimit;
  }

  if (ok) {
    uint8_t indexHeader[8];
    writeFourcc(indexHeader, "idx1");
    writeLe32(indexHeader + 4, static_cast<uint32_t>(indexBytes64));
    ok = writeBytes(file, indexHeader, sizeof(indexHeader));
  }

  uint8_t indexBatch[kIndexBatchEntries * kIndexEntryBytes];
  uint32_t indexed = 0;
  uint32_t frameOffset = 4U;
  while (ok && indexed < frames) {
    const uint32_t count = frames - indexed < kIndexBatchEntries
                               ? frames - indexed
                               : kIndexBatchEntries;
    for (uint32_t i = 0; i < count; i++) {
      uint8_t *entry = indexBatch + i * kIndexEntryBytes;
      const uint32_t frame = indexed + i;
      const uint32_t payloadBytes = frameSizes != nullptr
                                        ? frameSizes[frame]
                                        : maxFrameBytes;
      writeFourcc(entry, frameSizes != nullptr ? "00dc" : "00db");
      writeLe32(entry + 4, 0x10U); // AVIIF_KEYFRAME
      writeLe32(entry + 8, frameOffset);
      writeLe32(entry + 12, payloadBytes);
      frameOffset += 8U + alignToWord(payloadBytes);
    }
    ok = writeBytes(file, indexBatch, count * kIndexEntryBytes);
    indexed += count;
  }

  if (ok) {
    ok = patchLe32(file, kRiffSizeOffset,
                   static_cast<uint32_t>(fileBytes64 - 8U)) &&
         patchLe32(file, kTotalFramesOffset, frames) &&
         patchLe32(file, kStreamLengthOffset, frames) &&
         patchLe32(file, kMoviSizeOffset,
                   static_cast<uint32_t>(moviBytes64));
  }

  psram::release(stagingFrame);
  stagingFrame = nullptr;
  psram::release(frameSizes);
  frameSizes = nullptr;
  frameSizeCapacity = 0;
  open = false;
  if (!ok) {
    return fail(error == Error::SizeLimit ? Error::SizeLimit
                                          : Error::WriteFailed);
  }
  error = Error::None;
  return true;
}

bool Encoder::isOpen() const { return open; }

Error Encoder::lastError() const { return error; }

uint32_t Encoder::frameCount() const { return frames; }

uint32_t Encoder::frameBytes() const { return maxFrameBytes; }

bool Encoder::fail(Error nextError) {
  error = nextError;
  return false;
}

bool Encoder::reserveFrameSize() {
  if (frames < frameSizeCapacity) {
    return true;
  }
  const uint32_t nextCapacity = frameSizeCapacity == 0
                                    ? kInitialFrameSizeCapacity
                                    : frameSizeCapacity * 2U;
  if (nextCapacity <= frameSizeCapacity ||
      nextCapacity > SIZE_MAX / sizeof(uint32_t)) {
    return fail(Error::SizeLimit);
  }
  void *next = psram::reallocate(
      frameSizes, static_cast<size_t>(nextCapacity) * sizeof(uint32_t));
  if (next == nullptr) {
    return fail(Error::NoMemory);
  }
  frameSizes = static_cast<uint32_t *>(next);
  frameSizeCapacity = nextCapacity;
  return true;
}

void Encoder::reset() {
  psram::release(stagingFrame);
  psram::release(frameSizes);
  stagingFrame = nullptr;
  frameSizes = nullptr;
  config = {};
  dibStride = 0;
  packedRowBytes = 0;
  maxFrameBytes = 0;
  moviRecordBytes = 0;
  frameSizeCapacity = 0;
  frames = 0;
  open = false;
}

} // namespace avi
