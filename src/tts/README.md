# TTS Integration

This directory contains the firmware-side TTS integration. The InflectNanoTTS runtime is kept as a submodule at `src/tts/InflectNanoTTS`.

## Submodules

Initialize the TTS runtime and its nested GGML submodule recursively:

```bash
git submodule update --init --recursive src/tts/InflectNanoTTS
```

InflectNanoTTS carries local GGML patches under `src/tts/InflectNanoTTS/patches/ggml/`. If `ggml` is reset or freshly initialized without those patches, reapply them from the InflectNanoTTS directory:

```bash
cd src/tts/InflectNanoTTS
git -C ggml apply ../patches/ggml/*.patch
```

## Build

TTS is optional. Enable it explicitly:

```bash
WITH_TTS=1 bin/app.sh
```

Equivalent flag:

```bash
ENABLE_TTS=1 bin/app.sh
```

Container syntax check:

```bash
WITH_TTS=1 tools/container-compile.sh
```

## Runtime Assets

Place model and dictionary files on the SD card:

```text
/sdcard/tts/acoustic/inflect_acoustic_q4_0_E.gguf
/sdcard/tts/vocoder/inflect_vocoder_q4_0_E.gguf
/sdcard/tts/cmudict.bin
/sdcard/tts/cmudict.idx
```

`cmudict.bin` is the compiled pronunciation dictionary. `cmudict.idx` is its index file and is required for fast dictionary initialization.

## Build Flags

Default TTS flags are set by `bin/app.sh` and `tools/container-compile.sh`:

```text
INFLECT_LOW_MEMORY=1
INFLECT_USE_ESP_DSP_CONTIG=0
INFLECT_USE_ESP_DSP_STRIDED=1
INFLECT_USE_RESBLOCK_IM2COL=1
INFLECT_VOCODER_BACKEND=neural
INFLECT_GRIFFIN_LIM_ITERS=8
INFLECT_PROFILE_ACOUSTIC_OPS=0
INFLECT_PROFILE_VOCODER_OPS=0
INFLECT_ACOUSTIC_SKIP_POSTNET=0
```

Set any of these in the environment before running the build script to override the default.

## Measured Performance

Measurements are from ESP32-S3 low-memory builds using the one-word `hey` test.

| Build / path | Total time | Notes |
| --- | ---: | --- |
| Early neural baseline | >10 min | Initial working path before ESP32-specific optimization. |
| Neural, before `cmudict.idx` | +22 s load overhead | Dictionary load dominated startup. |
| Neural, with `cmudict.idx` | ~0.95 s dictionary load | Current indexed path. |
| Neural, scalar dot product | ~117 s | Correct audio, slower than ESP-DSP path. |
| Neural, ESP-DSP strided dot product | ~104 s | Stable speedup over scalar. |
| Neural, corrected acoustic duration | ~75 s | 46 length-regulated frames. |
| Griffin-Lim backend | ~10.7 s | Faster backend with lower audio quality than neural vocoder. |

The neural backend is the quality path. Griffin-Lim is used for lower-latency testing and does not load the vocoder model.
The control logic was implemented in [TTS app](https://github.com/remixer-dec/PocketInkOS/blob/master/src/apps/tts_app.cpp)
