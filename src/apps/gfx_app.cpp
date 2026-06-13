#include "apps/gfx_app.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>

namespace {
uint8_t resolveShaderSlot(uint8_t slot) {
  static const uint8_t kShaderOrder[8] = {2, 0, 1, 3, 4, 5, 6, 7};
  return kShaderOrder[slot % 8];
}

struct Vec2 {
  float x;
  float y;
};

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Point2D {
  int16_t x;
  int16_t y;
};

static const float kPi = 3.14159265f;
static const int kShader3LineCount = 72;
static uint8_t shader3Accum[EPD_WIDTH * EPD_HEIGHT];

inline float clamp01(float v) {
  if (v < 0.0f) {
    return 0.0f;
  }
  if (v > 1.0f) {
    return 1.0f;
  }
  return v;
}

inline float stepf(float edge, float x) { return x < edge ? 0.0f : 1.0f; }

inline float smoothstepf(float edge0, float edge1, float x) {
  if (edge0 == edge1) {
    return x < edge0 ? 0.0f : 1.0f;
  }
  float t = clamp01((x - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}

inline Vec2 rotate2(Vec2 v, float a) {
  float s = sinf(a);
  float c = cosf(a);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}

inline float hash12(int32_t x, int32_t y) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u ^
               static_cast<uint32_t>(y) * 668265263u ^ 0x9E3779B9u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= (h >> 16);
  return (h & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

inline float bayer4(int16_t x, int16_t y) {
  static const uint8_t table[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };
  return (table[y & 3][x & 3] + 0.5f) * (1.0f / 16.0f);
}

inline float bayer8(int16_t x, int16_t y) {
  static const uint8_t table[8][8] = {
      {0, 48, 12, 60, 3, 51, 15, 63},  {32, 16, 44, 28, 35, 19, 47, 31},
      {8, 56, 4, 52, 11, 59, 7, 55},   {40, 24, 36, 20, 43, 27, 39, 23},
      {2, 50, 14, 62, 1, 49, 13, 61},  {34, 18, 46, 30, 33, 17, 45, 29},
      {10, 58, 6, 54, 9, 57, 5, 53},   {42, 26, 38, 22, 41, 25, 37, 21},
  };
  return (table[y & 7][x & 7] + 0.5f) * (1.0f / 64.0f);
}

float mod2(float x) {
  float m = fmodf(x + 1.0f, 2.0f);
  if (m < 0.0f) {
    m += 2.0f;
  }
  return m - 1.0f;
}

float kset(Vec3 p, float t) {
  Vec2 xy = {p.x, p.y};
  float lenXY = sqrtf(xy.x * xy.x + xy.y * xy.y);
  xy = rotate2(xy, expf(-5.0f * lenXY) * 4.0f + t * 0.1f);
  p.x = xy.x;
  p.y = xy.y;
  static const float cos04 = 0.92106099f;
  static const float sin04 = 0.38941834f;
  float px = p.x * cos04 - p.z * sin04;
  float pz = p.x * sin04 + p.z * cos04;
  p.x = px;
  p.z = pz;
  p.x += t * 0.3f;
  p.x = mod2(p.x);
  p.y = mod2(p.y);
  p.z = mod2(p.z);
  for (int i = 0; i < 3; i++) {
    p.x = fabsf(p.x);
    p.y = fabsf(p.y);
    p.z = fabsf(p.z);
    float d = p.x * p.x + p.y * p.y + p.z * p.z + 0.0001f;
    p.x = p.x / d - 0.8f;
    p.y = p.y / d - 0.8f;
    p.z = p.z / d - 0.8f;
  }
  return sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
}

float march(Vec3 from, Vec3 dir, float t) {
  Vec3 p = {from.x + dir.x * 0.8f, from.y + dir.y * 0.8f, from.z + dir.z * 0.8f};
  float d = 0.06f;
  float g = 0.0f;
  static const float cos002 = 0.99980003f;
  static const float sin002 = 0.01999867f;
  for (int i = 0; i < 30; i++) {
    p.x += dir.x * d;
    p.y += dir.y * d;
    p.z += dir.z * d;
    float x = p.x * cos002 - p.y * sin002;
    float y = p.x * sin002 + p.y * cos002;
    p.x = x;
    p.y = y;
    float k = kset(p, t);
    g += stepf(2.4f, k);
    if (x * x + y * y < 0.0324f) {
      g += 0.35f;
    }
    d = 0.03f + k * 0.02f;
    if (d > 0.16f) {
      d = 0.16f;
    }
    if (g > 17.0f) {
      break;
    }
  }
  return clamp01(g * 0.075f);
}

float sampleShader1(float u, float v, float t) {
  float tilt = 1.0f - v * 0.45f;
  if (tilt < 0.3f) {
    tilt = 0.3f;
  }
  float uu = u / tilt;
  Vec3 from = {0.0f, 0.0f, -10.0f};
  Vec3 dir = {uu, v, 1.0f};
  float invLen = 1.0f / sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  dir.x *= invLen;
  dir.y *= invLen;
  dir.z *= invLen;
  return march(from, dir, t);
}

float sampleShader2(float u, float v, float t) {
  float angle = atan2f(v, u);
  float radial = sqrtf(u * u + v * v);
  float stage = sinf(-t * 7.0f + 10.0f * (radial * 8.0f + 0.2f * angle));
  return 0.5f + 0.5f * stage;
}

float sampleShader3(float u, float v, float t) {
  float accum = 0.0f;
  for (int i = 0; i < kShader3LineCount; i++) {
    float fi = static_cast<float>(i);
    float wave = sinf(fi / 11.0f + t * 0.65f);
    float y = -0.10f - fi * fi * (2.2f / (9.0f * 9.0f * 300.0f));
    float x = u * 4.4f + wave * fi * (1.7f * 3.3f / 300.0f);
    y += wave * 0.165f * (cosf(x * kPi) + 1.0f) * stepf(-1.0f, x) *
         stepf(x, 1.0f);
    float width = 0.012f + fi * 0.00010f;
    accum += 1.0f - smoothstepf(width, width + 0.020f, fabsf(v - y));
  }
  return clamp01(accum * 0.095f);
}

float sampleShader4(float u, float v, float t) {
  Vec2 uv = {u * 0.5f, v * 0.5f};
  float circle = smoothstepf(0.42f, 0.10f, sqrtf(uv.x * uv.x + uv.y * uv.y));
  int32_t cellX = static_cast<int32_t>(floorf(uv.x * 30.0f));
  int32_t cellY = static_cast<int32_t>(floorf(uv.y * 30.0f));
  float dimg = hash12(cellX, cellY);
  float weight = 0.5f + 0.5f * sinf(t * 0.2f);
  float disp = sinf((uv.x + dimg * 2.0f) * 10.0f + t * 0.9f) * 0.2f * weight;
  Vec2 duv = {uv.x, uv.y + disp};
  float r = sqrtf(duv.x * duv.x + duv.y * duv.y);
  float a = atan2f(duv.y, duv.x);
  float radial = 0.5f + 0.5f * sinf(r * 28.0f - t * 0.45f + a * 5.0f);
  radial += (hash12(static_cast<int32_t>(floorf(u * 42.0f)),
                    static_cast<int32_t>(floorf(v * 42.0f))) -
             0.5f) *
            0.12f;
  return circle * smoothstepf(0.90f, 1.0f, radial);
}

float sampleShader5(float u, float v, float t) {
  float period = 5.2f;
  float x = fmodf(t / period, 1.0f);
  if (x < 0.0f) {
    x += 1.0f;
  }
  float smoothT = 0.5f - cosf(clamp01(x) * kPi) * 0.5f;
  Vec2 uv = {u * 1.8f, v * 1.8f};
  float blur = 0.012f;
  float accum = 0.0f;
  for (int i = 0; i < 9; i++) {
    float n = static_cast<float>(i);
    float size = 1.0f - n / 9.0f;
    float rot = (n * 0.5f + 0.25f) * kPi * 2.0f * smoothT * 0.58f;
    Vec2 p = rotate2(uv, -rot);
    float dOuter = fmaxf(fabsf(p.x), fabsf(p.y)) - size;
    float outer = 1.0f - smoothstepf(-blur, blur, dOuter);
    float blackOffset = (0.25f + (0.5f - 0.25f) * (n / 9.0f)) / 9.0f;
    Vec2 q = rotate2(uv, -(rot + kPi * 0.5f * smoothT));
    float dInner = fmaxf(fabsf(q.x), fabsf(q.y)) - (size - blackOffset);
    float inner = 1.0f - smoothstepf(-blur, blur, dInner);
    float layer = outer * (1.0f - inner);
    if (layer > accum) {
      accum = layer;
    }
  }
  return clamp01(accum);
}

float sampleShader6(float u, float v, float t) {
  float rho = sqrtf(u * u + v * v);
  float theta = atan2f(v, u);
  float alpha = 0.1f;
  float drive = 0.5f + 0.5f * sinf(t * 0.16f);
  float beta = 1.0f + (drive + 0.19f) * (drive + 0.19f) * 2.5f;
  float gamma = (0.5f + 0.5f * sinf(t * 0.14f)) * 2.0f * kPi;
  int arms = 5;
  int whiteMeshFactor = 1;
  int spiralSize = 2;

  float logBeta = logf(beta);
  float betaPrime = expf(-1.0f / logBeta);
  int parallelArms = arms * whiteMeshFactor;
  int orthArms =
      static_cast<int>(roundf(static_cast<float>(parallelArms) / logBeta));
  if (orthArms < 1) {
    orthArms = 1;
  }
  if (orthArms > 10) {
    orthArms = 10;
  }

  float mesh = 0.0f;
  for (int arm = 0; arm < parallelArms; arm++) {
    float armOffset =
        (static_cast<float>(arm) / static_cast<float>(parallelArms)) *
        2.0f * kPi;
    for (int k = -spiralSize; k <= spiralSize; k++) {
      float phase = theta + gamma + (static_cast<float>(k) * 2.0f * kPi) +
                    armOffset;
      float target = alpha * expf(logBeta * phase);
      float d = fabsf(rho - target);
      mesh += expf(-d * 22.0f);
    }
  }

  float logBetaPrime = logf(betaPrime);
  for (int arm = 0; arm < orthArms; arm++) {
    float armOffset = (static_cast<float>(arm) / static_cast<float>(orthArms)) *
                      2.0f * kPi;
    for (int k = -spiralSize; k <= spiralSize; k++) {
      float phase = theta + gamma + (static_cast<float>(k) * 2.0f * kPi) +
                    armOffset;
      float target = alpha * expf(logBetaPrime * phase);
      float d = fabsf(rho - target);
      mesh += expf(-d * 20.0f) * 0.80f;
    }
  }

  float bulbR = rho / 0.1f;
  float bulb = expf(-(bulbR * bulbR * bulbR));
  return clamp01(mesh * 0.16f + bulb * 0.40f);
}

float sampleShader7(float u, float v, float t) {
  float d2 = u * u + v * v;
  float d = sqrtf(d2);
  if (d >= 1.0f) {
    return 0.0f;
  }
  if (d >= 0.99f) {
    return 1.0f;
  }

  float z = sqrtf(fmaxf(0.0f, 1.0f - d2));
  float sunX = cosf(t * 0.32f);
  float sunZ = sinf(t * 0.32f);
  float a = u * sunX + z * sunZ;
  float brightness = clamp01(a);
  return 1.0f - brightness;
}

float sampleShaderByIndex(uint8_t shaderIndex, float u, float v, float t) {
  switch (shaderIndex) {
  case 0:
    return sampleShader1(u, v, t);
  case 1:
    return sampleShader2(u, v, t);
  case 2:
    return sampleShader3(u, v, t);
  case 3:
    return sampleShader4(u, v, t);
  case 4:
    return sampleShader5(u, v, t);
  case 5:
    return sampleShader6(u, v, t);
  case 6:
    return sampleShader7(u, v, t);
  default:
    return 0.0f;
  }
}

inline void drawBlackPixel(Adafruit_GFX &gfx, AppDisplay *fastDisplay,
                           int16_t x, int16_t y) {
  if (fastDisplay != nullptr) {
    fastDisplay->drawBlackPixelUnchecked(x, y);
  } else {
    gfx.drawPixel(x, y, 1);
  }
}

void drawShader3Fast(Adafruit_GFX &gfx, AppDisplay *fastDisplay, float t) {
  std::memset(shader3Accum, 0, sizeof(shader3Accum));

  const float invWidth = 1.0f / static_cast<float>(EPD_WIDTH);
  const float invHeight = 1.0f / static_cast<float>(EPD_HEIGHT);
  const float aspect =
      static_cast<float>(EPD_WIDTH) / static_cast<float>(EPD_HEIGHT);

  for (int i = 0; i < kShader3LineCount; i++) {
    float fi = static_cast<float>(i);
    float wave = sinf(fi / 11.0f + t * 0.65f);
    float baseY = -0.10f - fi * fi * (2.2f / (9.0f * 9.0f * 300.0f));
    float xOffset = wave * fi * (1.7f * 3.3f / 300.0f);
    float yAmp = wave * 0.165f;
    float width = 0.012f + fi * 0.00010f;
    float edge = width + 0.020f;
    int halfBandPx =
        static_cast<int>(ceilf(edge * static_cast<float>(EPD_HEIGHT) * 0.5f)) +
        1;

    for (int16_t x = 0; x < EPD_WIDTH; x++) {
      float fx = (static_cast<float>(x) + 0.5f) * invWidth;
      float u = (fx * 2.0f - 1.0f) * aspect;
      float lineX = u * 4.4f + xOffset;
      if (lineX < -1.0f || lineX > 1.0f) {
        continue;
      }

      float lineV = baseY + yAmp * (cosf(lineX * kPi) + 1.0f);
      float centerY =
          ((1.0f - lineV) * static_cast<float>(EPD_HEIGHT) - 1.0f) * 0.5f;
      int yStart = static_cast<int>(floorf(centerY)) - halfBandPx;
      int yEnd = static_cast<int>(ceilf(centerY)) + halfBandPx;
      if (yEnd < 0 || yStart >= EPD_HEIGHT) {
        continue;
      }
      if (yStart < 0) {
        yStart = 0;
      }
      if (yEnd >= EPD_HEIGHT) {
        yEnd = EPD_HEIGHT - 1;
      }

      for (int16_t y = static_cast<int16_t>(yStart);
           y <= static_cast<int16_t>(yEnd); y++) {
        float fy = (static_cast<float>(y) + 0.5f) * invHeight;
        float v = -(fy * 2.0f - 1.0f);
        float coverage = 1.0f - smoothstepf(width, edge, fabsf(v - lineV));
        if (coverage <= 0.0f) {
          continue;
        }

        int index = y * EPD_WIDTH + x;
        int shade = shader3Accum[index] + static_cast<int>(coverage * 24.0f);
        shader3Accum[index] = shade > 255 ? 255 : static_cast<uint8_t>(shade);
      }
    }
  }

  for (int16_t y = 0; y < EPD_HEIGHT; y++) {
    for (int16_t x = 0; x < EPD_WIDTH; x++) {
      uint8_t shade = shader3Accum[y * EPD_WIDTH + x];
      if (shade == 0) {
        continue;
      }
      if (static_cast<float>(shade) > bayer4(x, y) * 255.0f) {
        drawBlackPixel(gfx, fastDisplay, x, y);
      }
    }
  }
}
} // namespace

void GfxApp::reset() {
  shaderIndex = 0;
  running = false;
  lastFrameAt = 0;
  timeSec = 0.0f;
  frame = 0;
}

void GfxApp::start(AppDisplay &targetDisplay) {
  display = &targetDisplay;
  running = true;
  lastFrameAt = millis();
  timeSec = 0.0f;
  frame = 0;
  display->requestFullRefresh();
}

bool GfxApp::hasActiveSession() const { return running; }

uint16_t GfxApp::frameIntervalMs() const {
  switch (resolveShaderSlot(shaderIndex)) {
  case 0:
    return 650;
  case 1:
    return 260;
  case 2:
    return 340;
  case 3:
    return 320;
  case 4:
    return 320;
  case 5:
    return 360;
  case 6:
    return 560;
  case 7:
    return 36;
  default:
    return 260;
  }
}

bool GfxApp::update() {
  if (!running) {
    return false;
  }
  unsigned long now = millis();
  if (lastFrameAt == 0) {
    lastFrameAt = now;
    return true;
  }
  uint16_t interval = frameIntervalMs();
  if (now - lastFrameAt < interval) {
    return false;
  }
  float dt = static_cast<float>(now - lastFrameAt) * 0.001f;
  if (shaderIndex == 4) {
    if (dt > 0.14f) {
      dt = 0.14f;
    }
    dt *= 0.75f;
  }
  lastFrameAt = now;
  timeSec += dt;
  frame++;
  return true;
}

bool GfxApp::handleTouch(const TouchPoint &point) {
  (void)point;
  return false;
}

bool GfxApp::handlePowerButton() {
  if (!running) {
    return false;
  }
  shaderIndex = (shaderIndex + 1) % SHADER_COUNT;
  timeSec = 0.0f;
  frame = 0;
  lastFrameAt = millis();
  if (display != nullptr) {
    display->requestFullRefresh();
  }
  return true;
}

float GfxApp::sampleShader(int16_t x, int16_t y, float t) const {
  float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(EPD_WIDTH);
  float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(EPD_HEIGHT);
  float u = fx * 2.0f - 1.0f;
  float v = -(fy * 2.0f - 1.0f);
  u *= static_cast<float>(EPD_WIDTH) / static_cast<float>(EPD_HEIGHT);

  return sampleShaderByIndex(resolveShaderSlot(shaderIndex), u, v, t);
}

void GfxApp::drawShader(Adafruit_GFX &gfx) {
  uint8_t shader = resolveShaderSlot(shaderIndex);
  if (shader == 7) {
    drawCubeShader(gfx);
    return;
  }

  AppDisplay *fastDisplay =
      display != nullptr && &gfx == static_cast<Adafruit_GFX *>(display)
          ? display
          : nullptr;

  if (shader == 2) {
    drawShader3Fast(gfx, fastDisplay, timeSec);
    return;
  }

  if (shader == 6) {
    const float sunX = cosf(timeSec * 0.32f);
    const float sunZ = sinf(timeSec * 0.32f);
    const float invWidth = 1.0f / static_cast<float>(EPD_WIDTH);
    const float invHeight = 1.0f / static_cast<float>(EPD_HEIGHT);

    for (int16_t y = 0; y < EPD_HEIGHT; y++) {
      float fy = (static_cast<float>(y) + 0.5f) * invHeight;
      float v = -(fy * 2.0f - 1.0f);
      for (int16_t x = 0; x < EPD_WIDTH; x++) {
        float fx = (static_cast<float>(x) + 0.5f) * invWidth;
        float u = fx * 2.0f - 1.0f;
        float d2 = u * u + v * v;

        if (d2 < 0.9801f) {
          float z = sqrtf(1.0f - d2);
          float blackness = 1.0f - clamp01(u * sunX + z * sunZ);
          if (blackness > bayer8(x, y)) {
            drawBlackPixel(gfx, fastDisplay, x, y);
          }
        } else if (d2 < 1.0f) {
          drawBlackPixel(gfx, fastDisplay, x, y);
        }
      }
    }
    return;
  }

  int step = 2;
  if (shader == 5) {
    step = 3;
  }
  const float invWidth = 1.0f / static_cast<float>(EPD_WIDTH);
  const float invHeight = 1.0f / static_cast<float>(EPD_HEIGHT);
  const float aspect =
      static_cast<float>(EPD_WIDTH) / static_cast<float>(EPD_HEIGHT);

  for (int16_t y = 0; y < EPD_HEIGHT; y += step) {
    float fy = (static_cast<float>(y) + 0.5f) * invHeight;
    float v = -(fy * 2.0f - 1.0f);
    for (int16_t x = 0; x < EPD_WIDTH; x += step) {
      float fx = (static_cast<float>(x) + 0.5f) * invWidth;
      float u = (fx * 2.0f - 1.0f) * aspect;
      float blackness = sampleShaderByIndex(shader, u, v, timeSec);
      float threshold = shader == 4 ? bayer8(x, y) : bayer4(x, y);
      if (blackness <= threshold) {
        continue;
      }
      if (fastDisplay != nullptr && x + 1 < EPD_WIDTH && y + 1 < EPD_HEIGHT) {
        fastDisplay->drawBlackBlock2x2Unchecked(x, y);
        continue;
      }
      gfx.drawPixel(x, y, 1);
      if (x + 1 < EPD_WIDTH) {
        gfx.drawPixel(x + 1, y, 1);
      }
      if (y + 1 < EPD_HEIGHT) {
        gfx.drawPixel(x, y + 1, 1);
      }
      if (x + 1 < EPD_WIDTH && y + 1 < EPD_HEIGHT) {
        gfx.drawPixel(x + 1, y + 1, 1);
      }
    }
  }
}

void GfxApp::draw(Adafruit_GFX &gfx) {
  drawShader(gfx);
}

void GfxApp::drawCubeShader(Adafruit_GFX &gfx) {
  static const Vec3 vertices[8] = {
      {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f},
      {-1.0f, 1.0f, -1.0f},  {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f},
      {1.0f, 1.0f, 1.0f},    {-1.0f, 1.0f, 1.0f}};
  static const uint8_t edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                       {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                       {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  static const int16_t centerX = EPD_WIDTH / 2;
  static const int16_t centerY = EPD_HEIGHT / 2 + 4;
  static const float cubeSize = 46.0f;
  static const float cameraDistance = 3.2f;
  static const float projectionScale = 70.0f;

  float angle = timeSec * 1.2f;
  float sinY = sinf(angle);
  float cosY = cosf(angle);
  float sinX = sinf(angle * 0.63f);
  float cosX = cosf(angle * 0.63f);
  Point2D projected[8];

  for (int i = 0; i < 8; i++) {
    float x = vertices[i].x * cubeSize;
    float y = vertices[i].y * cubeSize;
    float z = vertices[i].z * cubeSize;

    float rotatedX = x * cosY - z * sinY;
    float rotatedZ = x * sinY + z * cosY;
    float rotatedY = y * cosX - rotatedZ * sinX;
    rotatedZ = y * sinX + rotatedZ * cosX;

    float depth = cameraDistance + rotatedZ / cubeSize;
    float scale = projectionScale / depth;
    projected[i].x = centerX + static_cast<int16_t>(rotatedX / cubeSize * scale);
    projected[i].y = centerY + static_cast<int16_t>(rotatedY / cubeSize * scale);
  }

  for (int i = 0; i < 12; i++) {
    Point2D a = projected[edges[i][0]];
    Point2D b = projected[edges[i][1]];
    gfx.drawLine(a.x, a.y, b.x, b.y, 1);
  }

  for (int i = 0; i < 8; i++) {
    gfx.fillRect(projected[i].x - 1, projected[i].y - 1, 3, 3, 1);
  }
}
