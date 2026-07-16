/// Minimal streaming AVI encoder for packed one-bit display frames.
#ifndef SYS_VIDEO_AVI_ENCODER_H
#define SYS_VIDEO_AVI_ENCODER_H

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

namespace avi {

enum class Compression : uint8_t {
  Uncompressed,
  Msrle4,
};

enum class Error : uint8_t {
  None,
  InvalidArgument,
  NoMemory,
  FramebufferTooSmall,
  SizeLimit,
  WriteFailed,
  NotOpen,
};

struct Config {
  uint16_t width = 200;
  uint16_t height = 200;
  uint16_t framesPerSecond = 2;
  Compression compression = Compression::Uncompressed;
};

/// Writes a two-color AVI from packed one-bit display frames.
///
/// Uncompressed frames use a PSRAM staging buffer. MSRLE4 frames are encoded in
/// small sequential batches and retain only their sizes for the final index.
class Encoder final {
public:
  Encoder() = default;
  Encoder(const Encoder &) = delete;
  Encoder &operator=(const Encoder &) = delete;
  ~Encoder();

  /// Initializes metadata and writes the placeholder AVI header to `file`.
  bool begin(File &file, const Config &config);

  /// Appends a top-down, packed one-bit frame. The source uses MSB-first pixels
  /// with zero=black and one=white, matching the display framebuffer.
  bool writeFrame(File &file, const uint8_t *framebuffer,
                  size_t framebufferSize, size_t sourceStrideBytes);

  /// Writes idx1, patches sizes/counts, and releases working memory.
  bool finish(File &file);

  bool isOpen() const;
  Error lastError() const;
  uint32_t frameCount() const;
  /// Returns the maximum encoded payload size for the configured format.
  uint32_t frameBytes() const;

private:
  uint8_t *stagingFrame = nullptr;
  uint32_t *frameSizes = nullptr;
  uint32_t dibStride = 0;
  uint32_t packedRowBytes = 0;
  uint32_t maxFrameBytes = 0;
  uint32_t moviRecordBytes = 0;
  uint32_t frameSizeCapacity = 0;
  uint32_t frames = 0;
  Config config = {};
  Error error = Error::None;
  bool open = false;

  bool fail(Error nextError);
  bool reserveFrameSize();
  void reset();
};

} // namespace avi

#endif
