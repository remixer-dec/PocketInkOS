#include "ui/system_qr.h"

#include <cstdlib>
#include <cstring>

namespace {

constexpr int kDataCodewords = 48;
constexpr int kEccCodewords = 26;
constexpr int kBlocks = 2;
constexpr int kBlockDataCodewords = kDataCodewords / kBlocks;
constexpr int kTotalCodewords = kDataCodewords + kEccCodewords * kBlocks;
constexpr int kQuietModules = 4;
constexpr int kModuleStorageBytes =
    (SystemQrCode::moduleCount * SystemQrCode::moduleCount + 7) / 8;

bool packedBit(const uint8_t *bits, int row, int column) {
  const int index = row * SystemQrCode::moduleCount + column;
  return (bits[index / 8] & (uint8_t(1U) << (index % 8))) != 0;
}

void setPackedBit(uint8_t *bits, int row, int column, bool value) {
  const int index = row * SystemQrCode::moduleCount + column;
  const uint8_t mask = uint8_t(1U) << (index % 8);
  if (value) {
    bits[index / 8] |= mask;
  } else {
    bits[index / 8] &= static_cast<uint8_t>(~mask);
  }
}

uint8_t gfMul(uint8_t x, uint8_t y) {
  uint8_t result = 0;
  while (y) {
    if (y & 1U) {
      result ^= x;
    }
    const bool carry = (x & 0x80U) != 0;
    x <<= 1U;
    if (carry) {
      x ^= 0x1dU;
    }
    y >>= 1U;
  }
  return result;
}

void reedSolomon(const uint8_t *data, uint8_t *ecc) {
  uint8_t generator[kEccCodewords] = {};
  generator[kEccCodewords - 1] = 1;
  uint8_t root = 1;
  for (int i = 0; i < kEccCodewords; i++) {
    for (int j = 0; j < kEccCodewords; j++) {
      generator[j] = gfMul(generator[j], root);
      if (j + 1 < kEccCodewords) {
        generator[j] ^= generator[j + 1];
      }
    }
    root = gfMul(root, 2);
  }

  memset(ecc, 0, kEccCodewords);
  for (int i = 0; i < kBlockDataCodewords; i++) {
    const uint8_t factor = data[i] ^ ecc[0];
    memmove(ecc, ecc + 1, kEccCodewords - 1);
    ecc[kEccCodewords - 1] = 0;
    for (int j = 0; j < kEccCodewords; j++) {
      ecc[j] ^= gfMul(generator[j], factor);
    }
  }
}

} // namespace

void SystemQrCode::clear() { memset(modules, 0, sizeof(modules)); }

bool SystemQrCode::encode(const char *text) {
  const size_t length = text != nullptr ? strlen(text) : 0;
  if (length == 0 || length > maxPayload) {
    clear();
    return false;
  }

  uint8_t reserved[kModuleStorageBytes] = {};
  clear();
  auto setModule = [&](int row, int col, bool value, bool reserve) {
    if (row < 0 || row >= moduleCount || col < 0 || col >= moduleCount) {
      return;
    }
    setPackedBit(modules, row, col, value);
    if (reserve) {
      setPackedBit(reserved, row, col, true);
    }
  };
  auto finder = [&](int row, int col) {
    for (int y = -1; y <= 7; y++) {
      for (int x = -1; x <= 7; x++) {
        const bool dark =
            x >= 0 && x <= 6 && y >= 0 && y <= 6 &&
            (x == 0 || x == 6 || y == 0 || y == 6 ||
             (x >= 2 && x <= 4 && y >= 2 && y <= 4));
        setModule(row + y, col + x, dark, true);
      }
    }
  };

  finder(0, 0);
  finder(0, moduleCount - 7);
  finder(moduleCount - 7, 0);
  for (int i = 8; i < moduleCount - 8; i++) {
    setModule(6, i, i % 2 == 0, true);
    setModule(i, 6, i % 2 == 0, true);
  }
  for (int y = -2; y <= 2; y++) {
    for (int x = -2; x <= 2; x++) {
      const int absX = abs(x);
      const int absY = abs(y);
      const int distance = absX > absY ? absX : absY;
      setModule(moduleCount - 7 + y, moduleCount - 7 + x, distance != 1,
                true);
    }
  }
  setModule(moduleCount - 8, 8, true, true);
  for (int i = 0; i < 9; i++) {
    if (i != 6) {
      setPackedBit(reserved, 8, i, true);
      setPackedBit(reserved, i, 8, true);
    }
  }
  for (int i = 0; i < 8; i++) {
    setPackedBit(reserved, 8, moduleCount - 1 - i, true);
    setPackedBit(reserved, moduleCount - 1 - i, 8, true);
  }

  uint8_t data[kDataCodewords] = {};
  int bitLength = 0;
  auto appendBits = [&](uint32_t value, int count) {
    for (int i = count - 1; i >= 0; i--) {
      if (bitLength / 8 < kDataCodewords && ((value >> i) & 1U) != 0) {
        data[bitLength / 8] |= static_cast<uint8_t>(0x80U >> (bitLength % 8));
      }
      bitLength++;
    }
  };
  appendBits(0x4, 4);
  appendBits(static_cast<uint32_t>(length), 8);
  for (size_t i = 0; i < length; i++) {
    appendBits(static_cast<uint8_t>(text[i]), 8);
  }
  const int capacityBits = kDataCodewords * 8;
  appendBits(0, capacityBits - bitLength < 4 ? capacityBits - bitLength : 4);
  while (bitLength % 8 != 0) {
    appendBits(0, 1);
  }
  constexpr uint8_t pads[2] = {0xec, 0x11};
  int padIndex = 0;
  while (bitLength / 8 < kDataCodewords) {
    appendBits(pads[padIndex], 8);
    padIndex = 1 - padIndex;
  }

  uint8_t all[kTotalCodewords] = {};
  uint8_t ecc[kBlocks][kEccCodewords] = {};
  for (int block = 0; block < kBlocks; block++) {
    reedSolomon(data + block * kBlockDataCodewords, ecc[block]);
  }
  int write = 0;
  for (int i = 0; i < kBlockDataCodewords; i++) {
    for (int block = 0; block < kBlocks; block++) {
      all[write++] = data[block * kBlockDataCodewords + i];
    }
  }
  for (int i = 0; i < kEccCodewords; i++) {
    for (int block = 0; block < kBlocks; block++) {
      all[write++] = ecc[block][i];
    }
  }

  int bit = 0;
  int direction = -1;
  for (int col = moduleCount - 1; col > 0; col -= 2) {
    if (col == 6) {
      col--;
    }
    int row = direction < 0 ? moduleCount - 1 : 0;
    while (row >= 0 && row < moduleCount) {
      for (int offset = 0; offset < 2; offset++) {
        const int currentCol = col - offset;
        if (!packedBit(reserved, row, currentCol)) {
          bool value = bit < kTotalCodewords * 8 &&
                       (all[bit / 8] & (0x80U >> (bit % 8))) != 0;
          if ((row + currentCol) % 2 == 0) {
            value = !value;
          }
          setPackedBit(modules, row, currentCol, value);
          bit++;
        }
      }
      row += direction;
    }
    direction = -direction;
  }

  constexpr uint16_t format = 0x355f;
  for (int i = 0; i < 15; i++) {
    const bool value = ((format >> i) & 1U) != 0;
    if (i < 6) {
      setModule(i, 8, value, true);
    } else if (i < 8) {
      setModule(i + 1, 8, value, true);
    } else {
      setModule(moduleCount - 15 + i, 8, value, true);
    }
  }
  for (int i = 0; i < 15; i++) {
    const bool value = ((format >> i) & 1U) != 0;
    if (i < 8) {
      setModule(8, moduleCount - i - 1, value, true);
    } else if (i < 9) {
      setModule(8, 7, value, true);
    } else {
      setModule(8, 14 - i, value, true);
    }
  }
  return true;
}

void SystemQrCode::draw(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t size,
                        uint16_t color) const {
  if (size <= 0) {
    return;
  }
  gfx.fillRect(x, y, size, size, color == 0 ? 1 : 0);
  const int totalModules = moduleCount + kQuietModules * 2;
  for (int row = 0; row < moduleCount; row++) {
    for (int col = 0; col < moduleCount; col++) {
      if (!darkModule(row, col)) {
        continue;
      }
      const int16_t left =
          x + static_cast<int16_t>(((col + kQuietModules) * size) /
                                   totalModules);
      const int16_t top =
          y + static_cast<int16_t>(((row + kQuietModules) * size) /
                                   totalModules);
      const int16_t right =
          x + static_cast<int16_t>(((col + kQuietModules + 1) * size) /
                                   totalModules);
      const int16_t bottom =
          y + static_cast<int16_t>(((row + kQuietModules + 1) * size) /
                                   totalModules);
      gfx.fillRect(left, top, right - left, bottom - top, color);
    }
  }
}
