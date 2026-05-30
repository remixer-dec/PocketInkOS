#include "games/cube_app.h"

#include <Arduino.h>
#include <cmath>

static const int CENTER_X = 100;
static const int CENTER_Y = 104;
static const float CUBE_SIZE = 46.0f;
static const float CAMERA_DISTANCE = 3.2f;
static const float PROJECTION_SCALE = 70.0f;

struct Point2D {
  int x;
  int y;
};

struct Point3D {
  float x;
  float y;
  float z;
};

void CubeApp::reset() {
  angle = 0.0f;
  frame = 0;
  started = false;
}

void CubeApp::advance() {
  angle += 0.16f;
  frame++;
}

bool CubeApp::handleTouch(const TouchPoint &point) {
  (void)point;
  started = true;
  advance();
  return true;
}

bool CubeApp::hasActiveSession() const { return started; }

void CubeApp::draw(Adafruit_GFX &gfx) {
  static const Point3D vertices[8] = {
      {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
      {1.0f, 1.0f, -1.0f},   {-1.0f, 1.0f, -1.0f},
      {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},
      {1.0f, 1.0f, 1.0f},    {-1.0f, 1.0f, 1.0f}};
  static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                   {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                   {0, 4}, {1, 5}, {2, 6}, {3, 7}};

  const float sinY = sin(angle);
  const float cosY = cos(angle);
  const float sinX = sin(angle * 0.63f);
  const float cosX = cos(angle * 0.63f);
  Point2D projected[8];

  for (int i = 0; i < 8; i++) {
    float x = vertices[i].x * CUBE_SIZE;
    float y = vertices[i].y * CUBE_SIZE;
    float z = vertices[i].z * CUBE_SIZE;

    float rotatedX = x * cosY - z * sinY;
    float rotatedZ = x * sinY + z * cosY;
    float rotatedY = y * cosX - rotatedZ * sinX;
    rotatedZ = y * sinX + rotatedZ * cosX;

    float depth = CAMERA_DISTANCE + rotatedZ / CUBE_SIZE;
    float scale = PROJECTION_SCALE / depth;
    projected[i].x = CENTER_X + (int)(rotatedX / CUBE_SIZE * scale);
    projected[i].y = CENTER_Y + (int)(rotatedY / CUBE_SIZE * scale);
  }

  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(6, 5);
  gfx.print("CUBE REFRESH");
  gfx.setCursor(154, 5);
  gfx.print("F");
  gfx.print((int)(frame % 1000));

  gfx.drawRect(0, 0, 200, 200, 1);
  for (int i = 0; i < 12; i++) {
    Point2D a = projected[edges[i][0]];
    Point2D b = projected[edges[i][1]];
    gfx.drawLine(a.x, a.y, b.x, b.y, 1);
  }

  for (int i = 0; i < 8; i++) {
    gfx.fillRect(projected[i].x - 2, projected[i].y - 2, 5, 5, 1);
  }
}
