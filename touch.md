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

## Shared I2C Bus Conflict

The touch controller and the PCF85063 RTC share the same physical I2C pins:

```cpp
#define ESP32_I2C_SDA 47
#define ESP32_I2C_SCL 48
```

The touch driver owns this bus through the ESP-IDF I2C master API:

```cpp
i2c_new_master_bus(...)
i2c_master_bus_add_device(...)
i2c_master_transmit_receive(...)
```

Do not initialize Arduino `Wire` on these pins after touch has started:

```cpp
Wire.begin(ESP32_I2C_SDA, ESP32_I2C_SCL); // breaks touch on this firmware
```

That creates a second I2C owner on the same peripheral/pins and can invalidate
or disrupt the IDF bus/device handles used by the touch polling task. The result
is severe: the UI still runs, but touch reads stop producing events.

Any future RTC support must use the same ESP-IDF bus ownership model as touch.
The clean design is a shared I2C bus module that creates one bus on GPIO 47/48
and registers both devices:

- FT6336 touch controller at `0x38`
- PCF85063 RTC at `0x51`

Until that shared bus exists, RTC code must not call `Wire.begin(...)`.

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

There are two separate "partial" concepts in the firmware:

1. App drawing: what gets rendered into the in-memory framebuffer.
2. Display flushing: what part of the framebuffer is sent to the e-paper
   controller.

`draw(...)` only draws into the framebuffer. It does not update the physical
panel. The panel changes only when `AppDisplay::flush()` or
`AppDisplay::flushPartial(...)` is called.

Normal full render flow:

1. `display.clear()` clears the driver framebuffer.
2. `drawActiveScreen()` renders the current screen/app into that framebuffer.
3. `display.flush()` sends the framebuffer to the panel.
4. After a full refresh, the framebuffer is copied into controller old-image RAM
   with `EPD_LoadPartBaseImage()`.
5. `EPD_Init_Partial()` leaves the controller ready for partial waveform
   updates.

Normal touch update flow:

1. The touch handler updates app/screen state.
2. `redrawActiveScreenPartial()` redraws the affected screen content into the
   framebuffer.
3. `display.flushPartial(...)` sends a partial update to the panel.

For apps that do not report a dirty region, `redrawActiveScreenPartial()` uses a
full-screen partial update: it clears the framebuffer, calls `drawActiveScreen()`,
and then calls `flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT)`.

Apps can optionally report a dirty rectangle with `consumeDirtyRegion(...)`.
When they do, the runtime clears that rectangle, calls normal full app `draw(...)`
into the framebuffer, and then calls `flushPartial(x, y, w, h)`. This is not
partial CPU-side drawing; it is only a dirty-region hint for the display flush.

`AppDisplay::flushPartial(...)` uses the known-good full-buffer partial path when
the partial baseline is ready. If partial mode is not ready, it falls back through
`display.flush()` to establish a valid full-refresh baseline first.

`EPD_DisplayRegion(...)` is not proven safe on this panel. Hardware testing has
shown shifted/warped rectangular output, stale-frame flipping, and very long
lockups when many tiny regions are flushed sequentially. It is deliberately
neutralized to call `EPD_DisplayPart()` and marked deprecated so future uses
produce a compile warning. Region-test measurements showed no useful speedup:
single region refreshes still took about 576-584 ms, two region refreshes were
additive, and the controller showed alternating two-frame behavior unless both
EPD image planes were carefully managed. The known-good production path is
full-buffer partial refresh: `EPD_DisplayPart()` sends the full 5 KB framebuffer
but uses the partial waveform to avoid the full black/white blink.

## Paint Refresh Path

Paint is not driven by normal `handleScreenTouch(...)` point events. Its app
behavior registers `onRawTouch`, so the main loop forwards raw touch down/move/up
events directly to `PaintApp::handleTouchEvent(...)`.

In normal mode, `PaintApp` runs a small FreeRTOS task:

1. Raw touch events are queued as paint commands.
2. The paint task locks the display.
3. It drains one or more queued touch/clear commands.
4. Touch commands write strokes directly into the framebuffer with
   `display->drawPixel(...)` or `display->drawLine(...)`.
5. The task calls `display->flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT)` once for
   the drained batch.
6. The task unlocks the display.

If the paint task cannot be created, fallback mode handles raw touch events
synchronously. In fallback mode, touch down/move events update the framebuffer,
but the display flush happens only on `TOUCH_EVENT_UP`.

The paint clear action also uses this path. The power-button handler calls
`paintApp.clear()`, which either queues a clear command for the paint task or
clears and flushes immediately in fallback mode.

Paint currently flushes the full panel rectangle. Its special behavior is that it
avoids full app redraws while drawing: strokes are written directly into the
framebuffer and flushed afterward.

Touch input is sampled by a small FreeRTOS task on the other core. The app loop consumes the queued press after display refresh returns, so short taps made during synchronous e-paper updates are not lost.
