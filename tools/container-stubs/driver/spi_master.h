#pragma once
#include "Arduino.h"
typedef int esp_err_t;
#define ESP_OK 0
typedef void* spi_device_handle_t;
typedef int spi_host_device_t;
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 0
typedef struct { int miso_io_num; int mosi_io_num; int sclk_io_num; int quadwp_io_num; int quadhd_io_num; int max_transfer_sz; } spi_bus_config_t;
typedef struct { int spics_io_num; int clock_speed_hz; int mode; int queue_size; } spi_device_interface_config_t;
typedef struct { int flags; int length; const void *tx_buffer; uint8_t tx_data[4]; } spi_transaction_t;
inline esp_err_t spi_bus_initialize(spi_host_device_t, const spi_bus_config_t *, int) { return ESP_OK; }
inline esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t *, spi_device_handle_t *out) { *out = nullptr; return ESP_OK; }
inline esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t *) { return ESP_OK; }
