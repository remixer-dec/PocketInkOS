#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/examples/pink/tetris/tetris.cpp}"
OUT="${2:-$ROOT/build/pink/tetris.pink}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/pink/$(basename "${SRC%.*}")}"
LINKER="${PINK_LINKER:-$ROOT/tools/pink_app.ld}"
ABI_VERSION=7
MAX_IMAGE_BYTES=$((64 * 1024))

usage() {
  cat <<EOF
Usage: tools/build-pink-app.sh [source.cpp] [out.pink]

Builds a freestanding ESP32-S3 .pink executable.

Environment overrides:
  CXX      path to xtensa ESP32-S3 g++
  OBJCOPY  path to matching objcopy
  NM       path to matching nm
  READELF  path to matching readelf
  PINK_FQBN board FQBN used to query arduino-cli tool paths
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

default_fqbn() {
  if [[ -n "${PINK_FQBN:-}" ]]; then
    printf '%s\n' "$PINK_FQBN"
    return 0
  fi
  if [[ -f "$ROOT/bin/config.sh" ]]; then
    # Reuse the firmware board target without reading any secret config.
    # shellcheck source=/dev/null
    source "$ROOT/bin/config.sh"
  fi
  printf '%s\n' "${FQBN:-esp32:esp32:esp32s3}"
}

arduino_data_dirs() {
  arduino_build_artifact_dirs

  printf '%s\n' \
    "${ARDUINO_DIRECTORIES_DATA:-}" \
    "$HOME/.arduino15" \
    "$HOME/Library/Arduino15" \
    "$HOME/.arduinoIDE" \
    "$ROOT"

  if command -v arduino-cli >/dev/null 2>&1; then
    arduino-cli config dump 2>/dev/null |
      awk '
        $1 == "data:" { print $2 }
        $1 == "user:" { print $2 }
      '
  fi
}

arduino_build_artifact_dirs() {
  python3 - "$ROOT" <<'PY' 2>/dev/null || true
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
seen = set()
for path in (
    root / "build" / "network" / "build.options.json",
    root / "build" / "no-network" / "build.options.json",
    root / "build" / "build.options.json",
):
    if not path.exists():
        continue
    try:
        options = json.loads(path.read_text())
    except Exception:
        continue
    for item in options.get("hardwareFolders", "").split(","):
        hardware = pathlib.Path(item)
        candidates = [hardware]
        parts = hardware.parts
        if len(parts) >= 4 and parts[-3:] and parts[-3] == "hardware":
            candidates.append(pathlib.Path(*parts[:-3]))
        for candidate in candidates:
            text = str(candidate)
            if text and text not in seen:
                seen.add(text)
                print(text)
PY
}

tool_from_arduino_properties() {
  local fqbn="$1"
  local props compiler_path compiler_cmd probe_dir
  if ! command -v arduino-cli >/dev/null 2>&1; then
    return 0
  fi
  probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/pink-tool-probe.XXXXXX")"
  mkdir -p "$probe_dir/PinkToolProbe"
  printf 'void setup() {}\nvoid loop() {}\n' > "$probe_dir/PinkToolProbe/PinkToolProbe.ino"
  props="$(arduino-cli compile --fqbn "$fqbn" --show-properties=expanded "$probe_dir/PinkToolProbe" 2>/dev/null || true)"
  rm -rf "$probe_dir"
  compiler_path="$(printf '%s\n' "$props" | awk -F= '$1 == "compiler.path" { print $2; exit }')"
  compiler_cmd="$(printf '%s\n' "$props" | awk -F= '$1 == "compiler.cpp.cmd" { print $2; exit }')"
  if [[ -n "$compiler_path" && -n "$compiler_cmd" && -x "$compiler_path$compiler_cmd" ]]; then
    printf '%s\n' "$compiler_path$compiler_cmd"
  fi
}

find_tool() {
  local env_name="$1"
  local explicit="${!env_name:-}"
  shift
  if [[ -n "$explicit" ]]; then
    printf '%s\n' "$explicit"
    return 0
  fi
  for name in "$@"; do
    if command -v "$name" >/dev/null 2>&1; then
      command -v "$name"
      return 0
    fi
  done

  local predicates=()
  for name in "$@"; do
    predicates+=(-name "$name" -o)
  done
  unset 'predicates[${#predicates[@]}-1]'

  while IFS= read -r dir; do
    [[ -n "$dir" && -d "$dir" ]] || continue
    find "$dir" -type f \( "${predicates[@]}" \) 2>/dev/null | head -1
  done < <(arduino_data_dirs)
}

FQBN_FOR_TOOLS="$(default_fqbn)"
CXX_PATH="${CXX:-}"
if [[ -z "$CXX_PATH" ]]; then
  CXX_PATH="$(tool_from_arduino_properties "$FQBN_FOR_TOOLS" || true)"
fi
if [[ -z "$CXX_PATH" ]]; then
  CXX_PATH="$(find_tool CXX xtensa-esp32s3-elf-g++ xtensa-esp-elf-g++ || true)"
fi
if [[ -z "$CXX_PATH" ]]; then
  cat >&2 <<EOF
ESP32-S3 compiler not found.

The firmware FQBN used for lookup was:
  $FQBN_FOR_TOOLS

If firmware builds on this machine, run this to inspect Arduino's compiler path:
  arduino-cli compile --fqbn "$FQBN_FOR_TOOLS" --show-properties=expanded src | grep 'compiler.path\\|compiler.cpp.cmd'

Or set CXX/OBJCOPY/NM explicitly. If the ESP32 core is missing:
  arduino-cli core update-index
  arduino-cli core install esp32:esp32
EOF
  exit 1
fi

TOOL_DIR="$(dirname "$CXX_PATH")"
TOOL_BASE="$(basename "$CXX_PATH")"
TOOL_PREFIX="${TOOL_BASE%g++}"
OBJCOPY_PATH="${OBJCOPY:-$TOOL_DIR/${TOOL_PREFIX}objcopy}"
NM_PATH="${NM:-$TOOL_DIR/${TOOL_PREFIX}nm}"
READELF_PATH="${READELF:-$TOOL_DIR/${TOOL_PREFIX}readelf}"
if [[ ! -x "$READELF_PATH" ]] && command -v readelf >/dev/null 2>&1; then
  READELF_PATH="$(command -v readelf)"
fi

if [[ ! -x "$OBJCOPY_PATH" || ! -x "$NM_PATH" || ! -x "$READELF_PATH" ]]; then
  printf 'Matching objcopy/nm/readelf not found for %s\n' "$CXX_PATH" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR" "$(dirname "$OUT")"
OBJ="$BUILD_DIR/app.o"
RUNTIME_OBJ="$BUILD_DIR/pink_runtime.o"
ELF="$BUILD_DIR/app.elf"
BIN="$BUILD_DIR/app.bin"
CODE_BIN="$BUILD_DIR/app.text.bin"
RODATA_BIN="$BUILD_DIR/app.rodata.bin"
MAP="$BUILD_DIR/app.map"
RELOCS="$BUILD_DIR/app.relocs"

CXXFLAGS=(
  -std=gnu++17
  -Os
  -ffunction-sections
  -fdata-sections
  -fno-exceptions
  -fno-rtti
  -fno-threadsafe-statics
  -fno-stack-protector
  -fno-jump-tables
  -fno-tree-switch-conversion
  -fno-builtin
  -nostdlib
  -I"$ROOT/examples/pink/include"
)

LDFLAGS=(
  -nostdlib
  -Wl,-T,"$LINKER"
  -Wl,--gc-sections
  -Wl,--emit-relocs
  -Wl,-Map,"$MAP"
)

"$CXX_PATH" "${CXXFLAGS[@]}" -c "$SRC" -o "$OBJ"
"$CXX_PATH" "${CXXFLAGS[@]}" -c "$ROOT/examples/pink/include/pink_runtime.cpp" -o "$RUNTIME_OBJ"
"$CXX_PATH" "${LDFLAGS[@]}" "$OBJ" "$RUNTIME_OBJ" -o "$ELF"

UNDEFINED="$("$NM_PATH" -u "$ELF" || true)"
if [[ -n "$UNDEFINED" ]]; then
  printf 'Undefined symbols in %s:\n%s\n' "$ELF" "$UNDEFINED" >&2
  exit 1
fi

DATA_SYMBOLS="$("$NM_PATH" --defined-only "$ELF" | awk '$2 ~ /^[BDS]$/ { print }')"
if [[ -n "$DATA_SYMBOLS" ]]; then
  cat >&2 <<EOF
This .pink format does not relocate writable data/BSS symbols yet.
Use host->memory for writable state and avoid global/static objects.

$DATA_SYMBOLS
EOF
  exit 1
fi

ENTRY_HEX="$("$NM_PATH" --defined-only "$ELF" | awk '$3 == "pinkEntry" { print $1; exit }')"
if [[ -z "$ENTRY_HEX" ]]; then
  printf 'pinkEntry symbol not found in %s\n' "$ELF" >&2
  exit 1
fi

"$OBJCOPY_PATH" -j .text -O binary "$ELF" "$CODE_BIN"
if "$READELF_PATH" -SW "$ELF" | python3 -c 'import re,sys; sys.exit(0 if any((m:=re.match(r"\s*\[\s*\d+\]\s+\.rodata\s+\S+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)", line)) and int(m.group(1), 16) > 0 for line in sys.stdin) else 1)'; then
  "$OBJCOPY_PATH" -j .rodata -O binary "$ELF" "$RODATA_BIN"
else
  : > "$RODATA_BIN"
fi

python3 - "$CODE_BIN" "$RODATA_BIN" "$OUT" "$ENTRY_HEX" "$ABI_VERSION" "$MAX_IMAGE_BYTES" "$READELF_PATH" "$ELF" "$RELOCS" <<'PY'
import pathlib
import re
import struct
import subprocess
import sys

code_path = pathlib.Path(sys.argv[1])
rodata_path = pathlib.Path(sys.argv[2])
out_path = pathlib.Path(sys.argv[3])
entry = int(sys.argv[4], 16)
abi = int(sys.argv[5])
max_size = int(sys.argv[6])
readelf = sys.argv[7]
elf = pathlib.Path(sys.argv[8])
reloc_path = pathlib.Path(sys.argv[9])
code = code_path.read_bytes()
rodata = rodata_path.read_bytes()

sections = {}
section_output = subprocess.check_output(
    [readelf, "-SW", str(elf)], text=True, stderr=subprocess.STDOUT
)
for line in section_output.splitlines():
    match = re.match(
        r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]+)\s+"
        r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+",
        line,
    )
    if not match:
        continue
    name = match.group(1)
    sections[name] = {
        "addr": int(match.group(2), 16),
        "offset": int(match.group(3), 16),
        "size": int(match.group(4), 16),
    }

text = sections.get(".text")
if text is None or text["size"] != len(code):
    raise SystemExit(".text section size mismatch")
rodata_section = sections.get(".rodata", {"addr": text["addr"] + text["size"], "size": 0})
if rodata_section["size"] != len(rodata):
    raise SystemExit(".rodata section size mismatch")
linked_code_size = text["size"]
if rodata_section["size"]:
    if rodata_section["addr"] < text["addr"] + text["size"]:
        raise SystemExit(".rodata overlaps .text")
    linked_code_size = rodata_section["addr"] - text["addr"]
if linked_code_size < len(code):
    raise SystemExit("linked .text size is smaller than extracted code")
if linked_code_size > len(code):
    code += bytes(linked_code_size - len(code))
if len(code) == 0:
    raise SystemExit(".text section is empty")
if len(code) + len(rodata) > max_size:
    raise SystemExit(f"image is too large: {len(code) + len(rodata)} > {max_size}")
if entry < text["addr"] or entry >= text["addr"] + text["size"]:
    raise SystemExit(f"entry outside .text: 0x{entry:x}")
entry_offset = entry - text["addr"]

for name in (".data", ".bss", ".sdata", ".sbss"):
    section = sections.get(name)
    if section is not None and section["size"] != 0:
        raise SystemExit(f"unsupported writable section {name} size {section['size']}")

def segment_for_address(value):
    if text["addr"] <= value < text["addr"] + text["size"]:
        return "text", value - text["addr"]
    if rodata_section["size"] and rodata_section["addr"] <= value < rodata_section["addr"] + rodata_section["size"]:
        return "rodata", value - rodata_section["addr"]
    return None, None

def storage_for_section(name):
    if name.endswith(".text") or name == ".rela.text":
        return "text"
    if name.endswith(".rodata") or name == ".rela.rodata":
        return "rodata"
    return None

relocations = []
reloc_output = subprocess.check_output(
    [readelf, "-rW", str(elf)], text=True, stderr=subprocess.STDOUT
)
reloc_path.write_text(reloc_output)
current_reloc_section = None
for line in reloc_output.splitlines():
    section_match = re.match(r"Relocation section '([^']+)'", line)
    if section_match:
        current_reloc_section = section_match.group(1)
        continue
    if "R_XTENSA_32" not in line:
        continue
    match = re.match(r"\s*([0-9a-fA-F]+)\s+", line)
    if not match:
        continue
    offset = int(match.group(1), 16)
    if offset % 4 != 0:
        raise SystemExit(f"unaligned relocation offset: 0x{offset:x}")
    storage = storage_for_section(current_reloc_section or "")
    if storage is None:
        raise SystemExit(f"unsupported R_XTENSA_32 storage section: {current_reloc_section}")
    storage_bytes = code if storage == "text" else rodata
    if offset + 4 > len(storage_bytes):
        raise SystemExit(f"relocation offset outside image: 0x{offset:x}")
    current = struct.unpack_from("<I", storage_bytes, offset)[0]
    target, _ = segment_for_address(current)
    if target is None:
        raise SystemExit(f"relocation target outside image: offset=0x{offset:x} value=0x{current:x}")
    location = offset
    if storage == "rodata":
        location |= 0x80000000
    relocations.append(location)

relocations = sorted(set(relocations))
header_size = 28
header = struct.pack(
    "<IHHIIIII", 0x4B4E4950, abi, header_size, len(code), len(rodata), entry_offset,
    len(relocations), 0
)
reloc_table = b"".join(struct.pack("<I", location) for location in relocations)
out_path.write_bytes(header + reloc_table + code + rodata)
print(
    f"Wrote {out_path} ({len(header) + len(reloc_table) + len(code) + len(rodata)} bytes, "
    f"entry=0x{entry_offset:x}, code={len(code)}, rodata={len(rodata)}, relocs={len(relocations)})"
)
PY
