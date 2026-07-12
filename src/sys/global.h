/// Central board definition for device hardware: GPIO assignments, panel
/// dimensions, SPI selection, and touch orientation. Drivers use these constants
/// so board wiring is not duplicated throughout the firmware.
#ifndef GLOBAL_H
#define GLOBAL_H

#include "Arduino.h"

// Physical buttons.
#define BOOT_BUTTON_PIN GPIO_NUM_0
#define PWR_BUTTON_PIN GPIO_NUM_18

// Battery measurement and divider control.
#define BAT_ADC_PIN 4
#define BAT_CTRL_PIN 17

// Audio amplifier and e-paper SPI wiring.
#define AUDIO_PWR_PIN 42
#define PA_CTRL_PIN 46
#define EPD_PWR_PIN 6
#define EPD_CS_PIN 11
#define EPD_DC_PIN 10
#define EPD_RST_PIN 9
#define EPD_BUSY_PIN 8
#define EPD_SCK_PIN 12
#define EPD_MOSI_PIN 13
#define EPD_SPI_HOST SPI2_HOST

// Touch controller reset/interrupt and the shared I2C bus.
#define EPD_TP_RST_PIN 7
#define EPD_TP_INT_PIN 21
#define ESP32_I2C_SDA 47
#define ESP32_I2C_SCL 48

// SD card's one-bit SD_MMC wiring.
#define SD_MMC_CLK_PIN 39
#define SD_MMC_D0_PIN 40
#define SD_MMC_CMD_PIN 41

// Native panel framebuffer dimensions.
#define EPD_WIDTH 200
#define EPD_HEIGHT 200

// Coordinate transform from the touch controller into display orientation.
#define TOUCH_SWAP_XY 0
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 0

#endif
