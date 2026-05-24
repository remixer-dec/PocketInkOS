Things to be aware of :
When developing firmware for this hardware stack (ESP32-S3, ES8311, e-Paper, and FT6336 Touch), there are several critical physical and structural pitfalls to be aware of. 

---

### 1. E-Paper Screen Damage and "Burn-In" (DC Bias)
E-Paper displays (EPDs) are chemically physical; they move charged pigment particles through a viscous fluid.
*   **The DC Bias Danger:** Sending commands to the display without powering it down completely or leaving a static voltage across the microcapsules for long periods can cause permanent physical degradation (polarization of the fluid). 
*   **The Fix:** Always put the display into "Deep Sleep" mode (Command `0x10` with data `0x01` or similar depending on the controller IC) immediately after an update. Never leave the display driver active without updates.
*   **Ghosting:** Frequent partial updates (`EPD_Init_Partial`) cause charge accumulation, leading to "ghosting" (shadows of previous frames). You must execute a full refresh cycle (`EPD_Init` followed by `EPD_Display`) every 5 to 10 partial updates to clear the charge.

---

### 2. The ESP32-S3 PSRAM and DMA Trap
The ESP32-S3 features high-speed Octal or Quad PSRAM (SPIRAM), but its DMA (Direct Memory Access) engine has strict boundaries.
*   **DMA over PSRAM:** On the ESP32-S3, standard SPI and I2S DMA cannot directly access external PSRAM unless specific alignment, block-size, and cache-synchronization rules are met. If you attempt to pass a PSRAM pointer directly to a standard `spi_device_transmit` or `i2s_channel_write` call, it will often fail or crash with a memory access violation.
*   **The Fix:** Keep your primary SPI display buffers (5000 bytes) and I2S audio transfer chunks (typically 512 to 1024 bytes) in **Internal SRAM** using heap allocation with the `MALLOC_CAP_DMA` and `MALLOC_CAP_INTERNAL` flags. Use PSRAM only for storing large master assets (like raw audio tracks or large uncompressed images) and copy them to internal SRAM in chunks before sending.

---

### 3. Deep Sleep and Power Rail Leakage
The board utilizes GPIOs to control power to its subsystems (`EPD_PWR_PIN`, `Audio_PWR_PIN`, `VBAT_PWR_PIN`).
*   **The Floating Pin Leak:** When the ESP32-S3 enters deep sleep, its standard digital GPIOs default to a high-impedance (floating) state. If your power-enable pin floats, the power rail may partially turn on, leaking current and draining the battery rapidly.
*   **The Fix:** You must transition these control pins to the RTC power domain and enable "hold" state before sleep:
    ```cpp
    // ESP-IDF Example
    rtc_gpio_init(EPD_PWR_PIN);
    rtc_gpio_set_direction(EPD_PWR_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_hold_en(EPD_PWR_PIN); // Lock state (e.g. HIGH to keep EPD off)
    ```
    Upon wake-up, you must explicitly disable the hold state (`gpio_hold_dis` or `rtc_gpio_hold_dis`) before you can control the pin again.

---

### 4. Audio "Pop" Noises and Power Amplification Sequencing
The onboard audio amplifier can produce a loud, sharp pop sound when starting or stopping audio streams. This is caused by sudden voltage changes on the DAC pins.
*   **The Pop Sequence:** If the Power Amplifier (PA) pin (GPIO 46) is turned ON before the I2S clocks are stable, any transient DC offset on the codec's analog outputs is instantly amplified.
*   **The Fix:** Implement a strict startup and shutdown sequence:
    *   *Startup:* Turn on Codec Power -> Initialize I2S clocks -> Initialize Codec registers -> Wait 50-100ms (analog ramp up) -> Enable PA.
    *   *Shutdown:* Disable PA -> Mute Codec DAC -> Stop I2S clocks -> Power down Codec.

---

### 5. I2C Bus Contention and ISR Lockups
The Touch Controller (FT6336), RTC (PCF85063), and Temperature Sensor (SHTC3) all share a single I2C bus (SDA 47, SCL 48).
*   **The ISR Pitfall:** The touch panel uses an interrupt line (`EPD_TP_INT_PIN`) to signal touch events. **Never** execute I2C read/write transactions directly inside the GPIO Interrupt Service Routine (ISR). I2C transactions are slow, rely on interrupts themselves, and will trigger a Guru Meditation Error or Watchdog timeout.
*   **The Fix:** Use a lightweight synchronization primitive, such as a FreeRTOS Queue or Binary Semaphore, inside the ISR to wake up a dedicated, low-priority processing task:
    ```cpp
    // Inside ISR
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &pin, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    ```

---

### 6. Clock Drift and Audio/Display SPI Conflicts
The S3 has shared clock sources internally.
*   **Clock Source Conflicts:** If you configure the I2S clock to use the APLL (Audio Phase-Locked Loop) for high fidelity, ensure your SPI and I2C buses are configured to use independent clock sources (like `APB_CLK` or `XTAL_CLK`). Setting them to auto-select can sometimes cause timing drift on the display when the audio pipeline is actively altering APLL frequencies.

Demos (provided by manufacturer):

Here is a consolidated, minimal structural guide for the **Waveshare ESP32-S3 ePaper 1.54"** (Touch/Non-Touch) board. These snippets extract the core architectural requirements from the codebase, bypassing heavy external libraries (like LVGL or NXP GUI-Guider) to focus purely on hardware registers, GPIO configs, and driver interfaces.

---

### Core Hardware Configuration & GPIO Map

```cpp
#define EPD_DC_PIN      10
#define EPD_CS_PIN      11
#define EPD_SCK_PIN     12
#define EPD_MOSI_PIN    13
#define EPD_RST_PIN     9
#define EPD_BUSY_PIN    8

#define EPD_PWR_PIN     6    // LOW = Display ON, HIGH = Display OFF
#define Audio_PWR_PIN   42   // LOW = Audio ON, HIGH = Audio OFF
#define VBAT_PWR_PIN    17   // HIGH = Battery ADC read ON, LOW = OFF

#define LED_PIN         3    // Built-in LED (Active LOW)

#define BOOT_BUTTON_PIN 0
#define PWR_BUTTON_PIN  18

#define ESP32_I2C_SDA   47
#define ESP32_I2C_SCL   48

#define EPD_TP_INT_PIN  21   // Touch Interrupt
#define EPD_TP_RST_PIN  7    // Touch Reset

// I2S Codec (ES8311) Pins
#define I2S_MCLK        14
#define I2S_BCLK        15
#define I2S_WS          38
#define I2S_DIN         16
#define I2S_DOUT        45
```

---

## 1. Arduino IDE Implementation

Ensure you have **PSRAM Enabled (OPI)** in your Arduino board settings.

### Basic Initialization, LED, and PSRAM Usage

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);

  // Enable VBAT power for ADC, Display Power, and Audio Power
  pinMode(VBAT_PWR_PIN, OUTPUT);
  pinMode(EPD_PWR_PIN, OUTPUT);
  pinMode(Audio_PWR_PIN, OUTPUT);
  
  digitalWrite(VBAT_PWR_PIN, HIGH); 
  digitalWrite(EPD_PWR_PIN, LOW);   // Power on EPD
  digitalWrite(Audio_PWR_PIN, LOW); // Power on Codec/Amp

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // LED ON

  // Verify PSRAM allocation
  log_d("Total PSRAM: %d bytes", ESP.getPsramSize());
  log_d("Free PSRAM: %d bytes", ESP.getFreePsram());

  // Allocate frame buffer in PSRAM
  uint8_t* display_buffer = (uint8_t*)ps_malloc(200 * 200 / 8); // 5000 bytes
  if (display_buffer != nullptr) {
    memset(display_buffer, 0xFF, 5000); // Clear to white
    log_d("Allocated display buffer in PSRAM");
    free(display_buffer);
  }
}

void loop() {
  // Blink LED
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  delay(500);
}
```

---

### E-ink Display Control (SPI Direct Writes)

This is a structural demonstration of writing to the display without heavy graphic libraries.

```cpp
#include <Arduino.h>
#include <SPI.h>

SPIClass hspi(HSPI); // SPI2_HOST on S3

void epdWriteCommand(uint8_t cmd) {
  digitalWrite(EPD_DC_PIN, LOW);
  digitalWrite(EPD_CS_PIN, LOW);
  hspi.transfer(cmd);
  digitalWrite(EPD_CS_PIN, HIGH);
}

void epdWriteData(uint8_t data) {
  digitalWrite(EPD_DC_PIN, HIGH);
  digitalWrite(EPD_CS_PIN, LOW);
  hspi.transfer(data);
  digitalWrite(EPD_CS_PIN, HIGH);
}

void epdWaitBusy() {
  while (digitalRead(EPD_BUSY_PIN) == HIGH) {
    delay(5); // Wait for Busy Pin to go LOW
  }
}

void epdInit() {
  pinMode(EPD_RST_PIN, OUTPUT);
  pinMode(EPD_DC_PIN, OUTPUT);
  pinMode(EPD_CS_PIN, OUTPUT);
  pinMode(EPD_BUSY_PIN, INPUT);

  hspi.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);
  hspi.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));

  // Hard Reset
  digitalWrite(EPD_RST_PIN, LOW);
  delay(20);
  digitalWrite(EPD_RST_PIN, HIGH);
  delay(20);

  epdWaitBusy();
  epdWriteCommand(0x12); // Software Reset
  epdWaitBusy();

  epdWriteCommand(0x01); // Driver Output control
  epdWriteData(0xC7);
  epdWriteData(0x00);
  epdWriteData(0x01);

  epdWriteCommand(0x11); // Data Entry Mode
  epdWriteData(0x01);    // X increment; Y increment

  // Set Ram Areas
  epdWriteCommand(0x44); // Set RAM X Address Start/End Position
  epdWriteData(0x00);
  epdWriteData(0x18);    // 200 pixels -> 25 bytes (0x18)

  epdWriteCommand(0x45); // Set RAM Y Address Start/End Position
  epdWriteData(0x00);
  epdWriteData(0x00);
  epdWriteData(0xC7);    // 200 pixels (0xC7)
  epdWriteData(0x00);
}

void epdDisplay(const uint8_t* frame_buffer, size_t size) {
  epdWriteCommand(0x24); // Write RAM
  for (size_t i = 0; i < size; i++) {
    epdWriteData(frame_buffer[i]);
  }
  
  epdWriteCommand(0x22); // Display Update Control 2
  epdWriteData(0xC7);    // Load LUT, Clock, CP, Pattern Display
  epdWriteCommand(0x20); // Master Activation
  epdWaitBusy();
}
```

---

### Touch Input & Event Handling (FT6336 via I2C)

```cpp
#include <Arduino.h>
#include <Wire.h>

#define FT6336_ADDR 0x38

void setup() {
  Serial.begin(115200);
  Wire.begin(ESP32_I2C_SDA, ESP32_I2C_SCL, 400000U);

  // Initialize Touch Interrupt & Reset Pins
  pinMode(EPD_TP_RST_PIN, OUTPUT);
  pinMode(EPD_TP_INT_PIN, INPUT_PULLUP);

  // Reset Touch Controller
  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(50);
  digitalWrite(EPD_TP_RST_PIN, LOW);
  delay(50);
  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(100);
}

bool readTouch(uint16_t &x, uint16_t &y) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(0x02); // TD_STATUS register
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom(FT6336_ADDR, 5);
  if (Wire.available() == 5) {
    uint8_t touch_count = Wire.read() & 0x0F;
    if (touch_count > 0) {
      uint8_t x_msb = Wire.read();
      uint8_t x_lsb = Wire.read();
      uint8_t y_msb = Wire.read();
      uint8_t y_lsb = Wire.read();

      x = ((x_msb & 0x0F) << 8) | x_lsb;
      y = ((y_msb & 0x0F) << 8) | y_lsb;
      return true;
    }
  }
  return false;
}

void loop() {
  if (digitalRead(EPD_TP_INT_PIN) == LOW) { // Low represents active touch event
    uint16_t tx, ty;
    if (readTouch(tx, ty)) {
      Serial.printf("Touch registered: X=%d, Y=%d\n", tx, ty);
    }
    delay(150); // Debounce
  }
}
```

---

### WiFi Client & HTTP Request

```cpp
#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  HTTPClient http;
  if (http.begin("http://api.coindesk.com/v1/bpi/currentprice.json")) {
    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println(payload.substring(0, 150)); // Print head of the payload
    }
    http.end();
  }
}

void loop() {}
```

---

### BLE Scanning (ESP32-S3 Native BLE)

```cpp
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.printf("BLE Device Found: %s, RSSI: %d \n", advertisedDevice.toString().c_str(), advertisedDevice.getRSSI());
    }
};

void setup() {
  Serial.begin(115200);
  BLEDevice::init("ESP32-S3-ePaper");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop() {
  Serial.println("Starting BLE Scan...");
  BLEDevice::getScan()->start(5, false); // Scan for 5 seconds
  delay(10000);
}
```

---

### ES8311 Codec Audio Processing Pipeline

Using standard ESP32 I2S structures.

```cpp
#include <Arduino.h>
#include <driver/i2s.h>
#include <Wire.h>

#define ES8311_ADDR 0x18

void es8311_write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(ESP32_I2C_SDA, ESP32_I2C_SCL);

  // Core register initialization for ES8311 Codec
  es8311_write_reg(0x00, 0x80); // Reset
  es8311_write_reg(0x00, 0x1F); // Clear Reset
  es8311_write_reg(0x01, 0x30); // Enable MCLK
  es8311_write_reg(0x02, 0x00); // Divider settings
  es8311_write_reg(0x0D, 0x01); // Power Up Analog
  es8311_write_reg(0x14, 0x1A); // Enable PGA
  es8311_write_reg(0x32, 0xC0); // Max DAC Volume

  // Set up I2S
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_DIN
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  
  // S3 PLL configuration
  REG_SET_FIELD(SYSTEM_SYSCLK_CONF_REG, SYSTEM_SOC_CLK_SEL, 1); // Switch Clock
}

void loop() {
  // Stream zero/sine pattern structural demo
  int16_t test_buffer[128] = {0};
  size_t bytes_written;
  i2s_write(I2S_NUM_0, test_buffer, sizeof(test_buffer), &bytes_written, portMAX_DELAY);
  delay(10);
}
```

---

## 2. ESP-IDF Implementation (v5.x+)

The following demonstrates micro-implementations directly communicating with hardware registers via the ESP-IDF APIs.

### CPU Frequency Scaling & PSRAM Allocation

```c
#include <stdio.h>
#include "esp_private/esp_clk.h"
#include "esp_pm.h"
#include "esp_log.h"
#include "esp_psram.h"

void app_main(void) {
    // 1. Log native S3 clocks
    ESP_LOGI("SYS", "Initial CPU Clock: %d MHz", esp_clk_cpu_freq() / 1000000);

    // 2. Set CPU Clock to 240 MHz (Max S3 performance)
    rtc_cpu_freq_config_t freq_config;
    rtc_clk_cpu_freq_mhz_to_config(240, &freq_config);
    rtc_clk_cpu_freq_set_config(&freq_config);
    ESP_LOGI("SYS", "Adjusted CPU Clock: %d MHz", esp_clk_cpu_freq() / 1000000);

    // 3. PSRAM Verification
    if (esp_psram_is_initialized()) {
        ESP_LOGI("SYS", "PSRAM available: %d bytes", esp_psram_get_size());
        
        // Allocate buffer in SPIRAM
        uint16_t *buf = (uint16_t *)heap_caps_malloc(100 * 1024 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (buf != NULL) {
            ESP_LOGI("SYS", "Allocated 200KB in PSRAM successfully");
            heap_caps_free(buf);
        }
    }
}
```

---

### Display Initialization and Frame Rendering

```c
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EPD_DC_PIN      10
#define EPD_CS_PIN      11
#define EPD_SCK_PIN     12
#define EPD_MOSI_PIN    13
#define EPD_RST_PIN     9
#define EPD_BUSY_PIN    8
#define EPD_PWR_PIN     6

static spi_device_handle_t spi_handle;

void spi_send_cmd(const uint8_t cmd) {
    gpio_set_level(EPD_DC_PIN, 0);
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {cmd}
    };
    spi_device_polling_transmit(spi_handle, &t);
}

void spi_send_data(const uint8_t data) {
    gpio_set_level(EPD_DC_PIN, 1);
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {data}
    };
    spi_device_polling_transmit(spi_handle, &t);
}

void epd_wait_busy(void) {
    while (gpio_get_level(EPD_BUSY_PIN) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void init_epd_panel(void) {
    // GPIO Config
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<EPD_DC_PIN) | (1ULL<<EPD_RST_PIN) | (1ULL<<EPD_PWR_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);
    
    gpio_config_t busy_conf = {
        .pin_bit_mask = (1ULL<<EPD_BUSY_PIN),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&busy_conf);

    // Power On Screen Hardware
    gpio_set_level(EPD_PWR_PIN, 0); 
    vTaskDelay(pdMS_TO_TICKS(50));

    // Initialize SPI
    spi_bus_config_t buscfg = {
        .mosi_io_num = EPD_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = EPD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 5000
    };
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = EPD_CS_PIN,
        .queue_size = 7,
    };
    
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);

    // HW Reset
    gpio_set_level(EPD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(EPD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    epd_wait_busy();
    spi_send_cmd(0x12); // Software Reset
    epd_wait_busy();
}
```

---

### Low-Level Audio Record and Playback (ES8311 I2S & I2C Master)

*Requires Espressif’s legacy or advanced I2C/I2S APIs depending on exact IDF build. Example below leverages IDF v5.3+ I2C & I2S Master drivers.*

```c
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define I2C_PORT        0
#define ES8311_ADDR     0x18

static i2c_master_dev_handle_t codec_dev_handle;
static i2s_chan_handle_t tx_chan;
static i2s_chan_handle_t rx_chan;

void init_i2c_master(void) {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = ESP32_I2C_SDA,
        .scl_io_num = ESP32_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true}
    };
    i2c_new_master_bus(&bus_config, &bus_handle);

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_bus_add_device(bus_handle, &dev_config, &codec_dev_handle);
}

void codec_write_reg(uint8_t reg, uint8_t val) {
    uint8_t tx_buf[2] = {reg, val};
    i2c_master_transmit(codec_dev_handle, tx_buf, 2, -1);
}

void init_i2s_pipeline(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_DIN
        },
    };

    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_init_std_mode(rx_chan, &std_cfg);

    i2s_channel_enable(tx_chan);
    i2s_channel_enable(rx_chan);
}

void app_main(void) {
    init_i2c_master();
    
    // Minimal register sequence to unlock DAC & ADC pipeline inside ES8311
    codec_write_reg(0x00, 0x80); // Reset
    codec_write_reg(0x00, 0x1F); // Clear Reset
    codec_write_reg(0x0D, 0x01); // Power on analog sections
    codec_write_reg(0x01, 0x30); // Enable system clock
    codec_write_reg(0x14, 0x1A); // Input Pre-Amp Configuration
    codec_write_reg(0x17, 0xC0); // Max ADC Gain
    codec_write_reg(0x32, 0xC0); // Max DAC Volume
    
    init_i2s_pipeline();

    ESP_LOGI("AUDIO", "Codec and I2S active.");

    size_t bytes_rw = 0;
    int16_t *pcm_buffer = (int16_t *)malloc(512 * sizeof(int16_t));
    
    while(1) {
        // Read buffer from Mic pipeline
        i2s_channel_read(rx_chan, pcm_buffer, 512 * sizeof(int16_t), &bytes_rw, portMAX_DELAY);
        // Direct pass-through route to Speaker pipeline
        i2s_channel_write(tx_chan, pcm_buffer, bytes_rw, &bytes_rw, portMAX_DELAY);
    }
}
```
