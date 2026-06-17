/*
 * InkWebP - header-only WBMP parser and renderer for WBMP: Wireless Application Protocol Bitmap Format
 * Targets small e-ink displays, Supports scaling.
 * (c) Remixer Dec 2026 | This header is licensed under MIT License.
 * Distributed as a part of PocketInkOS https://github.com/remixer-dec/PocketInkOS
 */

#ifndef INKWBMP_H
#define INKWBMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef IWBMP_MALLOC
#define IWBMP_MALLOC malloc
#define IWBMP_FREE free
#endif

#ifndef IWBMP_MAX_ROW_BYTES
#define IWBMP_MAX_ROW_BYTES 65535U
#endif

#ifndef IWBMP_STRICT_PADDING
#define IWBMP_STRICT_PADDING 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  IWBMP_OK = 0,
  IWBMP_ERR_IO = -1,
  IWBMP_ERR_FORMAT = -2,
  IWBMP_ERR_UNSUPPORTED = -3,
  IWBMP_ERR_MEMORY = -4,
} iwbmp_result;

typedef enum {
  IWBMP_SCALE_NONE = 0,
  IWBMP_SCALE_FIT = 1,
} iwbmp_scale_mode;

typedef struct {
  void *user;
  int (*read)(void *user);
} iwbmp_reader;

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t type;
  uint8_t bit_depth;
} iwbmp_info;

typedef void (*iwbmp_pixel_fn)(void *user, int16_t x, int16_t y);

typedef struct {
  void *user;
  iwbmp_pixel_fn pixel;
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
  uint8_t scale;
} iwbmp_render;

static iwbmp_result iwbmp_read_info(iwbmp_reader *reader, iwbmp_info *info);
static iwbmp_result iwbmp_decode(iwbmp_reader *reader,
                                 const iwbmp_render *render,
                                 iwbmp_info *out_info);

#ifdef __cplusplus
}
#endif

typedef struct {
  iwbmp_info info;
  const iwbmp_render *render;
  uint32_t row_bytes;
  uint8_t scale_mode;
  uint8_t scaled;
  uint16_t output_w;
  uint16_t output_h;
  int16_t output_x;
  int16_t output_y;
  uint32_t *scale_black;
  uint32_t *scale_count;
  uint32_t scale_y;
  uint8_t scale_row_active;
} iwbmp_state;

static int iwbmp_reader_byte(iwbmp_reader *reader) {
  return (reader != NULL && reader->read != NULL) ? reader->read(reader->user)
                                                   : -1;
}

static iwbmp_result iwbmp_read_mb_u32(iwbmp_reader *reader, uint32_t *out) {
  uint64_t value = 0;
  for (uint8_t i = 0; i < 5; i++) {
    const int c = iwbmp_reader_byte(reader);
    if (c < 0) {
      return IWBMP_ERR_IO;
    }
    if (i == 0 && c == 0x80) {
      return IWBMP_ERR_FORMAT;
    }
    value = (value << 7) | (uint32_t)(c & 0x7f);
    if (value > 0xffffffffULL) {
      return IWBMP_ERR_FORMAT;
    }
    if ((c & 0x80) == 0) {
      if (out != NULL) {
        *out = (uint32_t)value;
      }
      return IWBMP_OK;
    }
  }
  return IWBMP_ERR_FORMAT;
}

static bool iwbmp_row_bytes_checked(uint32_t width, uint32_t *out) {
  const uint64_t bytes = ((uint64_t)width + 7ULL) >> 3;
  if (width == 0 || bytes == 0 || bytes > (uint64_t)((size_t)-1) ||
      bytes > 0xffffffffULL || bytes > (uint64_t)IWBMP_MAX_ROW_BYTES) {
    return false;
  }
  if (out != NULL) {
    *out = (uint32_t)bytes;
  }
  return true;
}

static iwbmp_result iwbmp_parse_header(iwbmp_reader *reader,
                                       iwbmp_info *info) {
  uint32_t type = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  int fix = 0;
  iwbmp_result result = IWBMP_OK;
  if (reader == NULL || info == NULL) {
    return IWBMP_ERR_FORMAT;
  }
  memset(info, 0, sizeof(*info));
  result = iwbmp_read_mb_u32(reader, &type);
  if (result != IWBMP_OK) {
    return result;
  }
  fix = iwbmp_reader_byte(reader);
  if (fix < 0) {
    return IWBMP_ERR_IO;
  }
  result = iwbmp_read_mb_u32(reader, &width);
  if (result != IWBMP_OK) {
    return result;
  }
  result = iwbmp_read_mb_u32(reader, &height);
  if (result != IWBMP_OK) {
    return result;
  }
  if (type != 0) {
    return IWBMP_ERR_UNSUPPORTED;
  }
  if (fix != 0) {
    return IWBMP_ERR_UNSUPPORTED;
  }
  if (width == 0 || height == 0) {
    return IWBMP_ERR_FORMAT;
  }
  info->width = width;
  info->height = height;
  info->type = type;
  info->bit_depth = 1;
  return IWBMP_OK;
}

static iwbmp_result iwbmp_read_info(iwbmp_reader *reader, iwbmp_info *info) {
  return iwbmp_parse_header(reader, info);
}

static uint16_t iwbmp_fit_dim(uint32_t src, uint32_t other_src,
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

static bool iwbmp_setup_output(iwbmp_state *wbmp,
                               const iwbmp_render *render) {
  if (wbmp == NULL) {
    return false;
  }
  wbmp->output_w = (uint16_t)wbmp->info.width;
  wbmp->output_h = (uint16_t)wbmp->info.height;
  wbmp->output_x = render != NULL ? render->x : 0;
  wbmp->output_y = render != NULL ? render->y : 0;
  wbmp->scale_mode =
      render != NULL && render->scale <= (uint8_t)IWBMP_SCALE_FIT
          ? render->scale
          : (uint8_t)IWBMP_SCALE_NONE;
  if (render == NULL || render->width == 0 || render->height == 0) {
    return true;
  }
  if (wbmp->info.width > UINT16_MAX || wbmp->info.height > UINT16_MAX) {
    wbmp->scale_mode = (uint8_t)IWBMP_SCALE_FIT;
  }
  if (wbmp->scale_mode == (uint8_t)IWBMP_SCALE_FIT &&
      (wbmp->info.width > render->width ||
       wbmp->info.height > render->height)) {
    const uint16_t fit_w =
        iwbmp_fit_dim(wbmp->info.width, wbmp->info.height, render->width,
                      render->height);
    const uint16_t fit_h =
        iwbmp_fit_dim(wbmp->info.height, wbmp->info.width, render->height,
                      render->width);
    if (fit_w == 0 || fit_h == 0) {
      return false;
    }
    wbmp->output_w = fit_w;
    wbmp->output_h = fit_h;
    wbmp->output_x = (int16_t)(render->x + (int16_t)((render->width - fit_w) / 2U));
    wbmp->output_y =
        (int16_t)(render->y + (int16_t)((render->height - fit_h) / 2U));
    wbmp->scaled = 1;
    wbmp->scale_black =
        (uint32_t *)IWBMP_MALLOC((size_t)wbmp->output_w * sizeof(uint32_t));
    wbmp->scale_count =
        (uint32_t *)IWBMP_MALLOC((size_t)wbmp->output_w * sizeof(uint32_t));
    if (wbmp->scale_black == NULL || wbmp->scale_count == NULL) {
      if (wbmp->scale_black != NULL) {
        IWBMP_FREE(wbmp->scale_black);
        wbmp->scale_black = NULL;
      }
      if (wbmp->scale_count != NULL) {
        IWBMP_FREE(wbmp->scale_count);
        wbmp->scale_count = NULL;
      }
      return false;
    }
    memset(wbmp->scale_black, 0,
           (size_t)wbmp->output_w * sizeof(uint32_t));
    memset(wbmp->scale_count, 0,
           (size_t)wbmp->output_w * sizeof(uint32_t));
  }
  return true;
}

static void iwbmp_flush_scale(iwbmp_state *wbmp) {
  if (wbmp == NULL || wbmp->scale_black == NULL ||
      wbmp->scale_count == NULL || !wbmp->scale_row_active) {
    return;
  }
  const iwbmp_render *r = wbmp->render;
  if (r == NULL || r->pixel == NULL) {
    return;
  }
  for (uint16_t sx = 0; sx < wbmp->output_w; sx++) {
    const uint32_t count = wbmp->scale_count[sx];
    if (count == 0) {
      continue;
    }
    if (wbmp->scale_black[sx] >= ((count + 1U) >> 1)) {
      const int32_t px = (int32_t)wbmp->output_x + sx;
      const int32_t py = (int32_t)wbmp->output_y + wbmp->scale_y;
      if (px >= INT16_MIN && px <= INT16_MAX && py >= INT16_MIN &&
          py <= INT16_MAX) {
        r->pixel(r->user, (int16_t)px, (int16_t)py);
      }
    }
  }
  memset(wbmp->scale_black, 0,
         (size_t)wbmp->output_w * sizeof(uint32_t));
  memset(wbmp->scale_count, 0,
         (size_t)wbmp->output_w * sizeof(uint32_t));
  wbmp->scale_row_active = 0;
}

static void iwbmp_emit_render(iwbmp_state *wbmp, uint32_t x, uint32_t y,
                              uint8_t black) {
  const iwbmp_render *r = wbmp->render;
  if (r == NULL || r->pixel == NULL) {
    return;
  }
  if (wbmp->scaled) {
    const uint32_t dx =
        (uint32_t)(((uint64_t)x * wbmp->output_w) / wbmp->info.width);
    const uint32_t dy =
        (uint32_t)(((uint64_t)y * wbmp->output_h) / wbmp->info.height);
    if (dx >= wbmp->output_w || dy >= wbmp->output_h) {
      return;
    }
    if (wbmp->scale_row_active && dy != wbmp->scale_y) {
      iwbmp_flush_scale(wbmp);
    }
    wbmp->scale_y = dy;
    wbmp->scale_row_active = 1;
    if (wbmp->scale_count[dx] != UINT32_MAX) {
      wbmp->scale_count[dx]++;
      if (black && wbmp->scale_black[dx] != UINT32_MAX) {
        wbmp->scale_black[dx]++;
      }
    }
    return;
  }
  if (!black || x >= r->width || y >= r->height) {
    return;
  }
  {
    const int32_t px = (int32_t)wbmp->output_x + (int32_t)x;
    const int32_t py = (int32_t)wbmp->output_y + (int32_t)y;
    if (px >= INT16_MIN && px <= INT16_MAX && py >= INT16_MIN &&
        py <= INT16_MAX) {
      r->pixel(r->user, (int16_t)px, (int16_t)py);
    }
  }
}

static iwbmp_result iwbmp_decode_pixels(iwbmp_reader *reader,
                                        iwbmp_state *wbmp) {
  const uint32_t width = wbmp->info.width;
  const uint32_t height = wbmp->info.height;
  const uint32_t row_bytes = wbmp->row_bytes;
  const uint8_t tail_bits = (uint8_t)(width & 7U);
  for (uint32_t y = 0; y < height; y++) {
    uint32_t x = 0;
    for (uint32_t bx = 0; bx < row_bytes; bx++) {
      const int c = iwbmp_reader_byte(reader);
      if (c < 0) {
        return IWBMP_ERR_IO;
      }
#if IWBMP_STRICT_PADDING
      if (tail_bits != 0 && bx + 1U == row_bytes) {
        const uint8_t padding_mask =
            (uint8_t)((1U << (8U - tail_bits)) - 1U);
        if (((uint8_t)c & padding_mask) != 0) {
          return IWBMP_ERR_FORMAT;
        }
      }
#endif
      for (uint8_t bit = 0; bit < 8 && x < width; bit++, x++) {
        const uint8_t black = (((uint8_t)c & (uint8_t)(0x80U >> bit)) == 0);
        iwbmp_emit_render(wbmp, x, y, black);
      }
    }
  }
  iwbmp_flush_scale(wbmp);
  return IWBMP_OK;
}

static iwbmp_result iwbmp_decode(iwbmp_reader *reader,
                                 const iwbmp_render *render,
                                 iwbmp_info *out_info) {
  iwbmp_state wbmp;
  iwbmp_result result = IWBMP_OK;
  memset(&wbmp, 0, sizeof(wbmp));
  wbmp.render = render;
  result = iwbmp_parse_header(reader, &wbmp.info);
  if (result != IWBMP_OK) {
    return result;
  }
  if (!iwbmp_row_bytes_checked(wbmp.info.width, &wbmp.row_bytes)) {
    return IWBMP_ERR_UNSUPPORTED;
  }
  if (!iwbmp_setup_output(&wbmp, render)) {
    return IWBMP_ERR_MEMORY;
  }
  result = iwbmp_decode_pixels(reader, &wbmp);
  if (wbmp.scale_black != NULL) {
    IWBMP_FREE(wbmp.scale_black);
  }
  if (wbmp.scale_count != NULL) {
    IWBMP_FREE(wbmp.scale_count);
  }
  if (out_info != NULL) {
    *out_info = wbmp.info;
  }
  return result;
}

#endif // INKWBMP_H
