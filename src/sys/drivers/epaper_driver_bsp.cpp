#include "sys/drivers/epaper_driver_bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "driver";

const uint8_t WF_Full_1IN54[159] = {
    0x80, 0x48, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x40,
    0x48, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x80, 0x48,
    0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x40, 0x48, 0x80,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0xA, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x8,  0x1,  0x0,  0x8,  0x1,  0x0, 0x2, 0xA, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0, 0x22, 0x17, 0x41,
    0x0,  0x32, 0x20};

unsigned char WF_PARTIAL_1IN54_0[159] = {
    0x0,  0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x80,
    0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x40, 0x40,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x80, 0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0xF, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x1,  0x1,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0,  0x0,
    0x0,  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0, 0x02, 0x17, 0x41,
    0xB0, 0x32, 0x28,
};

epaper_driver_display::epaper_driver_display(int width, int height,
                                             custom_lcd_spi_t _lcd_spi_data)
    : lcd_spi_data(_lcd_spi_data), Width(width), Height(height) {

  // CHANGED: Removed hardware init and malloc from here.
  // Constructors for global objects run BEFORE the system is ready.
  buffer = NULL;
}

epaper_driver_display::~epaper_driver_display() {}

void epaper_driver_display::spi_gpio_init() {
  int rst = lcd_spi_data.rst;
  int cs = lcd_spi_data.cs;
  int dc = lcd_spi_data.dc;
  int busy = lcd_spi_data.busy;

  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_conf.pin_bit_mask = (0x1ULL << rst) | (0x1ULL << dc) | (0x1ULL << cs);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

  gpio_conf.mode = GPIO_MODE_INPUT;
  gpio_conf.pin_bit_mask = (0x1ULL << busy);
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

  set_rst_1();
}

void epaper_driver_display::spi_port_init() {
  if (spi != NULL)
    return;
  int mosi = lcd_spi_data.mosi;
  int scl = lcd_spi_data.scl;
  int spi_host = lcd_spi_data.spi_host;
  esp_err_t ret;
  spi_bus_config_t buscfg = {};
  buscfg.miso_io_num = -1;
  buscfg.mosi_io_num = mosi;
  buscfg.sclk_io_num = scl;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = Width * Height;

  spi_device_interface_config_t devcfg = {};
  devcfg.spics_io_num = -1;
  devcfg.clock_speed_hz = 40 * 1000 * 1000; // Clock out at 10 MHz
  devcfg.mode = 0;                          // SPI mode 0
  devcfg.queue_size = 7; // We want to be able to queue 7 transactions at a time

  ret =
      spi_bus_initialize((spi_host_device_t)spi_host, &buscfg, SPI_DMA_CH_AUTO);
  ESP_ERROR_CHECK(ret);
  ret = spi_bus_add_device((spi_host_device_t)spi_host, &devcfg, &spi);
  ESP_ERROR_CHECK(ret);
}

void epaper_driver_display::read_busy() {
  int busy = lcd_spi_data.busy;
  uint16_t waits = 0;
  while (gpio_get_level((gpio_num_t)busy) == 1 && waits < 1000) {
    vTaskDelay(pdMS_TO_TICKS(5)); // LOW: idle, HIGH: busy
    waits++;
  }
  if (waits >= 1000) {
    ESP_LOGW(TAG, "EPD busy wait timed out");
  }
}

void epaper_driver_display::ensureBuffer() {
  if (buffer != NULL) {
    return;
  }

  // This 5 KB framebuffer is touched per pixel while rendering. Keep it in
  // internal RAM first; PSRAM is slower and not needed for a buffer this small.
  buffer = (uint8_t *)heap_caps_malloc(lcd_spi_data.buffer_len,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA |
                                           MALLOC_CAP_8BIT);

  // Fall back to PSRAM only if internal RAM is too fragmented or unavailable.
  if (buffer == NULL) {
    ESP_LOGW(TAG,
             "Internal framebuffer allocation failed, falling back to PSRAM");
    buffer = (uint8_t *)heap_caps_malloc(lcd_spi_data.buffer_len,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }

  assert(buffer);
  memset(buffer, 0xFF, lcd_spi_data.buffer_len);
}

void epaper_driver_display::SPI_SendByte(uint8_t data) {
  esp_err_t ret;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8;
  t.tx_buffer = &data;
  ret = spi_device_polling_transmit(spi, &t); // Transmit!
  assert(ret == ESP_OK);                      // Should have had no issues.
}

void epaper_driver_display::EPD_SendData(uint8_t data) {
  set_dc_1();
  set_cs_0();
  SPI_SendByte(data);
  set_cs_1();
}

void epaper_driver_display::EPD_SendCommand(uint8_t command) {
  set_dc_0();
  set_cs_0();
  SPI_SendByte(command);
  set_cs_1();
}

void epaper_driver_display::writeBytes(uint8_t *buffer, int len) {
  set_dc_1();
  set_cs_0();
  esp_err_t ret;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8 * len;
  t.tx_buffer = buffer;
  ret = spi_device_polling_transmit(spi, &t); // Transmit!
  assert(ret == ESP_OK);
  set_cs_1();
}

void epaper_driver_display::writeBytes(const uint8_t *buffer, int len) {
  set_dc_1();
  set_cs_0();
  esp_err_t ret;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8 * len;
  t.tx_buffer = buffer;
  ret = spi_device_polling_transmit(spi, &t); // Transmit!
  assert(ret == ESP_OK);
  set_cs_1();
}

void epaper_driver_display::EPD_SetWindows(uint16_t Xstart, uint16_t Ystart,
                                           uint16_t Xend, uint16_t Yend) {
  EPD_SendCommand(0x44); // SET_RAM_X_ADDRESS_START_END_POSITION
  EPD_SendData((Xstart >> 3) & 0xFF);
  EPD_SendData((Xend >> 3) & 0xFF);

  EPD_SendCommand(0x45); // SET_RAM_Y_ADDRESS_START_END_POSITION
  EPD_SendData(Ystart & 0xFF);
  EPD_SendData((Ystart >> 8) & 0xFF);
  EPD_SendData(Yend & 0xFF);
  EPD_SendData((Yend >> 8) & 0xFF);
}

void epaper_driver_display::EPD_SetCursor(uint16_t Xstart, uint16_t Ystart) {
  EPD_SendCommand(0x4E); // SET_RAM_X_ADDRESS_COUNTER
  EPD_SendData(Xstart & 0xFF);

  EPD_SendCommand(0x4F); // SET_RAM_Y_ADDRESS_COUNTER
  EPD_SendData(Ystart & 0xFF);
  EPD_SendData((Ystart >> 8) & 0xFF);
}

void epaper_driver_display::EPD_SetLut(const uint8_t *lut) {
  EPD_SendCommand(0x32);
  writeBytes(lut, 153);
  read_busy();

  EPD_SendCommand(0x3f);
  EPD_SendData(lut[153]);

  EPD_SendCommand(0x03);
  EPD_SendData(lut[154]);

  EPD_SendCommand(0x04);
  EPD_SendData(lut[155]);
  EPD_SendData(lut[156]);
  EPD_SendData(lut[157]);

  EPD_SendCommand(0x2c);
  EPD_SendData(lut[158]);
}

void epaper_driver_display::EPD_TurnOnDisplay() {
  EPD_SendCommand(0x22);
  EPD_SendData(0xc7);
  EPD_SendCommand(0x20);
  read_busy();
}

void epaper_driver_display::EPD_TurnOnDisplayPart() {
  EPD_SendCommand(0x22);
  EPD_SendData(0xcf);
  EPD_SendCommand(0x20);
  read_busy();
}

void epaper_driver_display::EPD_Init() {
  // 1. Initialize Hardware (moved from constructor)
  ESP_LOGI(TAG, "Initialize SPI");
  spi_port_init();
  spi_gpio_init();

  // 2. Allocate Memory (moved from constructor)
  ensureBuffer();

  // 3. Begin Reset Sequence
  set_rst_1();
  vTaskDelay(pdMS_TO_TICKS(50));
  set_rst_0();
  vTaskDelay(pdMS_TO_TICKS(20));
  set_rst_1();
  vTaskDelay(pdMS_TO_TICKS(50));

  read_busy();
  EPD_SendCommand(0x12); // SWRESET
  read_busy();

  EPD_SendCommand(0x01); // Driver output control
  EPD_SendData(0xC7);
  EPD_SendData(0x00);
  EPD_SendData(0x01);

  EPD_SendCommand(0x11); // Data entry mode
  EPD_SendData(0x01);

  EPD_SetWindows(0, Width - 1, Height - 1, 0);

  EPD_SendCommand(0x3C); // BorderWavefrom
  EPD_SendData(0x01);

  EPD_SendCommand(0x18);
  EPD_SendData(0x80);

  EPD_SendCommand(0x22); // Load Temperature and waveform setting.
  EPD_SendData(0XB1);
  EPD_SendCommand(0x20);

  EPD_SetCursor(0, Height - 1);
  read_busy();

  EPD_SetLut(WF_Full_1IN54);
}

void epaper_driver_display::EPD_InitColdPartial() {
  spi_port_init();
  spi_gpio_init();
  ensureBuffer();

  set_rst_1();
  vTaskDelay(pdMS_TO_TICKS(50));
  set_rst_0();
  vTaskDelay(pdMS_TO_TICKS(20));
  set_rst_1();
  vTaskDelay(pdMS_TO_TICKS(50));

  read_busy();
  EPD_SendCommand(0x12); // SWRESET
  read_busy();

  EPD_SendCommand(0x01); // Driver output control
  EPD_SendData(0xC7);
  EPD_SendData(0x00);
  EPD_SendData(0x01);

  EPD_SendCommand(0x11); // Data entry mode
  EPD_SendData(0x01);

  EPD_SetWindows(0, Width - 1, Height - 1, 0);
  EPD_SetCursor(0, Height - 1);

  EPD_SendCommand(0x3C); // BorderWavefrom
  EPD_SendData(0x80);

  EPD_SendCommand(0x18);
  EPD_SendData(0x80);

  EPD_SetLut(WF_PARTIAL_1IN54_0);
}

void epaper_driver_display::EPD_Clear() {
  int buffer_len = lcd_spi_data.buffer_len;
  memset(buffer, 0xff, buffer_len);
}

void epaper_driver_display::EPD_Display() {
  int buffer_len = lcd_spi_data.buffer_len;
  EPD_SendCommand(0x24);
  assert(buffer);
  writeBytes(buffer, buffer_len);
  EPD_TurnOnDisplay();
}

void epaper_driver_display::EPD_DisplayRegion(int16_t x, int16_t y, int16_t w,
                                              int16_t h) {
  assert(buffer);
  if (w <= 0 || h <= 0) {
    return;
  }

  int16_t x0 = x < 0 ? 0 : x;
  int16_t y0 = y < 0 ? 0 : y;
  int16_t x1 = x + w - 1;
  int16_t y1 = y + h - 1;
  if (x1 >= Width) {
    x1 = Width - 1;
  }
  if (y1 >= Height) {
    y1 = Height - 1;
  }
  if (x0 > x1 || y0 > y1) {
    return;
  }

  const int16_t byteX0 = x0 >> 3;
  const int16_t byteX1 = x1 >> 3;
  EPD_SetWindows(byteX0 << 3, y1, (byteX1 << 3) + 7, y0);
  EPD_SetCursor(byteX0 << 3, y1);

  EPD_SendCommand(0x24);
  for (int16_t row = y0; row <= y1; row++) {
    const int rowOffset = row * (Width / 8);
    for (int16_t byteX = byteX0; byteX <= byteX1; byteX++) {
      EPD_SendData(buffer[rowOffset + byteX]);
    }
  }
  EPD_TurnOnDisplayPart();
  EPD_SetWindows(0, Width - 1, Height - 1, 0);
  EPD_SetCursor(0, Height - 1);
}

void epaper_driver_display::EPD_LoadPartBaseImage() {
  int buffer_len = lcd_spi_data.buffer_len;
  EPD_SendCommand(0x26);
  assert(buffer);
  writeBytes(buffer, buffer_len);
}

void epaper_driver_display::EPD_LoadPartBothImages() {
  int buffer_len = lcd_spi_data.buffer_len;
  assert(buffer);
  EPD_SendCommand(0x24);
  writeBytes(buffer, buffer_len);
  EPD_SendCommand(0x26);
  writeBytes(buffer, buffer_len);
}

void epaper_driver_display::EPD_DisplayPartBaseImage() {
  int buffer_len = lcd_spi_data.buffer_len;
  EPD_SendCommand(0x24);
  assert(buffer);
  writeBytes(buffer, buffer_len);
  EPD_SendCommand(0x26);
  writeBytes(buffer, buffer_len);
  EPD_TurnOnDisplay();
}

void epaper_driver_display::EPD_Init_Partial() {
  set_rst_1();
  vTaskDelay(pdMS_TO_TICKS(50));
  set_rst_0();
  vTaskDelay(pdMS_TO_TICKS(20));
  set_rst_1();
  vTaskDelay(pdMS_TO_TICKS(50));

  read_busy();

  EPD_SetLut(WF_PARTIAL_1IN54_0);

  EPD_SendCommand(0x37);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x40);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);

  EPD_SendCommand(0x3C); // BorderWavefrom
  EPD_SendData(0x80);

  EPD_SendCommand(0x22);
  EPD_SendData(0xc0);
  EPD_SendCommand(0x20);
  read_busy();
}

void epaper_driver_display::EPD_ReattachPartial() {
  spi_port_init();
  spi_gpio_init();
  ensureBuffer();
  read_busy();

  EPD_SetLut(WF_PARTIAL_1IN54_0);

  EPD_SendCommand(0x37);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x40);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);
  EPD_SendData(0x00);

  EPD_SendCommand(0x3C); // BorderWavefrom
  EPD_SendData(0x80);

  EPD_SendCommand(0x22);
  EPD_SendData(0xc0);
  EPD_SendCommand(0x20);
  read_busy();
}

void epaper_driver_display::EPD_DisplayPart() {
  EPD_SendCommand(0x24);
  assert(buffer);
  writeBytes(buffer, 5000);
  EPD_TurnOnDisplayPart();
}

void epaper_driver_display::EPD_DrawColorPixel(uint16_t x, uint16_t y,
                                               uint8_t color) {
  if (x >= Width || y >= Height) {
    ESP_LOGE("EPD", "Out of bounds pixel: (%d,%d)", x, y);
    return;
  }

  uint16_t index = y * 25 + (x >> 3); // 25是200/8
  uint8_t bit = 7 - (x & 0x07);
  if (color == DRIVER_COLOR_WHITE) {
    buffer[index] |= (0x01 << bit);
  } else {
    buffer[index] &= ~(0x01 << bit);
  }
}

// Unchecked black-only writes for renderer hot loops. Callers must guarantee
// x/y are in bounds and the framebuffer exists; use EPD_DrawColorPixel() for
// normal UI drawing, clipping, or white-pixel writes.
void epaper_driver_display::EPD_DrawBlackPixelUnchecked(uint16_t x,
                                                        uint16_t y) {
  uint16_t index = y * 25 + (x >> 3);
  buffer[index] &= ~(0x01 << (7 - (x & 0x07)));
}

// Same unchecked contract as EPD_DrawBlackPixelUnchecked(); x+1/y+1 must also
// be inside the framebuffer.
void epaper_driver_display::EPD_DrawBlackBlock2x2Unchecked(uint16_t x,
                                                           uint16_t y) {
  EPD_DrawBlackPixelUnchecked(x, y);
  EPD_DrawBlackPixelUnchecked(x + 1, y);
  EPD_DrawBlackPixelUnchecked(x, y + 1);
  EPD_DrawBlackPixelUnchecked(x + 1, y + 1);
}
