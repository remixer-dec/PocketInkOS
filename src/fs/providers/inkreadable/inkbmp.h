#ifndef INKREADABLE_INKBMP_H
#define INKREADABLE_INKBMP_H

#include <stddef.h>
#include <stdint.h>

#ifndef IBMP_MALLOC
#include <stdlib.h>
#define IBMP_MALLOC malloc
#endif

#ifndef IBMP_FREE
#include <stdlib.h>
#define IBMP_FREE free
#endif

#ifndef IBMP_COOPERATE
#define IBMP_COOPERATE()
#endif

typedef enum {
  IBMP_OK = 0,
  IBMP_ERR_FORMAT,
  IBMP_ERR_UNSUPPORTED,
  IBMP_ERR_MEMORY,
  IBMP_ERR_IO,
} ibmp_result;

typedef bool (*ibmp_read_at_fn)(void *user, size_t offset, uint8_t *out,
                                size_t count);
typedef void (*ibmp_pixel_fn)(void *user, int16_t x, int16_t y);

typedef struct {
  void *user;
  size_t size;
  ibmp_read_at_fn read_at;
} ibmp_reader;

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t pixel_offset;
  uint32_t palette_offset;
  uint32_t row_bytes;
  uint16_t bits_per_pixel;
  uint16_t palette_entries;
  bool top_down;
} ibmp_info;

typedef struct {
  void *user;
  ibmp_pixel_fn pixel;
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
  uint8_t dither;
  bool scale_to_fit;
} ibmp_render;

static uint16_t ibmp_le16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         (static_cast<uint16_t>(bytes[1]) << 8U);
}

static uint32_t ibmp_le32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8U) |
         (static_cast<uint32_t>(bytes[2]) << 16U) |
         (static_cast<uint32_t>(bytes[3]) << 24U);
}

static ibmp_result ibmp_read_info(const ibmp_reader *reader, ibmp_info *info) {
  uint8_t header[54] = {};
  if (reader == nullptr || info == nullptr || reader->read_at == nullptr ||
      !reader->read_at(reader->user, 0, header, sizeof(header))) {
    return IBMP_ERR_IO;
  }
  const uint32_t dib_size = ibmp_le32(header + 14);
  if (header[0] != 'B' || header[1] != 'M' || dib_size < 40U ||
      ibmp_le16(header + 26) != 1U) {
    return IBMP_ERR_FORMAT;
  }
  if (ibmp_le32(header + 30) != 0U) {
    return IBMP_ERR_UNSUPPORTED;
  }

  const int32_t signed_width = static_cast<int32_t>(ibmp_le32(header + 18));
  const int32_t signed_height = static_cast<int32_t>(ibmp_le32(header + 22));
  const uint16_t bits = ibmp_le16(header + 28);
  if (signed_width <= 0 || signed_height == 0 ||
      signed_height == INT32_MIN) {
    return IBMP_ERR_FORMAT;
  }
  if (bits != 1U && bits != 4U && bits != 8U && bits != 24U &&
      bits != 32U) {
    return IBMP_ERR_UNSUPPORTED;
  }

  *info = {};
  info->width = static_cast<uint32_t>(signed_width);
  info->height = static_cast<uint32_t>(signed_height < 0 ? -signed_height
                                                         : signed_height);
  if (info->width > 32768U || info->height > 32768U) {
    return IBMP_ERR_UNSUPPORTED;
  }
  info->top_down = signed_height < 0;
  info->bits_per_pixel = bits;
  info->pixel_offset = ibmp_le32(header + 10);
  const uint64_t palette_offset = 14ULL + dib_size;
  if (palette_offset > reader->size || palette_offset > UINT32_MAX) {
    return IBMP_ERR_FORMAT;
  }
  info->palette_offset = static_cast<uint32_t>(palette_offset);
  if (palette_offset > info->pixel_offset) {
    return IBMP_ERR_FORMAT;
  }
  const uint64_t row_bits =
      static_cast<uint64_t>(info->width) * info->bits_per_pixel;
  info->row_bytes =
      static_cast<uint32_t>(((row_bits + 31U) / 32U) * 4U);
  if (info->row_bytes == 0U || info->row_bytes > 256U * 1024U) {
    return IBMP_ERR_UNSUPPORTED;
  }
  if (bits <= 8U) {
    const uint32_t used = ibmp_le32(header + 46);
    const uint32_t maximum = 1UL << bits;
    info->palette_entries =
        static_cast<uint16_t>(used == 0U || used > maximum ? maximum : used);
    const uint64_t palette_end =
        palette_offset + static_cast<uint64_t>(info->palette_entries) * 4U;
    if (palette_end > info->pixel_offset || palette_end > reader->size) {
      return IBMP_ERR_FORMAT;
    }
  }
  const uint64_t pixel_bytes =
      static_cast<uint64_t>(info->row_bytes) * info->height;
  const uint64_t pixel_end =
      static_cast<uint64_t>(info->pixel_offset) + pixel_bytes;
  if (info->pixel_offset < sizeof(header) || pixel_end > reader->size ||
      pixel_end > SIZE_MAX) {
    return IBMP_ERR_FORMAT;
  }
  return IBMP_OK;
}

static uint8_t ibmp_luminance(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint8_t>((static_cast<uint16_t>(red) * 77U +
                               static_cast<uint16_t>(green) * 150U +
                               static_cast<uint16_t>(blue) * 29U) >>
                              8U);
}

static uint8_t ibmp_threshold(uint8_t dither, int16_t x, int16_t y) {
  static const uint8_t bayer4[16] = {
      0,  8,  2,  10, 12, 4,  14, 6,
      3,  11, 1,  9,  15, 7,  13, 5,
  };
  if ((dither & 3U) == 0U) {
    return 128;
  }
  const uint8_t cell = bayer4[((y & 3) << 2) | (x & 3)];
  const uint8_t spread = (dither & 3U) == 1U ? 12U : 16U;
  return static_cast<uint8_t>(8U + cell * spread);
}

static ibmp_result ibmp_decode(const ibmp_reader *reader,
                               const ibmp_render *render,
                               ibmp_info *decoded_info = nullptr) {
  if (reader == nullptr || render == nullptr || render->pixel == nullptr ||
      render->width <= 0 || render->height <= 0) {
    return IBMP_ERR_FORMAT;
  }
  const int32_t render_right =
      static_cast<int32_t>(render->x) + render->width;
  const int32_t render_bottom =
      static_cast<int32_t>(render->y) + render->height;
  if (render_right > INT16_MAX || render_bottom > INT16_MAX) {
    return IBMP_ERR_UNSUPPORTED;
  }
  ibmp_info info;
  const ibmp_result info_result = ibmp_read_info(reader, &info);
  if (info_result != IBMP_OK) {
    return info_result;
  }
  if (decoded_info != nullptr) {
    *decoded_info = info;
  }

  uint8_t *row = static_cast<uint8_t *>(IBMP_MALLOC(info.row_bytes));
  uint8_t *palette =
      info.palette_entries == 0
          ? nullptr
          : static_cast<uint8_t *>(IBMP_MALLOC(info.palette_entries));
  if (row == nullptr || (info.palette_entries != 0 && palette == nullptr)) {
    IBMP_FREE(row);
    IBMP_FREE(palette);
    return IBMP_ERR_MEMORY;
  }
  for (uint16_t i = 0; i < info.palette_entries; i++) {
    uint8_t color[4];
    const size_t color_offset =
        static_cast<size_t>(info.palette_offset) +
        static_cast<size_t>(i) * 4U;
    if (!reader->read_at(reader->user, color_offset, color, sizeof(color))) {
      IBMP_FREE(row);
      IBMP_FREE(palette);
      return IBMP_ERR_IO;
    }
    palette[i] = ibmp_luminance(color[2], color[1], color[0]);
  }
  bool monochrome = info.bits_per_pixel == 1U;
  if (info.bits_per_pixel == 4U || info.bits_per_pixel == 8U) {
    monochrome = true;
    for (uint16_t i = 0; i < info.palette_entries; i++) {
      if (palette[i] != 0U && palette[i] != 255U) {
        monochrome = false;
        break;
      }
    }
  }

  int32_t output_width = static_cast<int32_t>(info.width);
  int32_t output_height = static_cast<int32_t>(info.height);
  if (render->scale_to_fit &&
      (output_width > render->width || output_height > render->height)) {
    if (static_cast<uint64_t>(info.width) * render->height >=
        static_cast<uint64_t>(info.height) * render->width) {
      output_width = render->width;
      output_height = static_cast<int32_t>(
          static_cast<uint64_t>(info.height) * render->width / info.width);
    } else {
      output_height = render->height;
      output_width = static_cast<int32_t>(
          static_cast<uint64_t>(info.width) * render->height / info.height);
    }
  }
  if (output_width <= 0 || output_height <= 0) {
    IBMP_FREE(row);
    IBMP_FREE(palette);
    return IBMP_ERR_FORMAT;
  }

  const int32_t origin_x =
      render->x + (render->width - output_width) / 2;
  const int32_t origin_y =
      render->y + (render->height - output_height) / 2;
  const int32_t visible_left =
      origin_x < render->x ? render->x - origin_x : 0;
  const int32_t visible_top =
      origin_y < render->y ? render->y - origin_y : 0;
  const int32_t visible_right =
      output_width < render->width - (origin_x - render->x)
          ? output_width
          : render->width - (origin_x - render->x);
  const int32_t visible_bottom =
      output_height < render->height - (origin_y - render->y)
          ? output_height
          : render->height - (origin_y - render->y);

  uint32_t loaded_source_y = UINT32_MAX;
  for (int32_t dy = visible_top; dy < visible_bottom; dy++) {
    const uint32_t source_y =
        static_cast<uint32_t>(static_cast<uint64_t>(dy) * info.height /
                              static_cast<uint32_t>(output_height));
    if (source_y != loaded_source_y) {
      const uint32_t stored_y =
          info.top_down ? source_y : info.height - 1U - source_y;
      const size_t row_offset =
          static_cast<size_t>(info.pixel_offset) +
          static_cast<size_t>(stored_y) * info.row_bytes;
      if (!reader->read_at(reader->user, row_offset, row, info.row_bytes)) {
        IBMP_FREE(row);
        IBMP_FREE(palette);
        return IBMP_ERR_IO;
      }
      loaded_source_y = source_y;
      IBMP_COOPERATE();
    }
    for (int32_t dx = visible_left; dx < visible_right; dx++) {
      const uint32_t source_x =
          static_cast<uint32_t>(static_cast<uint64_t>(dx) * info.width /
                                static_cast<uint32_t>(output_width));
      uint8_t luminance = 255;
      if (info.bits_per_pixel == 1U) {
        const uint8_t index =
            (row[source_x >> 3U] >> (7U - (source_x & 7U))) & 1U;
        luminance =
            index < info.palette_entries ? palette[index] : 255U;
      } else if (info.bits_per_pixel == 4U) {
        const uint8_t byte = row[source_x >> 1U];
        const uint8_t index =
            (source_x & 1U) == 0U ? byte >> 4U : byte & 0x0fU;
        luminance =
            index < info.palette_entries ? palette[index] : 255;
      } else if (info.bits_per_pixel == 8U) {
        const uint8_t index = row[source_x];
        luminance =
            index < info.palette_entries ? palette[index] : 255;
      } else {
        const uint8_t bytes = info.bits_per_pixel / 8U;
        const uint8_t *pixel = row + source_x * bytes;
        luminance = ibmp_luminance(pixel[2], pixel[1], pixel[0]);
      }
      const int16_t draw_x = static_cast<int16_t>(origin_x + dx);
      const int16_t draw_y = static_cast<int16_t>(origin_y + dy);
      const uint8_t threshold =
          monochrome ? 128U
                     : ibmp_threshold(render->dither, draw_x, draw_y);
      if (luminance < threshold) {
        render->pixel(render->user, draw_x, draw_y);
      }
    }
  }
  IBMP_FREE(row);
  IBMP_FREE(palette);
  return IBMP_OK;
}

#endif
