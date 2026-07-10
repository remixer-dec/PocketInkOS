#!/bin/bash
set -e

arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
printf 'Installing tested ESP32 core 3.3.8; newer releases may not have been validated yet.\n'
arduino-cli core install esp32:esp32@3.3.8 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "Adafruit GFX Library" "Bounce2"
