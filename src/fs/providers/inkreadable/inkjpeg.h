/*
 * InkJPEG - header-only baseline JPEG decoder and 1-bit renderer.
 * Targets small e-ink displays: streams MCU-by-MCU, converts to luma, supports
 * downscaling-to-fit, and applies small-row error diffusion dithering.
 * (c) Remixer Dec 2026 | CC BY-NC-SA 3.0
 * Distributed as a part of PocketInkOS https://github.com/remixer-dec/PocketInkOS
 */

#ifndef INKJPEG_H
#define INKJPEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef IJPG_MALLOC
#define IJPG_MALLOC malloc
#define IJPG_FREE free
#endif

#ifndef IJPG_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define IJPG_INLINE static inline __attribute__((always_inline))
#else
#define IJPG_INLINE static inline
#endif
#endif

#ifndef IJPG_MAX_DIMENSION
#define IJPG_MAX_DIMENSION 16384U
#endif

#ifndef IJPG_MAX_PROGRESSIVE_COEFF_BYTES
#define IJPG_MAX_PROGRESSIVE_COEFF_BYTES (7ULL * 1024ULL * 1024ULL)
#endif

#ifndef IJPG_PROGRESSIVE_DC_FALLBACK
#define IJPG_PROGRESSIVE_DC_FALLBACK 1
#endif

#ifndef IJPG_TRACE_PROGRESS
#define IJPG_TRACE_PROGRESS(scan, result) ((void)0)
#endif

#ifndef IJPG_TRACE_PROGRESS_BLOCK
#define IJPG_TRACE_PROGRESS_BLOCK(reason, comp, block, k, value) ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  IJPG_OK = 0,
  IJPG_ERR_IO = -1,
  IJPG_ERR_FORMAT = -2,
  IJPG_ERR_UNSUPPORTED = -3,
  IJPG_ERR_MEMORY = -4,
} ijpg_result;

typedef enum {
  IJPG_DITHER_THRESHOLD = 0,
  IJPG_DITHER_ATKINSON = 1,
  IJPG_DITHER_SIERRA_TWO_ROW = 2,
  IJPG_DITHER_SIMPLE2D = 3,
} ijpg_dither_mode;

typedef enum {
  IJPG_SCALE_NONE = 0,
  IJPG_SCALE_FIT = 1,
} ijpg_scale_mode;

typedef struct {
  void *user;
  int (*read)(void *user);
} ijpg_reader;

typedef struct {
  uint16_t width;
  uint16_t height;
  uint8_t components;
  uint8_t max_h;
  uint8_t max_v;
} ijpg_info;

typedef void (*ijpg_pixel_fn)(void *user, int16_t x, int16_t y);

typedef struct {
  void *user;
  ijpg_pixel_fn pixel;
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
  uint8_t dither;
  uint8_t scale;
} ijpg_render;

static inline ijpg_result ijpg_read_info(ijpg_reader *reader, ijpg_info *info);
static inline ijpg_result ijpg_decode(ijpg_reader *reader,
                                      const ijpg_render *render,
                                      ijpg_info *out_info);

#ifdef __cplusplus
}
#endif

#define IJPG_MARKER_SOI 0xd8U
#define IJPG_MARKER_EOI 0xd9U
#define IJPG_MARKER_SOS 0xdaU
#define IJPG_MARKER_DQT 0xdbU
#define IJPG_MARKER_DHT 0xc4U
#define IJPG_MARKER_DRI 0xddU

typedef struct {
  int16_t *row0;
  int16_t *row1;
  int16_t *row2;
  uint16_t width;
  int32_t current_y;
  uint8_t mode;
} ijpg_dither;

typedef struct {
  uint8_t id;
  uint8_t h;
  uint8_t v;
  uint8_t tq;
  uint8_t td;
  uint8_t ta;
  int32_t pred;
} ijpg_component;

typedef struct {
  uint8_t counts[16];
  uint8_t values[256];
  int32_t mincode[17];
  int32_t maxcode[18];
  int16_t valptr[17];
  uint8_t lookup_sym[256];
  uint8_t lookup_len[256];
  uint8_t ready;
} ijpg_huff;

typedef struct {
  ijpg_reader *reader;
  uint32_t bits;
  uint8_t bit_count;
  uint8_t marker;
  uint8_t failed;
} ijpg_bits;

typedef struct {
  ijpg_info info;
  ijpg_component comp[4];
  uint8_t comp_count;
  uint8_t progressive;
  uint8_t scan_count;
  uint8_t scan_comp[4];
  uint8_t scan_ss;
  uint8_t scan_se;
  uint8_t scan_ah;
  uint8_t scan_al;
  uint16_t quant[4][64];
  uint8_t has_quant[4];
  ijpg_huff huff[2][4];
  uint16_t restart_interval;
  uint32_t mcu_cols;
  uint32_t mcu_rows;
  uint8_t mcu_w;
  uint8_t mcu_h;
  uint8_t sample[4][4][64];
  ijpg_bits bits;
  const ijpg_render *render;
  ijpg_dither dither;
  uint16_t output_w;
  uint16_t output_h;
  int32_t output_x;
  int32_t output_y;
  uint8_t scaled;
  uint64_t *scale_sum;
  uint32_t *scale_count;
  uint32_t scale_y;
  uint8_t scale_row_active;
  uint8_t *row_luma;
  uint16_t row_luma_h;
  int16_t *coeff[4];
  uint32_t block_cols[4];
  uint32_t block_rows[4];
  uint32_t actual_block_cols[4];
  uint32_t actual_block_rows[4];
  uint32_t block_count[4];
} ijpg_jpeg;

static const uint8_t ijpg_zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static const int16_t ijpg_idct_basis[8][8] = {
    {2896, 4017, 3784, 3406, 2896, 2276, 1567, 799},
    {2896, 3406, 1567, -799, -2896, -4017, -3784, -2276},
    {2896, 2276, -1567, -4017, -2896, 799, 3784, 3406},
    {2896, 799, -3784, -2276, 2896, 3406, -1567, -4017},
    {2896, -799, -3784, 2276, 2896, -3406, -1567, 4017},
    {2896, -2276, -1567, 4017, -2896, -799, 3784, -3406},
    {2896, -3406, 1567, 799, -2896, 4017, -3784, 2276},
    {2896, -4017, 3784, -3406, 2896, -2276, 1567, -799},
};

IJPG_INLINE int ijpg_reader_byte(ijpg_reader *reader) {
  return (reader != NULL && reader->read != NULL) ? reader->read(reader->user)
                                                  : -1;
}

static bool ijpg_read_exact(ijpg_reader *reader, uint8_t *out, uint16_t len) {
  if (out == NULL) {
    for (uint16_t i = 0; i < len; i++) {
      if (ijpg_reader_byte(reader) < 0) {
        return false;
      }
    }
    return true;
  }
  for (uint16_t i = 0; i < len; i++) {
    const int c = ijpg_reader_byte(reader);
    if (c < 0) {
      return false;
    }
    out[i] = (uint8_t)c;
  }
  return true;
}

static bool ijpg_skip(ijpg_reader *reader, uint16_t len) {
  return ijpg_read_exact(reader, NULL, len);
}

static bool ijpg_read_u16(ijpg_reader *reader, uint16_t *out) {
  uint8_t b[2];
  if (!ijpg_read_exact(reader, b, 2)) {
    return false;
  }
  *out = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
  return true;
}

IJPG_INLINE uint8_t ijpg_clamp_u8(int32_t value) {
  value = value < 0 ? 0 : value;
  value = value > 255 ? 255 : value;
  return (uint8_t)value;
}

IJPG_INLINE void ijpg_plot_black(const ijpg_render *r, int32_t x, int32_t y) {
  if (r != NULL && r->pixel != NULL &&
      ((uint32_t)x + 32768U) <= 65535U &&
      ((uint32_t)y + 32768U) <= 65535U) {
    r->pixel(r->user, (int16_t)x, (int16_t)y);
  }
}

static uint16_t ijpg_fit_dim(uint32_t src, uint32_t other_src, uint16_t target,
                             uint16_t other_target) {
  if (src == 0 || other_src == 0 || target == 0 || other_target == 0) {
    return 0;
  }
  if ((uint64_t)other_src * target <= (uint64_t)src * other_target) {
    return target;
  }
  uint64_t value = ((uint64_t)src * other_target) / other_src;
  value += value == 0;
  return value > target ? target : (uint16_t)value;
}

static void ijpg_dither_free(ijpg_dither *d) {
  if (d->row0 != NULL) {
    IJPG_FREE(d->row0);
  }
  if (d->row1 != NULL) {
    IJPG_FREE(d->row1);
  }
  if (d->row2 != NULL) {
    IJPG_FREE(d->row2);
  }
  memset(d, 0, sizeof(*d));
  d->current_y = -1;
}

static bool ijpg_dither_init(ijpg_dither *d, uint16_t width, uint8_t mode) {
  memset(d, 0, sizeof(*d));
  d->current_y = -1;
  d->width = width;
  d->mode = mode <= (uint8_t)IJPG_DITHER_SIMPLE2D
                ? mode
                : (uint8_t)IJPG_DITHER_THRESHOLD;
  if (d->mode == IJPG_DITHER_THRESHOLD || width == 0) {
    return true;
  }
  const size_t bytes = ((size_t)width + 4U) * sizeof(int16_t);
  d->row0 = (int16_t *)IJPG_MALLOC(bytes);
  d->row1 = (int16_t *)IJPG_MALLOC(bytes);
  d->row2 = (int16_t *)IJPG_MALLOC(bytes);
  if (d->row0 == NULL || d->row1 == NULL || d->row2 == NULL) {
    ijpg_dither_free(d);
    return false;
  }
  memset(d->row0, 0, bytes);
  memset(d->row1, 0, bytes);
  memset(d->row2, 0, bytes);
  return true;
}

static void ijpg_dither_clear(ijpg_dither *d) {
  if (d->row0 == NULL) {
    return;
  }
  const size_t bytes = ((size_t)d->width + 4U) * sizeof(int16_t);
  memset(d->row0, 0, bytes);
  memset(d->row1, 0, bytes);
  memset(d->row2, 0, bytes);
}

static void ijpg_dither_roll(ijpg_dither *d) {
  if (d->row0 == NULL) {
    return;
  }
  int16_t *old = d->row0;
  d->row0 = d->row1;
  d->row1 = d->row2;
  d->row2 = old;
  memset(d->row2, 0, ((size_t)d->width + 4U) * sizeof(int16_t));
}

static void ijpg_dither_row(ijpg_dither *d, int32_t y) {
  if (d->current_y == y) {
    return;
  }
  if (d->current_y >= 0 && y == d->current_y + 1) {
    ijpg_dither_roll(d);
  } else {
    ijpg_dither_clear(d);
  }
  d->current_y = y;
}

IJPG_INLINE void ijpg_dither_add(int16_t *row, uint16_t width, int32_t x,
                                 int16_t value) {
  if (row != NULL && (uint32_t)x < (uint32_t)width) {
    int32_t sum = (int32_t)row[x + 2] + value;
    sum = sum < -4096 ? -4096 : sum;
    sum = sum > 4096 ? 4096 : sum;
    row[x + 2] = (int16_t)sum;
  }
}

IJPG_INLINE uint8_t ijpg_dither_pixel(ijpg_dither *d, uint16_t x, int32_t y,
                                      uint8_t gray) {
  ijpg_dither_row(d, y);
  if (d->mode == IJPG_DITHER_THRESHOLD || d->row0 == NULL) {
    return gray < 128;
  }

  const size_t ix = (size_t)x + 2U;
  int32_t adjusted = (int32_t)gray + d->row0[ix];
  d->row0[ix] = 0;
  adjusted = adjusted < 0 ? 0 : adjusted;
  adjusted = adjusted > 255 ? 255 : adjusted;
  const uint8_t black = adjusted < 128;
  const int16_t error = (int16_t)(adjusted - (black ? 0 : 255));

  if (d->mode == IJPG_DITHER_ATKINSON) {
    const int16_t e = (int16_t)(error / 8);
    ijpg_dither_add(d->row0, d->width, (int32_t)x + 1, e);
    ijpg_dither_add(d->row0, d->width, (int32_t)x + 2, e);
    ijpg_dither_add(d->row1, d->width, (int32_t)x - 1, e);
    ijpg_dither_add(d->row1, d->width, x, e);
    ijpg_dither_add(d->row1, d->width, (int32_t)x + 1, e);
    ijpg_dither_add(d->row2, d->width, x, e);
  } else if (d->mode == IJPG_DITHER_SIERRA_TWO_ROW) {
    ijpg_dither_add(d->row0, d->width, (int32_t)x + 1,
                    (int16_t)(error * 4 / 16));
    ijpg_dither_add(d->row0, d->width, (int32_t)x + 2,
                    (int16_t)(error * 3 / 16));
    ijpg_dither_add(d->row1, d->width, (int32_t)x - 2,
                    (int16_t)(error / 16));
    ijpg_dither_add(d->row1, d->width, (int32_t)x - 1,
                    (int16_t)(error * 2 / 16));
    ijpg_dither_add(d->row1, d->width, x, (int16_t)(error * 3 / 16));
    ijpg_dither_add(d->row1, d->width, (int32_t)x + 1,
                    (int16_t)(error * 2 / 16));
    ijpg_dither_add(d->row1, d->width, (int32_t)x + 2,
                    (int16_t)(error / 16));
  } else {
    const int16_t e = (int16_t)(error / 4);
    ijpg_dither_add(d->row0, d->width, (int32_t)x + 1, (int16_t)(e * 2));
    ijpg_dither_add(d->row1, d->width, x, e);
    ijpg_dither_add(d->row1, d->width, (int32_t)x + 1, e);
  }
  return black;
}

static void ijpg_flush_scale_row(ijpg_jpeg *jpg, const ijpg_render *r) {
  for (uint16_t sx = 0; sx < jpg->output_w; sx++) {
    if (jpg->scale_count[sx] == 0) {
      continue;
    }
    const uint8_t avg = (uint8_t)(jpg->scale_sum[sx] / jpg->scale_count[sx]);
    if (ijpg_dither_pixel(&jpg->dither, sx, (int32_t)jpg->scale_y, avg)) {
      ijpg_plot_black(r, (int32_t)jpg->output_x + sx,
                      (int32_t)jpg->output_y + jpg->scale_y);
    }
  }
  jpg->scale_row_active = 0;
}

IJPG_INLINE void ijpg_emit_scaled(ijpg_jpeg *jpg, const ijpg_render *r,
                                  uint32_t x, uint32_t y, uint8_t gray) {
  const uint32_t dx = ((uint64_t)x * jpg->output_w) / jpg->info.width;
  const uint32_t dy = ((uint64_t)y * jpg->output_h) / jpg->info.height;
  if (dx >= jpg->output_w || dy >= jpg->output_h) {
    return;
  }
  if (jpg->scale_row_active && dy != jpg->scale_y) {
    ijpg_flush_scale_row(jpg, r);
    memset(jpg->scale_sum, 0, (size_t)jpg->output_w * sizeof(uint64_t));
    memset(jpg->scale_count, 0, (size_t)jpg->output_w * sizeof(uint32_t));
  }
  jpg->scale_y = dy;
  jpg->scale_row_active = 1;
  jpg->scale_sum[dx] += gray;
  jpg->scale_count[dx]++;
}

IJPG_INLINE void ijpg_emit_unscaled(ijpg_jpeg *jpg, const ijpg_render *r,
                                    uint32_t x, uint32_t y, uint8_t gray) {
  if (x >= r->width || y >= r->height) {
    return;
  }
  if (ijpg_dither_pixel(&jpg->dither, (uint16_t)x, (int32_t)y, gray)) {
    ijpg_plot_black(r, jpg->output_x + (int32_t)x,
                    jpg->output_y + (int32_t)y);
  }
}

static void ijpg_flush_scale(ijpg_jpeg *jpg) {
  if (jpg == NULL || !jpg->scale_row_active || jpg->scale_sum == NULL ||
      jpg->scale_count == NULL || jpg->render == NULL ||
      jpg->render->pixel == NULL) {
    return;
  }
  ijpg_flush_scale_row(jpg, jpg->render);
}

static bool ijpg_setup_output(ijpg_jpeg *jpg, const ijpg_render *render) {
  jpg->output_w = jpg->info.width;
  jpg->output_h = jpg->info.height;
  jpg->output_x = render != NULL ? render->x : 0;
  jpg->output_y = render != NULL ? render->y : 0;
  if (render != NULL && render->scale == IJPG_SCALE_FIT && render->width != 0 &&
      render->height != 0 &&
      (jpg->info.width > render->width || jpg->info.height > render->height)) {
    jpg->output_w =
        ijpg_fit_dim(jpg->info.width, jpg->info.height, render->width,
                     render->height);
    jpg->output_h =
        ijpg_fit_dim(jpg->info.height, jpg->info.width, render->height,
                     render->width);
    if (jpg->output_w == 0 || jpg->output_h == 0) {
      return false;
    }
    jpg->output_x =
        (int32_t)render->x + (int32_t)((render->width - jpg->output_w) / 2U);
    jpg->output_y =
        (int32_t)render->y + (int32_t)((render->height - jpg->output_h) / 2U);
    jpg->scaled = 1;
    jpg->scale_sum =
        (uint64_t *)IJPG_MALLOC((size_t)jpg->output_w * sizeof(uint64_t));
    jpg->scale_count =
        (uint32_t *)IJPG_MALLOC((size_t)jpg->output_w * sizeof(uint32_t));
    if (jpg->scale_sum == NULL || jpg->scale_count == NULL) {
      return false;
    }
    memset(jpg->scale_sum, 0, (size_t)jpg->output_w * sizeof(uint64_t));
    memset(jpg->scale_count, 0, (size_t)jpg->output_w * sizeof(uint32_t));
  }
  return true;
}

static void ijpg_free_output(ijpg_jpeg *jpg) {
  ijpg_flush_scale(jpg);
  ijpg_dither_free(&jpg->dither);
  if (jpg->scale_sum != NULL) {
    IJPG_FREE(jpg->scale_sum);
  }
  if (jpg->scale_count != NULL) {
    IJPG_FREE(jpg->scale_count);
  }
  if (jpg->row_luma != NULL) {
    IJPG_FREE(jpg->row_luma);
    jpg->row_luma = NULL;
    jpg->row_luma_h = 0;
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (jpg->coeff[i] != NULL) {
      IJPG_FREE(jpg->coeff[i]);
      jpg->coeff[i] = NULL;
    }
  }
}

static int ijpg_next_marker(ijpg_reader *reader) {
  int c = 0;
  do {
    c = ijpg_reader_byte(reader);
    if (c < 0) {
      return -1;
    }
  } while (c != 0xff);
  do {
    c = ijpg_reader_byte(reader);
    if (c < 0) {
      return -1;
    }
  } while (c == 0xff);
  return c;
}

static bool ijpg_marker_has_length(uint8_t marker) {
  return marker != IJPG_MARKER_SOI && marker != IJPG_MARKER_EOI &&
         !(marker >= 0xd0U && marker <= 0xd7U) && marker != 0x01U;
}

static ijpg_result ijpg_parse_dqt(ijpg_jpeg *jpg, ijpg_reader *reader,
                                  uint16_t len) {
  if (len < 2) {
    return IJPG_ERR_FORMAT;
  }
  len = (uint16_t)(len - 2U);
  while (len > 0) {
    const int spec = ijpg_reader_byte(reader);
    if (spec < 0) {
      return IJPG_ERR_IO;
    }
    len--;
    const uint8_t precision = (uint8_t)(spec >> 4);
    const uint8_t tq = (uint8_t)(spec & 15);
    if (precision > 1 || tq >= 4) {
      return IJPG_ERR_UNSUPPORTED;
    }
    const uint16_t need = precision ? 128U : 64U;
    if (len < need) {
      return IJPG_ERR_FORMAT;
    }
    for (uint8_t i = 0; i < 64; i++) {
      uint16_t v = 0;
      if (precision) {
        if (!ijpg_read_u16(reader, &v)) {
          return IJPG_ERR_IO;
        }
      } else {
        const int c = ijpg_reader_byte(reader);
        if (c < 0) {
          return IJPG_ERR_IO;
        }
        v = (uint16_t)c;
      }
      jpg->quant[tq][ijpg_zigzag[i]] = v;
    }
    jpg->has_quant[tq] = 1;
    len = (uint16_t)(len - need);
  }
  return IJPG_OK;
}

static bool ijpg_huff_build(ijpg_huff *h) {
  int32_t code = 0;
  int32_t k = 0;
  memset(h->mincode, 0, sizeof(h->mincode));
  memset(h->valptr, 0, sizeof(h->valptr));
  memset(h->lookup_len, 0, sizeof(h->lookup_len));
  memset(h->lookup_sym, 0, sizeof(h->lookup_sym));
  for (uint8_t i = 0; i < 18; i++) {
    h->maxcode[i] = -1;
  }
  for (uint8_t i = 1; i <= 16; i++) {
    const uint8_t count = h->counts[i - 1U];
    code <<= 1;
    if (code + count > (1L << i)) {
      return false;
    }
    if (count == 0) {
      h->maxcode[i] = -1;
    } else {
      h->valptr[i] = (int16_t)k;
      h->mincode[i] = code;
      h->maxcode[i] = code + count - 1;
      for (uint8_t n = 0; n < count && i <= 8; n++) {
        const uint16_t prefix = (uint16_t)((code + n) << (8U - i));
        const uint16_t fill = (uint16_t)(1U << (8U - i));
        for (uint16_t j = 0; j < fill; j++) {
          h->lookup_len[prefix | j] = i;
          h->lookup_sym[prefix | j] = h->values[k + n];
        }
      }
      code += count;
      k += count;
    }
  }
  h->maxcode[17] = 0x7fff;
  h->ready = 1;
  return k > 0 && k <= 256;
}

static ijpg_result ijpg_parse_dht(ijpg_jpeg *jpg, ijpg_reader *reader,
                                  uint16_t len) {
  if (len < 2) {
    return IJPG_ERR_FORMAT;
  }
  len = (uint16_t)(len - 2U);
  while (len > 0) {
    const int spec = ijpg_reader_byte(reader);
    if (spec < 0) {
      return IJPG_ERR_IO;
    }
    len--;
    const uint8_t tc = (uint8_t)(spec >> 4);
    const uint8_t th = (uint8_t)(spec & 15);
    if (tc > 1 || th >= 4 || len < 16) {
      return IJPG_ERR_FORMAT;
    }
    ijpg_huff *h = &jpg->huff[tc][th];
    if (!ijpg_read_exact(reader, h->counts, 16)) {
      return IJPG_ERR_IO;
    }
    len = (uint16_t)(len - 16U);
    uint16_t total = 0;
    for (uint8_t i = 0; i < 16; i++) {
      total = (uint16_t)(total + h->counts[i]);
    }
    if (total > 256 || len < total) {
      return IJPG_ERR_FORMAT;
    }
    if (!ijpg_read_exact(reader, h->values, total)) {
      return IJPG_ERR_IO;
    }
    len = (uint16_t)(len - total);
    if (!ijpg_huff_build(h)) {
      return IJPG_ERR_FORMAT;
    }
  }
  return IJPG_OK;
}

static ijpg_result ijpg_parse_sof(ijpg_jpeg *jpg, ijpg_reader *reader,
                                  uint16_t len, uint8_t marker) {
  if (marker != 0xc0U && marker != 0xc1U && marker != 0xc2U) {
    return IJPG_ERR_UNSUPPORTED;
  }
  jpg->progressive = marker == 0xc2U;
  uint8_t hdr[6];
  if (len < 8 || !ijpg_read_exact(reader, hdr, sizeof(hdr))) {
    return IJPG_ERR_FORMAT;
  }
  if (hdr[0] != 8) {
    return IJPG_ERR_UNSUPPORTED;
  }
  jpg->info.height = (uint16_t)(((uint16_t)hdr[1] << 8) | hdr[2]);
  jpg->info.width = (uint16_t)(((uint16_t)hdr[3] << 8) | hdr[4]);
  jpg->comp_count = hdr[5];
  if ((jpg->comp_count != 1 && jpg->comp_count != 3 &&
       jpg->comp_count != 4) ||
      jpg->info.width == 0 ||
      jpg->info.height == 0 || jpg->info.width > IJPG_MAX_DIMENSION ||
      jpg->info.height > IJPG_MAX_DIMENSION ||
      len != (uint16_t)(8U + 3U * jpg->comp_count)) {
    return IJPG_ERR_UNSUPPORTED;
  }
  jpg->info.max_h = 1;
  jpg->info.max_v = 1;
  for (uint8_t i = 0; i < jpg->comp_count; i++) {
    uint8_t c[3];
    if (!ijpg_read_exact(reader, c, sizeof(c))) {
      return IJPG_ERR_IO;
    }
    for (uint8_t prev = 0; prev < i; prev++) {
      if (jpg->comp[prev].id == c[0]) {
        return IJPG_ERR_FORMAT;
      }
    }
    jpg->comp[i].id = c[0];
    jpg->comp[i].h = (uint8_t)(c[1] >> 4);
    jpg->comp[i].v = (uint8_t)(c[1] & 15);
    jpg->comp[i].tq = c[2];
    if (jpg->comp[i].h == 0 || jpg->comp[i].v == 0 || jpg->comp[i].h > 2 ||
        jpg->comp[i].v > 2 || jpg->comp[i].tq >= 4) {
      return IJPG_ERR_UNSUPPORTED;
    }
    jpg->info.max_h =
        jpg->comp[i].h > jpg->info.max_h ? jpg->comp[i].h : jpg->info.max_h;
    jpg->info.max_v =
        jpg->comp[i].v > jpg->info.max_v ? jpg->comp[i].v : jpg->info.max_v;
  }
  jpg->info.components = jpg->comp_count;
  jpg->mcu_w = (uint8_t)(jpg->info.max_h * 8U);
  jpg->mcu_h = (uint8_t)(jpg->info.max_v * 8U);
  jpg->mcu_cols = (jpg->info.width + jpg->mcu_w - 1U) / jpg->mcu_w;
  jpg->mcu_rows = (jpg->info.height + jpg->mcu_h - 1U) / jpg->mcu_h;
  return IJPG_OK;
}

static ijpg_result ijpg_parse_dri(ijpg_jpeg *jpg, ijpg_reader *reader,
                                  uint16_t len) {
  if (len != 4 || !ijpg_read_u16(reader, &jpg->restart_interval)) {
    return IJPG_ERR_FORMAT;
  }
  return IJPG_OK;
}

static int ijpg_component_index(const ijpg_jpeg *jpg, uint8_t id) {
  for (uint8_t i = 0; i < jpg->comp_count; i++) {
    if (jpg->comp[i].id == id) {
      return i;
    }
  }
  return -1;
}

IJPG_INLINE uint32_t ijpg_component_actual_block_cols(const ijpg_jpeg *jpg,
                                                      const ijpg_component *c) {
  return ((uint32_t)jpg->info.width * c->h + jpg->info.max_h * 8U - 1U) /
         (jpg->info.max_h * 8U);
}

IJPG_INLINE uint32_t ijpg_component_actual_block_rows(const ijpg_jpeg *jpg,
                                                      const ijpg_component *c) {
  return ((uint32_t)jpg->info.height * c->v + jpg->info.max_v * 8U - 1U) /
         (jpg->info.max_v * 8U);
}

static ijpg_result ijpg_parse_sos(ijpg_jpeg *jpg, ijpg_reader *reader,
                                  uint16_t len) {
  const int count = ijpg_reader_byte(reader);
  if (count < 0) {
    return IJPG_ERR_IO;
  }
  if ((!jpg->progressive && count != 1 && count != jpg->comp_count) ||
      (jpg->progressive && (count < 1 || count > jpg->comp_count)) ||
      len != (uint16_t)(6U + 2U * count)) {
    return IJPG_ERR_UNSUPPORTED;
  }
  jpg->scan_count = (uint8_t)count;
  for (uint8_t i = 0; i < (uint8_t)count; i++) {
    uint8_t c[2];
    if (!ijpg_read_exact(reader, c, sizeof(c))) {
      return IJPG_ERR_IO;
    }
    const int ci = ijpg_component_index(jpg, c[0]);
    if (ci < 0) {
      return IJPG_ERR_FORMAT;
    }
    if (!jpg->progressive && count != 1 && ci != i) {
      return IJPG_ERR_UNSUPPORTED;
    }
    for (uint8_t prev = 0; prev < i; prev++) {
      if (jpg->scan_comp[prev] == (uint8_t)ci) {
        return IJPG_ERR_FORMAT;
      }
    }
    jpg->scan_comp[i] = (uint8_t)ci;
    jpg->comp[ci].td = (uint8_t)(c[1] >> 4);
    jpg->comp[ci].ta = (uint8_t)(c[1] & 15);
    if (jpg->comp[ci].td >= 4 || jpg->comp[ci].ta >= 4) {
      return IJPG_ERR_FORMAT;
    }
  }
  uint8_t tail[3];
  if (!ijpg_read_exact(reader, tail, sizeof(tail))) {
    return IJPG_ERR_IO;
  }
  jpg->scan_ss = tail[0];
  jpg->scan_se = tail[1];
  jpg->scan_ah = (uint8_t)(tail[2] >> 4);
  jpg->scan_al = (uint8_t)(tail[2] & 15);
  if (!jpg->progressive) {
    return tail[0] == 0 && tail[1] == 63 && tail[2] == 0
               ? IJPG_OK
               : IJPG_ERR_UNSUPPORTED;
  }
  if (jpg->scan_ss > 63 || jpg->scan_se > 63 || jpg->scan_ss > jpg->scan_se ||
      jpg->scan_ah > 13 || jpg->scan_al > 13 ||
      (jpg->scan_ss == 0 && jpg->scan_se != 0) ||
      (jpg->scan_ss != 0 && count != 1)) {
    return IJPG_ERR_UNSUPPORTED;
  }
  return IJPG_OK;
}

static ijpg_result ijpg_parse_headers(ijpg_jpeg *jpg, ijpg_reader *reader,
                                      bool stop_at_sof) {
  memset(jpg, 0, sizeof(*jpg));
  int marker = ijpg_next_marker(reader);
  if (marker != IJPG_MARKER_SOI) {
    return IJPG_ERR_FORMAT;
  }
  for (;;) {
    marker = ijpg_next_marker(reader);
    if (marker < 0) {
      return IJPG_ERR_IO;
    }
    if (marker == IJPG_MARKER_EOI) {
      return IJPG_ERR_FORMAT;
    }
    if (!ijpg_marker_has_length((uint8_t)marker)) {
      continue;
    }
    uint16_t len = 0;
    if (!ijpg_read_u16(reader, &len) || len < 2) {
      return IJPG_ERR_FORMAT;
    }
    ijpg_result result = IJPG_OK;
    if (marker == IJPG_MARKER_DQT) {
      result = ijpg_parse_dqt(jpg, reader, len);
    } else if (marker == IJPG_MARKER_DHT) {
      result = ijpg_parse_dht(jpg, reader, len);
    } else if (marker == IJPG_MARKER_DRI) {
      result = ijpg_parse_dri(jpg, reader, len);
    } else if (marker == IJPG_MARKER_SOS) {
      if (jpg->info.width == 0) {
        return IJPG_ERR_FORMAT;
      }
      return ijpg_parse_sos(jpg, reader, len);
    } else if ((uint8_t)marker == 0xf7U) {
      return IJPG_ERR_UNSUPPORTED;
    } else if ((uint8_t)marker >= 0xc0U && (uint8_t)marker <= 0xcfU &&
               marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
      result = ijpg_parse_sof(jpg, reader, len, (uint8_t)marker);
      if (result == IJPG_OK && stop_at_sof) {
        return IJPG_OK;
      }
    } else {
      result = ijpg_skip(reader, (uint16_t)(len - 2U)) ? IJPG_OK : IJPG_ERR_IO;
    }
    if (result != IJPG_OK) {
      return result;
    }
  }
}

static inline ijpg_result ijpg_read_info(ijpg_reader *reader, ijpg_info *info) {
  if (reader == NULL || info == NULL) {
    return IJPG_ERR_FORMAT;
  }
  ijpg_jpeg *jpg = (ijpg_jpeg *)IJPG_MALLOC(sizeof(*jpg));
  if (jpg == NULL) {
    return IJPG_ERR_MEMORY;
  }
  const ijpg_result result = ijpg_parse_headers(jpg, reader, true);
  if (result == IJPG_OK) {
    *info = jpg->info;
  }
  IJPG_FREE(jpg);
  return result;
}

static void ijpg_bits_init(ijpg_bits *bits, ijpg_reader *reader) {
  memset(bits, 0, sizeof(*bits));
  bits->reader = reader;
}

static int ijpg_entropy_byte(ijpg_bits *bits) {
  const int c = ijpg_reader_byte(bits->reader);
  if (c < 0) {
    bits->failed = 1;
    return -1;
  }
  if (c != 0xff) {
    return c;
  }
  int marker = 0;
  do {
    marker = ijpg_reader_byte(bits->reader);
    if (marker < 0) {
      bits->failed = 1;
      return -1;
    }
  } while (marker == 0xff);
  if (marker == 0x00) {
    return 0xff;
  }
  bits->marker = (uint8_t)marker;
  return -1;
}

IJPG_INLINE bool ijpg_fill_bits(ijpg_bits *bits, uint8_t count) {
  while (bits->bit_count < count && bits->marker == 0 && !bits->failed) {
    const int c = ijpg_entropy_byte(bits);
    if (c < 0) {
      break;
    }
    bits->bits = (bits->bits << 8) | (uint8_t)c;
    bits->bit_count = (uint8_t)(bits->bit_count + 8U);
  }
  return bits->bit_count >= count && !bits->failed;
}

IJPG_INLINE int ijpg_peek_bits(ijpg_bits *bits, uint8_t count) {
  if (!ijpg_fill_bits(bits, count)) {
    return -1;
  }
  return (int)((bits->bits >> (bits->bit_count - count)) &
               ((1UL << count) - 1UL));
}

IJPG_INLINE int ijpg_get_bits(ijpg_bits *bits, uint8_t count) {
  const int value = ijpg_peek_bits(bits, count);
  if (value >= 0) {
    bits->bit_count = (uint8_t)(bits->bit_count - count);
  }
  return value;
}

IJPG_INLINE int ijpg_receive_extend(ijpg_bits *bits, uint8_t count) {
  if (count == 0) {
    return 0;
  }
  const int value = ijpg_get_bits(bits, count);
  if (value < 0) {
    return 0x7fffffff;
  }
  const int threshold = 1 << (count - 1U);
  return value - ((int)(value < threshold) * ((1 << count) - 1));
}

static int ijpg_huff_decode_symbol(ijpg_bits *bits, const ijpg_huff *h) {
  if (h == NULL || !h->ready) {
    return -1;
  }
  if (ijpg_fill_bits(bits, 8)) {
    const uint8_t peek =
        (uint8_t)((bits->bits >> (bits->bit_count - 8U)) & 255U);
    const uint8_t fast_len = h->lookup_len[peek];
    if (fast_len != 0) {
      bits->bit_count = (uint8_t)(bits->bit_count - fast_len);
      return h->lookup_sym[peek];
    }
  }

  int code = 0;
  for (uint8_t len = 1; len <= 16; len++) {
    const int bit = ijpg_get_bits(bits, 1);
    if (bit < 0) {
      return -1;
    }
    code = (code << 1) | bit;
    if (h->maxcode[len] >= 0 && code <= h->maxcode[len]) {
      const int idx = h->valptr[len] + code - h->mincode[len];
      return idx >= 0 && idx < 256 ? h->values[idx] : -1;
    }
  }
  return -1;
}

static ijpg_result ijpg_decode_block(ijpg_jpeg *jpg, ijpg_component *comp,
                                     int32_t block[64], uint8_t *dc_only) {
  memset(block, 0, 64U * sizeof(block[0]));
  *dc_only = 1;
  const ijpg_huff *hdc = &jpg->huff[0][comp->td];
  const ijpg_huff *hac = &jpg->huff[1][comp->ta];
  const uint16_t *quant = jpg->quant[comp->tq];
  if (!jpg->has_quant[comp->tq] || !hdc->ready || !hac->ready) {
    return IJPG_ERR_FORMAT;
  }

  const int dc_len = ijpg_huff_decode_symbol(&jpg->bits, hdc);
  if (dc_len < 0 || dc_len > 11) {
    return IJPG_ERR_FORMAT;
  }
  const int dc_delta = ijpg_receive_extend(&jpg->bits, (uint8_t)dc_len);
  if (dc_delta == 0x7fffffff) {
    return IJPG_ERR_FORMAT;
  }
  comp->pred += dc_delta;
  if (comp->pred < -32768 || comp->pred > 32767) {
    return IJPG_ERR_FORMAT;
  }
  block[0] = comp->pred * (int32_t)quant[0];

  for (uint8_t k = 1; k < 64;) {
    const int rs = ijpg_huff_decode_symbol(&jpg->bits, hac);
    if (rs < 0) {
      return IJPG_ERR_FORMAT;
    }
    const uint8_t run = (uint8_t)(rs >> 4);
    const uint8_t size = (uint8_t)(rs & 15);
    if (size == 0) {
      if (run == 15) {
        k = (uint8_t)(k + 16U);
        continue;
      }
      break;
    }
    k = (uint8_t)(k + run);
    if (k >= 64 || size > 10) {
      return IJPG_ERR_FORMAT;
    }
    const int ac = ijpg_receive_extend(&jpg->bits, size);
    if (ac == 0x7fffffff) {
      return IJPG_ERR_FORMAT;
    }
    const uint8_t zi = ijpg_zigzag[k];
    block[zi] = ac * (int32_t)quant[zi];
    *dc_only = 0;
    k++;
  }
  return IJPG_OK;
}

IJPG_INLINE int64_t ijpg_idct_dot8_i32_i16(const int32_t *a,
                                           const int16_t *b) {
  return (int64_t)a[0] * b[0] + (int64_t)a[1] * b[1] +
         (int64_t)a[2] * b[2] + (int64_t)a[3] * b[3] +
         (int64_t)a[4] * b[4] + (int64_t)a[5] * b[5] +
         (int64_t)a[6] * b[6] + (int64_t)a[7] * b[7];
}

IJPG_INLINE int64_t ijpg_idct_dot8_i64_i16_col(const int64_t *tmp,
                                               uint8_t x,
                                               const int16_t *b) {
  return tmp[x] * b[0] + tmp[8U + x] * b[1] + tmp[16U + x] * b[2] +
         tmp[24U + x] * b[3] + tmp[32U + x] * b[4] +
         tmp[40U + x] * b[5] + tmp[48U + x] * b[6] + tmp[56U + x] * b[7];
}

static void ijpg_idct_block(const int32_t in[64], uint8_t out[64],
                            uint8_t dc_only) {
  if (dc_only) {
    memset(out, ijpg_clamp_u8((in[0] >> 3) + 128), 64);
    return;
  }

  int64_t tmp[64];
  for (uint8_t y = 0; y < 8; y++) {
    const int32_t *row = in + y * 8U;
    for (uint8_t x = 0; x < 8; x++) {
      tmp[y * 8U + x] = ijpg_idct_dot8_i32_i16(row, ijpg_idct_basis[x]);
    }
  }
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {
      const int64_t sum =
          ijpg_idct_dot8_i64_i16_col(tmp, x, ijpg_idct_basis[y]);
      out[y * 8U + x] =
          ijpg_clamp_u8((int32_t)((sum + (1LL << 25)) >> 26) + 128);
    }
  }
}

static ijpg_result ijpg_decode_component_blocks(ijpg_jpeg *jpg,
                                                uint8_t comp_index) {
  ijpg_component *comp = &jpg->comp[comp_index];
  int32_t block[64];
  uint8_t dc_only = 1;
  for (uint8_t by = 0; by < comp->v; by++) {
    for (uint8_t bx = 0; bx < comp->h; bx++) {
      const uint8_t bi = (uint8_t)(by * comp->h + bx);
      const ijpg_result result = ijpg_decode_block(jpg, comp, block, &dc_only);
      if (result != IJPG_OK) {
        return result;
      }
      ijpg_idct_block(block, jpg->sample[comp_index][bi], dc_only);
    }
  }
  return IJPG_OK;
}

IJPG_INLINE uint8_t ijpg_sample_y(const ijpg_jpeg *jpg, uint8_t x, uint8_t y) {
  const uint8_t bx = (uint8_t)(x >> 3);
  const uint8_t by = (uint8_t)(y >> 3);
  return jpg->sample[0][by * jpg->info.max_h + bx][(y & 7U) * 8U + (x & 7U)];
}

static bool ijpg_progressive_alloc(ijpg_jpeg *jpg) {
  uint64_t total_bytes = 0;
  uint64_t bytes_by_comp[4] = {0, 0, 0, 0};
  for (uint8_t i = 0; i < jpg->comp_count; i++) {
    const ijpg_component *comp = &jpg->comp[i];
    jpg->block_cols[i] = jpg->mcu_cols * comp->h;
    jpg->block_rows[i] = jpg->mcu_rows * comp->v;
    jpg->actual_block_cols[i] = ijpg_component_actual_block_cols(jpg, comp);
    jpg->actual_block_rows[i] = ijpg_component_actual_block_rows(jpg, comp);
    jpg->block_count[i] = jpg->block_cols[i] * jpg->block_rows[i];
    const uint64_t bytes = (uint64_t)jpg->block_count[i] * 64ULL *
                           (uint64_t)sizeof(int16_t);
    if (bytes == 0 || bytes > (uint64_t)((size_t)-1) ||
        bytes > (uint64_t)IJPG_MAX_PROGRESSIVE_COEFF_BYTES ||
        total_bytes > (uint64_t)IJPG_MAX_PROGRESSIVE_COEFF_BYTES - bytes) {
      return false;
    }
    bytes_by_comp[i] = bytes;
    total_bytes += bytes;
  }
  if (total_bytes > (uint64_t)IJPG_MAX_PROGRESSIVE_COEFF_BYTES) {
    return false;
  }
  for (uint8_t i = 0; i < jpg->comp_count; i++) {
    const size_t bytes = (size_t)bytes_by_comp[i];
    jpg->coeff[i] = (int16_t *)IJPG_MALLOC(bytes);
    if (jpg->coeff[i] == NULL) {
      return false;
    }
    memset(jpg->coeff[i], 0, bytes);
  }
  return true;
}

IJPG_INLINE uint32_t ijpg_progressive_interleaved_block_index(
    const ijpg_jpeg *jpg, uint8_t comp_index, uint32_t mx, uint32_t my,
    uint8_t bx, uint8_t by) {
  const ijpg_component *comp = &jpg->comp[comp_index];
  return (my * comp->v + by) * jpg->block_cols[comp_index] +
         mx * comp->h + bx;
}

static ijpg_result ijpg_consume_restart(ijpg_jpeg *jpg,
                                        uint8_t *expected_rst) {
  jpg->bits.bit_count = 0;
  if (jpg->bits.marker == 0) {
    (void)ijpg_entropy_byte(&jpg->bits);
  }
  if (jpg->bits.marker != *expected_rst) {
    return IJPG_ERR_FORMAT;
  }
  jpg->bits.marker = 0;
  *expected_rst = (uint8_t)(0xd0U + ((*expected_rst + 1U) & 7U));
  return IJPG_OK;
}

IJPG_INLINE void ijpg_refine_coeff(int16_t *coef, int bit, int16_t p1) {
  if (bit != 0 && (*coef & p1) == 0) {
    *coef = (int16_t)(*coef + (*coef >= 0 ? p1 : -p1));
  }
}

static ijpg_result ijpg_progressive_decode_block(ijpg_jpeg *jpg,
                                                 uint8_t comp_index,
                                                 uint32_t block_index,
                                                 uint32_t *eobrun) {
  ijpg_component *comp = &jpg->comp[comp_index];
  int16_t *block = jpg->coeff[comp_index] + block_index * 64U;
  const uint8_t ss = jpg->scan_ss;
  const uint8_t se = jpg->scan_se;
  const uint8_t ah = jpg->scan_ah;
  const uint8_t al = jpg->scan_al;
  const int16_t p1 = (int16_t)(1U << al);

  if (ss == 0) {
    if (ah == 0) {
      const ijpg_huff *hdc = &jpg->huff[0][comp->td];
      const int dc_len = ijpg_huff_decode_symbol(&jpg->bits, hdc);
      if (dc_len < 0 || dc_len > 11) {
        return IJPG_ERR_FORMAT;
      }
      const int dc_delta = ijpg_receive_extend(&jpg->bits, (uint8_t)dc_len);
      if (dc_delta == 0x7fffffff) {
        return IJPG_ERR_FORMAT;
      }
      comp->pred += dc_delta;
      if (comp->pred < -32768 || comp->pred > 32767) {
        return IJPG_ERR_FORMAT;
      }
      block[0] = (int16_t)(comp->pred * (int32_t)(1U << al));
    } else {
      const int bit = ijpg_get_bits(&jpg->bits, 1);
      if (bit < 0) {
        return IJPG_ERR_FORMAT;
      }
      ijpg_refine_coeff(&block[0], bit, p1);
    }
    return IJPG_OK;
  }

  const ijpg_huff *hac = &jpg->huff[1][comp->ta];
  if (ah == 0) {
    if (*eobrun != 0) {
      (*eobrun)--;
      return IJPG_OK;
    }
    for (uint8_t k = ss; k <= se;) {
      const int rs = ijpg_huff_decode_symbol(&jpg->bits, hac);
      if (rs < 0) {
        IJPG_TRACE_PROGRESS_BLOCK(1, comp_index, block_index, k, rs);
        return IJPG_ERR_FORMAT;
      }
      const uint8_t r = (uint8_t)(rs >> 4);
      const uint8_t s = (uint8_t)(rs & 15);
      if (s == 0) {
        if (r == 15) {
          k = (uint8_t)(k + 16U);
          continue;
        }
        uint32_t run = 1UL << r;
        if (r != 0) {
          const int extra = ijpg_get_bits(&jpg->bits, r);
          if (extra < 0) {
            return IJPG_ERR_FORMAT;
          }
          run += (uint32_t)extra;
        }
        *eobrun = run - 1U;
        break;
      }
      if (s > 10) {
        IJPG_TRACE_PROGRESS_BLOCK(2, comp_index, block_index, k, s);
        return IJPG_ERR_FORMAT;
      }
      k = (uint8_t)(k + r);
      if (k > se) {
        IJPG_TRACE_PROGRESS_BLOCK(3, comp_index, block_index, k, se);
        return IJPG_ERR_FORMAT;
      }
      const int ac = ijpg_receive_extend(&jpg->bits, s);
      if (ac == 0x7fffffff) {
        IJPG_TRACE_PROGRESS_BLOCK(4, comp_index, block_index, k, s);
        return IJPG_ERR_FORMAT;
      }
      block[ijpg_zigzag[k]] = (int16_t)(ac * (int32_t)(1U << al));
      k++;
    }
    return IJPG_OK;
  }

  if (*eobrun != 0) {
    for (uint8_t k = ss; k <= se; k++) {
      int16_t *coef = &block[ijpg_zigzag[k]];
      if (*coef != 0) {
        const int bit = ijpg_get_bits(&jpg->bits, 1);
        if (bit < 0) {
          return IJPG_ERR_FORMAT;
        }
        ijpg_refine_coeff(coef, bit, p1);
      }
    }
    (*eobrun)--;
    return IJPG_OK;
  }

  for (uint8_t k = ss; k <= se;) {
    const int rs = ijpg_huff_decode_symbol(&jpg->bits, hac);
    if (rs < 0) {
      IJPG_TRACE_PROGRESS_BLOCK(5, comp_index, block_index, k, rs);
      return IJPG_ERR_FORMAT;
    }
    uint8_t r = (uint8_t)(rs >> 4);
    const uint8_t s = (uint8_t)(rs & 15);
    int16_t newcoef = 0;
    if (s == 0) {
      if (r < 15) {
        uint32_t run = 1UL << r;
        if (r != 0) {
          const int extra = ijpg_get_bits(&jpg->bits, r);
          if (extra < 0) {
            return IJPG_ERR_FORMAT;
          }
          run += (uint32_t)extra;
        }
        *eobrun = run - 1U;
        for (; k <= se; k++) {
          int16_t *coef = &block[ijpg_zigzag[k]];
          if (*coef != 0) {
            const int bit = ijpg_get_bits(&jpg->bits, 1);
            if (bit < 0) {
              return IJPG_ERR_FORMAT;
            }
            ijpg_refine_coeff(coef, bit, p1);
          }
        }
        return IJPG_OK;
      }
      r = 16;
    } else if (s == 1) {
      const int bit = ijpg_get_bits(&jpg->bits, 1);
      if (bit < 0) {
        return IJPG_ERR_FORMAT;
      }
      newcoef = bit ? p1 : (int16_t)-p1;
    } else {
      return IJPG_ERR_FORMAT;
    }

    for (;;) {
      if (k > se) {
        return newcoef == 0 ? IJPG_OK : IJPG_ERR_FORMAT;
      }
      int16_t *coef = &block[ijpg_zigzag[k]];
      if (*coef != 0) {
        const int bit = ijpg_get_bits(&jpg->bits, 1);
        if (bit < 0) {
          return IJPG_ERR_FORMAT;
        }
        ijpg_refine_coeff(coef, bit, p1);
      } else {
        if (r == 0) {
          if (newcoef != 0) {
            *coef = newcoef;
            k++;
          }
          break;
        }
        r--;
      }
      k++;
    }
  }
  return IJPG_OK;
}

static ijpg_result ijpg_progressive_decode_scan(ijpg_jpeg *jpg,
                                                ijpg_reader *reader) {
  ijpg_bits_init(&jpg->bits, reader);
  uint32_t eobrun = 0;
  uint32_t restart_count = jpg->restart_interval;
  uint8_t expected_rst = 0xd0;
  if (jpg->scan_count == 1) {
    const uint8_t ci = jpg->scan_comp[0];
    for (uint32_t by = 0; by < jpg->actual_block_rows[ci]; by++) {
      for (uint32_t bx = 0; bx < jpg->actual_block_cols[ci]; bx++) {
        const ijpg_result result =
            ijpg_progressive_decode_block(jpg, ci, by * jpg->block_cols[ci] + bx,
                                          &eobrun);
        if (result != IJPG_OK) {
          return result;
        }
        const uint8_t final_block =
            (uint8_t)(by + 1U == jpg->actual_block_rows[ci] &&
                      bx + 1U == jpg->actual_block_cols[ci]);
        if (jpg->restart_interval != 0 && --restart_count == 0 &&
            !final_block) {
          const ijpg_result rst = ijpg_consume_restart(jpg, &expected_rst);
          if (rst != IJPG_OK) {
            return rst;
          }
          restart_count = jpg->restart_interval;
          eobrun = 0;
          for (uint8_t i = 0; i < jpg->comp_count; i++) {
            jpg->comp[i].pred = 0;
          }
        } else if (jpg->restart_interval != 0 && restart_count == 0) {
          restart_count = jpg->restart_interval;
        }
      }
    }
  } else {
    for (uint32_t my = 0; my < jpg->mcu_rows; my++) {
      for (uint32_t mx = 0; mx < jpg->mcu_cols; mx++) {
        for (uint8_t si = 0; si < jpg->scan_count; si++) {
          const uint8_t ci = jpg->scan_comp[si];
          const ijpg_component *comp = &jpg->comp[ci];
          for (uint8_t by = 0; by < comp->v; by++) {
            for (uint8_t bx = 0; bx < comp->h; bx++) {
              const uint32_t block_index =
                  ijpg_progressive_interleaved_block_index(jpg, ci, mx, my, bx,
                                                           by);
              const ijpg_result result =
                  ijpg_progressive_decode_block(jpg, ci, block_index, &eobrun);
              if (result != IJPG_OK) {
                return result;
              }
            }
          }
        }
        const uint8_t final_mcu =
            (uint8_t)(my + 1U == jpg->mcu_rows && mx + 1U == jpg->mcu_cols);
        if (jpg->restart_interval != 0 && --restart_count == 0 &&
            !final_mcu) {
          const ijpg_result rst = ijpg_consume_restart(jpg, &expected_rst);
          if (rst != IJPG_OK) {
            return rst;
          }
          restart_count = jpg->restart_interval;
          eobrun = 0;
          for (uint8_t i = 0; i < jpg->comp_count; i++) {
            jpg->comp[i].pred = 0;
          }
        } else if (jpg->restart_interval != 0 && restart_count == 0) {
          restart_count = jpg->restart_interval;
        }
      }
    }
  }
  jpg->bits.bit_count = 0;
  if (jpg->bits.marker == 0) {
    (void)ijpg_entropy_byte(&jpg->bits);
  }
  return jpg->bits.failed ? IJPG_ERR_IO : IJPG_OK;
}

static bool ijpg_alloc_luma_row(ijpg_jpeg *jpg, uint16_t height) {
  if (height == 0) {
    return false;
  }
  if (jpg->row_luma != NULL && jpg->row_luma_h >= height) {
    return true;
  }
  if (jpg->row_luma != NULL) {
    IJPG_FREE(jpg->row_luma);
    jpg->row_luma = NULL;
    jpg->row_luma_h = 0;
  }
  const uint64_t bytes = (uint64_t)jpg->info.width * height;
  if (bytes == 0 || bytes > (uint64_t)((size_t)-1)) {
    return false;
  }
  jpg->row_luma = (uint8_t *)IJPG_MALLOC((size_t)bytes);
  if (jpg->row_luma == NULL) {
    return false;
  }
  jpg->row_luma_h = height;
  return true;
}

static void ijpg_clear_luma_row(ijpg_jpeg *jpg, uint16_t height) {
  if (jpg->row_luma != NULL) {
    memset(jpg->row_luma, 0, (size_t)jpg->info.width * height);
  }
}

static void ijpg_store_luma_block(ijpg_jpeg *jpg, uint32_t base_x,
                                  uint8_t row_offset,
                                  const uint8_t sample[64]) {
  if (jpg->row_luma == NULL) {
    return;
  }
  for (uint8_t y = 0; y < 8; y++) {
    const uint32_t dst_y = (uint32_t)row_offset + y;
    if (dst_y >= jpg->row_luma_h) {
      break;
    }
    uint8_t *dst = jpg->row_luma + (size_t)dst_y * jpg->info.width;
    for (uint8_t x = 0; x < 8; x++) {
      const uint32_t px = base_x + x;
      if (px >= jpg->info.width) {
        break;
      }
      dst[px] = sample[y * 8U + x];
    }
  }
}

static void ijpg_store_luma_dc_block(ijpg_jpeg *jpg, uint32_t base_x,
                                     uint8_t row_offset, uint8_t gray) {
  if (jpg->row_luma == NULL) {
    return;
  }
  for (uint8_t y = 0; y < 8; y++) {
    const uint32_t dst_y = (uint32_t)row_offset + y;
    if (dst_y >= jpg->row_luma_h) {
      break;
    }
    uint8_t *dst = jpg->row_luma + (size_t)dst_y * jpg->info.width;
    for (uint8_t x = 0; x < 8; x++) {
      const uint32_t px = base_x + x;
      if (px >= jpg->info.width) {
        break;
      }
      dst[px] = gray;
    }
  }
}

static void ijpg_store_mcu_luma(ijpg_jpeg *jpg, uint32_t mcux) {
  const uint32_t base_x = mcux * jpg->mcu_w;
  for (uint8_t y = 0; y < jpg->mcu_h; y++) {
    if (y >= jpg->row_luma_h) {
      break;
    }
    uint8_t *dst = jpg->row_luma + (size_t)y * jpg->info.width;
    for (uint8_t x = 0; x < jpg->mcu_w; x++) {
      const uint32_t px = base_x + x;
      if (px >= jpg->info.width) {
        break;
      }
      dst[px] = ijpg_sample_y(jpg, x, y);
    }
  }
}

static ijpg_result ijpg_decode_progressive_dc_coeff(ijpg_jpeg *jpg,
                                                    uint8_t comp_index,
                                                    int32_t *coeff) {
  ijpg_component *comp = &jpg->comp[comp_index];
  const ijpg_huff *hdc = &jpg->huff[0][comp->td];
  if (!jpg->has_quant[comp->tq] || !hdc->ready) {
    return IJPG_ERR_FORMAT;
  }
  const int dc_len = ijpg_huff_decode_symbol(&jpg->bits, hdc);
  if (dc_len < 0 || dc_len > 11) {
    return IJPG_ERR_FORMAT;
  }
  const int dc_delta = ijpg_receive_extend(&jpg->bits, (uint8_t)dc_len);
  if (dc_delta == 0x7fffffff) {
    return IJPG_ERR_FORMAT;
  }
  comp->pred += dc_delta;
  if (comp->pred < -32768 || comp->pred > 32767) {
    return IJPG_ERR_FORMAT;
  }
  *coeff = comp->pred * (int32_t)(1U << jpg->scan_al);
  return IJPG_OK;
}

IJPG_INLINE uint8_t ijpg_progressive_dc_gray(const ijpg_jpeg *jpg,
                                             int32_t coeff) {
  const int64_t value =
      (((int64_t)coeff * (int64_t)jpg->quant[jpg->comp[0].tq][0]) >> 3) + 128;
  return value < 0 ? 0 : (value > 255 ? 255 : (uint8_t)value);
}

static void ijpg_emit_luma_rows(ijpg_jpeg *jpg, uint32_t base_y,
                                uint16_t rows) {
  const ijpg_render *r = jpg->render;
  if (r == NULL || r->pixel == NULL || jpg->row_luma == NULL) {
    return;
  }
  if (jpg->scaled) {
    for (uint16_t y = 0; y < rows; y++) {
      const uint32_t py = base_y + y;
      if (py >= jpg->info.height) {
        break;
      }
      const uint8_t *src = jpg->row_luma + (size_t)y * jpg->info.width;
      for (uint32_t x = 0; x < jpg->info.width; x++) {
        ijpg_emit_scaled(jpg, r, x, py, src[x]);
      }
    }
  } else {
    for (uint16_t y = 0; y < rows; y++) {
      const uint32_t py = base_y + y;
      if (py >= jpg->info.height) {
        break;
      }
      const uint8_t *src = jpg->row_luma + (size_t)y * jpg->info.width;
      for (uint32_t x = 0; x < jpg->info.width; x++) {
        ijpg_emit_unscaled(jpg, r, x, py, src[x]);
      }
    }
  }
}

static ijpg_result ijpg_progressive_render(ijpg_jpeg *jpg) {
  if (!ijpg_alloc_luma_row(jpg, 8)) {
    return IJPG_ERR_MEMORY;
  }
  int32_t block[64];
  const uint16_t *quant = jpg->quant[jpg->comp[0].tq];
  for (uint32_t by = 0; by < jpg->actual_block_rows[0]; by++) {
    ijpg_clear_luma_row(jpg, 8);
    for (uint32_t bx = 0; bx < jpg->actual_block_cols[0]; bx++) {
      const int16_t *src = jpg->coeff[0] + (by * jpg->block_cols[0] + bx) * 64U;
      uint8_t dc_only = 1;
      for (uint8_t i = 0; i < 64; i++) {
        block[i] = (int32_t)src[i] * (int32_t)quant[i];
        dc_only = (uint8_t)(dc_only & (uint8_t)(i == 0 || src[i] == 0));
      }
      ijpg_idct_block(block, jpg->sample[0][0], dc_only);
      ijpg_store_luma_block(jpg, bx * 8U, 0, jpg->sample[0][0]);
    }
    ijpg_emit_luma_rows(jpg, by * 8U, 8);
  }
  return IJPG_OK;
}

static ijpg_result ijpg_decode_single_component_scan(ijpg_jpeg *jpg,
                                                     ijpg_reader *reader) {
  ijpg_bits_init(&jpg->bits, reader);
  const uint8_t ci = jpg->scan_comp[0];
  ijpg_component *comp = &jpg->comp[ci];
  const uint32_t cols = ijpg_component_actual_block_cols(jpg, comp);
  const uint32_t rows = ijpg_component_actual_block_rows(jpg, comp);
  uint32_t restart_count = jpg->restart_interval;
  uint8_t expected_rst = 0xd0;
  int32_t block[64];

  if (ci == 0 && !ijpg_alloc_luma_row(jpg, 8)) {
    return IJPG_ERR_MEMORY;
  }
  for (uint32_t by = 0; by < rows; by++) {
    if (ci == 0) {
      ijpg_clear_luma_row(jpg, 8);
    }
    for (uint32_t bx = 0; bx < cols; bx++) {
      uint8_t dc_only = 1;
      const ijpg_result result = ijpg_decode_block(jpg, comp, block, &dc_only);
      if (result != IJPG_OK) {
        return result;
      }
      ijpg_idct_block(block, jpg->sample[ci][0], dc_only);
      if (ci == 0) {
        ijpg_store_luma_block(jpg, bx * 8U, 0, jpg->sample[0][0]);
      }
      const uint8_t final_block =
          (uint8_t)(by + 1U == rows && bx + 1U == cols);
      if (jpg->restart_interval != 0 && --restart_count == 0 &&
          !final_block) {
        const ijpg_result rst = ijpg_consume_restart(jpg, &expected_rst);
        if (rst != IJPG_OK) {
          return rst;
        }
        restart_count = jpg->restart_interval;
        for (uint8_t i = 0; i < jpg->comp_count; i++) {
          jpg->comp[i].pred = 0;
        }
      } else if (jpg->restart_interval != 0 && restart_count == 0) {
        restart_count = jpg->restart_interval;
      }
    }
    if (ci == 0) {
      ijpg_emit_luma_rows(jpg, by * 8U, 8);
    }
  }
  jpg->bits.bit_count = 0;
  if (jpg->bits.marker == 0) {
    (void)ijpg_entropy_byte(&jpg->bits);
  }
  if (jpg->bits.marker != 0 && jpg->bits.marker != IJPG_MARKER_EOI &&
      jpg->bits.marker != IJPG_MARKER_SOS) {
    return IJPG_ERR_FORMAT;
  }
  return jpg->bits.failed ? IJPG_ERR_IO : IJPG_OK;
}

static ijpg_result ijpg_decode_scan(ijpg_jpeg *jpg, ijpg_reader *reader) {
  ijpg_bits_init(&jpg->bits, reader);
  if (!ijpg_alloc_luma_row(jpg, jpg->mcu_h)) {
    return IJPG_ERR_MEMORY;
  }
  uint32_t restart_count = jpg->restart_interval;
  uint8_t expected_rst = 0xd0;
  for (uint32_t my = 0; my < jpg->mcu_rows; my++) {
    ijpg_clear_luma_row(jpg, jpg->mcu_h);
    for (uint32_t mx = 0; mx < jpg->mcu_cols; mx++) {
      for (uint8_t ci = 0; ci < jpg->comp_count; ci++) {
        const ijpg_result result = ijpg_decode_component_blocks(jpg, ci);
        if (result != IJPG_OK) {
          return result;
        }
      }
      ijpg_store_mcu_luma(jpg, mx);
      const uint8_t final_mcu =
          (uint8_t)(my + 1U == jpg->mcu_rows && mx + 1U == jpg->mcu_cols);
      if (jpg->restart_interval != 0 && --restart_count == 0 && !final_mcu) {
        const ijpg_result rst = ijpg_consume_restart(jpg, &expected_rst);
        if (rst != IJPG_OK) {
          return rst;
        }
        restart_count = jpg->restart_interval;
        for (uint8_t i = 0; i < jpg->comp_count; i++) {
          jpg->comp[i].pred = 0;
        }
      } else if (jpg->restart_interval != 0 && restart_count == 0) {
        restart_count = jpg->restart_interval;
      }
    }
    ijpg_emit_luma_rows(jpg, my * jpg->mcu_h, jpg->mcu_h);
  }
  if (jpg->bits.marker != 0 && jpg->bits.marker != IJPG_MARKER_EOI) {
    return IJPG_ERR_FORMAT;
  }
  return jpg->bits.failed ? IJPG_ERR_IO : IJPG_OK;
}

#if IJPG_PROGRESSIVE_DC_FALLBACK
static ijpg_result ijpg_decode_progressive_dc_fallback(ijpg_jpeg *jpg,
                                                       ijpg_reader *reader) {
  if (jpg->scan_ss != 0 || jpg->scan_se != 0 || jpg->scan_ah != 0) {
    return IJPG_ERR_MEMORY;
  }

  uint8_t has_y = 0;
  for (uint8_t si = 0; si < jpg->scan_count; si++) {
    has_y = (uint8_t)(has_y | (uint8_t)(jpg->scan_comp[si] == 0));
  }
  if (!has_y) {
    return IJPG_ERR_MEMORY;
  }

  ijpg_bits_init(&jpg->bits, reader);
  uint32_t restart_count = jpg->restart_interval;
  uint8_t expected_rst = 0xd0;

  if (jpg->scan_count == 1) {
    const uint8_t ci = jpg->scan_comp[0];
    if (ci != 0) {
      return IJPG_ERR_MEMORY;
    }
    const uint32_t cols = ijpg_component_actual_block_cols(jpg, &jpg->comp[ci]);
    const uint32_t rows = ijpg_component_actual_block_rows(jpg, &jpg->comp[ci]);
    if (!ijpg_alloc_luma_row(jpg, 8)) {
      return IJPG_ERR_MEMORY;
    }
    for (uint32_t by = 0; by < rows; by++) {
      ijpg_clear_luma_row(jpg, 8);
      for (uint32_t bx = 0; bx < cols; bx++) {
        int32_t coeff = 0;
        const ijpg_result result =
            ijpg_decode_progressive_dc_coeff(jpg, ci, &coeff);
        if (result != IJPG_OK) {
          return result;
        }
        ijpg_store_luma_dc_block(jpg, bx * 8U, 0,
                                 ijpg_progressive_dc_gray(jpg, coeff));
        const uint8_t final_block =
            (uint8_t)(by + 1U == rows && bx + 1U == cols);
        if (jpg->restart_interval != 0 && --restart_count == 0 &&
            !final_block) {
          const ijpg_result rst = ijpg_consume_restart(jpg, &expected_rst);
          if (rst != IJPG_OK) {
            return rst;
          }
          restart_count = jpg->restart_interval;
          for (uint8_t i = 0; i < jpg->comp_count; i++) {
            jpg->comp[i].pred = 0;
          }
        } else if (jpg->restart_interval != 0 && restart_count == 0) {
          restart_count = jpg->restart_interval;
        }
      }
      ijpg_emit_luma_rows(jpg, by * 8U, 8);
    }
  } else {
    if (!ijpg_alloc_luma_row(jpg, jpg->mcu_h)) {
      return IJPG_ERR_MEMORY;
    }
    for (uint32_t my = 0; my < jpg->mcu_rows; my++) {
      ijpg_clear_luma_row(jpg, jpg->mcu_h);
      for (uint32_t mx = 0; mx < jpg->mcu_cols; mx++) {
        for (uint8_t si = 0; si < jpg->scan_count; si++) {
          const uint8_t ci = jpg->scan_comp[si];
          const ijpg_component *comp = &jpg->comp[ci];
          for (uint8_t by = 0; by < comp->v; by++) {
            for (uint8_t bx = 0; bx < comp->h; bx++) {
              int32_t coeff = 0;
              const ijpg_result result =
                  ijpg_decode_progressive_dc_coeff(jpg, ci, &coeff);
              if (result != IJPG_OK) {
                return result;
              }
              if (ci == 0) {
                ijpg_store_luma_dc_block(
                    jpg, mx * jpg->mcu_w + bx * 8U, (uint8_t)(by * 8U),
                    ijpg_progressive_dc_gray(jpg, coeff));
              }
            }
          }
        }
        const uint8_t final_mcu =
            (uint8_t)(my + 1U == jpg->mcu_rows && mx + 1U == jpg->mcu_cols);
        if (jpg->restart_interval != 0 && --restart_count == 0 &&
            !final_mcu) {
          const ijpg_result rst = ijpg_consume_restart(jpg, &expected_rst);
          if (rst != IJPG_OK) {
            return rst;
          }
          restart_count = jpg->restart_interval;
          for (uint8_t i = 0; i < jpg->comp_count; i++) {
            jpg->comp[i].pred = 0;
          }
        } else if (jpg->restart_interval != 0 && restart_count == 0) {
          restart_count = jpg->restart_interval;
        }
      }
      ijpg_emit_luma_rows(jpg, my * jpg->mcu_h, jpg->mcu_h);
    }
  }

  jpg->bits.bit_count = 0;
  if (jpg->bits.marker == 0) {
    (void)ijpg_entropy_byte(&jpg->bits);
  }
  return jpg->bits.failed ? IJPG_ERR_IO : IJPG_OK;
}
#endif

static ijpg_result ijpg_progressive_next_scan(ijpg_jpeg *jpg,
                                              ijpg_reader *reader,
                                              uint8_t marker) {
  for (;;) {
    if (marker == IJPG_MARKER_EOI) {
      return IJPG_OK;
    }
    if (!ijpg_marker_has_length(marker)) {
      const int next = ijpg_next_marker(reader);
      if (next < 0) {
        return IJPG_ERR_IO;
      }
      marker = (uint8_t)next;
      continue;
    }
    uint16_t len = 0;
    if (!ijpg_read_u16(reader, &len) || len < 2) {
      return IJPG_ERR_FORMAT;
    }
    ijpg_result result = IJPG_OK;
    if (marker == IJPG_MARKER_DHT) {
      result = ijpg_parse_dht(jpg, reader, len);
    } else if (marker == IJPG_MARKER_DQT) {
      result = ijpg_parse_dqt(jpg, reader, len);
    } else if (marker == IJPG_MARKER_DRI) {
      result = ijpg_parse_dri(jpg, reader, len);
    } else if (marker == IJPG_MARKER_SOS) {
      return ijpg_parse_sos(jpg, reader, len);
    } else {
      result = ijpg_skip(reader, (uint16_t)(len - 2U)) ? IJPG_OK : IJPG_ERR_IO;
    }
    if (result != IJPG_OK) {
      return result;
    }
    const int next = ijpg_next_marker(reader);
    if (next < 0) {
      return IJPG_ERR_IO;
    }
    marker = (uint8_t)next;
  }
}

static ijpg_result ijpg_decode_progressive(ijpg_jpeg *jpg,
                                           ijpg_reader *reader) {
  if (!ijpg_progressive_alloc(jpg)) {
    for (uint8_t i = 0; i < 4; i++) {
      if (jpg->coeff[i] != NULL) {
        IJPG_FREE(jpg->coeff[i]);
        jpg->coeff[i] = NULL;
      }
    }
#if IJPG_PROGRESSIVE_DC_FALLBACK
    return ijpg_decode_progressive_dc_fallback(jpg, reader);
#else
    return IJPG_ERR_MEMORY;
#endif
  }
  uint16_t scan = 0;
  for (;;) {
    ijpg_result result = ijpg_progressive_decode_scan(jpg, reader);
    IJPG_TRACE_PROGRESS(scan, result);
    if (result != IJPG_OK) {
      return result;
    }
    const uint8_t marker = jpg->bits.marker;
    result = ijpg_progressive_next_scan(jpg, reader, marker);
    IJPG_TRACE_PROGRESS(scan, result);
    if (result != IJPG_OK) {
      return result;
    }
    if (marker == IJPG_MARKER_EOI) {
      break;
    }
    scan++;
  }
  return ijpg_progressive_render(jpg);
}

static inline ijpg_result ijpg_decode(ijpg_reader *reader,
                                      const ijpg_render *render,
                                      ijpg_info *out_info) {
  if (reader == NULL) {
    return IJPG_ERR_FORMAT;
  }
  ijpg_jpeg *jpg = (ijpg_jpeg *)IJPG_MALLOC(sizeof(*jpg));
  if (jpg == NULL) {
    return IJPG_ERR_MEMORY;
  }
  ijpg_result result = ijpg_parse_headers(jpg, reader, false);
  if (result != IJPG_OK) {
    IJPG_FREE(jpg);
    return result;
  }
  if (out_info != NULL) {
    *out_info = jpg->info;
  }
  if (!jpg->progressive) {
    for (uint8_t si = 0; si < jpg->scan_count; si++) {
      const uint8_t i = jpg->scan_comp[si];
      if (!jpg->has_quant[jpg->comp[i].tq] ||
          !jpg->huff[0][jpg->comp[i].td].ready ||
          !jpg->huff[1][jpg->comp[i].ta].ready) {
        IJPG_FREE(jpg);
        return IJPG_ERR_FORMAT;
      }
    }
  } else {
    for (uint8_t i = 0; i < jpg->comp_count; i++) {
      if (!jpg->has_quant[jpg->comp[i].tq]) {
        IJPG_FREE(jpg);
        return IJPG_ERR_FORMAT;
      }
    }
  }
  jpg->render = render;
  if (!ijpg_setup_output(jpg, render)) {
    ijpg_free_output(jpg);
    IJPG_FREE(jpg);
    return IJPG_ERR_MEMORY;
  }
  if (!ijpg_dither_init(&jpg->dither, jpg->output_w,
                        render != NULL ? render->dither : 0)) {
    ijpg_free_output(jpg);
    IJPG_FREE(jpg);
    return IJPG_ERR_MEMORY;
  }
  if (jpg->progressive) {
    result = ijpg_decode_progressive(jpg, reader);
  } else {
    result = jpg->scan_count == 1 ? ijpg_decode_single_component_scan(jpg, reader)
                                  : ijpg_decode_scan(jpg, reader);
  }
  ijpg_free_output(jpg);
  IJPG_FREE(jpg);
  return result;
}

#endif
