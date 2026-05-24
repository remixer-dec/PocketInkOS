# Touch Input Notes

Hardware: Waveshare ESP32-S3 Touch e-Paper 1.54, FT6336 touch controller over I2C.

## Pins

From the board docs and manufacturer examples:

```cpp
#define ESP32_I2C_SDA   47
#define ESP32_I2C_SCL   48
#define EPD_TP_RST_PIN  7
#define EPD_TP_INT_PIN  21
#define EPD_PWR_PIN     6   // LOW = e-paper/touch rail ON, HIGH = OFF
```

The touch controller is expected at I2C address `0x38` (`56` decimal).

## Working Driver Path

The working implementation uses the ESP-IDF I2C master driver, not Arduino `Wire`.

Observed working serial output:

```text
touch reader selected addr=56 idf-split count=1 raw=110,60
touch raw=110,60 mapped=110,60 count=1 addr=56 reader=idf-split
touch raw=163,61 mapped=163,61 count=1 addr=56 reader=idf-split
```

Working read mode:

1. Initialize I2C using `i2c_new_master_bus` on `I2C_NUM_0`.
2. Add FT6336 as an I2C device at address `0x38` with `i2c_master_bus_add_device`.
3. Read `TD_STATUS` from register `0x02` as a single byte.
4. If touch count is nonzero, read coordinates from register `0x03` as 4 bytes.
5. Decode coordinates as:

```cpp
uint16_t rawX = (((uint16_t)buf[0] & 0x0F) << 8) | buf[1];
uint16_t rawY = (((uint16_t)buf[2] & 0x0F) << 8) | buf[3];
```

## Non-Working Paths

Arduino `Wire` did not work in this project on this board. Bus scans at both `100kHz` and `400kHz` found no devices:

```text
touch scan speed=100000
touch scan found none
touch scan speed=400000
touch scan found none
```

Do not use `Wire` for FT6336 here unless there is a specific reason to revisit it.

The ESP-IDF burst read variant was not selected. The working reader is `idf-split`.

## Power and Reset Requirements

Before initializing touch:

```cpp
pinMode(EPD_PWR_PIN, OUTPUT);
digitalWrite(EPD_PWR_PIN, LOW);
delay(100);
```

Then reset the touch controller:

```cpp
pinMode(EPD_TP_RST_PIN, OUTPUT);
pinMode(EPD_TP_INT_PIN, INPUT_PULLUP);

digitalWrite(EPD_TP_RST_PIN, HIGH);
delay(100);
digitalWrite(EPD_TP_RST_PIN, LOW);
delay(100);
digitalWrite(EPD_TP_RST_PIN, HIGH);
delay(300);
```

The interrupt pin idles HIGH. Touch can be read by polling `TD_STATUS`; INT is useful as a signal, but it should not be the only debug gate while bringing up the driver.

## Display Interaction

Ignore touch while the e-paper panel is busy refreshing:

```cpp
if (digitalRead(EPD_BUSY_PIN) == HIGH) {
  return false;
}
```

The e-paper high-voltage refresh can interfere with capacitive sensing, so touch reads should happen only when `EPD_BUSY_PIN` indicates idle.

## Partial Refresh Notes

The first true rectangular partial-region attempt was not usable on hardware. It caused the initial UI to render as a tiny outline, then subsequent touch updates shifted content and blackened most of the screen.

Current status: the app uses the safer full-buffer partial flow:

1. The first render calls `EPD_Display()` for a real full refresh.
2. The same framebuffer is then copied into the controller old-image RAM with `EPD_LoadPartBaseImage()`; this does not turn the panel on again.
3. `EPD_Init_Partial()` enables the partial waveform immediately after the full render.
4. Screen changes and in-screen updates use `EPD_DisplayPart()` while the partial baseline is valid. This still sends the full 5 KB framebuffer, but uses the partial waveform to avoid the multi-blink full refresh.

This is not true rectangle-only refresh yet. It is an intermediate hardware-safe step intended to avoid the destructive full black/white refresh while preserving correct layout and framebuffer ordering.

`EPD_DisplayRegion(...)` is deliberately neutralized to use `EPD_DisplayPart()` until a correct controller-specific rectangular window sequence is proven on hardware.

Touch input is sampled by a small FreeRTOS task on the other core. The app loop consumes the queued press after display refresh returns, so short taps made during synchronous e-paper updates are not lost.
