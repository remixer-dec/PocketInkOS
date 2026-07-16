#ifndef IMAGE_RENDER_CACHE_H
#define IMAGE_RENDER_CACHE_H

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

/// Row-major affine transform applied to rendered image pixels.
///
/// x' = a*x + c*y + tx
/// y' = b*x + d*y + ty
struct ImageTransform {
  float a = 1.0f;
  float b = 0.0f;
  float c = 0.0f;
  float d = 1.0f;
  float tx = 0.0f;
  float ty = 0.0f;
};

/// Reusable pre-transform monochrome render stored in strict PSRAM.
///
/// Pixels are always packed at one bit per pixel. Packed-byte RLE is selected
/// after rendering only when it materially reduces the final allocation.
class ImageRenderCache {
public:
  ImageRenderCache() = default;
  ~ImageRenderCache();

  ImageRenderCache(const ImageRenderCache &) = delete;
  ImageRenderCache &operator=(const ImageRenderCache &) = delete;

  /// Allocates an empty cache sized from the requested render rectangle.
  bool begin(uint16_t width, uint16_t height, uint64_t key);
  /// Records one black pixel in render-local coordinates.
  void setBlackPixel(int32_t x, int32_t y);
  /// Finalizes the cache and applies adaptive RLE when worthwhile.
  bool finish();
  /// Replays the cached pre-transform pixels into the destination.
  bool draw(Adafruit_GFX &gfx, int16_t x, int16_t y,
            const ImageTransform &transform) const;
  /// Copies a packed rectangle into a separate ready cache.
  bool copyRegion(ImageRenderCache &output, uint16_t x, uint16_t y,
                  uint16_t regionWidth, uint16_t regionHeight,
                  uint64_t outputKey) const;
  /// Rasterizes flips, quarter-turn rotation, and integer scaling once.
  bool rasterize(ImageRenderCache &output, uint8_t transformFlags,
                 uint8_t scale, uint64_t outputKey) const;
  /// Releases all cache-owned PSRAM.
  void clear();

  bool matches(uint16_t width, uint16_t height, uint64_t key) const;
  bool ready() const { return valid; }
  bool writable() const { return data != nullptr && !valid; }
  bool rleEncoded() const { return valid && rle; }
  size_t storageBytes() const { return valid ? bytes : 0; }
  size_t packedBytes() const { return rawBytes; }
  uint16_t cachedWidth() const { return width; }
  uint16_t cachedHeight() const { return height; }

private:
  typedef void (*PixelVisitor)(void *context, uint16_t x, uint16_t y);
  bool visitBlackPixels(PixelVisitor visitor, void *context) const;

  uint8_t *data = nullptr;
  size_t bytes = 0;
  size_t rawBytes = 0;
  uint64_t key = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t stride = 0;
  bool valid = false;
  bool rle = false;
};

/// Dynamically sized LRU collection of rendered-image caches.
class ImageRenderCachePool {
public:
  ImageRenderCachePool() = default;
  ~ImageRenderCachePool();

  ImageRenderCachePool(const ImageRenderCachePool &) = delete;
  ImageRenderCachePool &operator=(const ImageRenderCachePool &) = delete;

  /// Sets the maximum number of separately cached images. Zero disables it.
  bool setCapacity(uint8_t entries);
  ImageRenderCache *find(uint16_t width, uint16_t height, uint64_t key);
  ImageRenderCache *acquire();
  /// Returns the ready entry when this pool is configured for one image.
  const ImageRenderCache *onlyEntry() const;
  void clear();

  uint8_t capacity() const { return entryCapacity; }

private:
  void *entries = nullptr;
  uint32_t clock = 0;
  uint8_t entryCapacity = 0;
};

#endif
