# PocketInkOS

PocketInkOS is an e-ink-first operating system currently avaiable for the
[Waveshare ESP32-S3-ePaper-1.54 with touch](https://www.waveshare.com/esp32-s3-epaper-1.54.htm).
The firmware includes its own shell, apps, file viewers, extension runtimes,
power management, and hardware drivers. The project is in pre-release stage and some APIs may change.

## Firmware features

Built-in software includes a few apps and games such as calendar, file manager, alarm clock, qr encoder, chess, sudoku, and many more. Apps can also run from an SD card in two forms: compiled `.pink` executables, or `.js` files interpreted by [PocketInkJS](https://github.com/remixer-dec/PocketInkOS/tree/master/src/sys/runtime/pocketinkjs ), a JavaScript runtime written from scratch for this project.

The shell supports touch, two hardware buttons, SD storage, environmental sensors, audio recording, and audio playback. A shell-wide status bar and control panel provide Wi-Fi, Bluetooth, CPU frequency, volume, and screenshot controls. Persistent options live in the Settings app.

Partial e-paper refresh is part of the UI path, including reusable components: buttons, lists, selectors, keyboards, dialogs, and pickers written for the 200 by 200 display. Deep-sleep clock mode wakes once per minute to update the lock screen, then returns to sleep. In this mode the device battery can last for weeks.

The Files app opens each supported format through a viewer interface. Format decoders were written for this firmware from scratch; raster rendering produce 1-bit output directly, with scaling and selectable dithering for the e-ink panel.
Decoders are available for general purpose image rendering APIs and can be used among all types of supported apps.

| Edition | File formats |
| --- | --- |
| Standard | PNG, JPEG, SVG, WebP, WBMP, TXT, JSON, EPUB, MIDI, and partial PDF |
| Extended | Standard formats plus WAV, MP3, and AAC-LC |

The AI chat app has text and audio modes, including microphone recording. External apps receive bounded APIs for drawing, files, networking, sensors, GPIO, audio, system input, and periodic tasks while the firmware retains ownership of hardware and cleanup.

## Development and testing

Most of the code was implemented with assistance from Codex using GPT-5.5, and GPT-5.6-Sol. I specified the requirements and constraints, reviewed the code, tested it on hardware, and fixed the failures found during testing. The
project is human-directed and AI-assisted; manual hardware testing has caught many bugs that have been fixed.

## License

This PocketInkOS code is licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/). The extended edition has a separate license that allows commercial use.

## Third-party code

- [Adafruit GFX Library 1.12.6](https://github.com/adafruit/Adafruit-GFX-Library/tree/1.12.6),
  [BSD-2-Clause](https://github.com/adafruit/Adafruit-GFX-Library/blob/1.12.6/license.txt)
- [Adafruit BusIO 1.17.4](https://github.com/adafruit/Adafruit_BusIO/tree/1.17.4),
  [MIT](https://github.com/adafruit/Adafruit_BusIO/blob/1.17.4/LICENSE)
- Wire, SPI, FS, SD_MMC, NetworkClientSecure, Network, WiFi, and HTTPClient
  3.3.8 from [Arduino ESP32](https://github.com/espressif/arduino-esp32/tree/3.3.8),
  [LGPL-2.1-or-later](https://github.com/espressif/arduino-esp32/blob/3.3.8/LICENSE.md)
- [Bounce2 2.71](https://github.com/thomasfredericks/Bounce2/tree/2.71),
  [MIT](https://github.com/thomasfredericks/Bounce2/blob/2.71/LICENSE)
- ESP-IDF — underlying framework used by Arduino-ESP32 ([Apache-2.0](https://github.com/espressif/esp-idf/blob/master/LICENSE))
- FreeRTOS Kernel — real-time operating-system component used by ESP-IDF and Arduino-ESP32 ([MIT](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/LICENSE.md))

Thanks to the authors of the [Waveshare ESP32-S3 e-Paper demo](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54) and [epy-paper-host](https://github.com/EPDify/epy-paper-host). Their work helped this project get started.
