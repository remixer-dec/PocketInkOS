/*
 * InkPNG - header-only PNG decoder and 1-bit renderer for small displays.
 * Supports PNG filters, palette/gray/RGB/RGBA images, Adam7 interlace, and
 * streamed Deflate blocks without storing the full image.
 * (c) Remixer Dec 2026 | CC BY-NC-SA 3.0
 * Distributed as a part of PocketInkOS https://github.com/remixer-dec/PocketInkOS
 */

#ifndef INKPNG_H
#define INKPNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef IPNG_MALLOC
#define IPNG_MALLOC malloc
#define IPNG_FREE free
#endif

#ifndef IPNG_MAX_ROW_BYTES
#define IPNG_MAX_ROW_BYTES 65535U
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  IPNG_OK = 0,
  IPNG_ERR_IO = -1,
  IPNG_ERR_FORMAT = -2,
  IPNG_ERR_UNSUPPORTED = -3,
  IPNG_ERR_MEMORY = -4,
} ipng_result;

typedef enum {
  IPNG_DITHER_THRESHOLD = 0,
  IPNG_DITHER_ATKINSON = 1,
  IPNG_DITHER_SIERRA_TWO_ROW = 2,
  IPNG_DITHER_SIMPLE2D = 3,
} ipng_dither_mode;

typedef enum {
  IPNG_SCALE_NONE = 0,
  IPNG_SCALE_FIT = 1,
} ipng_scale_mode;

typedef struct {
  void *user;
  int (*read)(void *user);
} ipng_reader;

typedef struct {
  uint32_t width;
  uint32_t height;
  uint8_t bit_depth;
  uint8_t color_type;
  uint8_t interlace;
} ipng_info;

typedef void (*ipng_pixel_fn)(void *user, int16_t x, int16_t y);

typedef struct {
  void *user;
  ipng_pixel_fn pixel;
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
  uint8_t dither;
  uint8_t scale;
} ipng_render;

static ipng_result ipng_read_info(ipng_reader *reader, ipng_info *info);
static ipng_result ipng_decode(ipng_reader *reader, const ipng_render *render,
                               ipng_info *out_info);

#ifdef __cplusplus
}
#endif

#define IPNG_FOURCC(a, b, c, d)                                                \
  ((((uint32_t)(a)) << 24) | (((uint32_t)(b)) << 16) |                         \
   (((uint32_t)(c)) << 8) | ((uint32_t)(d)))

#define IPNG_CHUNK_IHDR IPNG_FOURCC('I', 'H', 'D', 'R')
#define IPNG_CHUNK_PLTE IPNG_FOURCC('P', 'L', 'T', 'E')
#define IPNG_CHUNK_IDAT IPNG_FOURCC('I', 'D', 'A', 'T')
#define IPNG_CHUNK_IEND IPNG_FOURCC('I', 'E', 'N', 'D')
#define IPNG_CHUNK_tRNS IPNG_FOURCC('t', 'R', 'N', 'S')

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} ipng_rgba;

typedef struct {
  int16_t *row0;
  int16_t *row1;
  int16_t *row2;
  uint16_t width;
  int32_t current_y;
  uint8_t mode;
} ipng_dither;

typedef struct {
  ipng_info info;
  ipng_rgba palette[256];
  uint16_t palette_size;
  uint16_t transparent_gray;
  uint16_t transparent_r;
  uint16_t transparent_g;
  uint16_t transparent_b;
  uint8_t has_gray_trns;
  uint8_t has_rgb_trns;
  uint8_t channels;
  uint8_t filter_bpp;
  uint32_t max_row_bytes;
  uint8_t *row;
  uint8_t *prev;
  uint8_t pass;
  uint32_t pass_w;
  uint32_t pass_h;
  uint32_t pass_row;
  uint32_t row_len;
  uint32_t row_pos;
  uint8_t filter;
  uint8_t need_filter;
  const ipng_render *render;
  ipng_dither dither;
  uint8_t scale_mode;
  uint8_t scaled;
  uint16_t output_w;
  uint16_t output_h;
  int16_t output_x;
  int16_t output_y;
  uint32_t *scale_sum;
  uint16_t *scale_count;
  uint32_t scale_y;
  uint8_t scale_row_active;
} ipng_png;

static int ipng_reader_byte(ipng_reader *reader) {
  return (reader != NULL && reader->read != NULL) ? reader->read(reader->user)
                                                  : -1;
}

static bool ipng_read_exact(ipng_reader *reader, uint8_t *out, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    const int c = ipng_reader_byte(reader);
    if (c < 0) {
      return false;
    }
    if (out != NULL) {
      out[i] = (uint8_t)c;
    }
  }
  return true;
}

static bool ipng_skip(ipng_reader *reader, uint32_t len) {
  return ipng_read_exact(reader, NULL, len);
}

static bool ipng_skip_chunk_tail(ipng_reader *reader, uint32_t len) {
  return ipng_skip(reader, len) && ipng_skip(reader, 4);
}

static bool ipng_read_u32(ipng_reader *reader, uint32_t *out) {
  uint8_t b[4];
  if (!ipng_read_exact(reader, b, 4)) {
    return false;
  }
  *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
  return true;
}

static bool ipng_read_u16(ipng_reader *reader, uint16_t *out) {
  uint8_t b[2];
  if (!ipng_read_exact(reader, b, 2)) {
    return false;
  }
  *out = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
  return true;
}

static bool ipng_read_chunk_header(ipng_reader *reader, uint32_t *len,
                                   uint32_t *type) {
  return ipng_read_u32(reader, len) && ipng_read_u32(reader, type);
}

static bool ipng_supported_format(const ipng_info *info) {
  const uint8_t d = info->bit_depth;
  switch (info->color_type) {
  case 0:
    return d == 1 || d == 2 || d == 4 || d == 8 || d == 16;
  case 2:
    return d == 8 || d == 16;
  case 3:
    return d == 1 || d == 2 || d == 4 || d == 8;
  case 4:
    return d == 8 || d == 16;
  case 6:
    return d == 8 || d == 16;
  default:
    return false;
  }
}

static uint8_t ipng_channels(uint8_t color_type) {
  static const uint8_t table[7] = {1, 0, 3, 1, 2, 0, 4};
  return color_type < 7 ? table[color_type] : 0;
}

static bool ipng_signature(ipng_reader *reader) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  uint8_t got[8];
  return ipng_read_exact(reader, got, 8) && memcmp(sig, got, sizeof(sig)) == 0;
}

static ipng_result ipng_parse_ihdr(ipng_reader *reader, uint32_t len,
                                   ipng_info *info) {
  uint8_t tail[5];
  if (len != 13 || !ipng_read_u32(reader, &info->width) ||
      !ipng_read_u32(reader, &info->height) ||
      !ipng_read_exact(reader, tail, sizeof(tail))) {
    return IPNG_ERR_FORMAT;
  }
  info->bit_depth = tail[0];
  info->color_type = tail[1];
  info->interlace = tail[4];
  if (tail[2] != 0 || tail[3] != 0 || info->interlace > 1 ||
      info->width == 0 || info->height == 0) {
    return IPNG_ERR_UNSUPPORTED;
  }
  return ipng_supported_format(info) ? IPNG_OK : IPNG_ERR_UNSUPPORTED;
}

static ipng_result ipng_read_info(ipng_reader *reader, ipng_info *info) {
  if (reader == NULL || info == NULL || !ipng_signature(reader)) {
    return IPNG_ERR_FORMAT;
  }
  memset(info, 0, sizeof(*info));
  for (;;) {
    uint32_t len = 0;
    uint32_t type = 0;
    if (!ipng_read_chunk_header(reader, &len, &type)) {
      return IPNG_ERR_IO;
    }
    if (type == IPNG_CHUNK_IHDR) {
      const ipng_result result = ipng_parse_ihdr(reader, len, info);
      if (result != IPNG_OK) {
        return result;
      }
      return ipng_skip(reader, 4) ? IPNG_OK : IPNG_ERR_IO;
    }
    return IPNG_ERR_FORMAT;
  }
}

static uint32_t ipng_pass_dim(uint32_t full, uint8_t start, uint8_t step) {
  return full > start ? (full - start + step - 1U) / step : 0;
}

static uint32_t ipng_row_bytes(uint32_t width, uint8_t channels,
                               uint8_t depth) {
  return (width * (uint32_t)channels * (uint32_t)depth + 7U) >> 3;
}

static bool ipng_row_bytes_checked(uint32_t width, uint8_t channels,
                                   uint8_t depth, uint32_t *out) {
  const uint64_t bits =
      (uint64_t)width * (uint64_t)channels * (uint64_t)depth;
  const uint64_t bytes = (bits + 7ULL) >> 3;
  if (channels == 0 || depth == 0 || bytes == 0 ||
      bytes > (uint64_t)((size_t)-1) || bytes > 0xffffffffULL ||
      bytes > (uint64_t)IPNG_MAX_ROW_BYTES) {
    return false;
  }
  if (out != NULL) {
    *out = (uint32_t)bytes;
  }
  return true;
}

static uint8_t ipng_luma(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  uint16_t y = (uint16_t)(((uint16_t)r * 77U + (uint16_t)g * 150U +
                           (uint16_t)b * 29U) >>
                          8);
  y = (uint16_t)((y * a + 255U * (255U - a) + 127U) / 255U);
  return (uint8_t)y;
}

static uint16_t ipng_fit_dim(uint32_t src, uint32_t other_src,
                             uint16_t target, uint16_t other_target) {
  if (src == 0 || other_src == 0 || target == 0 || other_target == 0) {
    return 0;
  }
  const uint64_t by_width = (uint64_t)other_src * target;
  const uint64_t by_height = (uint64_t)src * other_target;
  if (by_width <= by_height) {
    return target;
  }
  {
    uint64_t value = ((uint64_t)src * other_target) / other_src;
    if (value == 0) {
      value = 1;
    }
    return value > target ? target : (uint16_t)value;
  }
}

static uint8_t ipng_expand(uint16_t v, uint8_t depth) {
  if (depth == 8 || depth == 16) {
    return (uint8_t)v;
  }
  const uint16_t maxv = (uint16_t)((1U << depth) - 1U);
  return (uint8_t)((v * 255U + maxv / 2U) / maxv);
}

static uint16_t ipng_sample_bits(const uint8_t *row, uint32_t sample,
                                 uint8_t depth) {
  const uint32_t bit = sample * depth;
  const uint8_t shift = (uint8_t)(8U - depth - (bit & 7U));
  return (uint16_t)((row[bit >> 3] >> shift) & ((1U << depth) - 1U));
}

static void ipng_dither_free(ipng_dither *d) {
  if (d->row0 != NULL) {
    IPNG_FREE(d->row0);
  }
  if (d->row1 != NULL) {
    IPNG_FREE(d->row1);
  }
  if (d->row2 != NULL) {
    IPNG_FREE(d->row2);
  }
  memset(d, 0, sizeof(*d));
  d->current_y = -1;
}

static bool ipng_dither_init(ipng_dither *d, uint16_t width, uint8_t mode) {
  memset(d, 0, sizeof(*d));
  d->current_y = -1;
  d->width = width;
  d->mode = mode <= (uint8_t)IPNG_DITHER_SIMPLE2D
                ? mode
                : (uint8_t)IPNG_DITHER_THRESHOLD;
  if (mode == IPNG_DITHER_THRESHOLD || width == 0) {
    return true;
  }
  const size_t bytes = ((size_t)width + 4U) * sizeof(int16_t);
  d->row0 = (int16_t *)IPNG_MALLOC(bytes);
  d->row1 = (int16_t *)IPNG_MALLOC(bytes);
  d->row2 = (int16_t *)IPNG_MALLOC(bytes);
  if (d->row0 == NULL || d->row1 == NULL || d->row2 == NULL) {
    ipng_dither_free(d);
    return false;
  }
  memset(d->row0, 0, bytes);
  memset(d->row1, 0, bytes);
  memset(d->row2, 0, bytes);
  return true;
}

static void ipng_dither_clear(ipng_dither *d) {
  if (d->row0 == NULL) {
    return;
  }
  const size_t bytes = ((size_t)d->width + 4U) * sizeof(int16_t);
  memset(d->row0, 0, bytes);
  memset(d->row1, 0, bytes);
  memset(d->row2, 0, bytes);
}

static void ipng_dither_roll(ipng_dither *d) {
  if (d->row0 == NULL) {
    return;
  }
  int16_t *old = d->row0;
  d->row0 = d->row1;
  d->row1 = d->row2;
  d->row2 = old;
  memset(d->row2, 0, ((size_t)d->width + 4U) * sizeof(int16_t));
}

static void ipng_dither_row(ipng_dither *d, int32_t y) {
  if (d->current_y == y) {
    return;
  }
  if (d->current_y >= 0 && y == d->current_y + 1) {
    ipng_dither_roll(d);
  } else {
    ipng_dither_clear(d);
  }
  d->current_y = y;
}

static void ipng_dither_add(int16_t *row, uint16_t width, int32_t x,
                            int16_t value) {
  if (row != NULL && x >= 0 && x < (int32_t)width) {
    int32_t sum = (int32_t)row[x + 2] + value;
    sum = sum < -4096 ? -4096 : sum;
    sum = sum > 4096 ? 4096 : sum;
    row[x + 2] = (int16_t)sum;
  }
}

static uint8_t ipng_dither_pixel(ipng_dither *d, uint16_t x, int32_t y,
                                 uint8_t gray) {
  ipng_dither_row(d, y);
  if (d->mode == IPNG_DITHER_THRESHOLD || d->row0 == NULL) {
    return gray < 128;
  }

  const size_t ix = (size_t)x + 2U;
  int32_t adjusted = (int32_t)gray + d->row0[ix];
  d->row0[ix] = 0;
  adjusted = adjusted < 0 ? 0 : adjusted;
  adjusted = adjusted > 255 ? 255 : adjusted;
  const uint8_t black = adjusted < 128;
  const int16_t error = (int16_t)(adjusted - (black ? 0 : 255));

  if (d->mode == IPNG_DITHER_ATKINSON) {
    const int16_t e = (int16_t)(error / 8);
    ipng_dither_add(d->row0, d->width, (int32_t)x + 1, e);
    ipng_dither_add(d->row0, d->width, (int32_t)x + 2, e);
    ipng_dither_add(d->row1, d->width, (int32_t)x - 1, e);
    ipng_dither_add(d->row1, d->width, x, e);
    ipng_dither_add(d->row1, d->width, (int32_t)x + 1, e);
    ipng_dither_add(d->row2, d->width, x, e);
  } else if (d->mode == IPNG_DITHER_SIERRA_TWO_ROW) {
    ipng_dither_add(d->row0, d->width, (int32_t)x + 1,
                    (int16_t)(error * 4 / 16));
    ipng_dither_add(d->row0, d->width, (int32_t)x + 2,
                    (int16_t)(error * 3 / 16));
    ipng_dither_add(d->row1, d->width, (int32_t)x - 2,
                    (int16_t)(error / 16));
    ipng_dither_add(d->row1, d->width, (int32_t)x - 1,
                    (int16_t)(error * 2 / 16));
    ipng_dither_add(d->row1, d->width, x, (int16_t)(error * 3 / 16));
    ipng_dither_add(d->row1, d->width, (int32_t)x + 1,
                    (int16_t)(error * 2 / 16));
    ipng_dither_add(d->row1, d->width, (int32_t)x + 2,
                    (int16_t)(error / 16));
  } else {
    const int16_t e = (int16_t)(error / 4);
    ipng_dither_add(d->row0, d->width, (int32_t)x + 1, (int16_t)(e * 2));
    ipng_dither_add(d->row1, d->width, x, e);
    ipng_dither_add(d->row1, d->width, (int32_t)x + 1, e);
  }
  return black;
}

static void ipng_emit_render(ipng_png *png, uint32_t x, uint32_t y,
                             uint8_t gray) {
  const ipng_render *r = png->render;
  if (r == NULL || r->pixel == NULL) {
    return;
  }

  uint32_t dx = x;
  uint32_t dy = y;
  if (png->scaled) {
    dx = ((uint64_t)x * png->output_w) / png->info.width;
    dy = ((uint64_t)y * png->output_h) / png->info.height;
    if (dx >= png->output_w || dy >= png->output_h) {
      return;
    }
    if (png->scale_sum != NULL && png->scale_count != NULL) {
      if (png->scale_row_active && dy != png->scale_y) {
        for (uint16_t sx = 0; sx < png->output_w; sx++) {
          if (png->scale_count[sx] != 0) {
            const uint8_t avg =
                (uint8_t)(png->scale_sum[sx] / png->scale_count[sx]);
            const uint8_t black =
                ipng_dither_pixel(&png->dither, sx, (int32_t)png->scale_y, avg);
            if (black) {
              const int32_t px = (int32_t)png->output_x + sx;
              const int32_t py = (int32_t)png->output_y + png->scale_y;
              if (px >= INT16_MIN && px <= INT16_MAX && py >= INT16_MIN &&
                  py <= INT16_MAX) {
                r->pixel(r->user, (int16_t)px, (int16_t)py);
              }
            }
          }
        }
        memset(png->scale_sum, 0, (size_t)png->output_w * sizeof(uint32_t));
        memset(png->scale_count, 0, (size_t)png->output_w * sizeof(uint16_t));
      }
      png->scale_y = dy;
      png->scale_row_active = 1;
      png->scale_sum[dx] += gray;
      png->scale_count[dx]++;
      return;
    }

    const uint32_t sample_x =
        (((uint64_t)dx * 2U + 1U) * png->info.width) / (2U * png->output_w);
    const uint32_t sample_y =
        (((uint64_t)dy * 2U + 1U) * png->info.height) / (2U * png->output_h);
    if (x != sample_x || y != sample_y) {
      return;
    }
  } else if (dx >= r->width || dy >= r->height) {
    return;
  }

  const uint8_t black = ipng_dither_pixel(&png->dither, (uint16_t)dx, dy, gray);
  if (black) {
    const int32_t px = (int32_t)png->output_x + (int32_t)dx;
    const int32_t py = (int32_t)png->output_y + (int32_t)dy;
    if (px >= INT16_MIN && px <= INT16_MAX && py >= INT16_MIN &&
        py <= INT16_MAX) {
      r->pixel(r->user, (int16_t)px, (int16_t)py);
    }
  }
}

static bool ipng_setup_output(ipng_png *png, const ipng_render *render) {
  if (png == NULL) {
    return false;
  }
  png->output_w = (uint16_t)png->info.width;
  png->output_h = (uint16_t)png->info.height;
  png->output_x = render != NULL ? render->x : 0;
  png->output_y = render != NULL ? render->y : 0;
  png->scale_mode =
      render != NULL && render->scale <= (uint8_t)IPNG_SCALE_FIT
          ? render->scale
          : (uint8_t)IPNG_SCALE_NONE;
  if (render == NULL || render->width == 0 || render->height == 0) {
    return true;
  }
  if (png->info.width > UINT16_MAX || png->info.height > UINT16_MAX) {
    png->scale_mode = (uint8_t)IPNG_SCALE_FIT;
  }
  if (png->scale_mode == (uint8_t)IPNG_SCALE_FIT &&
      (png->info.width > render->width || png->info.height > render->height)) {
    const uint16_t fit_w =
        ipng_fit_dim(png->info.width, png->info.height, render->width,
                     render->height);
    const uint16_t fit_h =
        ipng_fit_dim(png->info.height, png->info.width, render->height,
                     render->width);
    if (fit_w == 0 || fit_h == 0) {
      return false;
    }
    png->output_w = fit_w;
    png->output_h = fit_h;
    png->output_x = (int16_t)(render->x + (int16_t)((render->width - fit_w) / 2U));
    png->output_y =
        (int16_t)(render->y + (int16_t)((render->height - fit_h) / 2U));
    png->scaled = 1;
    if (png->info.interlace == 0) {
      png->scale_sum =
          (uint32_t *)IPNG_MALLOC((size_t)png->output_w * sizeof(uint32_t));
      png->scale_count =
          (uint16_t *)IPNG_MALLOC((size_t)png->output_w * sizeof(uint16_t));
      if (png->scale_sum == NULL || png->scale_count == NULL) {
        if (png->scale_sum != NULL) {
          IPNG_FREE(png->scale_sum);
          png->scale_sum = NULL;
        }
        if (png->scale_count != NULL) {
          IPNG_FREE(png->scale_count);
          png->scale_count = NULL;
        }
        return false;
      }
      memset(png->scale_sum, 0, (size_t)png->output_w * sizeof(uint32_t));
      memset(png->scale_count, 0, (size_t)png->output_w * sizeof(uint16_t));
    }
  }
  return true;
}

static void ipng_flush_scale(ipng_png *png) {
  if (png == NULL || png->scale_sum == NULL || png->scale_count == NULL ||
      !png->scale_row_active) {
    return;
  }
  const ipng_render *r = png->render;
  if (r == NULL || r->pixel == NULL) {
    return;
  }
  for (uint16_t sx = 0; sx < png->output_w; sx++) {
    if (png->scale_count[sx] == 0) {
      continue;
    }
    const uint8_t avg =
        (uint8_t)(png->scale_sum[sx] / png->scale_count[sx]);
    if (ipng_dither_pixel(&png->dither, sx, (int32_t)png->scale_y, avg)) {
      r->pixel(r->user, (int16_t)(png->output_x + sx),
               (int16_t)(png->output_y + png->scale_y));
    }
  }
  png->scale_row_active = 0;
}

static uint8_t ipng_pixel_gray(ipng_png *png, const uint8_t *row, uint32_t x) {
  const uint8_t d = png->info.bit_depth;
  const uint8_t c = png->info.color_type;
  if (c == 3) {
    const uint16_t idx = d < 8 ? ipng_sample_bits(row, x, d) : row[x];
    if (idx >= png->palette_size) {
      return 255;
    }
    const ipng_rgba p = png->palette[idx];
    return ipng_luma(p.r, p.g, p.b, p.a);
  }
  if (c == 0) {
    const uint16_t gs =
        d < 8 ? ipng_sample_bits(row, x, d) : row[x * (d >> 3)];
    const uint8_t gray = ipng_expand(gs, d);
    const uint8_t a =
        png->has_gray_trns && gs == png->transparent_gray ? 0 : 255;
    return ipng_luma(gray, gray, gray, a);
  }
  const uint32_t bps = d >> 3;
  const uint32_t p = x * png->channels * bps;
  if (c == 4) {
    const uint8_t gray = row[p];
    const uint8_t a = row[p + bps];
    return ipng_luma(gray, gray, gray, a);
  }
  const uint8_t r = row[p];
  const uint8_t g = row[p + bps];
  const uint8_t b = row[p + bps * 2U];
  uint8_t a = 255;
  if (c == 6) {
    a = row[p + bps * 3U];
  } else if (png->has_rgb_trns) {
    const uint16_t sr = d == 16 ? (uint16_t)((row[p] << 8) | row[p + 1])
                                : row[p];
    const uint16_t sg =
        d == 16 ? (uint16_t)((row[p + 2] << 8) | row[p + 3]) : row[p + 1];
    const uint16_t sb =
        d == 16 ? (uint16_t)((row[p + 4] << 8) | row[p + 5]) : row[p + 2];
    a = sr == png->transparent_r && sg == png->transparent_g &&
                sb == png->transparent_b
            ? 0
            : 255;
  }
  return ipng_luma(r, g, b, a);
}

static uint8_t ipng_paeth(uint8_t a, uint8_t b, uint8_t c) {
  const int p = (int)a + (int)b - (int)c;
  const int pa = p > a ? p - a : a - p;
  const int pb = p > b ? p - b : b - p;
  const int pc = p > c ? p - c : c - p;
  return (uint8_t)(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
}

static void ipng_unfilter(ipng_png *png) {
  for (uint32_t i = 0; i < png->row_len; i++) {
    const uint8_t a = i >= png->filter_bpp ? png->row[i - png->filter_bpp] : 0;
    const uint8_t b = png->prev != NULL ? png->prev[i] : 0;
    const uint8_t c =
        png->prev != NULL && i >= png->filter_bpp ? png->prev[i - png->filter_bpp]
                                                  : 0;
    uint8_t add = 0;
    if (png->filter == 1) {
      add = a;
    } else if (png->filter == 2) {
      add = b;
    } else if (png->filter == 3) {
      add = (uint8_t)(((uint16_t)a + b) >> 1);
    } else if (png->filter == 4) {
      add = ipng_paeth(a, b, c);
    }
    png->row[i] = (uint8_t)(png->row[i] + add);
  }
}

static void ipng_render_row(ipng_png *png) {
  static const uint8_t x_start[7] = {0, 4, 0, 2, 0, 1, 0};
  static const uint8_t y_start[7] = {0, 0, 4, 0, 2, 0, 1};
  static const uint8_t x_step[7] = {8, 8, 4, 4, 2, 2, 1};
  static const uint8_t y_step[7] = {8, 8, 8, 4, 4, 2, 2};
  const uint8_t pass = png->pass;
  const uint32_t y = png->info.interlace
                         ? (uint32_t)y_start[pass] +
                               png->pass_row * y_step[pass]
                         : png->pass_row;
  for (uint32_t px = 0; px < png->pass_w; px++) {
    const uint32_t x = png->info.interlace
                           ? (uint32_t)x_start[pass] + px * x_step[pass]
                           : px;
    ipng_emit_render(png, x, y, ipng_pixel_gray(png, png->row, px));
  }
}

static bool ipng_next_pass(ipng_png *png) {
  static const uint8_t x_start[7] = {0, 4, 0, 2, 0, 1, 0};
  static const uint8_t y_start[7] = {0, 0, 4, 0, 2, 0, 1};
  static const uint8_t x_step[7] = {8, 8, 4, 4, 2, 2, 1};
  static const uint8_t y_step[7] = {8, 8, 8, 4, 4, 2, 2};
  if (!png->info.interlace && png->pass > 0) {
    return false;
  }
  while (png->pass < 7) {
    const uint8_t pass = png->pass;
    png->pass_w =
        png->info.interlace
            ? ipng_pass_dim(png->info.width, x_start[pass], x_step[pass])
            : png->info.width;
    png->pass_h =
        png->info.interlace
            ? ipng_pass_dim(png->info.height, y_start[pass], y_step[pass])
            : png->info.height;
    png->pass_row = 0;
    png->row_pos = 0;
    png->need_filter = png->pass_w > 0 && png->pass_h > 0;
    if (png->need_filter &&
        (!ipng_row_bytes_checked(png->pass_w, png->channels,
                                 png->info.bit_depth, &png->row_len) ||
         png->row_len > png->max_row_bytes)) {
      return false;
    }
    if (!png->need_filter) {
      png->row_len = 0;
    }
    memset(png->prev, 0, png->max_row_bytes);
    if (png->need_filter) {
      return true;
    }
    if (!png->info.interlace) {
      return false;
    }
    png->pass++;
  }
  return false;
}

static ipng_result ipng_png_byte(ipng_png *png, uint8_t byte) {
  if (!png->need_filter && !ipng_next_pass(png)) {
    return IPNG_ERR_FORMAT;
  }
  if (png->row_pos == 0) {
    if (byte > 4) {
      return IPNG_ERR_FORMAT;
    }
    png->filter = byte;
    png->row_pos = 1;
    return IPNG_OK;
  }

  png->row[png->row_pos - 1U] = byte;
  png->row_pos++;
  if (png->row_pos <= png->row_len) {
    return IPNG_OK;
  }

  ipng_unfilter(png);
  ipng_render_row(png);
  {
    uint8_t *swap = png->prev;
    png->prev = png->row;
    png->row = swap;
  }
  png->row_pos = 0;
  png->pass_row++;
  if (png->pass_row >= png->pass_h) {
    png->pass = png->info.interlace ? (uint8_t)(png->pass + 1U) : 7;
    png->need_filter = 0;
  }
  return IPNG_OK;
}

static bool ipng_image_complete(const ipng_png *png) {
  return png != NULL && png->need_filter == 0 && png->pass >= 7;
}

static bool ipng_finish_empty_passes(ipng_png *png) {
  if (png == NULL) {
    return false;
  }
  if (!png->need_filter && !ipng_image_complete(png) && ipng_next_pass(png)) {
    return false;
  }
  if (ipng_image_complete(png)) {
    ipng_flush_scale(png);
  }
  return ipng_image_complete(png);
}

typedef struct {
  ipng_reader *reader;
  uint32_t remaining;
} ipng_idat;

static int ipng_idat_byte(ipng_idat *idat) {
  while (idat->remaining == 0) {
    uint32_t len = 0;
    uint32_t type = 0;
    if (!ipng_skip(idat->reader, 4) ||
        !ipng_read_chunk_header(idat->reader, &len, &type)) {
      return -1;
    }
    if (type != IPNG_CHUNK_IDAT) {
      return -1;
    }
    idat->remaining = len;
  }
  idat->remaining--;
  return ipng_reader_byte(idat->reader);
}

typedef struct {
  int16_t left[576];
  int16_t right[576];
  int16_t sym[576];
  uint16_t nodes;
} ipng_huff;

typedef struct {
  ipng_idat *idat;
  uint32_t bits;
  uint8_t bit_count;
  uint8_t *window;
  uint16_t win_pos;
  uint32_t out_count;
  ipng_png *png;
} ipng_inflate;

static void ipng_huff_reset(ipng_huff *h) {
  h->nodes = 1;
  h->left[0] = h->right[0] = -1;
  h->sym[0] = -1;
}

static bool ipng_huff_insert(ipng_huff *h, uint16_t code, uint8_t len,
                             uint16_t sym) {
  uint16_t node = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (h->sym[node] >= 0) {
      return false;
    }
    int16_t *child =
        ((code >> (uint8_t)(len - 1U - i)) & 1U) ? &h->right[node]
                                                  : &h->left[node];
    if (*child < 0) {
      if (h->nodes >= 576) {
        return false;
      }
      *child = (int16_t)h->nodes;
      h->left[h->nodes] = h->right[h->nodes] = -1;
      h->sym[h->nodes] = -1;
      h->nodes++;
    }
    node = (uint16_t)*child;
  }
  if (h->sym[node] >= 0 || h->left[node] >= 0 || h->right[node] >= 0) {
    return false;
  }
  h->sym[node] = (int16_t)sym;
  return true;
}

static bool ipng_huff_build(ipng_huff *h, const uint8_t *lengths,
                            uint16_t count) {
  uint16_t bl_count[16] = {0};
  uint16_t next_code[16] = {0};
  uint16_t code = 0;
  int32_t left = 1;
  ipng_huff_reset(h);
  for (uint16_t i = 0; i < count; i++) {
    if (lengths[i] > 15) {
      return false;
    }
    bl_count[lengths[i]]++;
  }
  bl_count[0] = 0;
  for (uint8_t bits = 1; bits <= 15; bits++) {
    left = (left << 1) - bl_count[bits];
    if (left < 0) {
      return false;
    }
    code = (uint16_t)((code + bl_count[bits - 1U]) << 1);
    next_code[bits] = code;
  }
  for (uint16_t n = 0; n < count; n++) {
    const uint8_t len = lengths[n];
    if (len != 0 && !ipng_huff_insert(h, next_code[len]++, len, n)) {
      return false;
    }
  }
  return true;
}

static int ipng_bits(ipng_inflate *inf, uint8_t count) {
  while (inf->bit_count < count) {
    const int c = ipng_idat_byte(inf->idat);
    if (c < 0) {
      return -1;
    }
    inf->bits |= (uint32_t)((uint8_t)c) << inf->bit_count;
    inf->bit_count = (uint8_t)(inf->bit_count + 8U);
  }
  {
    const int out = (int)(inf->bits & ((1UL << count) - 1UL));
    inf->bits >>= count;
    inf->bit_count = (uint8_t)(inf->bit_count - count);
    return out;
  }
}

static int ipng_huff_decode(ipng_inflate *inf, const ipng_huff *h) {
  int16_t node = 0;
  while (node >= 0 && h->sym[node] < 0) {
    const int bit = ipng_bits(inf, 1);
    if (bit < 0) {
      return -1;
    }
    node = bit ? h->right[node] : h->left[node];
  }
  return node >= 0 ? h->sym[node] : -1;
}

static ipng_result ipng_emit(ipng_inflate *inf, uint8_t byte) {
  inf->window[inf->win_pos] = byte;
  inf->win_pos = (uint16_t)((inf->win_pos + 1U) & 32767U);
  if (inf->out_count < 32768U) {
    inf->out_count++;
  }
  return ipng_png_byte(inf->png, byte);
}

static ipng_result ipng_inflate_codes(ipng_inflate *inf, const ipng_huff *lit,
                                      const ipng_huff *dist) {
  static const uint16_t len_base[29] = {
      3,   4,   5,   6,   7,   8,   9,   10,  11,  13,
      15,  17,  19,  23,  27,  31,  35,  43,  51,  59,
      67,  83,  99,  115, 131, 163, 195, 227, 258};
  static const uint8_t len_extra[29] = {
      0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const uint16_t dist_base[30] = {
      1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
      33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t dist_extra[30] = {
      0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4,  4,  5,  5,  6,
      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  for (;;) {
    const int sym = ipng_huff_decode(inf, lit);
    if (sym < 0) {
      return IPNG_ERR_FORMAT;
    }
    if (sym < 256) {
      const ipng_result r = ipng_emit(inf, (uint8_t)sym);
      if (r != IPNG_OK) {
        return r;
      }
    } else if (sym == 256) {
      return IPNG_OK;
    } else if (sym <= 285) {
      const uint8_t li = (uint8_t)(sym - 257);
      int length = len_base[li];
      int dsym = 0;
      int distance = 0;
      if (len_extra[li] != 0) {
        const int extra = ipng_bits(inf, len_extra[li]);
        if (extra < 0) {
          return IPNG_ERR_FORMAT;
        }
        length += extra;
      }
      dsym = ipng_huff_decode(inf, dist);
      if (dsym < 0 || dsym >= 30) {
        return IPNG_ERR_FORMAT;
      }
      distance = dist_base[dsym];
      if (dist_extra[dsym] != 0) {
        const int extra = ipng_bits(inf, dist_extra[dsym]);
        if (extra < 0) {
          return IPNG_ERR_FORMAT;
        }
        distance += extra;
      }
      if (distance <= 0 || (uint32_t)distance > inf->out_count ||
          distance > 32768) {
        return IPNG_ERR_FORMAT;
      }
      while (length-- > 0) {
        const uint8_t b =
            inf->window[(uint16_t)(inf->win_pos - distance) & 32767U];
        const ipng_result r = ipng_emit(inf, b);
        if (r != IPNG_OK) {
          return r;
        }
      }
    } else {
      return IPNG_ERR_FORMAT;
    }
  }
}

static bool ipng_fixed_trees(ipng_huff *lit, ipng_huff *dist) {
  uint8_t ll[288];
  uint8_t dd[32];
  for (uint16_t i = 0; i < 288; i++) {
    ll[i] = i <= 143 ? 8 : (i <= 255 ? 9 : (i <= 279 ? 7 : 8));
  }
  for (uint8_t i = 0; i < 32; i++) {
    dd[i] = 5;
  }
  return ipng_huff_build(lit, ll, 288) && ipng_huff_build(dist, dd, 32);
}

static ipng_result ipng_dynamic_trees(ipng_inflate *inf, ipng_huff *lit,
                                      ipng_huff *dist) {
  static const uint8_t order[19] = {16, 17, 18, 0,  8, 7,  9, 6, 10, 5,
                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
  uint8_t clen[19] = {0};
  uint8_t lengths[320];
  ipng_huff *code_tree = (ipng_huff *)IPNG_MALLOC(sizeof(ipng_huff));
  if (code_tree == NULL) {
    return IPNG_ERR_MEMORY;
  }
  const int hlit = ipng_bits(inf, 5);
  const int hdist = ipng_bits(inf, 5);
  const int hclen = ipng_bits(inf, 4);
#define IPNG_DYNAMIC_RETURN(value)                                             \
  do {                                                                         \
    IPNG_FREE(code_tree);                                                      \
    return (value);                                                            \
  } while (0)
  if (hlit < 0 || hdist < 0 || hclen < 0) {
    IPNG_DYNAMIC_RETURN(IPNG_ERR_FORMAT);
  }
  const uint16_t lit_count = (uint16_t)(hlit + 257);
  const uint16_t dist_count = (uint16_t)(hdist + 1);
  const uint16_t total = (uint16_t)(lit_count + dist_count);
  for (uint8_t i = 0; i < (uint8_t)(hclen + 4); i++) {
    const int v = ipng_bits(inf, 3);
    if (v < 0) {
      IPNG_DYNAMIC_RETURN(IPNG_ERR_FORMAT);
    }
    clen[order[i]] = (uint8_t)v;
  }
  if (!ipng_huff_build(code_tree, clen, 19)) {
    IPNG_DYNAMIC_RETURN(IPNG_ERR_FORMAT);
  }
  for (uint16_t i = 0; i < total;) {
    const int sym = ipng_huff_decode(inf, code_tree);
    uint8_t value = 0;
    uint16_t repeat = 0;
    if (sym < 0) {
      IPNG_DYNAMIC_RETURN(IPNG_ERR_FORMAT);
    }
    if (sym <= 15) {
      lengths[i++] = (uint8_t)sym;
      continue;
    }
    if (sym == 16) {
      const int extra = ipng_bits(inf, 2);
      if (i == 0 || extra < 0) {
        IPNG_DYNAMIC_RETURN(IPNG_ERR_FORMAT);
      }
      value = lengths[i - 1U];
      repeat = (uint16_t)(extra + 3);
    } else if (sym == 17) {
      const int extra = ipng_bits(inf, 3);
      if (extra < 0) {
        IPNG_DYNAMIC_RETURN(IPNG_ERR_FORMAT);
      }
      value = 0;
      repeat = (uint16_t)(extra + 3);
    } else {
      const int extra = ipng_bits(inf, 7);
      if (extra < 0) {
        IPNG_DYNAMIC_RETURN(IPNG_ERR_FORMAT);
      }
      value = 0;
      repeat = (uint16_t)(extra + 11);
    }
    if (repeat > total - i) {
      IPNG_DYNAMIC_RETURN(IPNG_ERR_FORMAT);
    }
    while (repeat-- > 0 && i < total) {
      lengths[i++] = value;
    }
  }
  IPNG_DYNAMIC_RETURN(ipng_huff_build(lit, lengths, lit_count) &&
                              ipng_huff_build(dist, lengths + lit_count,
                                              dist_count)
                          ? IPNG_OK
                          : IPNG_ERR_FORMAT);
#undef IPNG_DYNAMIC_RETURN
}

static ipng_result ipng_inflate_stored(ipng_inflate *inf) {
  inf->bits = 0;
  inf->bit_count = 0;
  const int l0 = ipng_idat_byte(inf->idat);
  const int l1 = ipng_idat_byte(inf->idat);
  const int n0 = ipng_idat_byte(inf->idat);
  const int n1 = ipng_idat_byte(inf->idat);
  if (l0 < 0 || l1 < 0 || n0 < 0 || n1 < 0) {
    return IPNG_ERR_FORMAT;
  }
  const uint16_t len = (uint16_t)(l0 | (l1 << 8));
  const uint16_t nlen = (uint16_t)(n0 | (n1 << 8));
  if ((uint16_t)~len != nlen) {
    return IPNG_ERR_FORMAT;
  }
  for (uint16_t i = 0; i < len; i++) {
    const int b = ipng_idat_byte(inf->idat);
    if (b < 0) {
      return IPNG_ERR_FORMAT;
    }
    const ipng_result r = ipng_emit(inf, (uint8_t)b);
    if (r != IPNG_OK) {
      return r;
    }
  }
  return IPNG_OK;
}

static ipng_result ipng_inflate_zlib(ipng_idat *idat, ipng_png *png) {
  ipng_inflate inf;
  ipng_huff *lit = NULL;
  ipng_huff *dist = NULL;
  const int cmf = ipng_idat_byte(idat);
  const int flg = ipng_idat_byte(idat);
  if (cmf < 0 || flg < 0 || (cmf & 15) != 8 ||
      (((cmf << 8) + flg) % 31) != 0 || (flg & 32) != 0) {
    return IPNG_ERR_FORMAT;
  }
  memset(&inf, 0, sizeof(inf));
  inf.window = (uint8_t *)IPNG_MALLOC(32768);
  lit = (ipng_huff *)IPNG_MALLOC(sizeof(ipng_huff));
  dist = (ipng_huff *)IPNG_MALLOC(sizeof(ipng_huff));
  if (inf.window == NULL || lit == NULL || dist == NULL) {
    if (inf.window != NULL) {
      IPNG_FREE(inf.window);
    }
    if (lit != NULL) {
      IPNG_FREE(lit);
    }
    if (dist != NULL) {
      IPNG_FREE(dist);
    }
    return IPNG_ERR_MEMORY;
  }
  inf.idat = idat;
  inf.png = png;
#define IPNG_INFLATE_RETURN(value)                                             \
  do {                                                                         \
    IPNG_FREE(inf.window);                                                     \
    IPNG_FREE(lit);                                                            \
    IPNG_FREE(dist);                                                           \
    return (value);                                                            \
  } while (0)
  for (;;) {
    const int final = ipng_bits(&inf, 1);
    const int type = ipng_bits(&inf, 2);
    ipng_result result = IPNG_OK;
    if (final < 0 || type < 0 || type == 3) {
      IPNG_INFLATE_RETURN(IPNG_ERR_FORMAT);
    }
    if (type == 0) {
      result = ipng_inflate_stored(&inf);
    } else {
      if (type == 1) {
        if (!ipng_fixed_trees(lit, dist)) {
          IPNG_INFLATE_RETURN(IPNG_ERR_FORMAT);
        }
      } else {
        result = ipng_dynamic_trees(&inf, lit, dist);
      }
      if (result == IPNG_OK) {
        result = ipng_inflate_codes(&inf, lit, dist);
      }
    }
    if (result != IPNG_OK) {
      IPNG_INFLATE_RETURN(result);
    }
    if (final) {
      inf.bits = 0;
      inf.bit_count = 0;
      for (uint8_t i = 0; i < 4; i++) {
        if (ipng_idat_byte(idat) < 0) {
          IPNG_INFLATE_RETURN(IPNG_ERR_FORMAT);
        }
      }
      IPNG_INFLATE_RETURN(ipng_finish_empty_passes(png) ? IPNG_OK
                                                        : IPNG_ERR_FORMAT);
    }
  }
#undef IPNG_INFLATE_RETURN
}

static ipng_result ipng_read_plte(ipng_reader *reader, ipng_png *png,
                                  uint32_t len) {
  if (len == 0 || len % 3U != 0 || len > 768U) {
    return IPNG_ERR_FORMAT;
  }
  png->palette_size = (uint16_t)(len / 3U);
  for (uint16_t i = 0; i < png->palette_size; i++) {
    const int r = ipng_reader_byte(reader);
    const int g = ipng_reader_byte(reader);
    const int b = ipng_reader_byte(reader);
    if (r < 0 || g < 0 || b < 0) {
      return IPNG_ERR_IO;
    }
    png->palette[i].r = (uint8_t)r;
    png->palette[i].g = (uint8_t)g;
    png->palette[i].b = (uint8_t)b;
    png->palette[i].a = 255;
  }
  return IPNG_OK;
}

static ipng_result ipng_read_trns(ipng_reader *reader, ipng_png *png,
                                  uint32_t len) {
  if (png->info.color_type == 3) {
    const uint32_t count = len < png->palette_size ? len : png->palette_size;
    for (uint32_t i = 0; i < count; i++) {
      const int a = ipng_reader_byte(reader);
      if (a < 0) {
        return IPNG_ERR_IO;
      }
      png->palette[i].a = (uint8_t)a;
    }
    return ipng_skip(reader, len - count) ? IPNG_OK : IPNG_ERR_IO;
  }
  if (png->info.color_type == 0) {
    if (len != 2) {
      return IPNG_ERR_FORMAT;
    }
    png->has_gray_trns = ipng_read_u16(reader, &png->transparent_gray);
    return png->has_gray_trns ? IPNG_OK : IPNG_ERR_IO;
  }
  if (png->info.color_type == 2) {
    if (len != 6) {
      return IPNG_ERR_FORMAT;
    }
    png->has_rgb_trns =
        ipng_read_u16(reader, &png->transparent_r) &&
        ipng_read_u16(reader, &png->transparent_g) &&
        ipng_read_u16(reader, &png->transparent_b);
    return png->has_rgb_trns ? IPNG_OK : IPNG_ERR_IO;
  }
  return IPNG_ERR_FORMAT;
}

static ipng_result ipng_decode(ipng_reader *reader, const ipng_render *render,
                               ipng_info *out_info) {
  ipng_png png;
  bool have_ihdr = false;
  bool have_plte = false;
  bool have_trns = false;
  memset(&png, 0, sizeof(png));
  png.render = render;
  png.transparent_gray = 0xffffU;
  png.transparent_r = png.transparent_g = png.transparent_b = 0xffffU;
  for (uint16_t i = 0; i < 256; i++) {
    png.palette[i].a = 255;
  }
  if (reader == NULL || !ipng_signature(reader)) {
    return IPNG_ERR_FORMAT;
  }
  for (;;) {
    uint32_t len = 0;
    uint32_t type = 0;
    ipng_result result = IPNG_OK;
    if (!ipng_read_chunk_header(reader, &len, &type)) {
      return IPNG_ERR_IO;
    }
    if (type == IPNG_CHUNK_IHDR) {
      if (have_ihdr) {
        return IPNG_ERR_FORMAT;
      }
      result = ipng_parse_ihdr(reader, len, &png.info);
      have_ihdr = result == IPNG_OK;
      if (!ipng_skip(reader, 4)) {
        return IPNG_ERR_IO;
      }
      if (result != IPNG_OK) {
        return result;
      }
      continue;
    }
    if (!have_ihdr) {
      return IPNG_ERR_FORMAT;
    }
    if (type == IPNG_CHUNK_PLTE) {
      if (have_plte || have_trns ||
          (png.info.color_type != 2 && png.info.color_type != 3 &&
           png.info.color_type != 6)) {
        return IPNG_ERR_FORMAT;
      }
      result = ipng_read_plte(reader, &png, len);
      if (result != IPNG_OK) {
        return result;
      }
      if (!ipng_skip(reader, 4)) {
        return IPNG_ERR_IO;
      }
      have_plte = true;
    } else if (type == IPNG_CHUNK_tRNS) {
      if (have_trns || png.info.color_type == 4 || png.info.color_type == 6 ||
          (png.info.color_type == 3 && !have_plte)) {
        return IPNG_ERR_FORMAT;
      }
      result = ipng_read_trns(reader, &png, len);
      if (result != IPNG_OK) {
        return result;
      }
      if (!ipng_skip(reader, 4)) {
        return IPNG_ERR_IO;
      }
      have_trns = true;
    } else if (type == IPNG_CHUNK_IDAT) {
      ipng_idat idat;
      if (png.info.color_type == 3 && !have_plte) {
        return IPNG_ERR_FORMAT;
      }
      png.channels = ipng_channels(png.info.color_type);
      png.filter_bpp =
          (uint8_t)((png.channels * png.info.bit_depth + 7U) >> 3);
      png.filter_bpp = png.filter_bpp == 0 ? 1 : png.filter_bpp;
      if (!ipng_row_bytes_checked(png.info.width, png.channels,
                                  png.info.bit_depth, &png.max_row_bytes)) {
        return IPNG_ERR_UNSUPPORTED;
      }
      if (!ipng_setup_output(&png, render)) {
        return IPNG_ERR_MEMORY;
      }
      png.row = (uint8_t *)IPNG_MALLOC(png.max_row_bytes);
      png.prev = (uint8_t *)IPNG_MALLOC(png.max_row_bytes);
      if (png.row == NULL || png.prev == NULL ||
          !ipng_dither_init(&png.dither, png.output_w,
                            render != NULL ? render->dither
                                           : (uint8_t)IPNG_DITHER_THRESHOLD)) {
        result = IPNG_ERR_MEMORY;
      } else {
        memset(png.prev, 0, png.max_row_bytes);
        png.pass = 0;
        png.need_filter = 0;
        idat.reader = reader;
        idat.remaining = len;
        result = ipng_inflate_zlib(&idat, &png);
      }
      if (png.row != NULL) {
        IPNG_FREE(png.row);
      }
      if (png.prev != NULL) {
        IPNG_FREE(png.prev);
      }
      if (png.scale_sum != NULL) {
        IPNG_FREE(png.scale_sum);
      }
      if (png.scale_count != NULL) {
        IPNG_FREE(png.scale_count);
      }
      ipng_dither_free(&png.dither);
      if (out_info != NULL) {
        *out_info = png.info;
      }
      return result;
    } else if (type == IPNG_CHUNK_IEND) {
      return IPNG_ERR_FORMAT;
    } else if (!ipng_skip_chunk_tail(reader, len)) {
      return IPNG_ERR_IO;
    }
    if (result != IPNG_OK) {
      return result;
    }
  }
}

#endif // INKPNG_H
