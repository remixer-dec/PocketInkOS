#include "touch_input.h"
#include "global.h"
#include <Wire.h>

#define FT6336_ADDR 0x38

void TouchInput::begin() {
  Wire.begin(ESP32_I2C_SDA, ESP32_I2C_SCL, 400000U);
  pinMode(EPD_TP_RST_PIN, OUTPUT);
  pinMode(EPD_TP_INT_PIN, INPUT_PULLUP);
  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(50);
  digitalWrite(EPD_TP_RST_PIN, LOW);
  delay(50);
  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(100);
}

bool TouchInput::read(TouchPoint &point) {
  bool down = digitalRead(EPD_TP_INT_PIN) == LOW;
  if (!down) {
    wasDown = false;
    return false;
  }
  if (wasDown) {
    return false;
  }
  wasDown = true;

  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.requestFrom(FT6336_ADDR, 5);
  if (Wire.available() != 5) {
    return false;
  }

  uint8_t count = Wire.read() & 0x0F;
  uint8_t xMsb = Wire.read();
  uint8_t xLsb = Wire.read();
  uint8_t yMsb = Wire.read();
  uint8_t yLsb = Wire.read();
  if (count == 0) {
    return false;
  }

  point.x = ((xMsb & 0x0F) << 8) | xLsb;
  point.y = ((yMsb & 0x0F) << 8) | yLsb;
  return point.x < EPD_WIDTH && point.y < EPD_HEIGHT;
}
