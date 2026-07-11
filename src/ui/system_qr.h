#ifndef SYSTEM_QR_H
#define SYSTEM_QR_H

#include <Adafruit_GFX.h>
#include <stdint.h>

/// Fixed-size QR version 4-Q encoder used by the built-in app and external
/// runtimes. Payloads are byte-mode ASCII/UTF-8 bytes up to maxPayload bytes.
class SystemQrCode {
public:
  static constexpr int moduleCount = 33;
  static constexpr int maxPayload = 46;

  bool encode(const char *text);
  void clear();
  bool darkModule(int row, int column) const {
    return row >= 0 && row < moduleCount && column >= 0 &&
           column < moduleCount && modules[row][column];
  }
  void draw(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t size,
            uint16_t color = 1) const;

private:
  bool modules[moduleCount][moduleCount] = {};
};

#endif
