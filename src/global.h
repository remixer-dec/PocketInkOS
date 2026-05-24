#ifndef GLOBAL_H
#define GLOBAL_H

#include "Arduino.h"

#define BOOT_BUTTON_PIN GPIO_NUM_0
#define PWR_BUTTON_PIN GPIO_NUM_18

#define EPD_PWR_PIN 6
#define EPD_CS_PIN 11
#define EPD_DC_PIN 10
#define EPD_RST_PIN 9
#define EPD_BUSY_PIN 8
#define EPD_SCK_PIN 12
#define EPD_MOSI_PIN 13
#define EPD_SPI_HOST SPI2_HOST

#define EPD_TP_RST_PIN 7
#define EPD_TP_INT_PIN 21
#define ESP32_I2C_SDA 47
#define ESP32_I2C_SCL 48

#define EPD_WIDTH 200
#define EPD_HEIGHT 200

#endif
