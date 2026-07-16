#include "fs/image_render_cache.h"

#include "sys/psram_allocator.h"

#include <cmath>
#include <cstring>
#include <new>

namespace {

size_t repeatedBytes(const uint8_t *data, size_t size, size_t offset) {
  size_t length = 1;
  while (offset + length < size && length < 128U &&
         data[offset + length] == data[offset]) {
    length++;
  }
  return length;
}

size_t rleSize(const uint8_t *data, size_t size) {
  size_t encoded = 0;
  size_t offset = 0;
  while (offset < size) {
    const size_t repeated = repeatedBytes(data, size, offset);
    if (repeated >= 3U) {
      encoded += 2U;
      offset += repeated;
      continue;
    }

    const size_t literalStart = offset;
    while (offset < size && offset - literalStart < 128U) {
      if (repeatedBytes(data, size, offset) >= 3U) {
        break;
      }
      offset++;
    }
    if (offset == literalStart) {
      offset++;
    }
    encoded += 1U + offset - literalStart;
  }
  return encoded;
}

void encodeRle(const uint8_t *source, size_t size, uint8_t *output) {
  size_t inputOffset = 0;
  size_t outputOffset = 0;
  while (inputOffset < size) {
    const size_t repeated = repeatedBytes(source, size, inputOffset);
    if (repeated >= 3U) {
      output[outputOffset++] =
          static_cast<uint8_t>(0x80U | (repeated - 1U));
      output[outputOffset++] = source[inputOffset];
      inputOffset += repeated;
      continue;
    }

    const size_t literalStart = inputOffset;
    while (inputOffset < size && inputOffset - literalStart < 128U) {
      if (repeatedBytes(source, size, inputOffset) >= 3U) {
        break;
      }
      inputOffset++;
    }
    if (inputOffset == literalStart) {
      inputOffset++;
    }
    const size_t literalBytes = inputOffset - literalStart;
    output[outputOffset++] = static_cast<uint8_t>(literalBytes - 1U);
    memcpy(output + outputOffset, source + literalStart, literalBytes);
    outputOffset += literalBytes;
  }
}

bool identityTransform(const ImageTransform &transform) {
  return transform.a == 1.0f && transform.b == 0.0f &&
         transform.c == 0.0f && transform.d == 1.0f &&
         transform.tx == 0.0f && transform.ty == 0.0f;
}

bool transformPoint(const ImageTransform &transform, int32_t x, int32_t y,
                    int16_t &outX, int16_t &outY) {
  if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX) {
    return false;
  }
  const float transformedX =
      transform.a * static_cast<float>(x) +
      transform.c * static_cast<float>(y) + transform.tx;
  const float transformedY =
      transform.b * static_cast<float>(x) +
      transform.d * static_cast<float>(y) + transform.ty;
  if (!std::isfinite(transformedX) || !std::isfinite(transformedY) ||
      transformedX < INT16_MIN || transformedX > INT16_MAX ||
      transformedY < INT16_MIN || transformedY > INT16_MAX) {
    return false;
  }
  outX = static_cast<int16_t>(lroundf(transformedX));
  outY = static_cast<int16_t>(lroundf(transformedY));
  return true;
}

void drawPackedByte(Adafruit_GFX &gfx, uint8_t packed, uint16_t byteX,
                    uint16_t row, uint16_t width, int16_t originX,
                    int16_t originY, const ImageTransform &transform,
                    bool identity) {
  if (packed == 0U) {
    return;
  }
  const uint32_t firstX = static_cast<uint32_t>(byteX) * 8U;
  const uint8_t pixelCount =
      firstX + 8U <= width ? 8U : static_cast<uint8_t>(width - firstX);
  for (uint8_t bit = 0; bit < pixelCount; bit++) {
    if ((packed & (0x80U >> bit)) == 0U) {
      continue;
    }
    const int32_t sourceX = static_cast<int32_t>(originX) + firstX + bit;
    const int32_t sourceY = static_cast<int32_t>(originY) + row;
    if (identity) {
      if (sourceX >= INT16_MIN && sourceX <= INT16_MAX &&
          sourceY >= INT16_MIN && sourceY <= INT16_MAX) {
        gfx.drawPixel(static_cast<int16_t>(sourceX),
                      static_cast<int16_t>(sourceY), 1);
      }
      continue;
    }
    int16_t transformedX = 0;
    int16_t transformedY = 0;
    if (transformPoint(transform, sourceX, sourceY, transformedX,
                       transformedY)) {
      gfx.drawPixel(transformedX, transformedY, 1);
    }
  }
}

} // namespace

namespace {

struct ImageRenderCachePoolEntry {
  ImageRenderCache cache;
  uint32_t lastUsed = 0;
};

} // namespace

ImageRenderCache::~ImageRenderCache() { clear(); }

bool ImageRenderCache::begin(uint16_t nextWidth, uint16_t nextHeight,
                             uint64_t nextKey) {
  clear();
  if (nextWidth == 0U || nextHeight == 0U) {
    return false;
  }
  const size_t nextStride = (static_cast<size_t>(nextWidth) + 7U) / 8U;
  if (nextStride > SIZE_MAX / nextHeight) {
    return false;
  }
  const size_t nextBytes = nextStride * nextHeight;
  uint8_t *nextData = static_cast<uint8_t *>(psram::allocate(nextBytes));
  if (nextData == nullptr) {
    return false;
  }
  memset(nextData, 0, nextBytes);
  data = nextData;
  bytes = nextBytes;
  rawBytes = nextBytes;
  key = nextKey;
  width = nextWidth;
  height = nextHeight;
  stride = static_cast<uint16_t>(nextStride);
  return true;
}

void ImageRenderCache::setBlackPixel(int32_t x, int32_t y) {
  if (data == nullptr || valid || rle || x < 0 || y < 0 || x >= width ||
      y >= height) {
    return;
  }
  const size_t offset =
      static_cast<size_t>(y) * stride + static_cast<size_t>(x) / 8U;
  data[offset] |= static_cast<uint8_t>(0x80U >> (x & 7));
}

bool ImageRenderCache::finish() {
  if (data == nullptr || rawBytes == 0U || valid) {
    return valid;
  }

  const size_t encodedBytes = rleSize(data, rawBytes);
  // Keep packed rows unless RLE saves at least 12.5%. Small wins are not worth
  // adding token decoding to every replay.
  if (encodedBytes < rawBytes &&
      encodedBytes + rawBytes / 8U <= rawBytes) {
    uint8_t *encoded = static_cast<uint8_t *>(psram::allocate(encodedBytes));
    if (encoded != nullptr) {
      encodeRle(data, rawBytes, encoded);
      psram::release(data);
      data = encoded;
      bytes = encodedBytes;
      rle = true;
    }
  }
  valid = true;
  return true;
}

bool ImageRenderCache::draw(Adafruit_GFX &gfx, int16_t originX,
                            int16_t originY,
                            const ImageTransform &transform) const {
  if (!valid || data == nullptr || stride == 0U) {
    return false;
  }
  const bool identity = identityTransform(transform);
  uint16_t byteX = 0;
  uint16_t row = 0;
  size_t decodedBytes = 0;
  const auto drawByte = [&](uint8_t packed) {
    if (row < height) {
      drawPackedByte(gfx, packed, byteX, row, width, originX, originY,
                     transform, identity);
    }
    decodedBytes++;
    byteX++;
    if (byteX == stride) {
      byteX = 0;
      row++;
    }
  };

  if (!rle) {
    for (size_t i = 0; i < bytes; i++) {
      drawByte(data[i]);
    }
    return decodedBytes == rawBytes;
  }

  size_t offset = 0;
  while (offset < bytes && decodedBytes < rawBytes) {
    const uint8_t token = data[offset++];
    const size_t length = static_cast<size_t>(token & 0x7fU) + 1U;
    if (length > rawBytes - decodedBytes) {
      return false;
    }
    if ((token & 0x80U) != 0U) {
      if (offset >= bytes) {
        return false;
      }
      const uint8_t repeated = data[offset++];
      for (size_t i = 0; i < length; i++) {
        drawByte(repeated);
      }
      continue;
    }
    if (length > bytes - offset) {
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      drawByte(data[offset++]);
    }
  }
  return offset == bytes && decodedBytes == rawBytes;
}

bool ImageRenderCache::visitBlackPixels(PixelVisitor visitor,
                                        void *context) const {
  if (!valid || data == nullptr || stride == 0U || visitor == nullptr) {
    return false;
  }
  uint16_t byteX = 0;
  uint16_t row = 0;
  size_t decodedBytes = 0;
  const auto visitByte = [&](uint8_t packed) {
    if (row < height && packed != 0U) {
      const uint16_t firstX = static_cast<uint16_t>(byteX * 8U);
      const uint8_t count =
          firstX + 8U <= width ? 8U : static_cast<uint8_t>(width - firstX);
      for (uint8_t bit = 0; bit < count; bit++) {
        if ((packed & (0x80U >> bit)) != 0U) {
          visitor(context, static_cast<uint16_t>(firstX + bit), row);
        }
      }
    }
    decodedBytes++;
    byteX++;
    if (byteX == stride) {
      byteX = 0;
      row++;
    }
  };

  if (!rle) {
    for (size_t i = 0; i < bytes; i++) {
      visitByte(data[i]);
    }
    return decodedBytes == rawBytes;
  }

  size_t offset = 0;
  while (offset < bytes && decodedBytes < rawBytes) {
    const uint8_t token = data[offset++];
    const size_t length = static_cast<size_t>(token & 0x7fU) + 1U;
    if (length > rawBytes - decodedBytes) {
      return false;
    }
    if ((token & 0x80U) != 0U) {
      if (offset >= bytes) {
        return false;
      }
      const uint8_t repeated = data[offset++];
      for (size_t i = 0; i < length; i++) {
        visitByte(repeated);
      }
    } else {
      if (length > bytes - offset) {
        return false;
      }
      for (size_t i = 0; i < length; i++) {
        visitByte(data[offset++]);
      }
    }
  }
  return offset == bytes && decodedBytes == rawBytes;
}

bool ImageRenderCache::copyRegion(ImageRenderCache &output, uint16_t sourceX,
                                  uint16_t sourceY, uint16_t regionWidth,
                                  uint16_t regionHeight,
                                  uint64_t outputKey) const {
  if (!valid || regionWidth == 0U || regionHeight == 0U || sourceX >= width ||
      sourceY >= height || regionWidth > width - sourceX ||
      regionHeight > height - sourceY ||
      !output.begin(regionWidth, regionHeight, outputKey)) {
    return false;
  }
  struct CopyContext {
    ImageRenderCache *output;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
  } context{&output, sourceX, sourceY, regionWidth, regionHeight};
  const auto copyPixel = [](void *raw, uint16_t x, uint16_t y) {
    CopyContext *copy = static_cast<CopyContext *>(raw);
    if (x >= copy->x && y >= copy->y && x - copy->x < copy->width &&
        y - copy->y < copy->height) {
      copy->output->setBlackPixel(x - copy->x, y - copy->y);
    }
  };
  if (!visitBlackPixels(copyPixel, &context) || !output.finish()) {
    output.clear();
    return false;
  }
  return true;
}

bool ImageRenderCache::rasterize(ImageRenderCache &output,
                                 uint8_t transformFlags, uint8_t scale,
                                 uint64_t outputKey) const {
  if (!valid || scale == 0U || scale > 4U || transformFlags > 15U) {
    return false;
  }
  const uint8_t turns = static_cast<uint8_t>((transformFlags >> 2U) & 3U);
  const bool swapsAxes = (turns & 1U) != 0U;
  const uint32_t outputWidth =
      static_cast<uint32_t>(swapsAxes ? height : width) * scale;
  const uint32_t outputHeight =
      static_cast<uint32_t>(swapsAxes ? width : height) * scale;
  if (outputWidth == 0U || outputHeight == 0U || outputWidth > UINT16_MAX ||
      outputHeight > UINT16_MAX ||
      !output.begin(static_cast<uint16_t>(outputWidth),
                    static_cast<uint16_t>(outputHeight), outputKey)) {
    return false;
  }
  struct TransformContext {
    ImageRenderCache *output;
    uint16_t width;
    uint16_t height;
    uint8_t flags;
    uint8_t scale;
  } context{&output, width, height, transformFlags, scale};
  const auto transformPixel = [](void *raw, uint16_t sourceX,
                                 uint16_t sourceY) {
    TransformContext *transform = static_cast<TransformContext *>(raw);
    uint16_t x = (transform->flags & 1U) != 0U
                     ? static_cast<uint16_t>(transform->width - 1U - sourceX)
                     : sourceX;
    uint16_t y = (transform->flags & 2U) != 0U
                     ? static_cast<uint16_t>(transform->height - 1U - sourceY)
                     : sourceY;
    uint16_t rotatedX = x;
    uint16_t rotatedY = y;
    switch ((transform->flags >> 2U) & 3U) {
    case 1:
      rotatedX = static_cast<uint16_t>(transform->height - 1U - y);
      rotatedY = x;
      break;
    case 2:
      rotatedX = static_cast<uint16_t>(transform->width - 1U - x);
      rotatedY = static_cast<uint16_t>(transform->height - 1U - y);
      break;
    case 3:
      rotatedX = y;
      rotatedY = static_cast<uint16_t>(transform->width - 1U - x);
      break;
    default:
      break;
    }
    const uint16_t scaledX =
        static_cast<uint16_t>(rotatedX * transform->scale);
    const uint16_t scaledY =
        static_cast<uint16_t>(rotatedY * transform->scale);
    for (uint8_t dy = 0; dy < transform->scale; dy++) {
      for (uint8_t dx = 0; dx < transform->scale; dx++) {
        transform->output->setBlackPixel(scaledX + dx, scaledY + dy);
      }
    }
  };
  if (!visitBlackPixels(transformPixel, &context) || !output.finish()) {
    output.clear();
    return false;
  }
  return true;
}

void ImageRenderCache::clear() {
  psram::release(data);
  data = nullptr;
  bytes = 0;
  rawBytes = 0;
  key = 0;
  width = 0;
  height = 0;
  stride = 0;
  valid = false;
  rle = false;
}

bool ImageRenderCache::matches(uint16_t expectedWidth,
                               uint16_t expectedHeight,
                               uint64_t expectedKey) const {
  return valid && width == expectedWidth && height == expectedHeight &&
         key == expectedKey;
}

ImageRenderCachePool::~ImageRenderCachePool() { clear(); }

bool ImageRenderCachePool::setCapacity(uint8_t requestedEntries) {
  static const uint8_t kMaxEntries = 32;
  if (requestedEntries > kMaxEntries) {
    return false;
  }
  if (requestedEntries == entryCapacity) {
    return true;
  }
  if (requestedEntries == 0U) {
    clear();
    return true;
  }
  if (sizeof(ImageRenderCachePoolEntry) >
      SIZE_MAX / static_cast<size_t>(requestedEntries)) {
    return false;
  }
  void *nextStorage = psram::allocate(
      sizeof(ImageRenderCachePoolEntry) * requestedEntries);
  if (nextStorage == nullptr) {
    return false;
  }
  ImageRenderCachePoolEntry *nextEntries =
      static_cast<ImageRenderCachePoolEntry *>(nextStorage);
  for (uint8_t i = 0; i < requestedEntries; i++) {
    new (&nextEntries[i]) ImageRenderCachePoolEntry();
  }
  clear();
  entries = nextEntries;
  entryCapacity = requestedEntries;
  return true;
}

ImageRenderCache *ImageRenderCachePool::find(uint16_t width, uint16_t height,
                                             uint64_t key) {
  ImageRenderCachePoolEntry *poolEntries =
      static_cast<ImageRenderCachePoolEntry *>(entries);
  if (poolEntries == nullptr) {
    return nullptr;
  }
  clock++;
  if (clock == 0U) {
    clock = 1U;
    for (uint8_t i = 0; i < entryCapacity; i++) {
      poolEntries[i].lastUsed = 0;
    }
  }
  for (uint8_t i = 0; i < entryCapacity; i++) {
    if (poolEntries[i].cache.matches(width, height, key)) {
      poolEntries[i].lastUsed = clock;
      return &poolEntries[i].cache;
    }
  }
  return nullptr;
}

ImageRenderCache *ImageRenderCachePool::acquire() {
  ImageRenderCachePoolEntry *poolEntries =
      static_cast<ImageRenderCachePoolEntry *>(entries);
  if (poolEntries == nullptr || entryCapacity == 0U) {
    return nullptr;
  }
  clock++;
  if (clock == 0U) {
    clock = 1U;
    for (uint8_t i = 0; i < entryCapacity; i++) {
      poolEntries[i].lastUsed = 0;
    }
  }
  uint8_t victim = 0;
  for (uint8_t i = 0; i < entryCapacity; i++) {
    if (!poolEntries[i].cache.ready()) {
      victim = i;
      break;
    }
    if (poolEntries[i].lastUsed < poolEntries[victim].lastUsed) {
      victim = i;
    }
  }
  poolEntries[victim].cache.clear();
  poolEntries[victim].lastUsed = clock;
  return &poolEntries[victim].cache;
}

const ImageRenderCache *ImageRenderCachePool::onlyEntry() const {
  const ImageRenderCachePoolEntry *poolEntries =
      static_cast<const ImageRenderCachePoolEntry *>(entries);
  return poolEntries != nullptr && entryCapacity == 1U &&
                 poolEntries[0].cache.ready()
             ? &poolEntries[0].cache
             : nullptr;
}

void ImageRenderCachePool::clear() {
  ImageRenderCachePoolEntry *poolEntries =
      static_cast<ImageRenderCachePoolEntry *>(entries);
  if (poolEntries != nullptr) {
    for (uint8_t i = 0; i < entryCapacity; i++) {
      poolEntries[i].~ImageRenderCachePoolEntry();
    }
  }
  psram::release(entries);
  entries = nullptr;
  clock = 0;
  entryCapacity = 0;
}
