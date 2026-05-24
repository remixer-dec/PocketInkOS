#!/bin/bash

export PORT="/dev/cu.usbmodem101"
export UPLOAD_SPEED="460800"
export FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio,FlashSize=8M,PartitionScheme=custom,PSRAM=opi"
export ESPTOOL="esptool"
