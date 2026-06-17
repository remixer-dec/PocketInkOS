/*
 * InkWebP - header-only WebP parser and 1-bit renderer for lossy and lossless webp.
 * Targets small e-ink displays: validates RIFF/WebP, handles extended headers,
 * skips metadata/alpha, and exposes callbacks, animation is not supported
 * (c) Remixer Dec 2026 | CC BY-NC-SA 3.0
 * Distributed as a part of PocketInkOS https://github.com/remixer-dec/PocketInkOS
 */

#ifndef INKWEBP_H
#define INKWEBP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef IWEBP_MALLOC
#define IWEBP_MALLOC malloc
#define IWEBP_FREE free
#endif

#ifndef IWEBP_MAX_DIMENSION
#define IWEBP_MAX_DIMENSION 4096U
#endif

#ifndef IWEBP_DEFAULT_THRESHOLD
#define IWEBP_DEFAULT_THRESHOLD 128U
#endif

#ifndef IWEBP_MAX_VP8L_PIXELS
#define IWEBP_MAX_VP8L_PIXELS 524288UL
#endif

#define IWEBP_MAX_TOKEN_PARTITIONS 8U

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  IWEBP_OK = 0,
  IWEBP_ERR_BAD_RIFF = -1,
  IWEBP_ERR_BAD_WEBP = -2,
  IWEBP_ERR_UNSUPPORTED_ANIMATION = -3,
  IWEBP_ERR_UNSUPPORTED_CODEC = -4,
  IWEBP_ERR_BAD_DIMENSIONS = -5,
  IWEBP_ERR_OUT_OF_DATA = -6,
  IWEBP_ERR_BAD_BITSTREAM = -7,
  IWEBP_ERR_SCRATCH_TOO_SMALL = -8,
  IWEBP_ERR_MEMORY = -9,
} iwebp_result;

typedef enum {
  IWEBP_CODEC_NONE = 0,
  IWEBP_CODEC_VP8 = 1,
  IWEBP_CODEC_VP8L = 2,
} iwebp_codec;

typedef enum {
  IWEBP_DITHER_THRESHOLD = 0,
  IWEBP_DITHER_ATKINSON = 1,
  IWEBP_DITHER_SIERRA_TWO_ROW = 2,
  IWEBP_DITHER_SIMPLE2D = 3,
} iwebp_dither_mode;

typedef enum {
  IWEBP_SCALE_NONE = 0,
  IWEBP_SCALE_FIT = 1,
} iwebp_scale_mode;

typedef struct {
  uint16_t width;
  uint16_t height;
  uint8_t codec;
  uint8_t has_alpha;
  uint8_t has_vp8x;
} iwebp_info;

typedef void (*iwebp_pixel_fn)(void *user, int16_t x, int16_t y);

typedef struct {
  void *user;
  iwebp_pixel_fn pixel;
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
  uint8_t dither;
  uint8_t scale;
  uint8_t threshold;
  uint8_t invert;
  uint8_t transparent_black;
  uint32_t max_vp8l_pixels;
} iwebp_render;

static inline iwebp_result iwebp_read_info(const uint8_t *data, size_t size,
                                           iwebp_info *info);
static inline iwebp_result iwebp_decode(const uint8_t *data, size_t size,
                                        const iwebp_render *render,
                                        iwebp_info *out_info);

#ifdef __cplusplus
}
#endif

#define IWEBP_FOURCC(a, b, c, d)                                               \
  (((uint32_t)(uint8_t)(a)) | ((uint32_t)(uint8_t)(b) << 8) |                  \
   ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

#define IWEBP_CHUNK_RIFF IWEBP_FOURCC('R', 'I', 'F', 'F')
#define IWEBP_CHUNK_WEBP IWEBP_FOURCC('W', 'E', 'B', 'P')
#define IWEBP_CHUNK_VP8 IWEBP_FOURCC('V', 'P', '8', ' ')
#define IWEBP_CHUNK_VP8L IWEBP_FOURCC('V', 'P', '8', 'L')
#define IWEBP_CHUNK_VP8X IWEBP_FOURCC('V', 'P', '8', 'X')
#define IWEBP_CHUNK_ALPH IWEBP_FOURCC('A', 'L', 'P', 'H')
#define IWEBP_CHUNK_ANIM IWEBP_FOURCC('A', 'N', 'I', 'M')
#define IWEBP_CHUNK_ANMF IWEBP_FOURCC('A', 'N', 'M', 'F')

typedef struct {
  int16_t *row0;
  int16_t *row1;
  int16_t *row2;
  uint16_t width;
  int32_t current_y;
  uint8_t mode;
  uint8_t threshold;
  uint8_t invert;
} iwebp_dither;

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t payload_offset;
  size_t payload_size;
  size_t alpha_offset;
  size_t alpha_size;
  iwebp_info info;
  uint8_t found_payload;
  uint8_t has_alpha_chunk;
} iwebp_parse;

typedef struct {
  iwebp_info info;
  const iwebp_render *render;
  iwebp_dither dither;
  uint16_t output_w;
  uint16_t output_h;
  int32_t output_x;
  int32_t output_y;
  uint8_t scaled;
  uint32_t *scale_sum;
  uint32_t *scale_count;
  uint16_t scale_y0;
  uint8_t scale_rows;
  uint8_t scale_row_active;
  uint8_t transparent_black;
  uint8_t *alpha;
} iwebp_output;

/* VP8 probability and quantization tables for InkWebP.
 * Values are the normative defaults/update probabilities from RFC 6386.
 */

static const uint8_t iwebp_dc_quant[128] = {
  4,     5,   6,   7,   8,   9,  10,  10,
  11,   12,  13,  14,  15,  16,  17,  17,
  18,   19,  20,  20,  21,  21,  22,  22,
  23,   23,  24,  25,  25,  26,  27,  28,
  29,   30,  31,  32,  33,  34,  35,  36,
  37,   37,  38,  39,  40,  41,  42,  43,
  44,   45,  46,  46,  47,  48,  49,  50,
  51,   52,  53,  54,  55,  56,  57,  58,
  59,   60,  61,  62,  63,  64,  65,  66,
  67,   68,  69,  70,  71,  72,  73,  74,
  75,   76,  76,  77,  78,  79,  80,  81,
  82,   83,  84,  85,  86,  87,  88,  89,
  91,   93,  95,  96,  98, 100, 101, 102,
  104, 106, 108, 110, 112, 114, 116, 118,
  122, 124, 126, 128, 130, 132, 134, 136,
  138, 140, 143, 145, 148, 151, 154, 157
};

static const uint16_t iwebp_ac_quant[128] = {
  4,     5,   6,   7,   8,   9,  10,  11,
  12,   13,  14,  15,  16,  17,  18,  19,
  20,   21,  22,  23,  24,  25,  26,  27,
  28,   29,  30,  31,  32,  33,  34,  35,
  36,   37,  38,  39,  40,  41,  42,  43,
  44,   45,  46,  47,  48,  49,  50,  51,
  52,   53,  54,  55,  56,  57,  58,  60,
  62,   64,  66,  68,  70,  72,  74,  76,
  78,   80,  82,  84,  86,  88,  90,  92,
  94,   96,  98, 100, 102, 104, 106, 108,
  110, 112, 114, 116, 119, 122, 125, 128,
  131, 134, 137, 140, 143, 146, 149, 152,
  155, 158, 161, 164, 167, 170, 173, 177,
  181, 185, 189, 193, 197, 201, 205, 209,
  213, 217, 221, 225, 229, 234, 239, 245,
  249, 254, 259, 264, 269, 274, 279, 284
};

static const uint8_t iwebp_coeff_probs[4][8][3][11] = {
  { { { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 }
    },
    { { 253, 136, 254, 255, 228, 219, 128, 128, 128, 128, 128 },
      { 189, 129, 242, 255, 227, 213, 255, 219, 128, 128, 128 },
      { 106, 126, 227, 252, 214, 209, 255, 255, 128, 128, 128 }
    },
    { { 1, 98, 248, 255, 236, 226, 255, 255, 128, 128, 128 },
      { 181, 133, 238, 254, 221, 234, 255, 154, 128, 128, 128 },
      { 78, 134, 202, 247, 198, 180, 255, 219, 128, 128, 128 },
    },
    { { 1, 185, 249, 255, 243, 255, 128, 128, 128, 128, 128 },
      { 184, 150, 247, 255, 236, 224, 128, 128, 128, 128, 128 },
      { 77, 110, 216, 255, 236, 230, 128, 128, 128, 128, 128 },
    },
    { { 1, 101, 251, 255, 241, 255, 128, 128, 128, 128, 128 },
      { 170, 139, 241, 252, 236, 209, 255, 255, 128, 128, 128 },
      { 37, 116, 196, 243, 228, 255, 255, 255, 128, 128, 128 }
    },
    { { 1, 204, 254, 255, 245, 255, 128, 128, 128, 128, 128 },
      { 207, 160, 250, 255, 238, 128, 128, 128, 128, 128, 128 },
      { 102, 103, 231, 255, 211, 171, 128, 128, 128, 128, 128 }
    },
    { { 1, 152, 252, 255, 240, 255, 128, 128, 128, 128, 128 },
      { 177, 135, 243, 255, 234, 225, 128, 128, 128, 128, 128 },
      { 80, 129, 211, 255, 194, 224, 128, 128, 128, 128, 128 }
    },
    { { 1, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 246, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 255, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 }
    }
  },
  { { { 198, 35, 237, 223, 193, 187, 162, 160, 145, 155, 62 },
      { 131, 45, 198, 221, 172, 176, 220, 157, 252, 221, 1 },
      { 68, 47, 146, 208, 149, 167, 221, 162, 255, 223, 128 }
    },
    { { 1, 149, 241, 255, 221, 224, 255, 255, 128, 128, 128 },
      { 184, 141, 234, 253, 222, 220, 255, 199, 128, 128, 128 },
      { 81, 99, 181, 242, 176, 190, 249, 202, 255, 255, 128 }
    },
    { { 1, 129, 232, 253, 214, 197, 242, 196, 255, 255, 128 },
      { 99, 121, 210, 250, 201, 198, 255, 202, 128, 128, 128 },
      { 23, 91, 163, 242, 170, 187, 247, 210, 255, 255, 128 }
    },
    { { 1, 200, 246, 255, 234, 255, 128, 128, 128, 128, 128 },
      { 109, 178, 241, 255, 231, 245, 255, 255, 128, 128, 128 },
      { 44, 130, 201, 253, 205, 192, 255, 255, 128, 128, 128 }
    },
    { { 1, 132, 239, 251, 219, 209, 255, 165, 128, 128, 128 },
      { 94, 136, 225, 251, 218, 190, 255, 255, 128, 128, 128 },
      { 22, 100, 174, 245, 186, 161, 255, 199, 128, 128, 128 }
    },
    { { 1, 182, 249, 255, 232, 235, 128, 128, 128, 128, 128 },
      { 124, 143, 241, 255, 227, 234, 128, 128, 128, 128, 128 },
      { 35, 77, 181, 251, 193, 211, 255, 205, 128, 128, 128 }
    },
    { { 1, 157, 247, 255, 236, 231, 255, 255, 128, 128, 128 },
      { 121, 141, 235, 255, 225, 227, 255, 255, 128, 128, 128 },
      { 45, 99, 188, 251, 195, 217, 255, 224, 128, 128, 128 }
    },
    { { 1, 1, 251, 255, 213, 255, 128, 128, 128, 128, 128 },
      { 203, 1, 248, 255, 255, 128, 128, 128, 128, 128, 128 },
      { 137, 1, 177, 255, 224, 255, 128, 128, 128, 128, 128 }
    }
  },
  { { { 253, 9, 248, 251, 207, 208, 255, 192, 128, 128, 128 },
      { 175, 13, 224, 243, 193, 185, 249, 198, 255, 255, 128 },
      { 73, 17, 171, 221, 161, 179, 236, 167, 255, 234, 128 }
    },
    { { 1, 95, 247, 253, 212, 183, 255, 255, 128, 128, 128 },
      { 239, 90, 244, 250, 211, 209, 255, 255, 128, 128, 128 },
      { 155, 77, 195, 248, 188, 195, 255, 255, 128, 128, 128 }
    },
    { { 1, 24, 239, 251, 218, 219, 255, 205, 128, 128, 128 },
      { 201, 51, 219, 255, 196, 186, 128, 128, 128, 128, 128 },
      { 69, 46, 190, 239, 201, 218, 255, 228, 128, 128, 128 }
    },
    { { 1, 191, 251, 255, 255, 128, 128, 128, 128, 128, 128 },
      { 223, 165, 249, 255, 213, 255, 128, 128, 128, 128, 128 },
      { 141, 124, 248, 255, 255, 128, 128, 128, 128, 128, 128 }
    },
    { { 1, 16, 248, 255, 255, 128, 128, 128, 128, 128, 128 },
      { 190, 36, 230, 255, 236, 255, 128, 128, 128, 128, 128 },
      { 149, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 }
    },
    { { 1, 226, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 247, 192, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 240, 128, 255, 128, 128, 128, 128, 128, 128, 128, 128 }
    },
    { { 1, 134, 252, 255, 255, 128, 128, 128, 128, 128, 128 },
      { 213, 62, 250, 255, 255, 128, 128, 128, 128, 128, 128 },
      { 55, 93, 255, 128, 128, 128, 128, 128, 128, 128, 128 }
    },
    { { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 }
    }
  },
  { { { 202, 24, 213, 235, 186, 191, 220, 160, 240, 175, 255 },
      { 126, 38, 182, 232, 169, 184, 228, 174, 255, 187, 128 },
      { 61, 46, 138, 219, 151, 178, 240, 170, 255, 216, 128 }
    },
    { { 1, 112, 230, 250, 199, 191, 247, 159, 255, 255, 128 },
      { 166, 109, 228, 252, 211, 215, 255, 174, 128, 128, 128 },
      { 39, 77, 162, 232, 172, 180, 245, 178, 255, 255, 128 }
    },
    { { 1, 52, 220, 246, 198, 199, 249, 220, 255, 255, 128 },
      { 124, 74, 191, 243, 183, 193, 250, 221, 255, 255, 128 },
      { 24, 71, 130, 219, 154, 170, 243, 182, 255, 255, 128 }
    },
    { { 1, 182, 225, 249, 219, 240, 255, 224, 128, 128, 128 },
      { 149, 150, 226, 252, 216, 205, 255, 171, 128, 128, 128 },
      { 28, 108, 170, 242, 183, 194, 254, 223, 255, 255, 128 }
    },
    { { 1, 81, 230, 252, 204, 203, 255, 192, 128, 128, 128 },
      { 123, 102, 209, 247, 188, 196, 255, 233, 128, 128, 128 },
      { 20, 95, 153, 243, 164, 173, 255, 203, 128, 128, 128 }
    },
    { { 1, 222, 248, 255, 216, 213, 128, 128, 128, 128, 128 },
      { 168, 175, 246, 252, 235, 205, 255, 255, 128, 128, 128 },
      { 47, 116, 215, 255, 211, 212, 255, 255, 128, 128, 128 }
    },
    { { 1, 121, 236, 253, 212, 214, 255, 255, 128, 128, 128 },
      { 141, 84, 213, 252, 201, 202, 255, 219, 128, 128, 128 },
      { 42, 80, 160, 240, 162, 185, 255, 205, 128, 128, 128 }
    },
    { { 1, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 244, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
      { 238, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 }
    }
  }
};

static const uint8_t iwebp_coeff_update_probs[4][8][3][11] = {
  { { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 176, 246, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 223, 241, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 249, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 244, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 234, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 246, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 239, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 254, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 248, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 251, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 251, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 254, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 254, 253, 255, 254, 255, 255, 255, 255, 255, 255 },
      { 250, 255, 254, 255, 254, 255, 255, 255, 255, 255, 255 },
      { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    }
  },
  { { { 217, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 225, 252, 241, 253, 255, 255, 254, 255, 255, 255, 255 },
      { 234, 250, 241, 250, 253, 255, 253, 254, 255, 255, 255 }
    },
    { { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 223, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 238, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 248, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 249, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 253, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 247, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 252, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 254, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 250, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    }
  },
  { { { 186, 251, 250, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 234, 251, 244, 254, 255, 255, 255, 255, 255, 255, 255 },
      { 251, 251, 243, 253, 254, 255, 254, 255, 255, 255, 255 }
    },
    { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 236, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 251, 253, 253, 254, 254, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    }
  },
  { { { 248, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 250, 254, 252, 254, 255, 255, 255, 255, 255, 255, 255 },
      { 248, 254, 249, 253, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 246, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 252, 254, 251, 254, 254, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 254, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 248, 254, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 253, 255, 254, 254, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 251, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 245, 251, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 253, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 251, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 252, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 252, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 249, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 255, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 250, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    },
    { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
    }
  }
};

static const uint8_t iwebp_coeff_bands[17] = {
  0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7,
  0  // extra entry as sentinel
};

static const uint8_t iwebp_bmode_probs[10][10][9] = {
  { { 231, 120, 48, 89, 115, 113, 120, 152, 112 },
    { 152, 179, 64, 126, 170, 118, 46, 70, 95 },
    { 175, 69, 143, 80, 85, 82, 72, 155, 103 },
    { 56, 58, 10, 171, 218, 189, 17, 13, 152 },
    { 114, 26, 17, 163, 44, 195, 21, 10, 173 },
    { 121, 24, 80, 195, 26, 62, 44, 64, 85 },
    { 144, 71, 10, 38, 171, 213, 144, 34, 26 },
    { 170, 46, 55, 19, 136, 160, 33, 206, 71 },
    { 63, 20, 8, 114, 114, 208, 12, 9, 226 },
    { 81, 40, 11, 96, 182, 84, 29, 16, 36 } },
  { { 134, 183, 89, 137, 98, 101, 106, 165, 148 },
    { 72, 187, 100, 130, 157, 111, 32, 75, 80 },
    { 66, 102, 167, 99, 74, 62, 40, 234, 128 },
    { 41, 53, 9, 178, 241, 141, 26, 8, 107 },
    { 74, 43, 26, 146, 73, 166, 49, 23, 157 },
    { 65, 38, 105, 160, 51, 52, 31, 115, 128 },
    { 104, 79, 12, 27, 217, 255, 87, 17, 7 },
    { 87, 68, 71, 44, 114, 51, 15, 186, 23 },
    { 47, 41, 14, 110, 182, 183, 21, 17, 194 },
    { 66, 45, 25, 102, 197, 189, 23, 18, 22 } },
  { { 88, 88, 147, 150, 42, 46, 45, 196, 205 },
    { 43, 97, 183, 117, 85, 38, 35, 179, 61 },
    { 39, 53, 200, 87, 26, 21, 43, 232, 171 },
    { 56, 34, 51, 104, 114, 102, 29, 93, 77 },
    { 39, 28, 85, 171, 58, 165, 90, 98, 64 },
    { 34, 22, 116, 206, 23, 34, 43, 166, 73 },
    { 107, 54, 32, 26, 51, 1, 81, 43, 31 },
    { 68, 25, 106, 22, 64, 171, 36, 225, 114 },
    { 34, 19, 21, 102, 132, 188, 16, 76, 124 },
    { 62, 18, 78, 95, 85, 57, 50, 48, 51 } },
  { { 193, 101, 35, 159, 215, 111, 89, 46, 111 },
    { 60, 148, 31, 172, 219, 228, 21, 18, 111 },
    { 112, 113, 77, 85, 179, 255, 38, 120, 114 },
    { 40, 42, 1, 196, 245, 209, 10, 25, 109 },
    { 88, 43, 29, 140, 166, 213, 37, 43, 154 },
    { 61, 63, 30, 155, 67, 45, 68, 1, 209 },
    { 100, 80, 8, 43, 154, 1, 51, 26, 71 },
    { 142, 78, 78, 16, 255, 128, 34, 197, 171 },
    { 41, 40, 5, 102, 211, 183, 4, 1, 221 },
    { 51, 50, 17, 168, 209, 192, 23, 25, 82 } },
  { { 138, 31, 36, 171, 27, 166, 38, 44, 229 },
    { 67, 87, 58, 169, 82, 115, 26, 59, 179 },
    { 63, 59, 90, 180, 59, 166, 93, 73, 154 },
    { 40, 40, 21, 116, 143, 209, 34, 39, 175 },
    { 47, 15, 16, 183, 34, 223, 49, 45, 183 },
    { 46, 17, 33, 183, 6, 98, 15, 32, 183 },
    { 57, 46, 22, 24, 128, 1, 54, 17, 37 },
    { 65, 32, 73, 115, 28, 128, 23, 128, 205 },
    { 40, 3, 9, 115, 51, 192, 18, 6, 223 },
    { 87, 37, 9, 115, 59, 77, 64, 21, 47 } },
  { { 104, 55, 44, 218, 9, 54, 53, 130, 226 },
    { 64, 90, 70, 205, 40, 41, 23, 26, 57 },
    { 54, 57, 112, 184, 5, 41, 38, 166, 213 },
    { 30, 34, 26, 133, 152, 116, 10, 32, 134 },
    { 39, 19, 53, 221, 26, 114, 32, 73, 255 },
    { 31, 9, 65, 234, 2, 15, 1, 118, 73 },
    { 75, 32, 12, 51, 192, 255, 160, 43, 51 },
    { 88, 31, 35, 67, 102, 85, 55, 186, 85 },
    { 56, 21, 23, 111, 59, 205, 45, 37, 192 },
    { 55, 38, 70, 124, 73, 102, 1, 34, 98 } },
  { { 125, 98, 42, 88, 104, 85, 117, 175, 82 },
    { 95, 84, 53, 89, 128, 100, 113, 101, 45 },
    { 75, 79, 123, 47, 51, 128, 81, 171, 1 },
    { 57, 17, 5, 71, 102, 57, 53, 41, 49 },
    { 38, 33, 13, 121, 57, 73, 26, 1, 85 },
    { 41, 10, 67, 138, 77, 110, 90, 47, 114 },
    { 115, 21, 2, 10, 102, 255, 166, 23, 6 },
    { 101, 29, 16, 10, 85, 128, 101, 196, 26 },
    { 57, 18, 10, 102, 102, 213, 34, 20, 43 },
    { 117, 20, 15, 36, 163, 128, 68, 1, 26 } },
  { { 102, 61, 71, 37, 34, 53, 31, 243, 192 },
    { 69, 60, 71, 38, 73, 119, 28, 222, 37 },
    { 68, 45, 128, 34, 1, 47, 11, 245, 171 },
    { 62, 17, 19, 70, 146, 85, 55, 62, 70 },
    { 37, 43, 37, 154, 100, 163, 85, 160, 1 },
    { 63, 9, 92, 136, 28, 64, 32, 201, 85 },
    { 75, 15, 9, 9, 64, 255, 184, 119, 16 },
    { 86, 6, 28, 5, 64, 255, 25, 248, 1 },
    { 56, 8, 17, 132, 137, 255, 55, 116, 128 },
    { 58, 15, 20, 82, 135, 57, 26, 121, 40 } },
  { { 164, 50, 31, 137, 154, 133, 25, 35, 218 },
    { 51, 103, 44, 131, 131, 123, 31, 6, 158 },
    { 86, 40, 64, 135, 148, 224, 45, 183, 128 },
    { 22, 26, 17, 131, 240, 154, 14, 1, 209 },
    { 45, 16, 21, 91, 64, 222, 7, 1, 197 },
    { 56, 21, 39, 155, 60, 138, 23, 102, 213 },
    { 83, 12, 13, 54, 192, 255, 68, 47, 28 },
    { 85, 26, 85, 85, 128, 128, 32, 146, 171 },
    { 18, 11, 7, 63, 144, 171, 4, 4, 246 },
    { 35, 27, 10, 146, 174, 171, 12, 26, 128 } },
  { { 190, 80, 35, 99, 180, 80, 126, 54, 45 },
    { 85, 126, 47, 87, 176, 51, 41, 20, 32 },
    { 101, 75, 128, 139, 118, 146, 116, 128, 85 },
    { 56, 41, 15, 176, 236, 85, 37, 9, 62 },
    { 71, 30, 17, 119, 118, 255, 17, 18, 138 },
    { 101, 38, 60, 138, 55, 70, 43, 26, 142 },
    { 146, 36, 19, 30, 171, 255, 97, 27, 20 },
    { 138, 45, 61, 62, 219, 1, 81, 188, 64 },
    { 32, 41, 20, 117, 151, 142, 20, 21, 163 },
    { 112, 19, 12, 61, 195, 128, 48, 4, 24 } }
};


typedef struct {
  uint32_t value;
  uint32_t range;
  int16_t bits;
  const uint8_t *pos;
  const uint8_t *end;
  uint8_t eof;
} iwebp_bool;

typedef struct {
  int16_t y1[2];
  int16_t y2[2];
  int16_t uv[2];
} iwebp_quant;

typedef struct {
  uint8_t enabled;
  uint8_t update_map;
  uint8_t absolute_delta;
  int16_t quantizer[4];
  uint8_t prob[3];
} iwebp_segment;

typedef struct {
  uint8_t coeff[4][8][3][11];
  uint8_t use_skip;
  uint8_t skip_prob;
  iwebp_quant q[4];
  iwebp_segment segment;
  uint16_t mb_w;
  uint16_t mb_h;
  uint16_t padded_w;
  uint8_t *above;
  uint8_t *mb;
  uint8_t *top_y2_nz;
  uint8_t *top_y_nz;
  uint8_t *top_uv_nz;
  uint8_t *top_bmode;
  uint8_t left_y_nz[4];
  uint8_t left_uv_nz[8];
  uint8_t left_bmode[4];
  uint8_t left_y2_nz;
  uint8_t last_right[16];
  uint8_t top_left;
  uint8_t bmode[16];
  uint8_t y_mode;
  uint8_t is_i4x4;
  uint8_t segment_id;
  uint8_t skip_coeff;
  uint8_t token_partitions;
} iwebp_vp8;

static const uint8_t iwebp_zigzag[16] = {
    0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};

static const uint8_t iwebp_cat3[] = {173, 148, 140, 0};
static const uint8_t iwebp_cat4[] = {176, 155, 140, 135, 0};
static const uint8_t iwebp_cat5[] = {180, 157, 141, 134, 130, 0};
static const uint8_t iwebp_cat6[] = {254, 254, 243, 230, 196, 177,
                                     153, 140, 133, 130, 129, 0};
static const uint8_t *const iwebp_cat3456[] = {iwebp_cat3, iwebp_cat4,
                                               iwebp_cat5, iwebp_cat6};

static void iwebp_bool_init(iwebp_bool *br, const uint8_t *data, size_t size) {
  br->value = 0;
  br->range = 254;
  br->bits = -8;
  br->pos = data;
  br->end = data + size;
  br->eof = 0;
}

static void iwebp_bool_load(iwebp_bool *br) {
  while (br->bits < 0) {
    uint8_t byte = 0;
    if (br->pos < br->end) {
      byte = *br->pos++;
    } else {
      br->eof = 1;
    }
    br->value = ((br->value << 8) | byte) & 0x00ffffffU;
    br->bits = (int16_t)(br->bits + 8);
    if (br->eof) {
      break;
    }
  }
}

static uint8_t iwebp_bool_bit(iwebp_bool *br, uint8_t prob) {
  uint32_t range = br->range;
  if (br->bits < 0) {
    iwebp_bool_load(br);
  }
  const uint32_t split = (range * prob) >> 8;
  const uint32_t value = br->bits >= 0 ? (br->value >> br->bits) : 0;
  uint8_t bit = (uint8_t)(value > split);
  if (bit) {
    range -= split + 1U;
    br->value -= (split + 1U) << br->bits;
  } else {
    range = split;
  }
  while (range < 127U) {
    range = (range << 1) | 1U;
    br->bits--;
  }
  br->range = range;
  return bit;
}

static uint32_t iwebp_bool_value(iwebp_bool *br, uint8_t bits) {
  uint32_t value = 0;
  while (bits-- != 0U) {
    value = (value << 1) | iwebp_bool_bit(br, 128);
  }
  return value;
}

static int32_t iwebp_bool_signed_value(iwebp_bool *br, uint8_t bits) {
  const int32_t value = (int32_t)iwebp_bool_value(br, bits);
  return iwebp_bool_bit(br, 128) ? -value : value;
}

static int16_t iwebp_bool_signed(iwebp_bool *br, int16_t value) {
  return iwebp_bool_bit(br, 128) ? (int16_t)-value : value;
}

static uint8_t iwebp_clip_u8(int32_t value) {
  value = value < 0 ? 0 : value;
  value = value > 255 ? 255 : value;
  return (uint8_t)value;
}

static uint8_t iwebp_clip_q(int32_t value, uint8_t max_value) {
  value = value < 0 ? 0 : value;
  value = value > max_value ? max_value : value;
  return (uint8_t)value;
}

static uint8_t iwebp_avg2(uint8_t a, uint8_t b) {
  return (uint8_t)(((uint16_t)a + b + 1U) >> 1);
}

static uint8_t iwebp_avg3(uint8_t a, uint8_t b, uint8_t c) {
  return (uint8_t)(((uint16_t)a + 2U * b + c + 2U) >> 2);
}

static void iwebp_parse_quant(iwebp_bool *br, iwebp_vp8 *vp8) {
  const int32_t base_q = (int32_t)iwebp_bool_value(br, 7);
  const int32_t dy1_dc =
      iwebp_bool_bit(br, 128) ? iwebp_bool_signed_value(br, 4) : 0;
  const int32_t dy2_dc =
      iwebp_bool_bit(br, 128) ? iwebp_bool_signed_value(br, 4) : 0;
  const int32_t dy2_ac =
      iwebp_bool_bit(br, 128) ? iwebp_bool_signed_value(br, 4) : 0;
  const int32_t duv_dc =
      iwebp_bool_bit(br, 128) ? iwebp_bool_signed_value(br, 4) : 0;
  const int32_t duv_ac =
      iwebp_bool_bit(br, 128) ? iwebp_bool_signed_value(br, 4) : 0;
  for (uint8_t i = 0; i < 4; i++) {
    int32_t q = base_q;
    if (vp8->segment.enabled) {
      q = vp8->segment.quantizer[i];
      q += (vp8->segment.absolute_delta == 0U) * base_q;
    }
    iwebp_quant *m = &vp8->q[i];
    m->y1[0] = iwebp_dc_quant[iwebp_clip_q(q + dy1_dc, 127)];
    m->y1[1] = (int16_t)iwebp_ac_quant[iwebp_clip_q(q, 127)];
    m->y2[0] = (int16_t)(iwebp_dc_quant[iwebp_clip_q(q + dy2_dc, 127)] * 2);
    m->y2[1] =
        (int16_t)((iwebp_ac_quant[iwebp_clip_q(q + dy2_ac, 127)] * 101581) >>
                  16);
    if (m->y2[1] < 8) {
      m->y2[1] = 8;
    }
    m->uv[0] = iwebp_dc_quant[iwebp_clip_q(q + duv_dc, 117)];
    m->uv[1] = (int16_t)iwebp_ac_quant[iwebp_clip_q(q + duv_ac, 127)];
  }
}

static void iwebp_wht(const int16_t *in, int16_t *out) {
  int32_t tmp[16];
  for (uint8_t i = 0; i < 4; i++) {
    const int32_t a0 = in[0 + i] + in[12 + i];
    const int32_t a1 = in[4 + i] + in[8 + i];
    const int32_t a2 = in[4 + i] - in[8 + i];
    const int32_t a3 = in[0 + i] - in[12 + i];
    tmp[0 + i] = a0 + a1;
    tmp[8 + i] = a0 - a1;
    tmp[4 + i] = a3 + a2;
    tmp[12 + i] = a3 - a2;
  }
  for (uint8_t i = 0; i < 4; i++) {
    const int32_t dc = tmp[0 + i * 4] + 3;
    const int32_t a0 = dc + tmp[3 + i * 4];
    const int32_t a1 = tmp[1 + i * 4] + tmp[2 + i * 4];
    const int32_t a2 = tmp[1 + i * 4] - tmp[2 + i * 4];
    const int32_t a3 = dc - tmp[3 + i * 4];
    out[0] = (int16_t)((a0 + a1) >> 3);
    out[16] = (int16_t)((a3 + a2) >> 3);
    out[32] = (int16_t)((a0 - a1) >> 3);
    out[48] = (int16_t)((a3 - a2) >> 3);
    out += 64;
  }
}

static int32_t iwebp_mul1(int32_t value) {
  return (((value * 20091) >> 16) + value);
}

static int32_t iwebp_mul2(int32_t value) {
  return ((value * 35468) >> 16);
}

static void iwebp_idct_add(const int16_t *in, uint8_t *dst, uint8_t stride) {
  int32_t tmp[16];
  for (uint8_t i = 0; i < 4; i++) {
    const int32_t a = in[0] + in[8];
    const int32_t b = in[0] - in[8];
    const int32_t c = iwebp_mul2(in[4]) - iwebp_mul1(in[12]);
    const int32_t d = iwebp_mul1(in[4]) + iwebp_mul2(in[12]);
    tmp[i * 4 + 0] = a + d;
    tmp[i * 4 + 1] = b + c;
    tmp[i * 4 + 2] = b - c;
    tmp[i * 4 + 3] = a - d;
    in++;
  }
  for (uint8_t i = 0; i < 4; i++) {
    const int32_t dc = tmp[i] + 4;
    const int32_t a = dc + tmp[8 + i];
    const int32_t b = dc - tmp[8 + i];
    const int32_t c = iwebp_mul2(tmp[4 + i]) - iwebp_mul1(tmp[12 + i]);
    const int32_t d = iwebp_mul1(tmp[4 + i]) + iwebp_mul2(tmp[12 + i]);
    dst[i * stride + 0] = iwebp_clip_u8(dst[i * stride + 0] + ((a + d) >> 3));
    dst[i * stride + 1] = iwebp_clip_u8(dst[i * stride + 1] + ((b + c) >> 3));
    dst[i * stride + 2] = iwebp_clip_u8(dst[i * stride + 2] + ((b - c) >> 3));
    dst[i * stride + 3] = iwebp_clip_u8(dst[i * stride + 3] + ((a - d) >> 3));
  }
}

static int32_t iwebp_large_coeff(iwebp_bool *br, const uint8_t *p) {
  int32_t v = 0;
  if (!iwebp_bool_bit(br, p[3])) {
    v = !iwebp_bool_bit(br, p[4]) ? 2 : 3 + iwebp_bool_bit(br, p[5]);
  } else if (!iwebp_bool_bit(br, p[6])) {
    if (!iwebp_bool_bit(br, p[7])) {
      v = 5 + iwebp_bool_bit(br, 159);
    } else {
      v = 7 + 2 * iwebp_bool_bit(br, 165);
      v += iwebp_bool_bit(br, 145);
    }
  } else {
    const uint8_t bit1 = iwebp_bool_bit(br, p[8]);
    const uint8_t bit0 = iwebp_bool_bit(br, p[9 + bit1]);
    const uint8_t cat = (uint8_t)(2U * bit1 + bit0);
    const uint8_t *tab = iwebp_cat3456[cat];
    while (*tab != 0U) {
      v += v + iwebp_bool_bit(br, *tab++);
    }
    v += 3 + (8 << cat);
  }
  return v;
}

static uint8_t iwebp_get_coeffs(iwebp_bool *br, iwebp_vp8 *vp8, uint8_t type,
                                uint8_t ctx, const int16_t dq[2], uint8_t n,
                                int16_t *out) {
  for (; n < 16U; n++) {
    const uint8_t band = iwebp_coeff_bands[n];
    const uint8_t *p = vp8->coeff[type][band][ctx > 2U ? 2U : ctx];
    if (!iwebp_bool_bit(br, p[0])) {
      return n;
    }
    while (!iwebp_bool_bit(br, p[1])) {
      n++;
      if (n == 16U) {
        return 16;
      }
      p = vp8->coeff[type][iwebp_coeff_bands[n]][0];
    }
    int32_t v = !iwebp_bool_bit(br, p[2]) ? 1 : iwebp_large_coeff(br, p);
    v = iwebp_bool_signed(br, (int16_t)v);
    out[iwebp_zigzag[n]] = (int16_t)(v * dq[n > 0U]);
    ctx = (uint8_t)(v == 1 || v == -1 ? 1 : 2);
  }
  return 16;
}

static uint32_t iwebp_read_le32_at(const uint8_t *data, size_t size,
                                   size_t offset, bool *ok) {
  if (offset > size || size - offset < 4U) {
    if (ok != NULL) {
      *ok = false;
    }
    return 0;
  }
  return (uint32_t)data[offset] | ((uint32_t)data[offset + 1U] << 8) |
         ((uint32_t)data[offset + 2U] << 16) |
         ((uint32_t)data[offset + 3U] << 24);
}

static uint16_t iwebp_read_le16_at(const uint8_t *data, size_t size,
                                   size_t offset, bool *ok) {
  if (offset > size || size - offset < 2U) {
    if (ok != NULL) {
      *ok = false;
    }
    return 0;
  }
  return (uint16_t)(data[offset] | ((uint16_t)data[offset + 1U] << 8));
}

static uint32_t iwebp_read_le24_at(const uint8_t *data, size_t size,
                                   size_t offset, bool *ok) {
  if (offset > size || size - offset < 3U) {
    if (ok != NULL) {
      *ok = false;
    }
    return 0;
  }
  return (uint32_t)data[offset] | ((uint32_t)data[offset + 1U] << 8) |
         ((uint32_t)data[offset + 2U] << 16);
}

static bool iwebp_valid_dimensions(uint32_t width, uint32_t height) {
  return width != 0U && height != 0U && width <= UINT16_MAX &&
         height <= UINT16_MAX && width <= 16384U && height <= 16384U &&
         width <= IWEBP_MAX_DIMENSION && height <= IWEBP_MAX_DIMENSION;
}

static iwebp_result iwebp_parse_vp8_info(const uint8_t *data, size_t size,
                                         iwebp_info *info) {
  bool ok = true;
  if (size < 10U) {
    return IWEBP_ERR_OUT_OF_DATA;
  }
  const uint32_t tag = iwebp_read_le24_at(data, size, 0, &ok);
  if (!ok || (tag & 1U) != 0U || ((tag >> 1) & 7U) > 3U ||
      ((tag >> 4) & 1U) == 0U) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  const uint32_t first_partition = tag >> 5;
  if (first_partition >= size) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  if (data[3] != 0x9d || data[4] != 0x01 || data[5] != 0x2a) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  const uint16_t packed_w = iwebp_read_le16_at(data, size, 6, &ok);
  const uint16_t packed_h = iwebp_read_le16_at(data, size, 8, &ok);
  const uint32_t width = packed_w & 0x3fffU;
  const uint32_t height = packed_h & 0x3fffU;
  if (!ok || !iwebp_valid_dimensions(width, height)) {
    return IWEBP_ERR_BAD_DIMENSIONS;
  }
  if (info != NULL) {
    info->width = (uint16_t)width;
    info->height = (uint16_t)height;
    info->codec = (uint8_t)IWEBP_CODEC_VP8;
  }
  return IWEBP_OK;
}

static iwebp_result iwebp_parse_vp8l_info(const uint8_t *data, size_t size,
                                          iwebp_info *info) {
  if (size < 5U) {
    return IWEBP_ERR_OUT_OF_DATA;
  }
  if (data[0] != 0x2f) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  const uint32_t bits = (uint32_t)data[1] | ((uint32_t)data[2] << 8) |
                        ((uint32_t)data[3] << 16) |
                        ((uint32_t)data[4] << 24);
  const uint32_t width = (bits & 0x3fffU) + 1U;
  const uint32_t height = ((bits >> 14) & 0x3fffU) + 1U;
  if (((bits >> 29) & 7U) != 0U) {
    return IWEBP_ERR_UNSUPPORTED_CODEC;
  }
  if (!iwebp_valid_dimensions(width, height)) {
    return IWEBP_ERR_BAD_DIMENSIONS;
  }
  if (info != NULL) {
    info->width = (uint16_t)width;
    info->height = (uint16_t)height;
    info->codec = (uint8_t)IWEBP_CODEC_VP8L;
    info->has_alpha = (uint8_t)(((bits >> 28) & 1U) != 0U);
  }
  return IWEBP_OK;
}

static iwebp_result iwebp_parse_vp8x(const uint8_t *data, size_t size,
                                     iwebp_info *info) {
  bool ok = true;
  if (size < 10U) {
    return IWEBP_ERR_OUT_OF_DATA;
  }
  const uint8_t flags = data[0];
  const uint32_t width = iwebp_read_le24_at(data, size, 4, &ok) + 1U;
  const uint32_t height = iwebp_read_le24_at(data, size, 7, &ok) + 1U;
  if (!ok || !iwebp_valid_dimensions(width, height)) {
    return IWEBP_ERR_BAD_DIMENSIONS;
  }
  if (info != NULL) {
    info->width = (uint16_t)width;
    info->height = (uint16_t)height;
    info->has_alpha = (uint8_t)((flags & 0x10U) != 0U);
    info->has_vp8x = 1;
  }
  return IWEBP_OK;
}

static iwebp_result iwebp_parse_image_payload(const uint8_t *data,
                                              size_t payload,
                                              uint32_t chunk_size,
                                              uint32_t chunk,
                                              bool require_vp8x_dimensions,
                                              iwebp_parse *parse) {
  iwebp_info payload_info = parse->info;
  const iwebp_result r =
      chunk == IWEBP_CHUNK_VP8
          ? iwebp_parse_vp8_info(data + payload, chunk_size, &payload_info)
          : iwebp_parse_vp8l_info(data + payload, chunk_size, &payload_info);
  if (r != IWEBP_OK) {
    return r;
  }
  if (require_vp8x_dimensions && parse->info.has_vp8x &&
      (parse->info.width != payload_info.width ||
       parse->info.height != payload_info.height)) {
    return IWEBP_ERR_BAD_DIMENSIONS;
  }
  payload_info.has_alpha =
      (uint8_t)(payload_info.has_alpha | parse->info.has_alpha |
                parse->has_alpha_chunk);
  payload_info.has_vp8x = parse->info.has_vp8x;
  parse->info = payload_info;
  parse->payload_offset = payload;
  parse->payload_size = chunk_size;
  parse->found_payload = 1;
  return IWEBP_OK;
}

static iwebp_result iwebp_parse_anmf(const uint8_t *data, size_t size,
                                     size_t payload, uint32_t chunk_size,
                                     iwebp_parse *parse) {
  bool ok = true;
  if (chunk_size < 16U) {
    return IWEBP_ERR_OUT_OF_DATA;
  }
  const uint32_t frame_w = iwebp_read_le24_at(data, size, payload + 6U, &ok) + 1U;
  const uint32_t frame_h = iwebp_read_le24_at(data, size, payload + 9U, &ok) + 1U;
  if (!ok || !iwebp_valid_dimensions(frame_w, frame_h)) {
    return IWEBP_ERR_BAD_DIMENSIONS;
  }
  const uint8_t canvas_alpha = parse->info.has_alpha;
  parse->info.width = (uint16_t)frame_w;
  parse->info.height = (uint16_t)frame_h;
  parse->info.has_alpha = canvas_alpha;

  const size_t end = payload + chunk_size;
  size_t offset = payload + 16U;
  while (offset < end) {
    if (end - offset < 8U) {
      return IWEBP_ERR_OUT_OF_DATA;
    }
    const uint32_t sub = iwebp_read_le32_at(data, size, offset, &ok);
    const uint32_t sub_size = iwebp_read_le32_at(data, size, offset + 4U, &ok);
    const size_t sub_payload = offset + 8U;
    const size_t padded = (size_t)sub_size + (sub_size & 1U);
    if (!ok || sub_payload > end || sub_size > end - sub_payload ||
        padded > end - sub_payload) {
      return IWEBP_ERR_OUT_OF_DATA;
    }
    if (sub == IWEBP_CHUNK_ALPH) {
      parse->alpha_offset = sub_payload;
      parse->alpha_size = sub_size;
      parse->has_alpha_chunk = 1;
      parse->info.has_alpha = 1;
    } else if (sub == IWEBP_CHUNK_VP8 || sub == IWEBP_CHUNK_VP8L) {
      return iwebp_parse_image_payload(data, sub_payload, sub_size, sub, false,
                                       parse);
    }
    offset = sub_payload + padded;
  }
  return IWEBP_ERR_UNSUPPORTED_CODEC;
}

static iwebp_result iwebp_parse_container(const uint8_t *data, size_t size,
                                          iwebp_parse *parse) {
  bool ok = true;
  if (data == NULL || parse == NULL || size < 12U) {
    return IWEBP_ERR_OUT_OF_DATA;
  }
  memset(parse, 0, sizeof(*parse));
  parse->data = data;
  parse->size = size;
  if (iwebp_read_le32_at(data, size, 0, &ok) != IWEBP_CHUNK_RIFF) {
    return IWEBP_ERR_BAD_RIFF;
  }
  const uint32_t riff_size = iwebp_read_le32_at(data, size, 4, &ok);
  if (!ok || iwebp_read_le32_at(data, size, 8, &ok) != IWEBP_CHUNK_WEBP) {
    return IWEBP_ERR_BAD_WEBP;
  }
  const size_t riff_end =
      riff_size <= (uint32_t)(SIZE_MAX - 8U) ? (size_t)riff_size + 8U : size;
  const size_t end = riff_end < size ? riff_end : size;
  size_t offset = 12U;
  while (offset < end) {
    if (end - offset < 8U) {
      return IWEBP_ERR_OUT_OF_DATA;
    }
    const uint32_t chunk = iwebp_read_le32_at(data, size, offset, &ok);
    const uint32_t chunk_size = iwebp_read_le32_at(data, size, offset + 4U, &ok);
    const size_t payload = offset + 8U;
    const size_t padded = (size_t)chunk_size + (chunk_size & 1U);
    if (!ok || payload > end || chunk_size > end - payload ||
        padded > end - payload) {
      return IWEBP_ERR_OUT_OF_DATA;
    }
    if (chunk == IWEBP_CHUNK_ANMF) {
      return iwebp_parse_anmf(data, size, payload, chunk_size, parse);
    }
    if (chunk == IWEBP_CHUNK_ANIM) {
      offset = payload + padded;
      continue;
    }
    if (chunk == IWEBP_CHUNK_VP8X) {
      const iwebp_result r = iwebp_parse_vp8x(data + payload, chunk_size,
                                             &parse->info);
      if (r != IWEBP_OK) {
        return r;
      }
    } else if (chunk == IWEBP_CHUNK_ALPH) {
      parse->alpha_offset = payload;
      parse->alpha_size = chunk_size;
      parse->has_alpha_chunk = 1;
      parse->info.has_alpha = 1;
    } else if (chunk == IWEBP_CHUNK_VP8 || chunk == IWEBP_CHUNK_VP8L) {
      const iwebp_result r = iwebp_parse_image_payload(
          data, payload, chunk_size, chunk, true, parse);
      if (r != IWEBP_OK) {
        return r;
      }
      return IWEBP_OK;
    }
    offset = payload + padded;
  }
  return IWEBP_ERR_UNSUPPORTED_CODEC;
}

static void iwebp_dither_free(iwebp_dither *d) {
  if (d->row0 != NULL) {
    IWEBP_FREE(d->row0);
  }
  if (d->row1 != NULL) {
    IWEBP_FREE(d->row1);
  }
  if (d->row2 != NULL) {
    IWEBP_FREE(d->row2);
  }
  memset(d, 0, sizeof(*d));
  d->current_y = -1;
}

static bool iwebp_dither_init(iwebp_dither *d, uint16_t width, uint8_t mode,
                              uint8_t threshold, uint8_t invert) {
  memset(d, 0, sizeof(*d));
  d->current_y = -1;
  d->width = width;
  d->mode = mode <= (uint8_t)IWEBP_DITHER_SIMPLE2D
                ? mode
                : (uint8_t)IWEBP_DITHER_THRESHOLD;
  d->threshold = threshold == 0U ? (uint8_t)IWEBP_DEFAULT_THRESHOLD : threshold;
  d->invert = invert;
  if (d->mode == IWEBP_DITHER_THRESHOLD || width == 0U) {
    return true;
  }
  const size_t bytes = ((size_t)width + 4U) * sizeof(int16_t);
  d->row0 = (int16_t *)IWEBP_MALLOC(bytes);
  d->row1 = (int16_t *)IWEBP_MALLOC(bytes);
  d->row2 = (int16_t *)IWEBP_MALLOC(bytes);
  if (d->row0 == NULL || d->row1 == NULL || d->row2 == NULL) {
    iwebp_dither_free(d);
    return false;
  }
  memset(d->row0, 0, bytes);
  memset(d->row1, 0, bytes);
  memset(d->row2, 0, bytes);
  return true;
}

static void iwebp_dither_clear(iwebp_dither *d) {
  if (d->row0 == NULL) {
    return;
  }
  const size_t bytes = ((size_t)d->width + 4U) * sizeof(int16_t);
  memset(d->row0, 0, bytes);
  memset(d->row1, 0, bytes);
  memset(d->row2, 0, bytes);
}

static void iwebp_dither_roll(iwebp_dither *d) {
  if (d->row0 == NULL) {
    return;
  }
  int16_t *old = d->row0;
  d->row0 = d->row1;
  d->row1 = d->row2;
  d->row2 = old;
  memset(d->row2, 0, ((size_t)d->width + 4U) * sizeof(int16_t));
}

static void iwebp_dither_row(iwebp_dither *d, int32_t y) {
  if (d->current_y == y) {
    return;
  }
  if (d->current_y >= 0 && y == d->current_y + 1) {
    iwebp_dither_roll(d);
  } else {
    iwebp_dither_clear(d);
  }
  d->current_y = y;
}

static void iwebp_dither_add(int16_t *row, uint16_t width, int32_t x,
                             int16_t value) {
  if (row != NULL && (uint32_t)x < (uint32_t)width) {
    int32_t sum = (int32_t)row[x + 2] + value;
    sum = sum < -4096 ? -4096 : sum;
    sum = sum > 4096 ? 4096 : sum;
    row[x + 2] = (int16_t)sum;
  }
}

static uint8_t iwebp_dither_pixel(iwebp_dither *d, uint16_t x, int32_t y,
                                  uint8_t gray) {
  iwebp_dither_row(d, y);
  if (d->mode == IWEBP_DITHER_THRESHOLD || d->row0 == NULL) {
    const uint8_t black = (uint8_t)(gray < d->threshold);
    return (uint8_t)(black ^ (d->invert != 0U));
  }

  const size_t ix = (size_t)x + 2U;
  int32_t adjusted = (int32_t)gray + d->row0[ix];
  d->row0[ix] = 0;
  adjusted = adjusted < 0 ? 0 : adjusted;
  adjusted = adjusted > 255 ? 255 : adjusted;
  const uint8_t black = (uint8_t)(adjusted < d->threshold);
  const int16_t target = black ? 0 : 255;
  const int16_t error = (int16_t)(adjusted - target);

  if (d->mode == IWEBP_DITHER_ATKINSON) {
    const int16_t e = (int16_t)(error / 8);
    iwebp_dither_add(d->row0, d->width, (int32_t)x + 1, e);
    iwebp_dither_add(d->row0, d->width, (int32_t)x + 2, e);
    iwebp_dither_add(d->row1, d->width, (int32_t)x - 1, e);
    iwebp_dither_add(d->row1, d->width, x, e);
    iwebp_dither_add(d->row1, d->width, (int32_t)x + 1, e);
    iwebp_dither_add(d->row2, d->width, x, e);
  } else if (d->mode == IWEBP_DITHER_SIERRA_TWO_ROW) {
    iwebp_dither_add(d->row0, d->width, (int32_t)x + 1,
                     (int16_t)(error * 4 / 16));
    iwebp_dither_add(d->row0, d->width, (int32_t)x + 2,
                     (int16_t)(error * 3 / 16));
    iwebp_dither_add(d->row1, d->width, (int32_t)x - 2,
                     (int16_t)(error / 16));
    iwebp_dither_add(d->row1, d->width, (int32_t)x - 1,
                     (int16_t)(error * 2 / 16));
    iwebp_dither_add(d->row1, d->width, x, (int16_t)(error * 3 / 16));
    iwebp_dither_add(d->row1, d->width, (int32_t)x + 1,
                     (int16_t)(error * 2 / 16));
    iwebp_dither_add(d->row1, d->width, (int32_t)x + 2,
                     (int16_t)(error / 16));
  } else {
    const int16_t e = (int16_t)(error / 4);
    iwebp_dither_add(d->row0, d->width, (int32_t)x + 1, (int16_t)(e * 2));
    iwebp_dither_add(d->row1, d->width, x, e);
    iwebp_dither_add(d->row1, d->width, (int32_t)x + 1, e);
  }
  return (uint8_t)(black ^ (d->invert != 0U));
}

static uint16_t iwebp_fit_dim(uint32_t src, uint32_t other_src,
                              uint16_t target, uint16_t other_target) {
  if (src == 0U || other_src == 0U || target == 0U || other_target == 0U) {
    return 0;
  }
  if ((uint64_t)other_src * target <= (uint64_t)src * other_target) {
    return target;
  }
  uint64_t value = ((uint64_t)src * other_target) / other_src;
  value += value == 0U;
  return value > target ? target : (uint16_t)value;
}

static void iwebp_plot_black(const iwebp_render *r, int32_t x, int32_t y) {
  if (r != NULL && r->pixel != NULL && ((uint32_t)x + 32768U) <= 65535U &&
      ((uint32_t)y + 32768U) <= 65535U) {
    r->pixel(r->user, (int16_t)x, (int16_t)y);
  }
}

static void iwebp_clear_scale_band(iwebp_output *out) {
  if (out == NULL || out->scale_sum == NULL || out->scale_count == NULL) {
    return;
  }
  const size_t cells = (size_t)out->output_w * out->scale_rows;
  memset(out->scale_sum, 0, cells * sizeof(uint32_t));
  memset(out->scale_count, 0, cells * sizeof(uint32_t));
}

static void iwebp_clear_scale_rows(iwebp_output *out, uint8_t first,
                                   uint8_t end) {
  if (out == NULL || out->scale_sum == NULL || out->scale_count == NULL ||
      first >= end || first >= out->scale_rows) {
    return;
  }
  if (end > out->scale_rows) {
    end = out->scale_rows;
  }
  const size_t first_cell = (size_t)first * out->output_w;
  const size_t cells = (size_t)(end - first) * out->output_w;
  memset(out->scale_sum + first_cell, 0, cells * sizeof(uint32_t));
  memset(out->scale_count + first_cell, 0, cells * sizeof(uint32_t));
}

static void iwebp_flush_scale_rows(iwebp_output *out, uint8_t first,
                                   uint8_t end) {
  if (out == NULL || out->scale_count == NULL || out->scale_sum == NULL ||
      out->render == NULL || first >= end || first >= out->scale_rows) {
    return;
  }
  if (end > out->scale_rows) {
    end = out->scale_rows;
  }
  for (uint8_t row = first; row < end; row++) {
    const uint16_t sy = (uint16_t)(out->scale_y0 + row);
    if (sy >= out->output_h) {
      break;
    }
    const size_t base = (size_t)row * out->output_w;
    for (uint16_t sx = 0; sx < out->output_w; sx++) {
      if (out->scale_count[base + sx] == 0U) {
        continue;
      }
      const uint8_t avg =
          (uint8_t)(out->scale_sum[base + sx] / out->scale_count[base + sx]);
      if (iwebp_dither_pixel(&out->dither, sx, sy, avg)) {
        iwebp_plot_black(out->render, out->output_x + sx, out->output_y + sy);
      }
    }
  }
}

static void iwebp_begin_scale_band(iwebp_output *out, uint32_t src_y) {
  if (out == NULL || !out->scaled || out->info.height == 0U ||
      out->output_h == 0U) {
    return;
  }
  const uint16_t new_y0 =
      (uint16_t)(((uint64_t)src_y * out->output_h) / out->info.height);
  if (!out->scale_row_active) {
    out->scale_y0 = new_y0;
    out->scale_row_active = 1;
    iwebp_clear_scale_band(out);
    return;
  }
  if (new_y0 <= out->scale_y0) {
    out->scale_y0 = new_y0;
    iwebp_clear_scale_band(out);
    return;
  }
  const uint16_t old_y0 = out->scale_y0;
  const uint16_t old_y1 = (uint16_t)(old_y0 + out->scale_rows);
  if (new_y0 >= old_y1) {
    iwebp_flush_scale_rows(out, 0, out->scale_rows);
    out->scale_y0 = new_y0;
    iwebp_clear_scale_band(out);
    return;
  }
  const uint8_t flush_rows = (uint8_t)(new_y0 - old_y0);
  const uint8_t keep_rows = (uint8_t)(old_y1 - new_y0);
  iwebp_flush_scale_rows(out, 0, flush_rows);
  const size_t shift = (size_t)flush_rows * out->output_w;
  const size_t keep = (size_t)keep_rows * out->output_w;
  memmove(out->scale_sum, out->scale_sum + shift, keep * sizeof(uint32_t));
  memmove(out->scale_count, out->scale_count + shift, keep * sizeof(uint32_t));
  out->scale_y0 = new_y0;
  out->scale_row_active = 1;
  iwebp_clear_scale_rows(out, keep_rows, out->scale_rows);
}

static void iwebp_flush_scale_band(iwebp_output *out) {
  if (out == NULL || out->scale_count == NULL || out->scale_sum == NULL ||
      out->render == NULL || !out->scale_row_active) {
    return;
  }
  iwebp_flush_scale_rows(out, 0, out->scale_rows);
  out->scale_row_active = 0;
}

static void iwebp_emit_luma(iwebp_output *out, uint32_t x, uint32_t y,
                            uint8_t gray) {
  const iwebp_render *r = out != NULL ? out->render : NULL;
  if (r == NULL || r->pixel == NULL) {
    return;
  }
  if (out->alpha != NULL && x < out->info.width && y < out->info.height &&
      out->alpha[(size_t)y * out->info.width + x] < 128U) {
    gray = out->transparent_black ? 0U : 255U;
  }
  if (out->scaled) {
    const uint32_t dx = ((uint64_t)x * out->output_w) / out->info.width;
    const uint32_t dy = ((uint64_t)y * out->output_h) / out->info.height;
    if (dx >= out->output_w || dy >= out->output_h) {
      return;
    }
    if (!out->scale_row_active || dy < out->scale_y0 ||
        dy >= (uint32_t)out->scale_y0 + out->scale_rows) {
      iwebp_flush_scale_band(out);
      iwebp_begin_scale_band(out, y);
    }
    const size_t cell =
        (size_t)(dy - out->scale_y0) * out->output_w + (size_t)dx;
    out->scale_sum[cell] += gray;
    out->scale_count[cell]++;
    return;
  }
  if (x >= out->output_w || y >= out->output_h) {
    return;
  }
  if (iwebp_dither_pixel(&out->dither, (uint16_t)x, (int32_t)y, gray)) {
    iwebp_plot_black(r, out->output_x + (int32_t)x,
                     out->output_y + (int32_t)y);
  }
}

static bool iwebp_setup_output(iwebp_output *out, const iwebp_info *info,
                               const iwebp_render *render) {
  memset(out, 0, sizeof(*out));
  out->info = *info;
  out->render = render;
  out->transparent_black = render != NULL ? render->transparent_black : 0U;
  out->output_w = info->width;
  out->output_h = info->height;
  out->output_x = render != NULL ? render->x : 0;
  out->output_y = render != NULL ? render->y : 0;
  if (render != NULL && render->scale == IWEBP_SCALE_FIT &&
      render->width != 0U && render->height != 0U &&
      (info->width > render->width || info->height > render->height)) {
    out->output_w =
        iwebp_fit_dim(info->width, info->height, render->width, render->height);
    out->output_h = iwebp_fit_dim(info->height, info->width, render->height,
                                  render->width);
    if (out->output_w == 0U || out->output_h == 0U) {
      return false;
    }
    out->output_x =
        (int32_t)render->x + (int32_t)((render->width - out->output_w) / 2U);
    out->output_y =
        (int32_t)render->y + (int32_t)((render->height - out->output_h) / 2U);
    out->scaled = 1;
    out->scale_rows = 18U;
    if (out->scale_rows > out->output_h) {
      out->scale_rows = (uint8_t)out->output_h;
    }
    out->scale_sum =
        (uint32_t *)IWEBP_MALLOC((size_t)out->output_w * out->scale_rows *
                                 sizeof(uint32_t));
    out->scale_count =
        (uint32_t *)IWEBP_MALLOC((size_t)out->output_w * out->scale_rows *
                                 sizeof(uint32_t));
    if (out->scale_sum == NULL || out->scale_count == NULL) {
      return false;
    }
    iwebp_clear_scale_band(out);
  }
  const uint8_t mode = render != NULL ? render->dither : 0U;
  const uint8_t threshold = render != NULL ? render->threshold : 0U;
  const uint8_t invert = render != NULL ? render->invert : 0U;
  return iwebp_dither_init(&out->dither, out->output_w, mode, threshold,
                           invert);
}

static void iwebp_free_output(iwebp_output *out) {
  if (out == NULL) {
    return;
  }
  if (out->scale_row_active) {
    iwebp_flush_scale_band(out);
  }
  iwebp_dither_free(&out->dither);
  if (out->scale_sum != NULL) {
    IWEBP_FREE(out->scale_sum);
  }
  if (out->scale_count != NULL) {
    IWEBP_FREE(out->scale_count);
  }
  if (out->alpha != NULL) {
    IWEBP_FREE(out->alpha);
  }
  memset(out, 0, sizeof(*out));
}

static bool iwebp_vp8_alloc(iwebp_vp8 *vp8, const iwebp_info *info) {
  vp8->mb_w = (uint16_t)((info->width + 15U) >> 4);
  vp8->mb_h = (uint16_t)((info->height + 15U) >> 4);
  vp8->padded_w = (uint16_t)(vp8->mb_w * 16U);
  vp8->above = (uint8_t *)IWEBP_MALLOC(vp8->padded_w);
  vp8->mb = (uint8_t *)IWEBP_MALLOC(16U * 16U);
  vp8->top_y2_nz = (uint8_t *)IWEBP_MALLOC(vp8->mb_w);
  vp8->top_y_nz = (uint8_t *)IWEBP_MALLOC((size_t)vp8->mb_w * 4U);
  vp8->top_uv_nz = (uint8_t *)IWEBP_MALLOC((size_t)vp8->mb_w * 8U);
  vp8->top_bmode = (uint8_t *)IWEBP_MALLOC((size_t)vp8->mb_w * 4U);
  if (vp8->above == NULL || vp8->mb == NULL || vp8->top_y2_nz == NULL ||
      vp8->top_y_nz == NULL || vp8->top_uv_nz == NULL ||
      vp8->top_bmode == NULL) {
    return false;
  }
  memset(vp8->above, 127, vp8->padded_w);
  memset(vp8->top_y2_nz, 0, vp8->mb_w);
  memset(vp8->top_y_nz, 0, (size_t)vp8->mb_w * 4U);
  memset(vp8->top_uv_nz, 0, (size_t)vp8->mb_w * 8U);
  memset(vp8->top_bmode, 0, (size_t)vp8->mb_w * 4U);
  return true;
}

static void iwebp_vp8_free(iwebp_vp8 *vp8) {
  if (vp8->above != NULL) {
    IWEBP_FREE(vp8->above);
  }
  if (vp8->mb != NULL) {
    IWEBP_FREE(vp8->mb);
  }
  if (vp8->top_y2_nz != NULL) {
    IWEBP_FREE(vp8->top_y2_nz);
  }
  if (vp8->top_y_nz != NULL) {
    IWEBP_FREE(vp8->top_y_nz);
  }
  if (vp8->top_uv_nz != NULL) {
    IWEBP_FREE(vp8->top_uv_nz);
  }
  if (vp8->top_bmode != NULL) {
    IWEBP_FREE(vp8->top_bmode);
  }
  memset(vp8, 0, sizeof(*vp8));
}

static iwebp_result iwebp_parse_segment_header(iwebp_bool *br,
                                               iwebp_vp8 *vp8) {
  iwebp_segment *s = &vp8->segment;
  s->enabled = iwebp_bool_bit(br, 128);
  s->absolute_delta = 1;
  s->prob[0] = s->prob[1] = s->prob[2] = 255;
  if (!s->enabled) {
    return IWEBP_OK;
  }
  s->update_map = iwebp_bool_bit(br, 128);
  if (iwebp_bool_bit(br, 128)) {
    s->absolute_delta = iwebp_bool_bit(br, 128);
    for (uint8_t i = 0; i < 4U; i++) {
      s->quantizer[i] =
          iwebp_bool_bit(br, 128) ? (int16_t)iwebp_bool_signed_value(br, 7) : 0;
    }
    for (uint8_t i = 0; i < 4U; i++) {
      if (iwebp_bool_bit(br, 128)) {
        (void)iwebp_bool_signed_value(br, 6);
      }
    }
  }
  if (s->update_map) {
    for (uint8_t i = 0; i < 3U; i++) {
      s->prob[i] = iwebp_bool_bit(br, 128) ? (uint8_t)iwebp_bool_value(br, 8)
                                           : 255U;
    }
  }
  return br->eof ? IWEBP_ERR_BAD_BITSTREAM : IWEBP_OK;
}

static void iwebp_skip_filter_header(iwebp_bool *br) {
  (void)iwebp_bool_bit(br, 128);
  (void)iwebp_bool_value(br, 6);
  (void)iwebp_bool_value(br, 3);
  if (iwebp_bool_bit(br, 128) && iwebp_bool_bit(br, 128)) {
    for (uint8_t i = 0; i < 4U; i++) {
      if (iwebp_bool_bit(br, 128)) {
        (void)iwebp_bool_signed_value(br, 6);
      }
    }
    for (uint8_t i = 0; i < 4U; i++) {
      if (iwebp_bool_bit(br, 128)) {
        (void)iwebp_bool_signed_value(br, 6);
      }
    }
  }
}

static iwebp_result iwebp_parse_vp8_frame_header(const uint8_t *data,
                                                 size_t size, iwebp_vp8 *vp8,
                                                 iwebp_bool *mode_br,
                                                 iwebp_bool *token_br) {
  bool ok = true;
  const uint32_t tag = iwebp_read_le24_at(data, size, 0, &ok);
  if (!ok || (tag & 1U) != 0U || ((tag >> 4) & 1U) == 0U ||
      data[3] != 0x9d || data[4] != 0x01 || data[5] != 0x2a) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  const uint32_t first_partition = tag >> 5;
  if (first_partition == 0U || first_partition > size - 10U) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  iwebp_bool_init(mode_br, data + 10U, first_partition);
  (void)iwebp_bool_bit(mode_br, 128);
  (void)iwebp_bool_bit(mode_br, 128);
  iwebp_result r = iwebp_parse_segment_header(mode_br, vp8);
  if (r != IWEBP_OK) {
    return r;
  }
  iwebp_skip_filter_header(mode_br);
  const uint8_t partition_bits = (uint8_t)iwebp_bool_value(mode_br, 2);
  vp8->token_partitions = (uint8_t)(1U << partition_bits);
  iwebp_parse_quant(mode_br, vp8);
  (void)iwebp_bool_bit(mode_br, 128);
  memcpy(vp8->coeff, iwebp_coeff_probs, sizeof(vp8->coeff));
  for (uint8_t t = 0; t < 4U; t++) {
    for (uint8_t b = 0; b < 8U; b++) {
      for (uint8_t c = 0; c < 3U; c++) {
        for (uint8_t p = 0; p < 11U; p++) {
          if (iwebp_bool_bit(mode_br, iwebp_coeff_update_probs[t][b][c][p])) {
            vp8->coeff[t][b][c][p] = (uint8_t)iwebp_bool_value(mode_br, 8);
          }
        }
      }
    }
  }
  vp8->use_skip = iwebp_bool_bit(mode_br, 128);
  if (vp8->use_skip) {
    vp8->skip_prob = (uint8_t)iwebp_bool_value(mode_br, 8);
  }
  size_t offset = 10U + first_partition;
  size_t remaining = size - offset;
  uint32_t part_size[IWEBP_MAX_TOKEN_PARTITIONS];
  memset(part_size, 0, sizeof(part_size));
  if (vp8->token_partitions > 1U) {
    const size_t header_bytes = (size_t)(vp8->token_partitions - 1U) * 3U;
    if (remaining < header_bytes) {
      return IWEBP_ERR_BAD_BITSTREAM;
    }
    for (uint8_t i = 0; i < vp8->token_partitions - 1U; i++) {
      part_size[i] = iwebp_read_le24_at(data, size, offset + (size_t)i * 3U,
                                        &ok);
    }
    if (!ok) {
      return IWEBP_ERR_BAD_BITSTREAM;
    }
    offset += header_bytes;
    remaining -= header_bytes;
  }
  for (uint8_t i = 0; i < vp8->token_partitions; i++) {
    size_t bytes = remaining;
    if (i + 1U < vp8->token_partitions) {
      bytes = part_size[i];
      if (bytes > remaining) {
        return IWEBP_ERR_BAD_BITSTREAM;
      }
    }
    iwebp_bool_init(&token_br[i], data + offset, bytes);
    offset += bytes;
    remaining -= bytes;
  }
  return mode_br->eof ? IWEBP_ERR_BAD_BITSTREAM : IWEBP_OK;
}

static void iwebp_parse_mb_mode(iwebp_bool *br, iwebp_vp8 *vp8,
                                uint16_t mbx) {
  uint8_t *top = vp8->top_bmode + (size_t)mbx * 4U;

  vp8->segment_id =
      (vp8->segment.enabled && vp8->segment.update_map)
          ? (!iwebp_bool_bit(br, vp8->segment.prob[0])
                 ? (uint8_t)iwebp_bool_bit(br, vp8->segment.prob[1])
                 : (uint8_t)(2U + iwebp_bool_bit(br, vp8->segment.prob[2])))
          : 0U;

  vp8->skip_coeff =
      vp8->use_skip ? (uint8_t)iwebp_bool_bit(br, vp8->skip_prob) : 0U;

  vp8->is_i4x4 = (uint8_t)!iwebp_bool_bit(br, 145);

  if (!vp8->is_i4x4) {
    vp8->y_mode =
        iwebp_bool_bit(br, 156)
            ? (iwebp_bool_bit(br, 128) ? 1U : 3U)
            : (iwebp_bool_bit(br, 163) ? 2U : 0U);

    memset(top, vp8->y_mode, 4U);
    memset(vp8->left_bmode, vp8->y_mode, 4U);
  } else {
    for (uint8_t y = 0; y < 4U; y++) {
      uint8_t left = vp8->left_bmode[y];

      for (uint8_t x = 0; x < 4U; x++) {
        const uint8_t *p = iwebp_bmode_probs[top[x]][left];

        const uint8_t mode =
            !iwebp_bool_bit(br, p[0]) ? 0U :
            !iwebp_bool_bit(br, p[1]) ? 1U :
            !iwebp_bool_bit(br, p[2]) ? 2U :
            !iwebp_bool_bit(br, p[3])
                ? (!iwebp_bool_bit(br, p[4]) ? 3U :
                   !iwebp_bool_bit(br, p[5]) ? 4U : 5U)
                : (!iwebp_bool_bit(br, p[6]) ? 6U :
                   !iwebp_bool_bit(br, p[7]) ? 7U :
                   !iwebp_bool_bit(br, p[8]) ? 8U : 9U);

        top[x] = mode;
        vp8->bmode[y * 4U + x] = mode;
        left = mode;
      }

      vp8->left_bmode[y] = left;
    }
  }
  (void)(!iwebp_bool_bit(br, 142)
             ? 0
             : (!iwebp_bool_bit(br, 114) ? 1
                                         : (iwebp_bool_bit(br, 183) ? 2 : 3)));
}

static void iwebp_predict_mb(iwebp_vp8 *vp8, uint16_t mbx, uint16_t mby) {
  const uint8_t have_top = (uint8_t)(mby != 0U);
  const uint8_t have_left = (uint8_t)(mbx != 0U);
  const uint16_t base_x = (uint16_t)(mbx * 16U);
  const uint8_t top_left = (have_top && have_left) ? vp8->top_left : 127U;
  uint8_t dc = 128;
  if (vp8->y_mode == 0U) {
    uint16_t sum = 0;
    uint8_t count = 0;
    if (have_top) {
      for (uint8_t x = 0; x < 16U; x++) {
        sum += vp8->above[base_x + x];
      }
      count += 16;
    }
    if (have_left) {
      for (uint8_t y = 0; y < 16U; y++) {
        sum += vp8->last_right[y];
      }
      count += 16;
    }
    dc = count != 0U ? (uint8_t)((sum + (count >> 1)) / count) : 128U;
  }
  for (uint8_t y = 0; y < 16U; y++) {
    for (uint8_t x = 0; x < 16U; x++) {
      uint8_t value = dc;
      if (vp8->y_mode == 1U) {
        const int32_t pred =
            (int32_t)(have_left ? vp8->last_right[y] : 129U) +
            (int32_t)(have_top ? vp8->above[base_x + x] : 127U) - top_left;
        value = iwebp_clip_u8(pred);
      } else if (vp8->y_mode == 2U) {
        value = have_top ? vp8->above[base_x + x] : 127U;
      } else if (vp8->y_mode == 3U) {
        value = have_left ? vp8->last_right[y] : 129U;
      }
      vp8->mb[(uint16_t)y * 16U + x] = value;
    }
  }
}

static void iwebp_update_edges(iwebp_vp8 *vp8, uint16_t mbx) {
  const uint16_t base_x = (uint16_t)(mbx * 16U);
  const uint8_t next_top_left = vp8->above[base_x + 15U];
  for (uint8_t x = 0; x < 16U; x++) {
    vp8->above[base_x + x] = vp8->mb[15U * 16U + x];
  }
  for (uint8_t y = 0; y < 16U; y++) {
    vp8->last_right[y] = vp8->mb[(uint16_t)y * 16U + 15U];
  }
  vp8->top_left = next_top_left;
}

static void iwebp_predict_4x4(iwebp_vp8 *vp8, uint16_t mbx, uint16_t mby,
                              uint8_t bx, uint8_t by, uint8_t mode) {
  const uint8_t have_top = (uint8_t)(mby != 0U || by != 0U);
  const uint8_t have_left = (uint8_t)(mbx != 0U || bx != 0U);
  const uint16_t base_x = (uint16_t)(mbx * 16U + bx * 4U);
  const uint8_t base_y = (uint8_t)(by * 4U);
  uint8_t top[8];
  uint8_t left[4];
  for (uint8_t i = 0; i < 8U; i++) {
    if (!have_top) {
      top[i] = 127U;
    } else if (by == 0U && base_x + i >= vp8->padded_w) {
      top[i] = top[i == 0U ? 0U : i - 1U];
    } else if (by != 0U && bx == 3U && i >= 4U) {
      top[i] = top[3];
    } else if (by == 0U) {
      top[i] = vp8->above[base_x + i];
    } else {
      top[i] = vp8->mb[(uint16_t)(base_y - 1U) * 16U + bx * 4U + i];
    }
  }
  for (uint8_t i = 0; i < 4U; i++) {
    left[i] =
        have_left
            ? (bx == 0U ? vp8->last_right[base_y + i]
                        : vp8->mb[(uint16_t)(base_y + i) * 16U + bx * 4U - 1U])
            : 129U;
  }
  const uint8_t top_left =
      (have_top && have_left)
          ? (by == 0U
                 ? (bx == 0U ? vp8->top_left : vp8->above[base_x - 1U])
                 : (bx == 0U
                        ? vp8->last_right[base_y - 1U]
                        : vp8->mb[(uint16_t)(base_y - 1U) * 16U + bx * 4U -
                                  1U]))
          : 127U;
  uint16_t sum = 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < 4U; i++) {
    sum += have_top ? top[i] : 0U;
    sum += have_left ? left[i] : 0U;
  }
  count = (uint8_t)(4U * have_top + 4U * have_left);
  const uint8_t dc = count != 0U ? (uint8_t)((sum + (count >> 1)) / count)
                                 : 128U;
  uint8_t *dst = vp8->mb + (uint16_t)base_y * 16U + bx * 4U;
  if (mode == 0U) {
    for (uint8_t y = 0; y < 4U; y++) {
      memset(dst + (uint16_t)y * 16U, dc, 4U);
    }
  } else if (mode == 1U) {
    for (uint8_t y = 0; y < 4U; y++) {
      for (uint8_t x = 0; x < 4U; x++) {
        dst[(uint16_t)y * 16U + x] =
            iwebp_clip_u8((int32_t)left[y] + top[x] - top_left);
      }
    }
  } else if (mode == 2U) {
    const uint8_t v0 = iwebp_avg3(top_left, top[0], top[1]);
    const uint8_t v1 = iwebp_avg3(top[0], top[1], top[2]);
    const uint8_t v2 = iwebp_avg3(top[1], top[2], top[3]);
    const uint8_t v3 = iwebp_avg3(top[2], top[3], top[4]);
    for (uint8_t y = 0; y < 4U; y++) {
      dst[(uint16_t)y * 16U + 0U] = v0;
      dst[(uint16_t)y * 16U + 1U] = v1;
      dst[(uint16_t)y * 16U + 2U] = v2;
      dst[(uint16_t)y * 16U + 3U] = v3;
    }
  } else if (mode == 3U) {
    const uint8_t h0 = iwebp_avg3(top_left, left[0], left[1]);
    const uint8_t h1 = iwebp_avg3(left[0], left[1], left[2]);
    const uint8_t h2 = iwebp_avg3(left[1], left[2], left[3]);
    const uint8_t h3 = iwebp_avg3(left[2], left[3], left[3]);
    memset(dst + 0U * 16U, h0, 4U);
    memset(dst + 1U * 16U, h1, 4U);
    memset(dst + 2U * 16U, h2, 4U);
    memset(dst + 3U * 16U, h3, 4U);
  } else if (mode == 4U) {
    const uint8_t X = top_left, A = top[0], B = top[1], C = top[2], D = top[3];
    const uint8_t I = left[0], J = left[1], K = left[2], L = left[3];
    dst[0U * 16U + 3U] = iwebp_avg3(D, C, B);
    dst[1U * 16U + 3U] = dst[0U * 16U + 2U] = iwebp_avg3(C, B, A);
    dst[2U * 16U + 3U] = dst[1U * 16U + 2U] = dst[0U * 16U + 1U] =
        iwebp_avg3(B, A, X);
    dst[3U * 16U + 3U] = dst[2U * 16U + 2U] = dst[1U * 16U + 1U] =
        dst[0U * 16U + 0U] = iwebp_avg3(A, X, I);
    dst[3U * 16U + 2U] = dst[2U * 16U + 1U] = dst[1U * 16U + 0U] =
        iwebp_avg3(X, I, J);
    dst[3U * 16U + 1U] = dst[2U * 16U + 0U] = iwebp_avg3(I, J, K);
    dst[3U * 16U + 0U] = iwebp_avg3(J, K, L);
  } else if (mode == 5U) {
    const uint8_t X = top_left, A = top[0], B = top[1], C = top[2], D = top[3];
    const uint8_t I = left[0], J = left[1], K = left[2];
    dst[0U * 16U + 0U] = dst[2U * 16U + 1U] = iwebp_avg2(X, A);
    dst[0U * 16U + 1U] = dst[2U * 16U + 2U] = iwebp_avg2(A, B);
    dst[0U * 16U + 2U] = dst[2U * 16U + 3U] = iwebp_avg2(B, C);
    dst[0U * 16U + 3U] = iwebp_avg2(C, D);
    dst[3U * 16U + 0U] = iwebp_avg3(K, J, I);
    dst[2U * 16U + 0U] = iwebp_avg3(J, I, X);
    dst[1U * 16U + 0U] = dst[3U * 16U + 1U] = iwebp_avg3(I, X, A);
    dst[1U * 16U + 1U] = dst[3U * 16U + 2U] = iwebp_avg3(X, A, B);
    dst[1U * 16U + 2U] = dst[3U * 16U + 3U] = iwebp_avg3(A, B, C);
    dst[1U * 16U + 3U] = iwebp_avg3(B, C, D);
  } else if (mode == 6U) {
    const uint8_t A = top[0], B = top[1], C = top[2], D = top[3];
    const uint8_t E = top[4], F = top[5], G = top[6], H = top[7];
    dst[0U * 16U + 0U] = iwebp_avg3(A, B, C);
    dst[0U * 16U + 1U] = dst[1U * 16U + 0U] = iwebp_avg3(B, C, D);
    dst[0U * 16U + 2U] = dst[1U * 16U + 1U] = dst[2U * 16U + 0U] =
        iwebp_avg3(C, D, E);
    dst[0U * 16U + 3U] = dst[1U * 16U + 2U] = dst[2U * 16U + 1U] =
        dst[3U * 16U + 0U] = iwebp_avg3(D, E, F);
    dst[1U * 16U + 3U] = dst[2U * 16U + 2U] = dst[3U * 16U + 1U] =
        iwebp_avg3(E, F, G);
    dst[2U * 16U + 3U] = dst[3U * 16U + 2U] = iwebp_avg3(F, G, H);
    dst[3U * 16U + 3U] = iwebp_avg3(G, H, H);
  } else if (mode == 7U) {
    const uint8_t A = top[0], B = top[1], C = top[2], D = top[3];
    const uint8_t E = top[4], F = top[5], G = top[6], H = top[7];
    dst[0U * 16U + 0U] = iwebp_avg2(A, B);
    dst[0U * 16U + 1U] = dst[2U * 16U + 0U] = iwebp_avg2(B, C);
    dst[0U * 16U + 2U] = dst[2U * 16U + 1U] = iwebp_avg2(C, D);
    dst[0U * 16U + 3U] = dst[2U * 16U + 2U] = iwebp_avg2(D, E);
    dst[1U * 16U + 0U] = iwebp_avg3(A, B, C);
    dst[1U * 16U + 1U] = dst[3U * 16U + 0U] = iwebp_avg3(B, C, D);
    dst[1U * 16U + 2U] = dst[3U * 16U + 1U] = iwebp_avg3(C, D, E);
    dst[1U * 16U + 3U] = dst[3U * 16U + 2U] = iwebp_avg3(D, E, F);
    dst[2U * 16U + 3U] = iwebp_avg3(E, F, G);
    dst[3U * 16U + 3U] = iwebp_avg3(F, G, H);
  } else if (mode == 8U) {
    const uint8_t X = top_left, A = top[0], B = top[1], C = top[2];
    const uint8_t I = left[0], J = left[1], K = left[2], L = left[3];
    dst[0U * 16U + 0U] = dst[1U * 16U + 2U] = iwebp_avg2(I, X);
    dst[1U * 16U + 0U] = dst[2U * 16U + 2U] = iwebp_avg2(J, I);
    dst[2U * 16U + 0U] = dst[3U * 16U + 2U] = iwebp_avg2(K, J);
    dst[3U * 16U + 0U] = iwebp_avg2(L, K);
    dst[0U * 16U + 3U] = iwebp_avg3(A, B, C);
    dst[0U * 16U + 2U] = iwebp_avg3(X, A, B);
    dst[0U * 16U + 1U] = dst[1U * 16U + 3U] = iwebp_avg3(I, X, A);
    dst[1U * 16U + 1U] = dst[2U * 16U + 3U] = iwebp_avg3(J, I, X);
    dst[2U * 16U + 1U] = dst[3U * 16U + 3U] = iwebp_avg3(K, J, I);
    dst[3U * 16U + 1U] = iwebp_avg3(L, K, J);
  } else {
    const uint8_t I = left[0], J = left[1], K = left[2], L = left[3];
    dst[0U * 16U + 0U] = iwebp_avg2(I, J);
    dst[0U * 16U + 2U] = dst[1U * 16U + 0U] = iwebp_avg2(J, K);
    dst[1U * 16U + 2U] = dst[2U * 16U + 0U] = iwebp_avg2(K, L);
    dst[0U * 16U + 1U] = iwebp_avg3(I, J, K);
    dst[0U * 16U + 3U] = dst[1U * 16U + 1U] = iwebp_avg3(J, K, L);
    dst[1U * 16U + 3U] = dst[2U * 16U + 1U] = iwebp_avg3(K, L, L);
    dst[2U * 16U + 3U] = dst[2U * 16U + 2U] = dst[3U * 16U + 0U] =
        dst[3U * 16U + 1U] = dst[3U * 16U + 2U] = dst[3U * 16U + 3U] = L;
  }
}

static uint8_t iwebp_decode_residuals(iwebp_bool *br, iwebp_vp8 *vp8,
                                      uint16_t mbx, int16_t coeffs[16][16]) {
  memset(coeffs, 0, sizeof(int16_t) * 16U * 16U);
  if (vp8->skip_coeff) {
    memset(vp8->left_y_nz, 0, sizeof(vp8->left_y_nz));
    memset(vp8->left_uv_nz, 0, sizeof(vp8->left_uv_nz));
    memset(vp8->top_y_nz + (size_t)mbx * 4U, 0, 4U);
    memset(vp8->top_uv_nz + (size_t)mbx * 8U, 0, 8U);
    if (!vp8->is_i4x4) {
      vp8->left_y2_nz = vp8->top_y2_nz[mbx] = 0;
    }
    return 1;
  }
  const iwebp_quant *q = &vp8->q[vp8->segment_id & 3U];
  uint8_t first = 0;
  uint8_t type = 3;
  if (!vp8->is_i4x4) {
    int16_t y2[16] = {0};
    const uint8_t y2_ctx = (uint8_t)(vp8->top_y2_nz[mbx] + vp8->left_y2_nz);
    uint8_t y2_nz = iwebp_get_coeffs(br, vp8, 1, y2_ctx, q->y2, 0, y2);
    vp8->top_y2_nz[mbx] = vp8->left_y2_nz = (uint8_t)(y2_nz > 0U);
    if (y2_nz > 1U) {
      iwebp_wht(y2, coeffs[0]);
    } else if (y2_nz == 1U) {
      const int16_t dc = (int16_t)((y2[0] + 3) >> 3);
      for (uint8_t i = 0; i < 16U; i++) {
        coeffs[i][0] = dc;
      }
    }
    first = 1;
    type = 0;
  }
  uint8_t *top_y = vp8->top_y_nz + (size_t)mbx * 4U;
  for (uint8_t by = 0; by < 4U; by++) {
    uint8_t left = vp8->left_y_nz[by];
    for (uint8_t bx = 0; bx < 4U; bx++) {
      const uint8_t idx = (uint8_t)(by * 4U + bx);
      const uint8_t ctx = (uint8_t)(left + top_y[bx]);
      const uint8_t nz =
          iwebp_get_coeffs(br, vp8, type, ctx, q->y1, first, coeffs[idx]);
      left = (uint8_t)(nz > first);
      top_y[bx] = left;
    }
    vp8->left_y_nz[by] = left;
  }
  uint8_t *top_uv = vp8->top_uv_nz + (size_t)mbx * 8U;
  int16_t discard[16];
  for (uint8_t ch = 0; ch < 2U; ch++) {
    uint8_t *top_ch = top_uv + (size_t)ch * 4U;
    uint8_t mid[2];
    mid[0] = mid[1] = 0;
    for (uint8_t by = 0; by < 2U; by++) {
      uint8_t left = vp8->left_uv_nz[ch * 2U + by];
      for (uint8_t bx = 0; bx < 2U; bx++) {
        memset(discard, 0, sizeof(discard));
        const uint8_t above = by == 0U ? top_ch[bx] : mid[bx];
        const uint8_t ctx = (uint8_t)(left + above);
        const uint8_t nz = iwebp_get_coeffs(br, vp8, 2, ctx, q->uv, 0, discard);
        left = (uint8_t)(nz > 0U);
        if (by == 0U) {
          mid[bx] = left;
        } else {
          top_ch[bx] = left;
        }
      }
      vp8->left_uv_nz[ch * 2U + by] = left;
    }
  }
  return 1U;
}

static void iwebp_reconstruct_mb(iwebp_vp8 *vp8, uint16_t mbx, uint16_t mby,
                                 int16_t coeffs[16][16]) {
  for (uint8_t by = 0; by < 4U; by++) {
    for (uint8_t bx = 0; bx < 4U; bx++) {
      const uint8_t idx = (uint8_t)(by * 4U + bx);
      if (vp8->is_i4x4) {
        iwebp_predict_4x4(vp8, mbx, mby, bx, by, vp8->bmode[idx]);
      }
      iwebp_idct_add(coeffs[idx],
                     vp8->mb + (uint16_t)by * 4U * 16U + bx * 4U, 16);
    }
  }
}

static void iwebp_emit_mb(iwebp_output *out, iwebp_vp8 *vp8, uint16_t mbx,
                          uint16_t mby) {
  const uint32_t base_x = (uint32_t)mbx * 16U;
  const uint32_t base_y = (uint32_t)mby * 16U;
  for (uint8_t y = 0; y < 16U; y++) {
    const uint32_t py = base_y + y;
    if (py >= out->info.height) {
      break;
    }
    for (uint8_t x = 0; x < 16U; x++) {
      const uint32_t px = base_x + x;
      if (px >= out->info.width) {
        break;
      }
      iwebp_emit_luma(out, px, py, vp8->mb[(uint16_t)y * 16U + x]);
    }
  }
}

static iwebp_result iwebp_decode_vp8(const uint8_t *data, size_t size,
                                     iwebp_output *out) {
  iwebp_vp8 vp8;
  iwebp_bool mode_br;
  iwebp_bool token_br[IWEBP_MAX_TOKEN_PARTITIONS];
  int16_t coeffs[16][16];
  memset(&vp8, 0, sizeof(vp8));
  if (!iwebp_vp8_alloc(&vp8, &out->info)) {
    iwebp_vp8_free(&vp8);
    return IWEBP_ERR_MEMORY;
  }
  iwebp_result result =
      iwebp_parse_vp8_frame_header(data, size, &vp8, &mode_br, token_br);
  if (result != IWEBP_OK) {
    iwebp_vp8_free(&vp8);
    return result;
  }
  for (uint16_t mby = 0; mby < vp8.mb_h; mby++) {
    memset(vp8.left_y_nz, 0, sizeof(vp8.left_y_nz));
    memset(vp8.left_uv_nz, 0, sizeof(vp8.left_uv_nz));
    memset(vp8.left_bmode, 0, sizeof(vp8.left_bmode));
    memset(vp8.last_right, 129, sizeof(vp8.last_right));
    vp8.left_y2_nz = 0;
    vp8.top_left = 127;
    if (out->scaled) {
      iwebp_begin_scale_band(out, (uint32_t)mby * 16U);
    }
    for (uint16_t mbx = 0; mbx < vp8.mb_w; mbx++) {
      iwebp_parse_mb_mode(&mode_br, &vp8, mbx);
      iwebp_predict_mb(&vp8, mbx, mby);
      iwebp_bool *tokens =
          &token_br[mby & (uint16_t)(vp8.token_partitions - 1U)];
      if (!iwebp_decode_residuals(tokens, &vp8, mbx, coeffs)) {
        result = IWEBP_ERR_BAD_BITSTREAM;
        goto done;
      }
      iwebp_reconstruct_mb(&vp8, mbx, mby, coeffs);
      iwebp_emit_mb(out, &vp8, mbx, mby);
      iwebp_update_edges(&vp8, mbx);
      if (mode_br.eof) {
        result = IWEBP_ERR_BAD_BITSTREAM;
        goto done;
      }
    }
  }
  result = IWEBP_OK;
done:
  iwebp_vp8_free(&vp8);
  return result;
}

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} iwebp_lcolor;

typedef struct {
  const uint8_t *pos;
  const uint8_t *end;
  uint32_t bits;
  uint8_t nb_bits;
  uint8_t eof;
} iwebp_lbit;

typedef struct {
  uint16_t count;
  uint16_t *symbol;
  uint16_t *code;
  uint8_t *length;
  uint16_t first[16];
  uint16_t length_count[16];
} iwebp_lhuff;

typedef struct {
  iwebp_lhuff main;
  iwebp_lhuff red;
  iwebp_lhuff blue;
  iwebp_lhuff alpha;
  iwebp_lhuff dist;
} iwebp_lgroup;

typedef struct {
  uint16_t width;
  uint16_t height;
  uint8_t block_bits;
  iwebp_lcolor *pixels;
} iwebp_limage;

typedef struct {
  uint8_t type;
  uint8_t bits;
  uint16_t width;
  uint16_t palette_size;
  iwebp_limage image;
} iwebp_ltransform;

typedef struct {
  uint8_t bits;
  uint16_t count;
  iwebp_lcolor *colors;
} iwebp_lcache;

#define IWEBP_L_COLOR_SYMBOLS 256U
#define IWEBP_L_LENGTH_SYMBOLS 24U
#define IWEBP_L_MAIN_SYMBOLS 280U
#define IWEBP_L_DIST_SYMBOLS 40U
#define IWEBP_L_META_SYMBOLS 19U
#define IWEBP_L_MAX_BITS 15U

static const uint8_t iwebp_lmeta_order[IWEBP_L_META_SYMBOLS] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static const uint8_t iwebp_lcode_to_plane[120] = {
    0x18, 0x07, 0x17, 0x19, 0x28, 0x06, 0x27, 0x29, 0x16, 0x1a,
    0x26, 0x2a, 0x38, 0x05, 0x37, 0x39, 0x15, 0x1b, 0x36, 0x3a,
    0x25, 0x2b, 0x48, 0x04, 0x47, 0x49, 0x14, 0x1c, 0x35, 0x3b,
    0x46, 0x4a, 0x24, 0x2c, 0x58, 0x45, 0x4b, 0x34, 0x3c, 0x03,
    0x57, 0x59, 0x13, 0x1d, 0x56, 0x5a, 0x23, 0x2d, 0x44, 0x4c,
    0x55, 0x5b, 0x33, 0x3d, 0x68, 0x02, 0x67, 0x69, 0x12, 0x1e,
    0x66, 0x6a, 0x22, 0x2e, 0x54, 0x5c, 0x43, 0x4d, 0x65, 0x6b,
    0x32, 0x3e, 0x78, 0x01, 0x77, 0x79, 0x53, 0x5d, 0x11, 0x1f,
    0x64, 0x6c, 0x42, 0x4e, 0x76, 0x7a, 0x21, 0x2f, 0x75, 0x7b,
    0x31, 0x3f, 0x63, 0x6d, 0x52, 0x5e, 0x00, 0x74, 0x7c, 0x41,
    0x4f, 0x10, 0x20, 0x62, 0x6e, 0x30, 0x73, 0x7d, 0x51, 0x5f,
    0x40, 0x72, 0x7e, 0x61, 0x6f, 0x50, 0x71, 0x7f, 0x60, 0x70};

static void iwebp_lbit_init(iwebp_lbit *br, const uint8_t *data, size_t size) {
  br->pos = data;
  br->end = data + size;
  br->bits = 0;
  br->nb_bits = 0;
  br->eof = 0;
}

static bool iwebp_lbit_fill(iwebp_lbit *br, uint8_t n) {
  while (br->nb_bits < n && br->pos < br->end) {
    br->bits |= (uint32_t)(*br->pos++) << br->nb_bits;
    br->nb_bits = (uint8_t)(br->nb_bits + 8U);
  }
  br->eof |= (uint8_t)(br->nb_bits < n);
  return br->nb_bits >= n;
}

static uint32_t iwebp_lbit_read(iwebp_lbit *br, uint8_t n) {
  if (n == 0U) {
    return 0;
  }
  if (!iwebp_lbit_fill(br, n)) {
    return 0;
  }
  const uint32_t mask = (1UL << n) - 1U;
  const uint32_t value = br->bits & mask;
  br->bits >>= n;
  br->nb_bits = (uint8_t)(br->nb_bits - n);
  return value;
}

static uint8_t iwebp_lbit_read1(iwebp_lbit *br) {
  return (uint8_t)iwebp_lbit_read(br, 1);
}

static uint8_t iwebp_lavg(uint8_t a, uint8_t b) {
  return (uint8_t)(((uint16_t)a + b) >> 1);
}

static iwebp_lcolor iwebp_lcolor_add(iwebp_lcolor a, iwebp_lcolor b) {
  iwebp_lcolor out;
  out.r = (uint8_t)(a.r + b.r);
  out.g = (uint8_t)(a.g + b.g);
  out.b = (uint8_t)(a.b + b.b);
  out.a = (uint8_t)(a.a + b.a);
  return out;
}

static iwebp_lcolor iwebp_lcolor_avg(iwebp_lcolor a, iwebp_lcolor b) {
  iwebp_lcolor out;
  out.r = iwebp_lavg(a.r, b.r);
  out.g = iwebp_lavg(a.g, b.g);
  out.b = iwebp_lavg(a.b, b.b);
  out.a = iwebp_lavg(a.a, b.a);
  return out;
}

static uint8_t iwebp_lclamp(int16_t v) {
  return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static iwebp_lcolor iwebp_lcolor_clamped_add(iwebp_lcolor left,
                                             iwebp_lcolor top,
                                             iwebp_lcolor top_left) {
  iwebp_lcolor out;
  out.r = iwebp_lclamp((int16_t)left.r + top.r - top_left.r);
  out.g = iwebp_lclamp((int16_t)left.g + top.g - top_left.g);
  out.b = iwebp_lclamp((int16_t)left.b + top.b - top_left.b);
  out.a = iwebp_lclamp((int16_t)left.a + top.a - top_left.a);
  return out;
}

static void iwebp_lhuff_free(iwebp_lhuff *h) {
  if (h->symbol != NULL) {
    IWEBP_FREE(h->symbol);
  }
  if (h->code != NULL) {
    IWEBP_FREE(h->code);
  }
  if (h->length != NULL) {
    IWEBP_FREE(h->length);
  }
  memset(h, 0, sizeof(*h));
}

static void iwebp_lgroup_free(iwebp_lgroup *g) {
  iwebp_lhuff_free(&g->main);
  iwebp_lhuff_free(&g->red);
  iwebp_lhuff_free(&g->blue);
  iwebp_lhuff_free(&g->alpha);
  iwebp_lhuff_free(&g->dist);
}

static void iwebp_limage_free(iwebp_limage *image) {
  if (image->pixels != NULL) {
    IWEBP_FREE(image->pixels);
  }
  memset(image, 0, sizeof(*image));
}

static void iwebp_lcache_free(iwebp_lcache *cache) {
  if (cache->colors != NULL) {
    IWEBP_FREE(cache->colors);
  }
  memset(cache, 0, sizeof(*cache));
}

static bool iwebp_lincrement_code(uint16_t *code, uint8_t length) {
  uint16_t inc = (uint16_t)(1U << (length - 1U));
  while ((*code & inc) != 0U) {
    inc >>= 1U;
  }
  if (inc == 0U) {
    return false;
  }
  *code = (uint16_t)((*code & (inc - 1U)) + inc);
  return true;
}

static iwebp_result iwebp_lhuff_build(iwebp_lhuff *h, uint16_t nb_lengths,
                                      const uint8_t *lengths) {
  memset(h, 0, sizeof(*h));
  uint16_t count = 0;
  for (uint16_t i = 0; i < nb_lengths; i++) {
    count = (uint16_t)(count + (lengths[i] != 0U));
  }
  if (count == 0U) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  h->symbol = (uint16_t *)IWEBP_MALLOC(count * sizeof(uint16_t));
  h->code = (uint16_t *)IWEBP_MALLOC(count * sizeof(uint16_t));
  h->length = (uint8_t *)IWEBP_MALLOC(count);
  if (h->symbol == NULL || h->code == NULL || h->length == NULL) {
    iwebp_lhuff_free(h);
    return IWEBP_ERR_MEMORY;
  }
  if (count == 1U) {
    for (uint16_t sym = 0; sym < nb_lengths; sym++) {
      if (lengths[sym] != 0U) {
        h->symbol[0] = sym;
        h->code[0] = 0;
        h->length[0] = 0;
        h->count = 1;
        return IWEBP_OK;
      }
    }
  }
  uint16_t code = 0;
  bool valid = true;
  h->count = 0;
  for (uint8_t len = 1; len <= IWEBP_L_MAX_BITS; len++) {
    for (uint16_t sym = 0; sym < nb_lengths; sym++) {
      if (lengths[sym] != len) {
        continue;
      }
      if (!valid) {
        iwebp_lhuff_free(h);
        return IWEBP_ERR_BAD_BITSTREAM;
      }
      h->symbol[h->count] = sym;
      h->code[h->count] = code;
      h->length[h->count] = len;
      if (h->length_count[len] == 0U) {
        h->first[len] = h->count;
      }
      h->length_count[len]++;
      h->count++;
      valid = iwebp_lincrement_code(&code, len);
    }
  }
  return IWEBP_OK;
}

static int32_t iwebp_lhuff_symbol(iwebp_lbit *br, const iwebp_lhuff *h,
                                  iwebp_result *err) {
  if (*err != IWEBP_OK) {
    return 0;
  }
  if (h->count == 1U && h->length[0] == 0U) {
    return h->symbol[0];
  }
  uint16_t code = 0;
  for (uint8_t len = 1; len <= IWEBP_L_MAX_BITS; len++) {
    code = (uint16_t)(code | (uint16_t)(iwebp_lbit_read1(br) << (len - 1U)));
    if (br->eof) {
      *err = IWEBP_ERR_OUT_OF_DATA;
      return 0;
    }
    const uint16_t end = (uint16_t)(h->first[len] + h->length_count[len]);
    for (uint16_t i = h->first[len]; i < end; i++) {
      if (h->code[i] == code) {
        return h->symbol[i];
      }
    }
  }
  *err = IWEBP_ERR_BAD_BITSTREAM;
  return 0;
}

static iwebp_result iwebp_lread_huffman(iwebp_lbit *br, iwebp_lhuff *h,
                                        uint16_t nb_lengths,
                                        uint8_t *lengths) {
  iwebp_result err = IWEBP_OK;
  memset(lengths, 0, nb_lengths);
  if (iwebp_lbit_read1(br)) {
    const uint8_t has_second = iwebp_lbit_read1(br);
    const uint8_t first_bits = iwebp_lbit_read1(br) ? 8U : 1U;
    const uint16_t first = (uint16_t)iwebp_lbit_read(br, first_bits);
    if (br->eof || first >= nb_lengths) {
      return IWEBP_ERR_BAD_BITSTREAM;
    }
    lengths[first] = 1;
    if (has_second) {
      const uint16_t second = (uint16_t)iwebp_lbit_read(br, 8);
      if (br->eof || second >= nb_lengths) {
        return IWEBP_ERR_BAD_BITSTREAM;
      }
      lengths[second] = 1;
    }
  } else {
    uint8_t meta_lengths[IWEBP_L_META_SYMBOLS];
    memset(meta_lengths, 0, sizeof(meta_lengths));
    const uint8_t nb_meta = (uint8_t)(iwebp_lbit_read(br, 4) + 4U);
    for (uint8_t i = 0; i < nb_meta; i++) {
      meta_lengths[iwebp_lmeta_order[i]] = (uint8_t)iwebp_lbit_read(br, 3);
    }
    if (br->eof) {
      return IWEBP_ERR_OUT_OF_DATA;
    }
    iwebp_lhuff meta;
    err = iwebp_lhuff_build(&meta, IWEBP_L_META_SYMBOLS, meta_lengths);
    if (err != IWEBP_OK) {
      return err;
    }
    uint16_t nb_symbols = nb_lengths;
    if (iwebp_lbit_read1(br)) {
      const uint8_t symbols_bits = (uint8_t)(iwebp_lbit_read(br, 3) * 2U + 2U);
      nb_symbols = (uint16_t)(iwebp_lbit_read(br, symbols_bits) + 2U);
      if (nb_symbols > nb_lengths) {
        iwebp_lhuff_free(&meta);
        return IWEBP_ERR_BAD_BITSTREAM;
      }
    }
    uint8_t prev = 8;
    for (uint16_t i = 0; i < nb_lengths && nb_symbols != 0U; nb_symbols--) {
      const int32_t symbol = iwebp_lhuff_symbol(br, &meta, &err);
      uint8_t length = 0;
      uint16_t repeat = 1;
      if (symbol == 16) {
        length = prev;
        repeat = (uint16_t)(iwebp_lbit_read(br, 2) + 3U);
      } else if (symbol == 17) {
        repeat = (uint16_t)(iwebp_lbit_read(br, 3) + 3U);
      } else if (symbol == 18) {
        repeat = (uint16_t)(iwebp_lbit_read(br, 7) + 11U);
      } else if (symbol >= 0 && symbol <= 15) {
        length = (uint8_t)symbol;
        prev = (uint8_t)(symbol != 0 ? symbol : prev);
      } else {
        err = IWEBP_ERR_BAD_BITSTREAM;
      }
      if (err != IWEBP_OK || i + repeat > nb_lengths) {
        err = err == IWEBP_OK ? IWEBP_ERR_BAD_BITSTREAM : err;
        break;
      }
      memset(lengths + i, length, repeat);
      i = (uint16_t)(i + repeat);
    }
    iwebp_lhuff_free(&meta);
    if (err != IWEBP_OK) {
      return err;
    }
  }
  return br->eof ? IWEBP_ERR_OUT_OF_DATA
                 : iwebp_lhuff_build(h, nb_lengths, lengths);
}

static iwebp_result iwebp_lread_group(iwebp_lbit *br, iwebp_lgroup *group,
                                      uint16_t nb_main_symbols,
                                      uint8_t *lengths) {
  memset(group, 0, sizeof(*group));
  iwebp_result err =
      iwebp_lread_huffman(br, &group->main, nb_main_symbols, lengths);
  if (err == IWEBP_OK) {
    err =
        iwebp_lread_huffman(br, &group->red, IWEBP_L_COLOR_SYMBOLS, lengths);
  }
  if (err == IWEBP_OK) {
    err =
        iwebp_lread_huffman(br, &group->blue, IWEBP_L_COLOR_SYMBOLS, lengths);
  }
  if (err == IWEBP_OK) {
    err =
        iwebp_lread_huffman(br, &group->alpha, IWEBP_L_COLOR_SYMBOLS, lengths);
  }
  if (err == IWEBP_OK) {
    err = iwebp_lread_huffman(br, &group->dist, IWEBP_L_DIST_SYMBOLS, lengths);
  }
  if (err != IWEBP_OK) {
    iwebp_lgroup_free(group);
  }
  return err;
}

static iwebp_result iwebp_lread_cache(iwebp_lbit *br, iwebp_lcache *cache) {
  memset(cache, 0, sizeof(*cache));
  if (!iwebp_lbit_read1(br)) {
    return br->eof ? IWEBP_ERR_OUT_OF_DATA : IWEBP_OK;
  }
  cache->bits = (uint8_t)iwebp_lbit_read(br, 4);
  if (br->eof || cache->bits < 1U || cache->bits > 11U) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  const size_t count = (size_t)1U << cache->bits;
  cache->count = (uint16_t)count;
  cache->colors = (iwebp_lcolor *)IWEBP_MALLOC(count * sizeof(iwebp_lcolor));
  if (cache->colors == NULL) {
    return IWEBP_ERR_MEMORY;
  }
  memset(cache->colors, 0, count * sizeof(iwebp_lcolor));
  return IWEBP_OK;
}

static void iwebp_lcache_insert(iwebp_lcache *cache, iwebp_lcolor color) {
  if (cache->bits == 0U || cache->colors == NULL || cache->count == 0U) {
    return;
  }
  const uint32_t argb = ((uint32_t)color.a << 24) |
                        ((uint32_t)color.r << 16) |
                        ((uint32_t)color.g << 8) | color.b;
  const uint32_t key = (0x1e35a7bdUL * argb) >> (32U - cache->bits);
  cache->colors[key & (cache->count - 1U)] = color;
}

static int32_t iwebp_lread_extrabits(iwebp_lbit *br, int32_t symbol) {
  if (symbol < 0 || symbol > 39) {
    br->eof = 1;
    return -1;
  }
  if (symbol < 4) {
    return symbol + 1;
  }
  const uint8_t extra = (uint8_t)(symbol / 2 - 1);
  const int32_t base = (((symbol & 1) + 2) << extra) + 1;
  return base + (int32_t)iwebp_lbit_read(br, extra);
}

static iwebp_result iwebp_lread_subimage(iwebp_lbit *br, iwebp_limage *sub,
                                         uint16_t width, uint16_t height);

static iwebp_result iwebp_ldecode_image(iwebp_lbit *br, iwebp_limage *image,
                                        iwebp_lcache *cache,
                                        const iwebp_limage *huffman_image) {
  uint16_t nb_groups = 1;
  iwebp_result err = IWEBP_OK;
  if (huffman_image != NULL) {
    const size_t meta_pixels = (size_t)huffman_image->width * huffman_image->height;
    for (size_t i = 0; i < meta_pixels; i++) {
      if (huffman_image->pixels[i].r != 0U) {
        return IWEBP_ERR_UNSUPPORTED_CODEC;
      }
      const uint16_t group = (uint16_t)huffman_image->pixels[i].g;
      nb_groups = group >= nb_groups ? (uint16_t)(group + 1U) : nb_groups;
    }
  }
  iwebp_lgroup *groups =
      (iwebp_lgroup *)IWEBP_MALLOC((size_t)nb_groups * sizeof(iwebp_lgroup));
  if (groups == NULL) {
    return IWEBP_ERR_MEMORY;
  }
  memset(groups, 0, (size_t)nb_groups * sizeof(iwebp_lgroup));
  const uint16_t nb_main =
      (uint16_t)(IWEBP_L_MAIN_SYMBOLS + (cache->bits ? (1U << cache->bits) : 0U));
  uint8_t *lengths = (uint8_t *)IWEBP_MALLOC(nb_main);
  if (lengths == NULL) {
    IWEBP_FREE(groups);
    return IWEBP_ERR_MEMORY;
  }
  uint16_t read_groups = 0;
  for (; read_groups < nb_groups; read_groups++) {
    err = iwebp_lread_group(br, &groups[read_groups], nb_main, lengths);
    if (err != IWEBP_OK) {
      break;
    }
  }
  IWEBP_FREE(lengths);
  if (err == IWEBP_OK) {
    const size_t pixels = (size_t)image->width * image->height;
    image->pixels = (iwebp_lcolor *)IWEBP_MALLOC(pixels * sizeof(iwebp_lcolor));
    if (image->pixels == NULL) {
      err = IWEBP_ERR_MEMORY;
    } else {
      iwebp_lcolor *pixel = image->pixels;
      iwebp_lcolor *end = image->pixels + pixels;
      uint16_t x = 0;
      while (pixel < end && err == IWEBP_OK) {
        iwebp_lgroup *group = &groups[0];
        if (huffman_image != NULL) {
          const uint32_t y = (uint32_t)(pixel - image->pixels) / image->width;
          const uint16_t hx = (uint16_t)(x >> huffman_image->block_bits);
          const uint16_t hy = (uint16_t)(y >> huffman_image->block_bits);
          group = &groups[huffman_image->pixels[(size_t)hy * huffman_image->width + hx].g];
        }
        const int32_t main = iwebp_lhuff_symbol(br, &group->main, &err);
        if (main < (int32_t)IWEBP_L_COLOR_SYMBOLS) {
          pixel->g = (uint8_t)main;
          pixel->r = (uint8_t)iwebp_lhuff_symbol(br, &group->red, &err);
          pixel->b = (uint8_t)iwebp_lhuff_symbol(br, &group->blue, &err);
          pixel->a = (uint8_t)iwebp_lhuff_symbol(br, &group->alpha, &err);
          if (err == IWEBP_OK) {
            iwebp_lcache_insert(cache, *pixel);
            pixel++;
            x = (uint16_t)((x + 1U) % image->width);
          }
        } else if (main >= (int32_t)IWEBP_L_MAIN_SYMBOLS) {
          const uint32_t idx = (uint32_t)(main - IWEBP_L_MAIN_SYMBOLS);
          if (cache->colors == NULL || idx >= cache->count) {
            err = IWEBP_ERR_BAD_BITSTREAM;
          } else {
            *pixel++ = cache->colors[idx];
            x = (uint16_t)((x + 1U) % image->width);
          }
        } else {
          const int32_t length =
              iwebp_lread_extrabits(br, main - IWEBP_L_COLOR_SYMBOLS);
          int32_t dist_symbol = iwebp_lhuff_symbol(br, &group->dist, &err);
          int32_t dist = iwebp_lread_extrabits(br, dist_symbol);
          if (br->eof || err != IWEBP_OK) {
            err = err == IWEBP_OK ? IWEBP_ERR_OUT_OF_DATA : err;
          } else if (dist <= 120) {
            const uint8_t plane = iwebp_lcode_to_plane[dist - 1];
            const int32_t xoffset = 8 - (plane & 0x0f);
            const int32_t yoffset = plane >> 4;
            dist = yoffset * image->width + xoffset;
            dist = dist < 1 ? 1 : dist;
          } else {
            dist -= 120;
          }
          if (err == IWEBP_OK) {
            const size_t pos = (size_t)(pixel - image->pixels);
            if (dist <= 0 || length < 0 || (size_t)dist > pos ||
                (size_t)length > (size_t)(end - pixel)) {
              err = IWEBP_ERR_BAD_BITSTREAM;
            } else {
              iwebp_lcolor *repeat = pixel - dist;
              for (int32_t i = 0; i < length; i++) {
                iwebp_lcache_insert(cache, *repeat);
                *pixel++ = *repeat++;
              }
              x = (uint16_t)(((uint32_t)x + (uint32_t)length) % image->width);
            }
          }
        }
      }
    }
  }
  for (uint16_t i = 0; i < read_groups; i++) {
    iwebp_lgroup_free(&groups[i]);
  }
  IWEBP_FREE(groups);
  if (err != IWEBP_OK) {
    iwebp_limage_free(image);
  }
  return err;
}

static iwebp_result iwebp_ldecode_stream(iwebp_lbit *br,
                                         iwebp_limage *image) {
  iwebp_lcache cache;
  iwebp_limage huffman_image;
  iwebp_limage *huff_ptr = NULL;
  memset(&cache, 0, sizeof(cache));
  memset(&huffman_image, 0, sizeof(huffman_image));
  iwebp_result err = iwebp_lread_cache(br, &cache);
  if (err != IWEBP_OK) {
    return err;
  }
  if (iwebp_lbit_read1(br)) {
    err = iwebp_lread_subimage(br, &huffman_image, image->width, image->height);
    if (err == IWEBP_OK) {
      huff_ptr = &huffman_image;
    }
  }
  if (err == IWEBP_OK) {
    err = iwebp_ldecode_image(br, image, &cache, huff_ptr);
  }
  iwebp_limage_free(&huffman_image);
  iwebp_lcache_free(&cache);
  return err;
}

static iwebp_result iwebp_ldecode_entropy_stream(iwebp_lbit *br,
                                                 iwebp_limage *image) {
  iwebp_lcache cache;
  memset(&cache, 0, sizeof(cache));
  iwebp_result err = iwebp_lread_cache(br, &cache);
  if (err == IWEBP_OK) {
    err = iwebp_ldecode_image(br, image, &cache, NULL);
  }
  iwebp_lcache_free(&cache);
  return err;
}

static iwebp_result iwebp_lread_subimage(iwebp_lbit *br, iwebp_limage *sub,
                                         uint16_t width, uint16_t height) {
  memset(sub, 0, sizeof(*sub));
  sub->block_bits = (uint8_t)(iwebp_lbit_read(br, 3) + 2U);
  sub->width = (uint16_t)((width + ((1U << sub->block_bits) - 1U)) >>
                          sub->block_bits);
  sub->height = (uint16_t)((height + ((1U << sub->block_bits) - 1U)) >>
                           sub->block_bits);
  if (br->eof || sub->width == 0U || sub->height == 0U) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  return iwebp_ldecode_entropy_stream(br, sub);
}

static int32_t iwebp_lcolor_distance(iwebp_lcolor a, iwebp_lcolor b) {
  return abs((int)a.r - (int)b.r) + abs((int)a.g - (int)b.g) +
         abs((int)a.b - (int)b.b) + abs((int)a.a - (int)b.a);
}

static iwebp_lcolor iwebp_lpredictor(uint8_t mode, const iwebp_limage *image,
                                     uint32_t x, uint32_t y) {
  static const iwebp_lcolor black = {0, 0, 0, 255};
  const iwebp_lcolor *p = image->pixels;
  const uint32_t w = image->width;
  if (x == 0U && y == 0U) {
    return black;
  }
  if (y == 0U) {
    return p[x - 1U];
  }
  if (x == 0U) {
    return p[(y - 1U) * w];
  }
  const iwebp_lcolor left = p[y * w + x - 1U];
  const iwebp_lcolor top = p[(y - 1U) * w + x];
  const iwebp_lcolor top_left = p[(y - 1U) * w + x - 1U];
  const iwebp_lcolor top_right = x + 1U < w ? p[(y - 1U) * w + x + 1U] : top;
  if (mode == 0U) {
    return black;
  }
  if (mode == 1U) {
    return left;
  }
  if (mode == 2U) {
    return top;
  }
  if (mode == 3U) {
    return top_right;
  }
  if (mode == 4U) {
    return top_left;
  }
  if (mode == 5U) {
    return iwebp_lcolor_avg(iwebp_lcolor_avg(left, top_right), top);
  }
  if (mode == 6U) {
    return iwebp_lcolor_avg(left, top_left);
  }
  if (mode == 7U) {
    return iwebp_lcolor_avg(left, top);
  }
  if (mode == 8U) {
    return iwebp_lcolor_avg(top_left, top);
  }
  if (mode == 9U) {
    return iwebp_lcolor_avg(top, top_right);
  }
  if (mode == 10U) {
    return iwebp_lcolor_avg(iwebp_lcolor_avg(left, top_left),
                            iwebp_lcolor_avg(top, top_right));
  }
  if (mode == 11U) {
    const int32_t ldist = iwebp_lcolor_distance(top_left, top);
    const int32_t tdist = iwebp_lcolor_distance(top_left, left);
    return ldist < tdist ? left : top;
  }
  if (mode == 12U) {
    return iwebp_lcolor_clamped_add(left, top, top_left);
  }
  iwebp_lcolor avg = iwebp_lcolor_avg(left, top);
  iwebp_lcolor out;
  out.r = iwebp_lclamp((int16_t)avg.r + ((int16_t)avg.r - top_left.r) / 2);
  out.g = iwebp_lclamp((int16_t)avg.g + ((int16_t)avg.g - top_left.g) / 2);
  out.b = iwebp_lclamp((int16_t)avg.b + ((int16_t)avg.b - top_left.b) / 2);
  out.a = iwebp_lclamp((int16_t)avg.a + ((int16_t)avg.a - top_left.a) / 2);
  return out;
}

static iwebp_result iwebp_lapply_predict(iwebp_limage *image,
                                         const iwebp_limage *pred) {
  const uint32_t block = 1UL << pred->block_bits;
  for (uint32_t y = 0; y < image->height; y++) {
    const uint32_t py = y >> pred->block_bits;
    for (uint32_t x = 0; x < image->width; x++) {
      const uint32_t px = x >> pred->block_bits;
      const uint8_t mode = (x == 0U && y == 0U) ? 0U
                           : (y == 0U)          ? 1U
                           : (x == 0U)          ? 2U
                                                : pred->pixels[py * pred->width + px].g;
      (void)block;
      if (mode >= 14U) {
        return IWEBP_ERR_BAD_BITSTREAM;
      }
      image->pixels[y * image->width + x] =
          iwebp_lcolor_add(image->pixels[y * image->width + x],
                           iwebp_lpredictor(mode, image, x, y));
    }
  }
  return IWEBP_OK;
}

static void iwebp_lapply_color(iwebp_limage *image, const iwebp_limage *color) {
  for (uint32_t y = 0; y < image->height; y++) {
    const uint32_t cy = y >> color->block_bits;
    for (uint32_t x = 0; x < image->width; x++) {
      const uint32_t cx = x >> color->block_bits;
      const iwebp_lcolor m = color->pixels[cy * color->width + cx];
      iwebp_lcolor *p = &image->pixels[y * image->width + x];
      p->r = (uint8_t)(p->r + (((int8_t)m.b * (int8_t)p->g) >> 5));
      p->b = (uint8_t)(p->b + (((int8_t)m.g * (int8_t)p->g) >> 5));
      p->b = (uint8_t)(p->b + (((int8_t)m.r * (int8_t)p->r) >> 5));
    }
  }
}

static void iwebp_lapply_green(iwebp_limage *image) {
  const size_t pixels = (size_t)image->width * image->height;
  for (size_t i = 0; i < pixels; i++) {
    image->pixels[i].r = (uint8_t)(image->pixels[i].r + image->pixels[i].g);
    image->pixels[i].b = (uint8_t)(image->pixels[i].b + image->pixels[i].g);
  }
}

static uint8_t iwebp_lpalette_bits(uint16_t palette_size) {
  return palette_size <= 2U  ? 3U
         : palette_size <= 4U  ? 2U
         : palette_size <= 16U ? 1U
                               : 0U;
}

static iwebp_result iwebp_lread_palette(iwebp_lbit *br,
                                        iwebp_ltransform *transform,
                                        uint16_t width) {
  transform->palette_size = (uint16_t)(iwebp_lbit_read(br, 8) + 1U);
  transform->bits = iwebp_lpalette_bits(transform->palette_size);
  transform->width = width;
  if (br->eof) {
    return IWEBP_ERR_OUT_OF_DATA;
  }
  transform->image.width = transform->palette_size;
  transform->image.height = 1;
  iwebp_result err = iwebp_ldecode_entropy_stream(br, &transform->image);
  if (err != IWEBP_OK) {
    return err;
  }
  iwebp_lcolor prev = {0, 0, 0, 0};
  for (uint16_t i = 0; i < transform->palette_size; i++) {
    transform->image.pixels[i] =
        iwebp_lcolor_add(transform->image.pixels[i], prev);
    prev = transform->image.pixels[i];
  }
  return IWEBP_OK;
}

static iwebp_result iwebp_lapply_palette(iwebp_limage *image,
                                         const iwebp_ltransform *transform) {
  if (transform->image.pixels == NULL || transform->palette_size == 0U ||
      transform->width == 0U || image->height == 0U) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  static const iwebp_lcolor transparent = {0, 0, 0, 0};
  const uint16_t out_w = transform->width;
  const uint8_t bits = transform->bits;
  const uint16_t packed_w =
      bits != 0U ? (uint16_t)((out_w + ((1U << bits) - 1U)) >> bits) : out_w;
  if (image->width != packed_w) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  if (bits == 0U) {
    const size_t pixels = (size_t)image->width * image->height;
    for (size_t i = 0; i < pixels; i++) {
      const uint8_t idx = image->pixels[i].g;
      image->pixels[i] = idx < transform->palette_size
                             ? transform->image.pixels[idx]
                             : transparent;
    }
    return IWEBP_OK;
  }

  const size_t pixels = (size_t)out_w * image->height;
  iwebp_lcolor *expanded =
      (iwebp_lcolor *)IWEBP_MALLOC(pixels * sizeof(iwebp_lcolor));
  if (expanded == NULL) {
    return IWEBP_ERR_MEMORY;
  }
  const uint8_t mask_bits = (uint8_t)(8U >> bits);
  const uint8_t mask = (uint8_t)((1U << mask_bits) - 1U);
  const uint8_t pack_mask = (uint8_t)((1U << bits) - 1U);
  for (uint16_t y = 0; y < image->height; y++) {
    const iwebp_lcolor *src = image->pixels + (size_t)y * image->width;
    iwebp_lcolor *dst = expanded + (size_t)y * out_w;
    for (uint16_t x = 0; x < out_w; x++) {
      const uint8_t packed = src[x >> bits].g;
      const uint8_t idx =
          (uint8_t)((packed >> ((x & pack_mask) * mask_bits)) & mask);
      dst[x] = idx < transform->palette_size ? transform->image.pixels[idx]
                                             : transparent;
    }
  }
  IWEBP_FREE(image->pixels);
  image->pixels = expanded;
  image->width = out_w;
  return IWEBP_OK;
}

static iwebp_result iwebp_decode_vp8l_image(iwebp_lbit *br,
                                            iwebp_limage *image) {
  iwebp_ltransform transforms[4];
  uint8_t nb_transforms = 0;
  uint8_t transform_mask = 0;
  uint16_t coded_width = image->width;
  memset(transforms, 0, sizeof(transforms));
  iwebp_result err = IWEBP_OK;
  while (iwebp_lbit_read1(br)) {
    if (nb_transforms >= 4U) {
      err = IWEBP_ERR_BAD_BITSTREAM;
      goto done;
    }
    transforms[nb_transforms].type = (uint8_t)iwebp_lbit_read(br, 2);
    if ((transform_mask & (1U << transforms[nb_transforms].type)) != 0U) {
      err = IWEBP_ERR_BAD_BITSTREAM;
      goto done;
    }
    transform_mask = (uint8_t)(transform_mask |
                               (uint8_t)(1U << transforms[nb_transforms].type));
    if (transforms[nb_transforms].type == 3U) {
      err = iwebp_lread_palette(br, &transforms[nb_transforms], coded_width);
      if (err != IWEBP_OK) {
        goto done;
      }
      if (transforms[nb_transforms].bits != 0U) {
        coded_width = (uint16_t)(
            (coded_width + ((1U << transforms[nb_transforms].bits) - 1U)) >>
            transforms[nb_transforms].bits);
      }
    } else if (transforms[nb_transforms].type != 2U) {
      err = iwebp_lread_subimage(br, &transforms[nb_transforms].image,
                                 coded_width, image->height);
      if (err != IWEBP_OK) {
        goto done;
      }
    }
    nb_transforms++;
  }
  if (br->eof) {
    err = IWEBP_ERR_OUT_OF_DATA;
    goto done;
  }
  image->width = coded_width;
  err = iwebp_ldecode_stream(br, image);
  if (err != IWEBP_OK) {
    goto done;
  }
  for (int8_t i = (int8_t)nb_transforms - 1; i >= 0; i--) {
    if (transforms[i].type == 0U) {
      err = iwebp_lapply_predict(image, &transforms[i].image);
    } else if (transforms[i].type == 1U) {
      iwebp_lapply_color(image, &transforms[i].image);
    } else if (transforms[i].type == 2U) {
      iwebp_lapply_green(image);
    } else {
      err = iwebp_lapply_palette(image, &transforms[i]);
    }
    if (err != IWEBP_OK) {
      break;
    }
  }
done:
  for (uint8_t i = 0; i < nb_transforms; i++) {
    iwebp_limage_free(&transforms[i].image);
  }
  return err;
}

static uint32_t iwebp_vp8l_pixel_cap(const iwebp_output *out) {
  return out != NULL && out->render != NULL && out->render->max_vp8l_pixels != 0U
             ? out->render->max_vp8l_pixels
             : (uint32_t)IWEBP_MAX_VP8L_PIXELS;
}

static iwebp_result iwebp_decode_alpha(const uint8_t *data, size_t size,
                                       iwebp_output *out) {
  if (data == NULL || out == NULL || size < 2U) {
    return IWEBP_ERR_OUT_OF_DATA;
  }
  const uint8_t header = data[0];
  const uint8_t method = header & 0x03U;
  const uint8_t filter = (header >> 2) & 0x03U;
  if (method > 1U || ((header >> 6) & 0x03U) != 0U) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  const uint32_t pixels = (uint32_t)out->info.width * out->info.height;
  if (pixels == 0U || pixels > iwebp_vp8l_pixel_cap(out)) {
    return IWEBP_ERR_SCRATCH_TOO_SMALL;
  }
  out->alpha = (uint8_t *)IWEBP_MALLOC(pixels);
  if (out->alpha == NULL) {
    return IWEBP_ERR_MEMORY;
  }
  if (method == 0U) {
    if (size - 1U < pixels) {
      return IWEBP_ERR_OUT_OF_DATA;
    }
    const uint8_t *src = data + 1U;
    for (uint32_t y = 0; y < out->info.height; y++) {
      for (uint32_t x = 0; x < out->info.width; x++) {
        const size_t i = (size_t)y * out->info.width + x;
        const uint8_t left = x != 0U ? out->alpha[i - 1U] : 0U;
        const uint8_t top = y != 0U ? out->alpha[i - out->info.width] : 0U;
        const uint8_t top_left =
            (x != 0U && y != 0U) ? out->alpha[i - out->info.width - 1U] : 0U;
        uint8_t pred = 0;
        if (filter == 1U) {
          pred = left;
        } else if (filter == 2U) {
          pred = top;
        } else if (filter == 3U) {
          pred = iwebp_clip_u8((int32_t)left + top - top_left);
        }
        out->alpha[i] = (uint8_t)(src[i] + pred);
      }
    }
    return IWEBP_OK;
  }

  iwebp_limage image;
  memset(&image, 0, sizeof(image));
  image.width = out->info.width;
  image.height = out->info.height;
  iwebp_lbit br;
  iwebp_lbit_init(&br, data + 1U, size - 1U);
  const iwebp_result err = iwebp_decode_vp8l_image(&br, &image);
  if (err != IWEBP_OK) {
    iwebp_limage_free(&image);
    return err;
  }
  for (uint32_t i = 0; i < pixels; i++) {
    out->alpha[i] = image.pixels[i].g;
  }
  iwebp_limage_free(&image);
  return IWEBP_OK;
}

static iwebp_result iwebp_decode_vp8l(const uint8_t *data, size_t size,
                                      iwebp_output *out) {
  if (size < 5U || data[0] != 0x2f) {
    return IWEBP_ERR_BAD_BITSTREAM;
  }
  iwebp_limage image;
  memset(&image, 0, sizeof(image));
  image.width = out->info.width;
  image.height = out->info.height;
  if ((uint32_t)image.width * image.height > iwebp_vp8l_pixel_cap(out)) {
    return IWEBP_ERR_SCRATCH_TOO_SMALL;
  }
  iwebp_lbit br;
  iwebp_lbit_init(&br, data + 5U, size - 5U);
  iwebp_result err = iwebp_decode_vp8l_image(&br, &image);
  if (err == IWEBP_OK) {
    for (uint32_t y = 0; y < image.height; y++) {
      for (uint32_t x = 0; x < image.width; x++) {
        const iwebp_lcolor p = image.pixels[y * image.width + x];
        const uint8_t gray =
            p.a < 128U ? (out->transparent_black ? 0U : 255U)
                      : (uint8_t)(((uint16_t)p.r * 77U +
                                   (uint16_t)p.g * 150U +
                                   (uint16_t)p.b * 29U) >>
                                  8);
        iwebp_emit_luma(out, x, y, gray);
      }
    }
  }
  iwebp_limage_free(&image);
  return err;
}

static inline iwebp_result iwebp_read_info(const uint8_t *data, size_t size,
                                           iwebp_info *info) {
  iwebp_parse parse;
  const iwebp_result r = iwebp_parse_container(data, size, &parse);
  if (r != IWEBP_OK) {
    return r;
  }
  if (info != NULL) {
    *info = parse.info;
  }
  return IWEBP_OK;
}

static inline iwebp_result iwebp_decode(const uint8_t *data, size_t size,
                                        const iwebp_render *render,
                                        iwebp_info *out_info) {
  iwebp_parse parse;
  iwebp_output out;
  const iwebp_result parsed = iwebp_parse_container(data, size, &parse);
  if (parsed != IWEBP_OK) {
    return parsed;
  }
  if (out_info != NULL) {
    *out_info = parse.info;
  }
  if (!iwebp_setup_output(&out, &parse.info, render)) {
    iwebp_free_output(&out);
    return IWEBP_ERR_MEMORY;
  }
  if (parse.has_alpha_chunk) {
    const iwebp_result alpha = iwebp_decode_alpha(
        data + parse.alpha_offset, parse.alpha_size, &out);
    if (alpha != IWEBP_OK) {
      iwebp_free_output(&out);
      return alpha;
    }
  }
  const uint8_t *payload = data + parse.payload_offset;
  const iwebp_result decoded =
      parse.info.codec == (uint8_t)IWEBP_CODEC_VP8
          ? iwebp_decode_vp8(payload, parse.payload_size, &out)
          : iwebp_decode_vp8l(payload, parse.payload_size, &out);
  iwebp_free_output(&out);
  return decoded;
}

#endif
