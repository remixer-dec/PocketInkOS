# The ESP32-S3-ePaper-1.54 is an e-Paper AIoT development board, equipped with ESP32-S3 microcontroller, which supports Wi-Fi and BLE dual-mode communication. The onboard 1.54inch e-paper display, features ultra-low power consumption and sunlight readability, suitable for portable devices and long-lasting scenarios. It integrates an RTC chip, SHTC3 temperature and humidity sensor, TF card slot, low-power audio codec chip circuit, and Lithium battery recharge management circuit. It reserves interfaces including USB, UART, I2C, and GPIO for easy functionality expansion and sensor connectivity, providing a flexible and reliable development platform for IoT terminals, electronic tags, portable displays, and other applications.
V2 Version: Equipped with ESP32-S3-PICO-1-N8R8, integrated 8MB Flash and 8MB PSRAM; also features optimized whole-board power consumption in sleep mode. (we have V2)

SKU	Product
32298	ESP32-S3-ePaper-1.54
32299	ESP32-S3-ePaper-1.54-EN
34211	ESP32-S3-Touch-ePaper-1.54
34212	ESP32-S3-Touch-ePaper-1.54-EN


# Features

    Powered by a high-performance Xtensa 32-bit LX7 dual-core processor, with a main frequency of up to 240MHz
    Supports 2.4GHz Wi-Fi and Bluetooth 5 (LE), with an onboard internal antenna
    Built-in 512KB SRAM and 384KB ROM, with integrated Flash and PSRAM in a stacked package
    Features a 1.54inch e-Paper display, resolution 200 × 200, offering high contrast and wide viewing angles
    Onboard audio codec chip supports voice capture and playback, facilitating AI voice interaction applications
    Onboard PCF85063 RTC real-time clock and SHTC3 temperature and humidity sensor enable precise RTC time management and environmental monitoring
    Built-in TF card slot, supports external storage of images or files
    Onboard PWR and BOOT side buttons, configurable for custom function development
    Reserved 2 × 6 2.54mm pitch female header interface for external expansion



ESP32-S3-PICO-1-N8R8 Wi-Fi and Bluetooth SoC, running at 240MHz, with integrated 8MB Flash and 8MB PSRAM in a stacked package
TF Card Slot TF card must be formatted as FAT32 for use
ES8311 Audio Codec Chip Supports audio input and output, low-power design, suitable for voice recognition and playback applications
BOOT Button Press and hold the BOOT button to power on again to enter download mode
PWR Power Button Hold BOOT, re-power on to enter download mode
Type-C Interface ESP32-S3 USB interface for program flashing and serial logging
Microphone For audio capture
SHTC3 Temperature and Humidity Sensor Provides ambient temperature and humidity measurement, enabling environmental monitoring function
MX1.25 2PIN Speaker Header Audio signal output, for connecting external speaker
MX1.25 2PIN Lithium Battery Header For connecting a lithium battery
Onboard Chip Antenna Supports 2.4GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE)
PCF85063 (back side) RTC clock chip, supports time-keeping function
2 × 6PIN 2.54mm Pitch Female Header Can be used for expansion
Speaker Plays audio

| GPIO | e-Paper    | SD Card | I2S        | UART&USB | RTC     | Other       | OUTPUT |
| ---- | ---------- | ------- | ---------- | -------- | ------- | ----------- | ------ |
| IO0  |            |         |            |          |         | BOOT0       |        |
| IO1  |            |         |            |          |         |             | IO1    |
| IO2  |            |         |            |          |         |             | IO2    |
| IO3  |            |         |            |          |         |             | IO3    |
| IO4  |            |         |            |          |         | BAT_ADC     |        |
| IO5  |            |         |            |          | RTC_INT |             |        |
| IO6  | EPD3V3_EN  |         |            |          |         |             |        |
| IO7  | EPD_TP_RST |         |            |          |         |             |        |
| IO8  | EPD_BUSY   |         |            |          |         |             |        |
| IO9  | EPD_RST    |         |            |          |         |             |        |
| IO10 | EPD_D/C    |         |            |          |         |             |        |
| IO11 | EPD_CS     |         |            |          |         |             |        |
| IO12 | EPD_SCLK   |         |            |          |         |             |        |
| IO13 | EPD_SDI    |         |            |          |         |             |        |
| IO14 |            |         | I2S_MCLK   |          |         |             |        |
| IO15 |            |         | I2S_SCLK   |          |         |             |        |
| IO16 |            |         | I2S_ASDOUT |          |         |             |        |
| IO17 |            |         |            |          |         | BAT_Control |        |
| IO18 |            |         |            |          |         | PA_EN       |        |
| IO19 |            |         |            | U_N      |         |             | IO19   |
| IO20 |            |         |            | U_P      |         |             | IO20   |
| IO21 | EPD_TP_INT |         |            |          |         |             |        |
| IO38 |            |         | I2S_LRCK   |          |         |             |        |
| IO39 |            | SD_CLK  |            |          |         |             |        |
| IO40 |            | SD_MISO |            |          |         |             |        |
| IO41 |            | SD_MOSI |            |          |         |             |        |
| IO42 |            |         | PA_EN      |          |         |             |        |
| IO43 |            |         |            | TXD      |         |             | IO43   |
| IO44 |            |         |            | RXD      |         |             | IO44   |
| IO45 |            |         | I2S_DSDIN  |          |         |             |        |
| IO46 |            |         | PA_CTRL    |          |         |             |        |
| IO47 | EPD_TP_SDA |         |            |          | RTC_SDA |             | IO47   |
| IO48 | EPD_TP_SCL |         |            |          | RTC_SCL |             | IO48   |


# display:
| Property                      | Value           | Property             | Value                |
| ----------------------------- | --------------- | -------------------- | -------------------- |
| DISPLAY PANEL                 | e-paper display | DISPLAY SIZE         | 1.54 inch            |
| RESOLUTION                    | 200 × 200       | GREY SCALE           | 2                    |
| COMMUNICATION INTERFACE       | SPI             | FULL REFRESH TIME    | 2s                   |
| DISPLAY COLOR                 | black, white    | PARTIAL REFRESH TIME | 0.3s                 |
| TOUCH IC (TOUCH VERSION ONLY) | FT6336          | DISPLAY TYPE         | Passively reflective |


# touch e-paper:
Description of a partial electrical schematic for a small embedded board containing:

* a 1.54" SPI e-paper display,
* a capacitive touch controller,
* power-generation circuitry for the e-paper panel,
* and an SHTC3 temperature/humidity sensor.

The schematic is divided into functional blocks.

---

# 1. Overall System Structure

The page contains three major subsystems:

| Block           | Purpose                                          |
| --------------- | ------------------------------------------------ |
| e-Paper & Touch | Main display and touch interface                 |
| Power circuitry | Generates the special voltages needed by e-paper |
| SHTC3           | Temperature/humidity sensor over I2C             |

The design appears intended for an ESP32-class MCU because the GPIO names match ESP32-S3 style numbering (`GP6`, `GP7`, `GP21`, etc.).

---

# 2. E-Paper Display Connector (J10)

At the center is connector `J10`, a 24-pin connector for the e-paper display panel.

The connector maps electrical signals between the MCU and the display hardware.

## Important Display Signals

| Signal     | Purpose                                |
| ---------- | -------------------------------------- |
| `EPD_BUSY` | Display tells MCU when busy refreshing |
| `EPD_RST`  | Hardware reset line                    |
| `EPD_D/C`  | Data vs command selector               |
| `EPD_CS`   | SPI chip select                        |
| `EPD_SCLK` | SPI clock                              |
| `EPD_SDI`  | SPI MOSI data                          |
| `EPD3V3`   | 3.3V display power                     |
| `PREVGH`   | Positive high-voltage rail             |
| `PREVGL`   | Negative high-voltage rail             |

The e-paper display uses SPI for communication.

---

# 3. GPIO Mapping

On the right side is a mapping table from logical display signals to MCU GPIO pins.

## GPIO Assignments

| Function     | GPIO |
| ------------ | ---- |
| `EPD_TP_SDA` | SDA  |
| `EPD_TP_SCL` | SCL  |
| `EPD_TP_RST` | GP7  |
| `EPD_TP_INT` | GP21 |
| `EPD_BUSY`   | GP8  |
| `EPD_RST`    | GP9  |
| `EPD_D/C`    | GP10 |
| `EPD_CS`     | GP11 |
| `EPD_SCLK`   | GP12 |
| `EPD_SDI`    | GP13 |

This tells firmware developers exactly how to wire software drivers.

A useful mental model:

* SPI lines move image data.
* GPIO control lines coordinate refresh timing.
* BUSY prevents sending data while the panel is updating.

---

# 4. Touch Controller Connector (Labeled “Touch”)

Upper-right is a 6-pin connector for the touch panel.

## Touch Pins

| Pin | Signal       |
| --- | ------------ |
| 6   | `EPD_TP_SDA` |
| 5   | `EPD_TP_SCL` |
| 4   | `3V3`        |
| 3   | `EPD_TP_RST` |
| 2   | `EPD_TP_INT` |
| 1   | GND          |

This is an I2C touch controller.

The touch interface uses:

* `SDA` = I2C data
* `SCL` = I2C clock
* `RST` = reset
* `INT` = interrupt when touched

The likely touch IC is FT6336 from the earlier specification image.

---

# 5. E-Paper Power Generation Circuit

This is the most electrically interesting part.

E-paper displays need unusual voltages:

* positive high voltage (`VGH`)
* negative voltage (`VGL`)
* gate/source driving voltages

These are much higher than normal 3.3V logic.

The schematic generates them using:

| Component  | Role                      |
| ---------- | ------------------------- |
| `L9`       | Inductor                  |
| `D5/D6/D7` | Schottky diodes           |
| `Q3`       | MOSFET switch             |
| Capacitors | Voltage smoothing/storage |

This is essentially a small boost/inverting power converter.

## Generated Rails

| Rail     | Purpose                    |
| -------- | -------------------------- |
| `PREVGH` | Positive high-voltage rail |
| `PREVGL` | Negative high-voltage rail |

E-paper pixels require electric fields to physically move pigment particles. That means the display needs voltages beyond ordinary MCU levels.

Think of the MCU as “logic and orchestration,” while this circuit acts more like a miniature high-voltage analog driver subsystem.

---

# 6. Display Power Enable Circuit

Lower-left is transistor `Q2` (`AO3401`).

This is a power switch controlled by signal:

`EPD3V3_EN`

Purpose:

* MCU can completely power down the display subsystem.
* Saves battery power.

Supporting components:

| Component | Role                    |
| --------- | ----------------------- |
| `R71`     | Pull-up resistor        |
| `Q2`      | High-side MOSFET switch |

---

# 7. SHTC3 Sensor Block

Bottom-right contains sensor `U1`, an `SHTC3`.

This is a Sensirion digital temperature/humidity sensor.

## Connections

| Pin   | Signal    |
| ----- | --------- |
| `VDD` | 3.3V      |
| `VSS` | Ground    |
| `SCL` | I2C clock |
| `SDA` | I2C data  |

There is also:

| Component  | Purpose                    |
| ---------- | -------------------------- |
| `C1` 100nF | Power decoupling capacitor |

The sensor communicates via I2C.

---

# 8. Communication Architecture

The board uses two main buses:

## SPI Bus

Used for:

* e-paper display image transfer

Signals:

* `EPD_SCLK`
* `EPD_SDI`
* `EPD_CS`

## I2C Bus

Used for:

* touch controller
* SHTC3 sensor

Signals:

* `SDA`
* `SCL`

This is a common embedded pattern:

* SPI for high-throughput display data
* I2C for low-speed peripherals

---

# 9. System-Level Interpretation

Conceptually, this board is architected like a layered system:

| Layer           | Responsibility                           |
| --------------- | ---------------------------------------- |
| MCU             | Logic and coordination                   |
| SPI             | Fast image transport                     |
| I2C             | Peripheral communication                 |
| Power circuitry | Generates e-paper drive voltages         |
| Display driver  | Converts voltages into pixel transitions |

E-paper systems are unusual because the display is not merely “drawing pixels.” It is physically moving charged pigment particles inside microcapsules. That is why the power circuitry is much more complex than a normal LCD interface.


# Codec / audio:

This schematic describes the complete audio subsystem of the board.

It contains four major functional areas:

| Block                | Purpose                               |
| -------------------- | ------------------------------------- |
| ES8311 codec         | Converts digital audio ↔ analog audio |
| Microphone front-end | Captures analog audio input           |
| Power amplifier (PA) | Drives speaker output                 |
| I2S + control wiring | Connects audio subsystem to MCU       |

At a systems level, this is a fairly standard embedded audio pipeline:

```text
MCU ↔ I2S ↔ ES8311 codec ↔ analog audio ↔ amplifier ↔ speaker
```

with a microphone path flowing in reverse:

```text
Microphone → codec ADC → I2S → MCU
```

---

# 1. Central Component: ES8311 Audio Codec

The large IC in the middle-left is:

`U10 = ES8311`

The ES8311 is a low-power audio codec.

A codec performs two opposite operations:

| Direction | Function               |
| --------- | ---------------------- |
| DAC       | Digital → analog audio |
| ADC       | Analog → digital audio |

So this chip simultaneously supports:

* audio playback,
* microphone recording,
* speaker/headphone output.

---

# 2. MCU Interface: I2S Bus

The bottom-left section maps MCU GPIOs to I2S signals.

## GPIO Mapping

| Function   | GPIO |
| ---------- | ---- |
| I2S_MCLK   | GP14 |
| I2S_SCLK   | GP15 |
| I2S_ASDOUT | GP16 |
| I2S_LRCK   | GP38 |
| I2S_DSDIN  | GP45 |
| PA_EN      | GP42 |
| PA_CTRL    | GP46 |

---

## Meaning of I2S Signals

| Signal   | Purpose                  |
| -------- | ------------------------ |
| `MCLK`   | Master audio clock       |
| `SCLK`   | Bit clock                |
| `LRCK`   | Left/right channel clock |
| `ASDOUT` | Codec → MCU audio data   |
| `DSDIN`  | MCU → codec audio data   |

Conceptually:

```text
MCU sends PCM samples → codec DAC → analog sound
```

and

```text
Microphone analog signal → codec ADC → MCU receives PCM
```

---

# 3. I2C Control Interface

Upper-left signals:

| Signal     |
| ---------- |
| ES8311_SDA |
| ES8311_SCL |

These are I2C lines.

I2C is used only for configuration/control:

* volume
* gain
* sample rate
* mute state
* routing

Audio itself does NOT travel over I2C.

---

# 4. Microphone Input Circuit

Upper-right area labeled:

`MIC2`

This is the microphone subsystem.

The microphone appears to be:

* an analog MEMS microphone,
* powered from 3.3V,
* outputting an analog audio waveform.

---

## Microphone Signal Path

The signal chain is:

```text
Microphone
→ filtering capacitors
→ AC coupling
→ codec microphone input
```

Important signals:

| Signal  | Meaning                    |
| ------- | -------------------------- |
| `MIC_P` | Positive microphone signal |
| `MIC_N` | Negative microphone signal |

The codec supports differential microphone input, which helps reject noise.

---

## Filtering Components

Many capacitors surround the mic path:

| Component Type   | Purpose          |
| ---------------- | ---------------- |
| 100nF capacitors | Power decoupling |
| 1uF capacitors   | AC coupling      |
| Ferrite/inductor | Noise filtering  |

These suppress:

* digital switching noise,
* RF interference,
* power ripple.

This matters because microphone signals are extremely small.

---

# 5. Codec Analog Outputs

The ES8311 exposes:

| Signal |
| ------ |
| OUTP   |
| OUTN   |

These are differential analog audio outputs.

They feed the power amplifier section.

---

# 6. Power Amplifier (PA)

Bottom-right component:

`U11 = NS4150B`

This is a speaker power amplifier.

The codec alone cannot drive a speaker directly because speakers require significantly more current.

So the chain becomes:

```text
Codec line output
→ power amplifier
→ speaker
```

---

## Amplifier Inputs

| Signal  | Meaning                  |
| ------- | ------------------------ |
| PA_INL+ | Positive amplifier input |
| PA_INL- | Negative amplifier input |

These come from codec outputs through coupling capacitors.

---

## Amplifier Outputs

| Signal |
|---|---|
| PA_OUTL+ |
| PA_OUTL- |

These are high-current differential speaker outputs.

---

# 7. Speaker Connector

Far-right connector:

`J2`

This is the speaker connector.

The speaker is connected differentially:

```text
PA_OUTL+ → speaker terminal
PA_OUTL- → speaker terminal
```

This is a bridge-tied load (BTL) configuration.

Benefits:

* higher power,
* no large output capacitor needed,
* better efficiency.

Very common in battery-powered devices.

---

# 8. EMI / Output Filtering

Near the speaker outputs are:

| Component      | Role       |
| -------------- | ---------- |
| L13/L14        | Inductors  |
| C125/C130/C135 | Capacitors |

These form an output EMI filter.

Purpose:

* suppress high-frequency switching noise,
* reduce electromagnetic emissions,
* protect nearby radios.

Class-D amplifiers often require these.

---

# 9. Power Control Circuit

Bottom-middle:

`Q6 = AO3401`

This transistor controls `PAVCC`.

Signal:

`PA_EN`

This lets firmware completely power down the amplifier.

Why?

Audio amplifiers waste power even when idle.

So firmware can:

* disable speaker when unused,
* save battery.

---

# 10. Analog vs Digital Grounding

The schematic carefully separates:

| Ground |
| ------ |
| GND    |
| AGND   |

`AGND` = analog ground.

This is important because digital switching noise can contaminate audio.

Audio systems often isolate:

* analog return currents,
* digital return currents.

This is a classic mixed-signal PCB design technique.

---

# 11. Clock Integrity Components

Several 22pF capacitors appear on clock/data lines.

Purpose:

* suppress ringing,
* improve signal integrity,
* reduce EMI.

High-speed digital audio clocks are sensitive to trace quality.

---

# 12. Architectural Interpretation

This design is essentially a miniature embedded audio stack.

## Playback Flow

```text
MCU
→ I2S digital audio
→ ES8311 DAC
→ analog line signal
→ NS4150B amplifier
→ speaker
```

## Recording Flow

```text
Microphone
→ ES8311 ADC
→ I2S digital stream
→ MCU
```

---

# 13. Design Characteristics

This schematic suggests:

| Characteristic           | Interpretation                  |
| ------------------------ | ------------------------------- |
| Differential audio paths | Noise-aware design              |
| Separate AGND            | Proper mixed-signal engineering |
| Dedicated PA enable      | Battery optimization            |
| I2S architecture         | Modern digital audio pipeline   |
| EMI output filters       | RF-conscious hardware           |
| ES8311 codec             | Low-power embedded audio        |

Overall this is a fairly sophisticated low-power embedded audio subsystem suitable for:

* smart displays,
* voice assistants,
* portable IoT devices,
* e-paper terminals with audio feedback,
* battery-powered interactive devices.

