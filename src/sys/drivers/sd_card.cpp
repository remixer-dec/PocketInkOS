#include "sys/drivers/sd_card.h"

#include "sys/global.h"

#include <SD_MMC.h>

bool sdCardMount() {
  SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN);
  return SD_MMC.begin("/sdcard", true) && SD_MMC.cardType() != CARD_NONE;
}

void sdCardUnmount() { SD_MMC.end(); }

bool sdCardPresent() { return SD_MMC.cardType() != CARD_NONE; }

SdCardCapacity sdCardCapacity() {
  SdCardCapacity capacity;
  capacity.totalBytes = SD_MMC.totalBytes();
  capacity.usedBytes = SD_MMC.usedBytes();
  return capacity;
}
