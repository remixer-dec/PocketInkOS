/// Low-level board driver for the 200x200 monochrome e-paper panel. It owns the
/// SPI device and framebuffer and implements full and partial refresh sequences;
/// higher-level drawing normally reaches it through AppDisplay.
#ifndef EPAPER_DRIVER_BSP_H
#define EPAPER_DRIVER_BSP_H

#include "driver/gpio.h"
#include "driver/spi_master.h"

/// Bit values used by the panel's one-bit white/black framebuffer.
typedef enum {
  DRIVER_COLOR_WHITE = 0xff,
  DRIVER_COLOR_BLACK = 0x00,
  FONT_BACKGROUND = DRIVER_COLOR_WHITE,
} COLOR_IMAGE;

/// Pin mapping and SPI parameters required to attach one panel instance.
typedef struct {
  uint8_t cs;
  uint8_t dc;
  uint8_t rst;
  uint8_t busy;
  uint8_t mosi;
  uint8_t scl;
  int spi_host;
  int buffer_len;
} custom_lcd_spi_t;

/// Owns one e-paper controller, its SPI attachment, and its framebuffer.
class epaper_driver_display {
private:
  const custom_lcd_spi_t lcd_spi_data;
  const int Width;
  const int Height;
  spi_device_handle_t spi = NULL;
  uint8_t *buffer = NULL;

  void spi_gpio_init();
  void spi_port_init();
  void read_busy();
  void ensureBuffer();

  void set_cs_1() { gpio_set_level((gpio_num_t)lcd_spi_data.cs, 1); }
  void set_cs_0() { gpio_set_level((gpio_num_t)lcd_spi_data.cs, 0); }
  void set_dc_1() { gpio_set_level((gpio_num_t)lcd_spi_data.dc, 1); }
  void set_dc_0() { gpio_set_level((gpio_num_t)lcd_spi_data.dc, 0); }
  void set_rst_1() { gpio_set_level((gpio_num_t)lcd_spi_data.rst, 1); }
  void set_rst_0() { gpio_set_level((gpio_num_t)lcd_spi_data.rst, 0); }

  void SPI_SendByte(uint8_t data);
  void EPD_SendData(uint8_t data);
  void EPD_SendCommand(uint8_t command);
  void writeBytes(uint8_t *buffer, int len);
  void writeBytes(const uint8_t *buffer, int len);
  void EPD_SetWindows(uint16_t Xstart, uint16_t Ystart, uint16_t Xend,
                      uint16_t Yend);
  void EPD_SetCursor(uint16_t Xstart, uint16_t Ystart);
  void EPD_SetLut(const uint8_t *lut);
  void EPD_TurnOnDisplay();
  void EPD_TurnOnDisplayPart();

public:
  /// Creates a panel driver for the supplied dimensions and board wiring.
  epaper_driver_display(int width, int height, custom_lcd_spi_t _lcd_spi_data);
  /// Detaches SPI and releases the framebuffer.
  ~epaper_driver_display();

  /// Performs the controller's normal full-refresh initialization sequence.
  void EPD_Init();
  /// Initializes a cold panel for a partial refresh with a known base image.
  void EPD_InitColdPartial();
  /// Reattaches a sleeping panel while preserving partial-refresh state.
  void EPD_ReattachPartial();
  /// Clears both controller memory and the local framebuffer to white.
  void EPD_Clear();
  /// Sends the framebuffer using a full display refresh.
  void EPD_Display();
  [[deprecated("EPD_DisplayRegion is not hardware-safe; use EPD_DisplayPart/full-buffer partial refresh")]]
  void EPD_DisplayRegion(int16_t x, int16_t y, int16_t w, int16_t h);

  /// Loads the framebuffer as the controller's partial-refresh base image.
  void EPD_LoadPartBaseImage();
  /// Loads the framebuffer into both controller image planes.
  void EPD_LoadPartBothImages();
  /// Loads and displays the framebuffer as a new partial-refresh baseline.
  void EPD_DisplayPartBaseImage();
  /// Switches an already initialized panel into partial-refresh mode.
  void EPD_Init_Partial();
  /// Refreshes the panel from the full local buffer using the partial waveform.
  void EPD_DisplayPart();
  /// Sets one clipped framebuffer pixel to the requested monochrome color.
  void EPD_DrawColorPixel(uint16_t x, uint16_t y, uint8_t color);
  /// Sets one known-in-range pixel black without bounds checks.
  void EPD_DrawBlackPixelUnchecked(uint16_t x, uint16_t y);
  /// Sets a known-in-range 2x2 block black without bounds checks.
  void EPD_DrawBlackBlock2x2Unchecked(uint16_t x, uint16_t y);
  /// Exposes the live framebuffer for read-only frame export.
  const uint8_t *framebufferData() const;
  /// Returns the byte length of the live framebuffer.
  int framebufferSize() const;
};
#endif
