/*
 * InkSVG - header only SVG Parser & 1-bit Rasterizer for microcontrollers with E-Ink screens
 * (c) Remixer Dec 2026 | CC BY-NC-SA 3.0
 * Distributed as a part of PocketInkOS https://github.com/remixer-dec/PocketInkOS
 */

#ifndef INKSVG_H
#define INKSVG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// Override these macros to use PSRAM or custom allocators
#ifndef ISVG_MALLOC
#define ISVG_MALLOC  malloc
#define ISVG_REALLOC realloc
#define ISVG_FREE    free
#endif

typedef struct {
    uint8_t* cmds;
    float*   pts;
    int      n_cmds, cap_cmds;
    int      n_pts, cap_pts;
    bool     failed;
} isvg_tape;

// Initialize and Free the tape
void isvg_init(isvg_tape* tape);
void isvg_free(isvg_tape* tape);

// Creates the tape from an SVG string (mutates the string!)
// Returns true on success, false on memory failure or invalid SVG.
bool isvg_parse(isvg_tape* tape, char* svg_string);

// Rasterizes the tape into a 1-bit buffer (1 byte = 8 pixels, MSB first)
// Stride is assumed to be (target_w + 7) / 8.
void isvg_rasterize(isvg_tape* tape, uint8_t* buffer, int target_w, int target_h);

#ifdef __cplusplus
}
#endif

#endif // INKSVG_H

#ifdef INKSVG_IMPLEMENTATION

#include <string.h>
#include <math.h>
#include <limits.h>

enum { CMD_MOVE, CMD_LINE, CMD_CUBIC, CMD_CLOSE, CMD_STYLE };
enum { ISVG_PAINT_NONE, ISVG_PAINT_BLACK, ISVG_PAINT_WHITE };
enum { ISVG_FILL_NONZERO, ISVG_FILL_EVENODD };
enum {
    ISVG_TAG_OTHER,
    ISVG_TAG_SVG,
    ISVG_TAG_G,
    ISVG_TAG_DEFS,
    ISVG_TAG_METADATA,
    ISVG_TAG_NAMEDVIEW,
    ISVG_TAG_CLIPPATH,
    ISVG_TAG_MASK,
    ISVG_TAG_PATTERN,
    ISVG_TAG_STYLE,
    ISVG_TAG_SYMBOL,
    ISVG_TAG_PATH,
    ISVG_TAG_POLYGON,
    ISVG_TAG_POLYLINE,
    ISVG_TAG_LINE,
    ISVG_TAG_RECT,
    ISVG_TAG_CIRCLE,
    ISVG_TAG_ELLIPSE
};
enum {
    ISVG_ATTR_OTHER,
    ISVG_ATTR_FILL,
    ISVG_ATTR_STROKE,
    ISVG_ATTR_STROKE_WIDTH,
    ISVG_ATTR_FILL_RULE,
    ISVG_ATTR_STYLE,
    ISVG_ATTR_D,
    ISVG_ATTR_POINTS,
    ISVG_ATTR_X,
    ISVG_ATTR_Y,
    ISVG_ATTR_WIDTH,
    ISVG_ATTR_HEIGHT,
    ISVG_ATTR_CX,
    ISVG_ATTR_CY,
    ISVG_ATTR_R,
    ISVG_ATTR_RX,
    ISVG_ATTR_RY,
    ISVG_ATTR_X1,
    ISVG_ATTR_Y1,
    ISVG_ATTR_X2,
    ISVG_ATTR_Y2,
    ISVG_ATTR_TRANSFORM
};

typedef struct { float m[6]; } isvg_xform;

// BRANCHLESS MATH & OPTIMIZATIONS

// Converts boolean conditionals into arithmetic to avoid pipeline branching.
static inline float isvg_fmin(float a, float b) {
    int is_less = (a < b);
    return a * is_less + b * (!is_less);
}

static inline float isvg_fmax(float a, float b) {
    int is_greater = (a > b);
    return a * is_greater + b * (!is_greater);
}

static inline int isvg_imin(int a, int b) {
    int is_less = (a < b);
    return a * is_less + b * (!is_less);
}

static inline int isvg_imax(int a, int b) {
    int is_greater = (a > b);
    return a * is_greater + b * (!is_greater);
}

// DYNAMIC TAPE ALLOCATION

void isvg_init(isvg_tape* tape) {
    if (!tape) return;
    tape->n_cmds = tape->cap_cmds = 0;
    tape->n_pts = tape->cap_pts = 0;
    tape->cmds = NULL;
    tape->pts = NULL;
    tape->failed = false;
}

void isvg_free(isvg_tape* tape) {
    if (!tape) return;
    if (tape->cmds) ISVG_FREE(tape->cmds);
    if (tape->pts) ISVG_FREE(tape->pts);
    isvg_init(tape);
}

static bool isvg_next_cap(int current, int needed, int min_cap, int* out) {
    if (!out || current < 0 || needed < 0 || min_cap <= 0) return false;
    int next = current > min_cap ? current : min_cap;
    while (next < needed) {
        if (next > INT_MAX / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    *out = next;
    return true;
}

static bool isvg_ensure_cmd(isvg_tape* t, int count) {
    if (!t || count < 0 || t->n_cmds > INT_MAX - count) {
        if (t) t->failed = true;
        return false;
    }
    int needed = t->n_cmds + count;
    if (needed > t->cap_cmds) {
        int next_cap = 0;
        if (!isvg_next_cap(t->cap_cmds, needed, 1024, &next_cap)) {
            t->failed = true;
            return false;
        }
        void* ptr = ISVG_REALLOC(t->cmds, (size_t)next_cap);
        if (!ptr) {
            t->failed = true;
            return false;
        }
        t->cmds = (uint8_t*)ptr;
        t->cap_cmds = next_cap;
    }
    return true;
}

static bool isvg_ensure_pt(isvg_tape* t, int count) {
    if (!t || count < 0 || t->n_pts > INT_MAX - count) {
        if (t) t->failed = true;
        return false;
    }
    int needed = t->n_pts + count;
    if (needed > t->cap_pts) {
        int next_cap = 0;
        if (!isvg_next_cap(t->cap_pts, needed, 1024, &next_cap) ||
            (size_t)next_cap > ((size_t)-1) / sizeof(float)) {
            t->failed = true;
            return false;
        }
        void* ptr = ISVG_REALLOC(t->pts, (size_t)next_cap * sizeof(float));
        if (!ptr) {
            t->failed = true;
            return false;
        }
        t->pts = (float*)ptr;
        t->cap_pts = next_cap;
    }
    return true;
}

static void isvg_add_pt(isvg_tape* t, float x, float y, const float* xf) {
    if (!t || !xf || !isvg_ensure_pt(t, 2)) {
        if (t) t->failed = true;
        return;
    }
    t->pts[t->n_pts++] = x * xf[0] + y * xf[2] + xf[4];
    t->pts[t->n_pts++] = x * xf[1] + y * xf[3] + xf[5];
}

static void isvg_add_cmd(isvg_tape* t, uint8_t cmd) {
    if (isvg_ensure_cmd(t, 1)) t->cmds[t->n_cmds++] = cmd;
}

// FAST STRING PARSERS

static int isvg_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int isvg_is_sep(char c) {
    return isvg_is_space(c) || c == ',';
}

static void isvg_skip_sep(const char** s) {
    while (**s && isvg_is_sep(**s)) (*s)++;
}

static bool isvg_parse_number(const char** s, float* out) {
    isvg_skip_sep(s);
    if (!**s) return false;
    char* end = NULL;
    float value = strtof(*s, &end);
    if (end == *s) return false;
    *out = value;
    *s = end;
    return true;
}

static float isvg_atof(const char** s) {
    float value = 0.0f;
    (void)isvg_parse_number(s, &value);
    return value;
}

static int isvg_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// B/W Heuristic: Dark colors evaluate to 1 (Black), light colors to 0 (White/None)
static uint8_t isvg_parse_paint(const char* s) {
    while (s && isvg_is_space(*s)) s++;
    if (!s || s[0] == 'n') return ISVG_PAINT_NONE; // "none"
    if (s[0] == '#') {
        int digits[6];
        int n = 0;
        const char* h = s + 1;
        while (n < 6) {
            int d = isvg_hex_digit(h[n]);
            if (d < 0) break;
            digits[n++] = d;
        }
        int r = 0, g = 0, b = 0;
        if (n == 3) {
            r = digits[0] * 17; g = digits[1] * 17; b = digits[2] * 17;
        } else if (n >= 6) {
            r = (digits[0] << 4) | digits[1];
            g = (digits[2] << 4) | digits[3];
            b = (digits[4] << 4) | digits[5];
        } else {
            return ISVG_PAINT_BLACK;
        }
        return ((r + g + b) < 384) ? ISVG_PAINT_BLACK : ISVG_PAINT_WHITE;
    }
    if (strncmp(s, "white", 5) == 0 || strncmp(s, "yellow", 6) == 0) {
        return ISVG_PAINT_WHITE;
    }
    return ISVG_PAINT_BLACK; // Default fallback to foreground
}

static uint8_t isvg_parse_fill_rule(const char* s) {
    while (s && isvg_is_space(*s)) s++;
    return (s && strncmp(s, "evenodd", 7) == 0) ? ISVG_FILL_EVENODD : ISVG_FILL_NONZERO;
}

static uint8_t isvg_quantize_stroke_width(float width) {
    if (width <= 0.0f) return 0;
    if (width >= 255.0f) return 255;
    return (uint8_t)(width + 0.5f);
}

static const char* isvg_local_name(const char* name) {
    const char* local = name;
    if (!name) return "";
    while (*name) {
        if (*name == ':') local = name + 1;
        name++;
    }
    return local;
}

static int isvg_tag_kind(const char* tag) {
    const char* name = isvg_local_name(tag);
    switch (name[0]) {
    case 'c':
        if (strcmp(name, "clipPath") == 0) return ISVG_TAG_CLIPPATH;
        if (strcmp(name, "circle") == 0) return ISVG_TAG_CIRCLE;
        break;
    case 'd':
        if (strcmp(name, "defs") == 0) return ISVG_TAG_DEFS;
        break;
    case 'e':
        if (strcmp(name, "ellipse") == 0) return ISVG_TAG_ELLIPSE;
        break;
    case 'g':
        if (name[1] == '\0') return ISVG_TAG_G;
        break;
    case 'l':
        if (strcmp(name, "line") == 0) return ISVG_TAG_LINE;
        break;
    case 'm':
        if (strcmp(name, "metadata") == 0) return ISVG_TAG_METADATA;
        if (strcmp(name, "mask") == 0) return ISVG_TAG_MASK;
        break;
    case 'n':
        if (strcmp(name, "namedview") == 0) return ISVG_TAG_NAMEDVIEW;
        break;
    case 'p':
        if (strcmp(name, "path") == 0) return ISVG_TAG_PATH;
        if (strcmp(name, "polygon") == 0) return ISVG_TAG_POLYGON;
        if (strcmp(name, "polyline") == 0) return ISVG_TAG_POLYLINE;
        if (strcmp(name, "pattern") == 0) return ISVG_TAG_PATTERN;
        break;
    case 'r':
        if (strcmp(name, "rect") == 0) return ISVG_TAG_RECT;
        break;
    case 's':
        if (strcmp(name, "svg") == 0) return ISVG_TAG_SVG;
        if (strcmp(name, "style") == 0) return ISVG_TAG_STYLE;
        if (strcmp(name, "symbol") == 0) return ISVG_TAG_SYMBOL;
        break;
    }
    return ISVG_TAG_OTHER;
}

static int isvg_tag_skipped_container(int kind) {
    return kind >= ISVG_TAG_DEFS && kind <= ISVG_TAG_SYMBOL;
}

static int isvg_tag_drawable(int kind) {
    return kind >= ISVG_TAG_PATH && kind <= ISVG_TAG_ELLIPSE;
}

static int isvg_attr_kind(const char* attr) {
    const char* name = isvg_local_name(attr);
    switch (name[0]) {
    case 'c':
        if (strcmp(name, "cx") == 0) return ISVG_ATTR_CX;
        if (strcmp(name, "cy") == 0) return ISVG_ATTR_CY;
        break;
    case 'd':
        if (name[1] == '\0') return ISVG_ATTR_D;
        break;
    case 'f':
        if (strcmp(name, "fill") == 0) return ISVG_ATTR_FILL;
        if (strcmp(name, "fill-rule") == 0) return ISVG_ATTR_FILL_RULE;
        break;
    case 'h':
        if (strcmp(name, "height") == 0) return ISVG_ATTR_HEIGHT;
        break;
    case 'p':
        if (strcmp(name, "points") == 0) return ISVG_ATTR_POINTS;
        break;
    case 'r':
        if (name[1] == '\0') return ISVG_ATTR_R;
        if (strcmp(name, "rx") == 0) return ISVG_ATTR_RX;
        if (strcmp(name, "ry") == 0) return ISVG_ATTR_RY;
        break;
    case 's':
        if (strcmp(name, "style") == 0) return ISVG_ATTR_STYLE;
        if (strcmp(name, "stroke") == 0) return ISVG_ATTR_STROKE;
        if (strcmp(name, "stroke-width") == 0) return ISVG_ATTR_STROKE_WIDTH;
        break;
    case 't':
        if (strcmp(name, "transform") == 0) return ISVG_ATTR_TRANSFORM;
        break;
    case 'w':
        if (strcmp(name, "width") == 0) return ISVG_ATTR_WIDTH;
        break;
    case 'x':
        if (name[1] == '\0') return ISVG_ATTR_X;
        if (strcmp(name, "x1") == 0) return ISVG_ATTR_X1;
        if (strcmp(name, "x2") == 0) return ISVG_ATTR_X2;
        break;
    case 'y':
        if (name[1] == '\0') return ISVG_ATTR_Y;
        if (strcmp(name, "y1") == 0) return ISVG_ATTR_Y1;
        if (strcmp(name, "y2") == 0) return ISVG_ATTR_Y2;
        break;
    }
    return ISVG_ATTR_OTHER;
}

static bool isvg_style_name_eq(const char* start, const char* end, const char* expected) {
    while (start < end && isvg_is_space(*start)) start++;
    while (end > start && isvg_is_space(*(end - 1))) end--;
    for (const char* p = start; p < end; p++) {
        if (*p == ':') start = p + 1;
    }
    size_t len = strlen(expected);
    return (size_t)(end - start) == len && strncmp(start, expected, len) == 0;
}

static void isvg_apply_style(const char* style, uint8_t* fill, uint8_t* stroke,
                             uint8_t* stroke_width, uint8_t* fill_rule) {
    if (!style) return;
    const char* p = style;
    while (*p) {
        while (*p == ';' || isvg_is_space(*p)) p++;
        const char* name = p;
        while (*p && *p != ':' && *p != ';') p++;
        const char* name_end = p;
        if (*p != ':') {
            while (*p && *p != ';') p++;
            continue;
        }
        p++;
        while (isvg_is_space(*p)) p++;
        const char* value = p;
        while (*p && *p != ';') p++;
        char saved = *p;
        char* value_end = (char*)p;
        while (value_end > value && isvg_is_space(*(value_end - 1))) value_end--;
        char saved_end = *value_end;
        *value_end = '\0';
        if (isvg_style_name_eq(name, name_end, "fill")) {
            *fill = isvg_parse_paint(value);
        } else if (isvg_style_name_eq(name, name_end, "stroke")) {
            *stroke = isvg_parse_paint(value);
        } else if (isvg_style_name_eq(name, name_end, "stroke-width")) {
            const char* width_text = value;
            *stroke_width = isvg_quantize_stroke_width(isvg_atof(&width_text));
        } else if (isvg_style_name_eq(name, name_end, "fill-rule")) {
            *fill_rule = isvg_parse_fill_rule(value);
        }
        *value_end = saved_end;
        if (saved == ';') p++;
    }
}

static void isvg_xform_mul(float* res, const float* a, const float* b) {
    float t[6];
    t[0] = b[0] * a[0] + b[1] * a[2];
    t[1] = b[0] * a[1] + b[1] * a[3];
    t[2] = b[2] * a[0] + b[3] * a[2];
    t[3] = b[2] * a[1] + b[3] * a[3];
    t[4] = b[4] * a[0] + b[5] * a[2] + a[4];
    t[5] = b[4] * a[1] + b[5] * a[3] + a[5];
    memcpy(res, t, sizeof(t));
}

static bool isvg_enter_transform_args(const char** v, int keyword_len) {
    const char* p = *v + keyword_len;
    while (isvg_is_space(*p)) p++;
    if (*p != '(') {
        *v += keyword_len;
        return false;
    }
    *v = p + 1;
    return true;
}

static void isvg_add_line_to(isvg_tape* tape, float x, float y, const float* xf) {
    isvg_add_cmd(tape, CMD_LINE);
    isvg_add_pt(tape, x, y, xf);
}

static void isvg_add_cubic_to(isvg_tape* tape, float x1, float y1, float x2, float y2,
                              float x, float y, const float* xf) {
    isvg_add_cmd(tape, CMD_CUBIC);
    isvg_add_pt(tape, x1, y1, xf);
    isvg_add_pt(tape, x2, y2, xf);
    isvg_add_pt(tape, x, y, xf);
}

static void isvg_add_arc_to(isvg_tape* tape, float x0, float y0, float rx, float ry,
                            float angle_deg, int large_arc, int sweep,
                            float x1, float y1, const float* xf) {
    if (rx <= 0.0f || ry <= 0.0f || (fabsf(x0 - x1) < 1e-5f && fabsf(y0 - y1) < 1e-5f)) {
        isvg_add_line_to(tape, x1, y1, xf);
        return;
    }

    rx = fabsf(rx);
    ry = fabsf(ry);
    const float pi = 3.1415927f;
    float phi = angle_deg * (pi / 180.0f);
    float cos_phi = cosf(phi);
    float sin_phi = sinf(phi);
    float dx = (x0 - x1) * 0.5f;
    float dy = (y0 - y1) * 0.5f;
    float x1p = cos_phi * dx + sin_phi * dy;
    float y1p = -sin_phi * dx + cos_phi * dy;
    float rx2 = rx * rx;
    float ry2 = ry * ry;
    float x1p2 = x1p * x1p;
    float y1p2 = y1p * y1p;
    float radii = x1p2 / rx2 + y1p2 / ry2;
    if (radii > 1.0f) {
        float scale = sqrtf(radii);
        rx *= scale;
        ry *= scale;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    float denom = rx2 * y1p2 + ry2 * x1p2;
    if (denom <= 1e-6f) {
        isvg_add_line_to(tape, x1, y1, xf);
        return;
    }
    float coef_sq = (rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2) / denom;
    if (coef_sq < 0.0f) coef_sq = 0.0f;
    float coef = ((large_arc != sweep) ? 1.0f : -1.0f) * sqrtf(coef_sq);
    float cxp = coef * ((rx * y1p) / ry);
    float cyp = coef * (-(ry * x1p) / rx);
    float cx = cos_phi * cxp - sin_phi * cyp + (x0 + x1) * 0.5f;
    float cy = sin_phi * cxp + cos_phi * cyp + (y0 + y1) * 0.5f;

    float ux = (x1p - cxp) / rx;
    float uy = (y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;
    float start = atan2f(uy, ux);
    float delta = atan2f(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && delta > 0.0f) delta -= 2.0f * pi;
    if (sweep && delta < 0.0f) delta += 2.0f * pi;

    int steps = isvg_imin(32, isvg_imax(4, (int)ceilf(fabsf(delta) / (pi / 8.0f))));
    for (int i = 1; i <= steps; i++) {
        float a = start + delta * ((float)i / (float)steps);
        float px = cx + cos_phi * rx * cosf(a) - sin_phi * ry * sinf(a);
        float py = cy + sin_phi * rx * cosf(a) + cos_phi * ry * sinf(a);
        isvg_add_line_to(tape, px, py, xf);
    }
}

// XML & SVG PARSER

bool isvg_parse(isvg_tape* tape, char* svg) {
    if (!tape || !svg) return false;
    tape->n_cmds = tape->n_pts = 0;

    isvg_xform xform_stack[16];
    uint8_t fill_stack[16];
    uint8_t stroke_stack[16];
    uint8_t stroke_width_stack[16];
    uint8_t fill_rule_stack[16];
    int xf_ptr = 0;
    int skip_depth = 0;
    xform_stack[0] = (isvg_xform){{1,0, 0,1, 0,0}};
    fill_stack[0] = ISVG_PAINT_BLACK;
    stroke_stack[0] = ISVG_PAINT_NONE;
    stroke_width_stack[0] = 1;
    fill_rule_stack[0] = ISVG_FILL_NONZERO;

    char *s = svg, *name, *val;

    while (*s && !tape->failed) {
        if (*s == '<') {
            s++;
            if (!*s) break;
            if (*s == '!' || *s == '?') {
                while (*s && *s != '>') s++;
                continue;
            }

            char tag[24] = {0};
            int i = 0;
            bool closing = false;
            if (*s == '/') {
                closing = true;
                s++;
            }
            while (*s && !isvg_is_space(*s) && *s != '>' && *s != '/') {
                if (i < 23) tag[i++] = *s;
                s++;
            }
            const int tag_kind = isvg_tag_kind(tag);

            if (closing) {
                if ((tag_kind == ISVG_TAG_G || tag_kind == ISVG_TAG_SVG) && xf_ptr > 0) {
                    xf_ptr--;
                }
                if (skip_depth > 0 && isvg_tag_skipped_container(tag_kind)) {
                    skip_depth--;
                }
                while (*s && *s != '>') s++;
                continue;
            }

            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            float cx_attr = 0.0f, cy_attr = 0.0f, r = 0.0f, rx = 0.0f, ry = 0.0f;
            float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
            uint8_t fill = fill_stack[xf_ptr], stroke = stroke_stack[xf_ptr];
            uint8_t stroke_width = stroke_width_stack[xf_ptr];
            uint8_t fill_rule = fill_rule_stack[xf_ptr];
            char* d_str = NULL;
            char* pts_str = NULL;
            isvg_xform cur_xf = xform_stack[xf_ptr];
            bool self_closing = false;

            while (*s && *s != '>') {
                while (isvg_is_space(*s)) s++;
                if (*s == '/') {
                    self_closing = true;
                    break;
                }
                if (*s == '>') break;
                name = s;
                while (*s && *s != '=' && *s != '>' && *s != '/' && !isvg_is_space(*s)) s++;
                char* name_end = s;
                while (isvg_is_space(*s)) s++;
                if (*s != '=') {
                    while (*s && *s != '>' && *s != '/' && !isvg_is_space(*s)) s++;
                    continue;
                }
                *name_end = '\0';
                s++;
                while (isvg_is_space(*s)) s++;
                if (!*s) break;
                char quote = *s++;
                if (quote != '"' && quote != '\'') continue;
                val = s;
                while (*s && *s != quote) s++;
                if (*s != quote) break;
                *s++ = 0;

                const char* value = val;
                switch (isvg_attr_kind(name)) {
                case ISVG_ATTR_FILL:
                    fill = isvg_parse_paint(val);
                    break;
                case ISVG_ATTR_STROKE:
                    stroke = isvg_parse_paint(val);
                    break;
                case ISVG_ATTR_STROKE_WIDTH:
                    stroke_width = isvg_quantize_stroke_width(isvg_atof(&value));
                    break;
                case ISVG_ATTR_FILL_RULE:
                    fill_rule = isvg_parse_fill_rule(val);
                    break;
                case ISVG_ATTR_STYLE:
                    isvg_apply_style(val, &fill, &stroke, &stroke_width, &fill_rule);
                    break;
                case ISVG_ATTR_D:
                    d_str = val;
                    break;
                case ISVG_ATTR_POINTS:
                    pts_str = val;
                    break;
                case ISVG_ATTR_X:
                    x = isvg_atof(&value);
                    break;
                case ISVG_ATTR_Y:
                    y = isvg_atof(&value);
                    break;
                case ISVG_ATTR_WIDTH:
                    width = isvg_atof(&value);
                    break;
                case ISVG_ATTR_HEIGHT:
                    height = isvg_atof(&value);
                    break;
                case ISVG_ATTR_CX:
                    cx_attr = isvg_atof(&value);
                    break;
                case ISVG_ATTR_CY:
                    cy_attr = isvg_atof(&value);
                    break;
                case ISVG_ATTR_R:
                    r = isvg_atof(&value);
                    break;
                case ISVG_ATTR_RX:
                    rx = isvg_atof(&value);
                    break;
                case ISVG_ATTR_RY:
                    ry = isvg_atof(&value);
                    break;
                case ISVG_ATTR_X1:
                    x1 = isvg_atof(&value);
                    break;
                case ISVG_ATTR_Y1:
                    y1 = isvg_atof(&value);
                    break;
                case ISVG_ATTR_X2:
                    x2 = isvg_atof(&value);
                    break;
                case ISVG_ATTR_Y2:
                    y2 = isvg_atof(&value);
                    break;
                case ISVG_ATTR_TRANSFORM: {
                    const char* v = val;
                    while (*v) {
                        if (strncmp(v, "translate", 9) == 0) {
                            if (!isvg_enter_transform_args(&v, 9)) continue;
                            float tx = isvg_atof(&v), ty = isvg_atof(&v);
                            isvg_xform t = {{1,0, 0,1, tx,ty}}; isvg_xform_mul(cur_xf.m, cur_xf.m, t.m);
                        } else if (strncmp(v, "scale", 5) == 0) {
                            if (!isvg_enter_transform_args(&v, 5)) continue;
                            float sx = isvg_atof(&v), sy = isvg_atof(&v);
                            sy = (sy == 0.0f) ? sx : sy; // Fallback if single param
                            isvg_xform t = {{sx,0, 0,sy, 0,0}}; isvg_xform_mul(cur_xf.m, cur_xf.m, t.m);
                        } else if (strncmp(v, "rotate", 6) == 0) {
                            if (!isvg_enter_transform_args(&v, 6)) continue;
                            float angle = isvg_atof(&v);
                            float rcx = 0.0f, rcy = 0.0f;
                            const char* save = v;
                            if (isvg_parse_number(&save, &rcx) && isvg_parse_number(&save, &rcy)) {
                                v = save;
                            }
                            float radians = angle * (3.1415927f / 180.0f);
                            float cs = cosf(radians), sn = sinf(radians);
                            isvg_xform t = {{cs,sn, -sn,cs,
                                             rcx - cs * rcx + sn * rcy,
                                             rcy - sn * rcx - cs * rcy}};
                            isvg_xform_mul(cur_xf.m, cur_xf.m, t.m);
                        } else if (strncmp(v, "matrix", 6) == 0) {
                            if (!isvg_enter_transform_args(&v, 6)) continue;
                            float a=isvg_atof(&v), b=isvg_atof(&v), c=isvg_atof(&v), d=isvg_atof(&v), e=isvg_atof(&v), f=isvg_atof(&v);
                            isvg_xform t = {{a,b, c,d, e,f}}; isvg_xform_mul(cur_xf.m, cur_xf.m, t.m);
                        } else v++;
                    }
                    break;
                }
                default:
                    break;
                }
            }

            if (isvg_tag_skipped_container(tag_kind)) {
                if (!self_closing) skip_depth++;
                continue;
            }

            if (tag_kind == ISVG_TAG_G || tag_kind == ISVG_TAG_SVG) {
                if (!self_closing && xf_ptr < 15) {
                    xf_ptr++;
                    xform_stack[xf_ptr] = cur_xf;
                    fill_stack[xf_ptr] = fill;
                    stroke_stack[xf_ptr] = stroke;
                    stroke_width_stack[xf_ptr] = stroke_width;
                    fill_rule_stack[xf_ptr] = fill_rule;
                }
                continue;
            }

            if (skip_depth > 0) {
                continue;
            }
            if (!isvg_tag_drawable(tag_kind)) continue;

            isvg_add_cmd(tape, CMD_STYLE);
            isvg_add_cmd(tape, fill);
            isvg_add_cmd(tape, stroke);
            isvg_add_cmd(tape, stroke_width);
            isvg_add_cmd(tape, fill_rule);

            // Synthesize Shapes -> Paths
            if (tag_kind == ISVG_TAG_PATH && d_str) {
                const char* p = d_str;
                char cmd = 0, prev_cmd = 0;
                float cx = 0.0f, cy = 0.0f, sx = 0.0f, sy = 0.0f;
                float last_cx = 0.0f, last_cy = 0.0f, last_qx = 0.0f, last_qy = 0.0f;
                while (*p && !tape->failed) {
                    float a[7] = {0};
                    isvg_skip_sep(&p);
                    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
                        cmd = *p++;
                        if (cmd == 'Z' || cmd == 'z') {
                            cx = sx; cy = sy;
                            isvg_add_cmd(tape, CMD_CLOSE);
                            prev_cmd = cmd;
                            continue;
                        }
                    }
                    if (cmd == 0) {
                        if (*p) p++;
                        continue;
                    }

                    int rel = (cmd >= 'a' && cmd <= 'z');
                    float px = rel ? cx : 0.0f;
                    float py = rel ? cy : 0.0f;
                    if (cmd == 'M' || cmd == 'm') {
                        if (!isvg_parse_number(&p, &a[0]) || !isvg_parse_number(&p, &a[1])) break;
                        cx = px + a[0]; cy = py + a[1]; sx = cx; sy = cy;
                        isvg_add_cmd(tape, CMD_MOVE); isvg_add_pt(tape, cx, cy, cur_xf.m);
                        cmd = rel ? 'l' : 'L';
                    } else if (cmd == 'L' || cmd == 'l') {
                        if (!isvg_parse_number(&p, &a[0]) || !isvg_parse_number(&p, &a[1])) break;
                        cx = px + a[0]; cy = py + a[1];
                        isvg_add_cmd(tape, CMD_LINE); isvg_add_pt(tape, cx, cy, cur_xf.m);
                    } else if (cmd == 'H' || cmd == 'h') {
                        if (!isvg_parse_number(&p, &a[0])) break;
                        cx = px + a[0];
                        isvg_add_cmd(tape, CMD_LINE); isvg_add_pt(tape, cx, cy, cur_xf.m);
                    } else if (cmd == 'V' || cmd == 'v') {
                        if (!isvg_parse_number(&p, &a[0])) break;
                        cy = py + a[0];
                        isvg_add_cmd(tape, CMD_LINE); isvg_add_pt(tape, cx, cy, cur_xf.m);
                    } else if (cmd == 'C' || cmd == 'c') {
                        for (int n = 0; n < 6; n++) if (!isvg_parse_number(&p, &a[n])) goto isvg_path_done;
                        isvg_add_cmd(tape, CMD_CUBIC);
                        isvg_add_pt(tape, px + a[0], py + a[1], cur_xf.m);
                        isvg_add_pt(tape, px + a[2], py + a[3], cur_xf.m);
                        cx = px + a[4]; cy = py + a[5];
                        last_cx = px + a[2]; last_cy = py + a[3];
                        isvg_add_pt(tape, cx, cy, cur_xf.m);
                    } else if (cmd == 'S' || cmd == 's') {
                        for (int n = 0; n < 4; n++) if (!isvg_parse_number(&p, &a[n])) goto isvg_path_done;
                        float c1x = (prev_cmd == 'C' || prev_cmd == 'c' || prev_cmd == 'S' || prev_cmd == 's') ? 2.0f * cx - last_cx : cx;
                        float c1y = (prev_cmd == 'C' || prev_cmd == 'c' || prev_cmd == 'S' || prev_cmd == 's') ? 2.0f * cy - last_cy : cy;
                        isvg_add_cmd(tape, CMD_CUBIC);
                        isvg_add_pt(tape, c1x, c1y, cur_xf.m);
                        isvg_add_pt(tape, px + a[0], py + a[1], cur_xf.m);
                        cx = px + a[2]; cy = py + a[3];
                        last_cx = px + a[0]; last_cy = py + a[1];
                        isvg_add_pt(tape, cx, cy, cur_xf.m);
                    } else if (cmd == 'Q' || cmd == 'q' || cmd == 'T' || cmd == 't') {
                        bool smooth = (cmd == 'T' || cmd == 't');
                        int req = smooth ? 2 : 4;
                        for (int n = 0; n < req; n++) if (!isvg_parse_number(&p, &a[n])) goto isvg_path_done;
                        float qx = smooth && (prev_cmd == 'Q' || prev_cmd == 'q' || prev_cmd == 'T' || prev_cmd == 't') ? 2.0f * cx - last_qx : (smooth ? cx : px + a[0]);
                        float qy = smooth && (prev_cmd == 'Q' || prev_cmd == 'q' || prev_cmd == 'T' || prev_cmd == 't') ? 2.0f * cy - last_qy : (smooth ? cy : py + a[1]);
                        float ex = smooth ? px + a[0] : px + a[2];
                        float ey = smooth ? py + a[1] : py + a[3];
                        isvg_add_cmd(tape, CMD_CUBIC);
                        isvg_add_pt(tape, cx + (2.0f / 3.0f) * (qx - cx), cy + (2.0f / 3.0f) * (qy - cy), cur_xf.m);
                        isvg_add_pt(tape, ex + (2.0f / 3.0f) * (qx - ex), ey + (2.0f / 3.0f) * (qy - ey), cur_xf.m);
                        cx = ex; cy = ey; last_qx = qx; last_qy = qy;
                        isvg_add_pt(tape, cx, cy, cur_xf.m);
                    } else if (cmd == 'A' || cmd == 'a') {
                        for (int n = 0; n < 7; n++) if (!isvg_parse_number(&p, &a[n])) goto isvg_path_done;
                        float ex = px + a[5], ey = py + a[6];
                        isvg_add_arc_to(tape, cx, cy, a[0], a[1], a[2], (int)a[3], (int)a[4], ex, ey, cur_xf.m);
                        cx = ex; cy = ey;
                    } else {
                        p++;
                    }
                    prev_cmd = cmd;
                }
isvg_path_done:
                ;
            }
            else if ((tag_kind == ISVG_TAG_POLYGON || tag_kind == ISVG_TAG_POLYLINE) && pts_str) {
                const char* p = pts_str;
                int first = 1;
                while (*p) {
                    float px = 0.0f, py = 0.0f;
                    if (!isvg_parse_number(&p, &px) || !isvg_parse_number(&p, &py)) break;
                    isvg_add_cmd(tape, first ? CMD_MOVE : CMD_LINE);
                    isvg_add_pt(tape, px, py, cur_xf.m);
                    first = 0;
                }
                if (tag_kind == ISVG_TAG_POLYGON) isvg_add_cmd(tape, CMD_CLOSE);
            }
            else if (tag_kind == ISVG_TAG_LINE) {
                isvg_add_cmd(tape, CMD_MOVE); isvg_add_pt(tape, x1, y1, cur_xf.m);
                isvg_add_cmd(tape, CMD_LINE); isvg_add_pt(tape, x2, y2, cur_xf.m);
            }
            else if (tag_kind == ISVG_TAG_RECT && width > 0 && height > 0) {
                if (rx <= 0.0f && ry > 0.0f) rx = ry;
                if (ry <= 0.0f && rx > 0.0f) ry = rx;
                rx = isvg_fmin(fabsf(rx), width * 0.5f);
                ry = isvg_fmin(fabsf(ry), height * 0.5f);
                if (rx > 0.0f && ry > 0.0f) {
                    float kx = 0.552284f * rx, ky = 0.552284f * ry;
                    float x2r = x + width, y2r = y + height;
                    isvg_add_cmd(tape, CMD_MOVE); isvg_add_pt(tape, x + rx, y, cur_xf.m);
                    isvg_add_line_to(tape, x2r - rx, y, cur_xf.m);
                    isvg_add_cubic_to(tape, x2r - rx + kx, y, x2r, y + ry - ky, x2r, y + ry, cur_xf.m);
                    isvg_add_line_to(tape, x2r, y2r - ry, cur_xf.m);
                    isvg_add_cubic_to(tape, x2r, y2r - ry + ky, x2r - rx + kx, y2r, x2r - rx, y2r, cur_xf.m);
                    isvg_add_line_to(tape, x + rx, y2r, cur_xf.m);
                    isvg_add_cubic_to(tape, x + rx - kx, y2r, x, y2r - ry + ky, x, y2r - ry, cur_xf.m);
                    isvg_add_line_to(tape, x, y + ry, cur_xf.m);
                    isvg_add_cubic_to(tape, x, y + ry - ky, x + rx - kx, y, x + rx, y, cur_xf.m);
                } else {
                    isvg_add_cmd(tape, CMD_MOVE); isvg_add_pt(tape, x, y, cur_xf.m);
                    isvg_add_line_to(tape, x + width, y, cur_xf.m);
                    isvg_add_line_to(tape, x + width, y + height, cur_xf.m);
                    isvg_add_line_to(tape, x, y + height, cur_xf.m);
                }
                isvg_add_cmd(tape, CMD_CLOSE);
            }
            else if (tag_kind == ISVG_TAG_CIRCLE || tag_kind == ISVG_TAG_ELLIPSE) {
                float erx = tag_kind == ISVG_TAG_CIRCLE ? r : rx;
                float ery = tag_kind == ISVG_TAG_CIRCLE ? r : ry;
                if (erx > 0 && ery > 0) {
                    float kx = 0.552284f * erx, ky = 0.552284f * ery;
                    isvg_add_cmd(tape, CMD_MOVE); isvg_add_pt(tape, cx_attr + erx, cy_attr, cur_xf.m);
                    isvg_add_cmd(tape, CMD_CUBIC); isvg_add_pt(tape, cx_attr+erx, cy_attr+ky, cur_xf.m); isvg_add_pt(tape, cx_attr+kx, cy_attr+ery, cur_xf.m); isvg_add_pt(tape, cx_attr, cy_attr+ery, cur_xf.m);
                    isvg_add_cmd(tape, CMD_CUBIC); isvg_add_pt(tape, cx_attr-kx, cy_attr+ery, cur_xf.m); isvg_add_pt(tape, cx_attr-erx, cy_attr+ky, cur_xf.m); isvg_add_pt(tape, cx_attr-erx, cy_attr, cur_xf.m);
                    isvg_add_cmd(tape, CMD_CUBIC); isvg_add_pt(tape, cx_attr-erx, cy_attr-ky, cur_xf.m); isvg_add_pt(tape, cx_attr-kx, cy_attr-ery, cur_xf.m); isvg_add_pt(tape, cx_attr, cy_attr-ery, cur_xf.m);
                    isvg_add_cmd(tape, CMD_CUBIC); isvg_add_pt(tape, cx_attr+kx, cy_attr-ery, cur_xf.m); isvg_add_pt(tape, cx_attr+erx, cy_attr-ky, cur_xf.m); isvg_add_pt(tape, cx_attr+erx, cy_attr, cur_xf.m);
                    isvg_add_cmd(tape, CMD_CLOSE);
                }
            }
        }
        if (*s) s++;
    }
    return !tape->failed && tape->n_pts > 0;
}

// RASTERIZER: FAST 1-BIT ACTIVE EDGE TABLE

typedef struct {
    float x, dx;
    float x0, y0;
    int start_y, end_y, dir;
} isvg_edge;

typedef struct {
    isvg_edge* edges;
    int n_edges;
    int cap_edges;
} isvg_edge_list;

typedef struct {
    float x0, y0, x1, y1;
} isvg_segment;

typedef struct {
    isvg_segment* segments;
    int n_segments;
    int cap_segments;
} isvg_segment_list;

// Helper to push edge to dynamic list
static void isvg_push_edge(isvg_edge** edges, int* n, int* cap, float x0, float y0, float x1, float y1) {
    if (!edges || !n || !cap || *n < 0 || *cap < 0 || *n == INT_MAX || fabsf(y0 - y1) < 1e-6f) return;
    if (*n >= *cap) {
        int next_cap = 0;
        if (!isvg_next_cap(*cap, *n + 1, 512, &next_cap) ||
            (size_t)next_cap > ((size_t)-1) / sizeof(isvg_edge)) {
            return;
        }
        isvg_edge* resized = (isvg_edge*)ISVG_REALLOC(*edges, (size_t)next_cap * sizeof(isvg_edge));
        if (!resized) return;
        *edges = resized;
        *cap = next_cap;
    }
    isvg_edge* e = &(*edges)[*n];
    int is_down = (y0 < y1);
    float top_x = is_down ? x0 : x1;
    float top_y = is_down ? y0 : y1;
    float bot_x = is_down ? x1 : x0;
    float bot_y = is_down ? y1 : y0;
    int start_y = (int)ceilf(top_y - 0.5f);
    int end_y = (int)ceilf(bot_y - 0.5f);
    if (start_y >= end_y) return;

    e->dir = is_down ? 1 : -1;
    e->x0 = top_x;
    e->y0 = top_y;
    e->start_y = start_y;
    e->end_y = end_y;
    e->x = top_x;
    e->dx = (bot_x - top_x) / (bot_y - top_y);
    (*n)++;
}

static void isvg_push_segment(isvg_segment_list* list, float x0, float y0, float x1, float y1) {
    if (!list || list->n_segments < 0 || list->cap_segments < 0 || list->n_segments == INT_MAX) return;
    if (list->n_segments >= list->cap_segments) {
        int next_cap = 0;
        if (!isvg_next_cap(list->cap_segments, list->n_segments + 1, 128, &next_cap) ||
            (size_t)next_cap > ((size_t)-1) / sizeof(isvg_segment)) {
            return;
        }
        isvg_segment* resized = (isvg_segment*)ISVG_REALLOC(list->segments, (size_t)next_cap * sizeof(isvg_segment));
        if (!resized) return;
        list->segments = resized;
        list->cap_segments = next_cap;
    }
    isvg_segment* seg = &list->segments[list->n_segments++];
    seg->x0 = x0;
    seg->y0 = y0;
    seg->x1 = x1;
    seg->y1 = y1;
}

static void isvg_paint_span(uint8_t* buf, int target_w, int y, int x0, int x1, uint8_t paint) {
    x0 = isvg_imax(0, x0);
    x1 = isvg_imin(target_w - 1, x1);
    if (x0 > x1 || paint == ISVG_PAINT_NONE) return;

    int stride = (target_w + 7) / 8;
    uint8_t* row = buf + (y * stride);
    int b0 = x0 / 8, b1 = x1 / 8;
    uint8_t m0 = 0xFF >> (x0 & 7), m1 = ~(0xFF >> ((x1 & 7) + 1));

    if (b0 == b1) {
        uint8_t mask = m0 & m1;
        if (paint == ISVG_PAINT_BLACK) row[b0] |= mask;
        else row[b0] &= (uint8_t)~mask;
    } else {
        if (paint == ISVG_PAINT_BLACK) {
            row[b0] |= m0;
            if (b1 - b0 > 1) memset(&row[b0 + 1], 0xFF, b1 - b0 - 1);
            row[b1] |= m1;
        } else {
            row[b0] &= (uint8_t)~m0;
            if (b1 - b0 > 1) memset(&row[b0 + 1], 0x00, b1 - b0 - 1);
            row[b1] &= (uint8_t)~m1;
        }
    }
}

static void isvg_paint_span_f(uint8_t* buf, int target_w, int y, float x0, float x1, uint8_t paint) {
    if (x1 < x0) {
        float tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    int ix0 = (int)ceilf(x0 - 0.5f);
    int ix1 = (int)ceilf(x1 - 0.5f) - 1;
    isvg_paint_span(buf, target_w, y, ix0, ix1, paint);
}

static void isvg_paint_disc(uint8_t* buf, int target_w, int target_h, float cx, float cy, float radius, uint8_t paint) {
    if (radius <= 0.0f || paint == ISVG_PAINT_NONE) return;
    int y0 = isvg_imax(0, (int)floorf(cy - radius));
    int y1 = isvg_imin(target_h - 1, (int)ceilf(cy + radius));
    float r2 = radius * radius;
    for (int y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f) - cy;
        float rem = r2 - dy * dy;
        if (rem < 0.0f) continue;
        float dx = sqrtf(rem);
        isvg_paint_span_f(buf, target_w, y, cx - dx, cx + dx, paint);
    }
}

static void isvg_paint_stroke_segment(uint8_t* buf, int target_w, int target_h,
                                      float x0, float y0, float x1, float y1,
                                      float width, uint8_t paint) {
    if (width <= 0.0f || paint == ISVG_PAINT_NONE) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    float radius = width * 0.5f;
    if (len < 1e-5f) {
        isvg_paint_disc(buf, target_w, target_h, x0, y0, radius, paint);
        return;
    }
    int steps = isvg_imax(1, (int)ceilf(len / isvg_fmax(1.0f, radius * 0.75f)));
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        isvg_paint_disc(buf, target_w, target_h, x0 + dx * t, y0 + dy * t, radius, paint);
    }
}

static void isvg_paint_edges(uint8_t* buffer, int target_w, int target_h,
                             isvg_edge* edges, int n_edges, uint8_t paint,
                             uint8_t fill_rule) {
    if (!edges || n_edges <= 0 || target_h <= 0 || paint == ISVG_PAINT_NONE) return;
    if ((size_t)n_edges > ((size_t)-1) / sizeof(int) ||
        (size_t)target_h > ((size_t)-1) / sizeof(int)) return;

    int* active = (int*)ISVG_MALLOC(n_edges * sizeof(int));
    int* row_head = (int*)ISVG_MALLOC(target_h * sizeof(int));
    int* row_next = (int*)ISVG_MALLOC(n_edges * sizeof(int));
    if (!active || !row_head || !row_next) {
        if (active) ISVG_FREE(active);
        if (row_head) ISVG_FREE(row_head);
        if (row_next) ISVG_FREE(row_next);
        return;
    }
    for (int y = 0; y < target_h; y++) row_head[y] = -1;
    for (int i = 0; i < n_edges; i++) {
        int start = isvg_imax(0, edges[i].start_y);
        if (start >= target_h || edges[i].end_y <= 0) continue;
        row_next[i] = row_head[start];
        row_head[start] = i;
    }

    int n_active = 0;

    for (int y = 0; y < target_h; y++) {
        for (int edge_idx = row_head[y]; edge_idx >= 0; edge_idx = row_next[edge_idx]) {
            if (edges[edge_idx].end_y > y) {
                edges[edge_idx].x =
                    edges[edge_idx].x0 + edges[edge_idx].dx * (((float)y + 0.5f) - edges[edge_idx].y0);
                active[n_active++] = edge_idx;
            }
        }

        for (int i = 0; i < n_active; ) {
            int e = active[i];
            if (y >= edges[e].end_y) {
                active[i] = active[--n_active];
            } else {
                i++;
            }
        }

        if (n_active == 0) continue;

        for (int i = 1; i < n_active; i++) {
            int key = active[i];
            float key_x = edges[key].x;
            int j = i - 1;
            while (j >= 0 && edges[active[j]].x > key_x) {
                active[j + 1] = active[j];
                j--;
            }
            active[j + 1] = key;
        }

        int winding = 0;
        int parity = 0;
        const int even_odd = (fill_rule == ISVG_FILL_EVENODD);
        const int non_zero = 1 - even_odd;
        float span_start = 0.0f;
        for (int i = 0; i < n_active; i++) {
            isvg_edge* e = &edges[active[i]];
            const float x = e->x;
            const int previous_winding = winding;
            const int previous_parity = parity;
            winding += e->dir;
            parity = 1 - parity;

            const int opens =
                non_zero * ((previous_winding == 0) && (winding != 0)) +
                even_odd * (previous_parity == 0);
            const int closes =
                non_zero * ((previous_winding != 0) && (winding == 0)) +
                even_odd * (previous_parity != 0);
            if (closes) {
                isvg_paint_span_f(buffer, target_w, y, span_start, x, paint);
            }
            span_start = span_start * (1 - opens) + x * opens;
            e->x = x + e->dx;
        }
    }

    ISVG_FREE(active);
    ISVG_FREE(row_head);
    ISVG_FREE(row_next);
}

void isvg_rasterize(isvg_tape* tape, uint8_t* buffer, int target_w, int target_h) {
    if (!tape || !buffer || tape->failed || tape->n_pts < 2 ||
        (tape->n_pts & 1) || target_w <= 0 || target_h <= 0) return;
    if ((tape->n_cmds > 0 && !tape->cmds) || !tape->pts) return;

    // 1. Bounding box over drawable filled or stroked geometry.
    float min_x = 0.0f, max_x = 0.0f, min_y = 0.0f, max_y = 0.0f;
    bool have_bounds = false;
    int bounds_pt_idx = 0;
    uint8_t bounds_fill = ISVG_PAINT_BLACK;
    uint8_t bounds_stroke = ISVG_PAINT_NONE;
    uint8_t bounds_stroke_width = 1;
    for (int c = 0; c < tape->n_cmds; c++) {
        uint8_t cmd = tape->cmds[c];
        int count = 0;
        if (cmd == CMD_STYLE) {
            if (c > tape->n_cmds - 5) return;
            bounds_fill = tape->cmds[++c];
            bounds_stroke = tape->cmds[++c];
            bounds_stroke_width = tape->cmds[++c];
            c++;
            continue;
        } else if (cmd == CMD_MOVE || cmd == CMD_LINE) {
            count = 1;
        } else if (cmd == CMD_CUBIC) {
            count = 3;
        }
        if (count > 0 && bounds_pt_idx > tape->n_pts - count * 2) return;
        for (int i = 0; i < count; i++) {
            float x = tape->pts[bounds_pt_idx++];
            float y = tape->pts[bounds_pt_idx++];
            if (bounds_fill == ISVG_PAINT_NONE && bounds_stroke == ISVG_PAINT_NONE) continue;
            float pad = bounds_stroke != ISVG_PAINT_NONE ? bounds_stroke_width * 0.5f : 0.0f;
            if (!have_bounds) {
                min_x = x - pad;
                max_x = x + pad;
                min_y = y - pad;
                max_y = y + pad;
                have_bounds = true;
            } else {
                min_x = isvg_fmin(min_x, x - pad);
                max_x = isvg_fmax(max_x, x + pad);
                min_y = isvg_fmin(min_y, y - pad);
                max_y = isvg_fmax(max_y, y + pad);
            }
        }
    }
    if (!have_bounds) {
        return;
    }

    // Auto-Fit Transformation
    float sw = target_w / (max_x - min_x + 1e-5f);
    float sh = target_h / (max_y - min_y + 1e-5f);
    float scale = isvg_fmin(sw, sh);
    float tx = (target_w - (max_x - min_x) * scale) * 0.5f - min_x * scale;
    float ty = (target_h - (max_y - min_y) * scale) * 0.5f - min_y * scale;

    for (int i = 0; i < tape->n_pts; i+=2) {
        tape->pts[i]   = tape->pts[i] * scale + tx;
        tape->pts[i+1] = tape->pts[i+1] * scale + ty;
    }

    // 2. Paint each SVG element in source order.
    isvg_edge_list fill_edges = {NULL, 0, 0};
    isvg_segment_list stroke_segments = {NULL, 0, 0};
    int pt_idx = 0;
    uint8_t fill = ISVG_PAINT_BLACK;
    uint8_t stroke = ISVG_PAINT_NONE;
    uint8_t stroke_width = 1;
    uint8_t fill_rule = ISVG_FILL_NONZERO;
    float cx = 0.0f, cy = 0.0f, sx = 0.0f, sy = 0.0f;

    #define ISVG_FLUSH_SHAPE() do { \
        if (fill != ISVG_PAINT_NONE && fill_edges.n_edges > 0) { \
            isvg_paint_edges(buffer, target_w, target_h, fill_edges.edges, fill_edges.n_edges, fill, fill_rule); \
        } \
        if (stroke != ISVG_PAINT_NONE && stroke_width > 0) { \
            float scaled_width = isvg_fmax(1.0f, (float)stroke_width * scale); \
            for (int si = 0; si < stroke_segments.n_segments; si++) { \
                isvg_segment* seg = &stroke_segments.segments[si]; \
                isvg_paint_stroke_segment(buffer, target_w, target_h, seg->x0, seg->y0, seg->x1, seg->y1, scaled_width, stroke); \
            } \
        } \
        fill_edges.n_edges = 0; \
        stroke_segments.n_segments = 0; \
    } while (0)

    bool stream_ok = true;
    for (int c = 0; c < tape->n_cmds && stream_ok; c++) {
        uint8_t cmd = tape->cmds[c];
        if (cmd == CMD_STYLE) {
            ISVG_FLUSH_SHAPE();
            if (c > tape->n_cmds - 5) {
                stream_ok = false;
                break;
            }
            fill = tape->cmds[++c];
            stroke = tape->cmds[++c];
            stroke_width = tape->cmds[++c];
            fill_rule = tape->cmds[++c];
        }
        else if (cmd == CMD_MOVE) {
            if (pt_idx > tape->n_pts - 2) { stream_ok = false; break; }
            cx = sx = tape->pts[pt_idx++]; cy = sy = tape->pts[pt_idx++];
        }
        else if (cmd == CMD_LINE) {
            if (pt_idx > tape->n_pts - 2) { stream_ok = false; break; }
            float nx = tape->pts[pt_idx++], ny = tape->pts[pt_idx++];
            if (fill != ISVG_PAINT_NONE) {
                isvg_push_edge(&fill_edges.edges, &fill_edges.n_edges, &fill_edges.cap_edges, cx, cy, nx, ny);
            }
            if (stroke != ISVG_PAINT_NONE) {
                isvg_push_segment(&stroke_segments, cx, cy, nx, ny);
            }
            cx = nx; cy = ny;
        }
        else if (cmd == CMD_CUBIC) {
            if (pt_idx > tape->n_pts - 6) { stream_ok = false; break; }
            float c1x = tape->pts[pt_idx++], c1y = tape->pts[pt_idx++];
            float c2x = tape->pts[pt_idx++], c2y = tape->pts[pt_idx++];
            float nx = tape->pts[pt_idx++], ny = tape->pts[pt_idx++];
            // Simplistic 8-step subdivision (O(1) approach)
            float px = cx, py = cy;
            for (int step = 1; step <= 8; step++) {
                float t = step / 8.0f, it = 1.0f - t;
                float tx = it*it*it*cx + 3*it*it*t*c1x + 3*it*t*t*c2x + t*t*t*nx;
                float ty = it*it*it*cy + 3*it*it*t*c1y + 3*it*t*t*c2y + t*t*t*ny;
                if (fill != ISVG_PAINT_NONE) {
                    isvg_push_edge(&fill_edges.edges, &fill_edges.n_edges, &fill_edges.cap_edges, px, py, tx, ty);
                }
                if (stroke != ISVG_PAINT_NONE) {
                    isvg_push_segment(&stroke_segments, px, py, tx, ty);
                }
                px = tx; py = ty;
            }
            cx = nx; cy = ny;
        }
        else if (cmd == CMD_CLOSE) {
            if (fill != ISVG_PAINT_NONE) {
                isvg_push_edge(&fill_edges.edges, &fill_edges.n_edges, &fill_edges.cap_edges, cx, cy, sx, sy);
            }
            if (stroke != ISVG_PAINT_NONE) {
                isvg_push_segment(&stroke_segments, cx, cy, sx, sy);
            }
            cx = sx; cy = sy;
        }
    }

    if (stream_ok) {
        ISVG_FLUSH_SHAPE();
    }
    #undef ISVG_FLUSH_SHAPE

    if (fill_edges.edges) ISVG_FREE(fill_edges.edges);
    if (stroke_segments.segments) ISVG_FREE(stroke_segments.segments);
}

#endif // INKSVG_IMPLEMENTATION
