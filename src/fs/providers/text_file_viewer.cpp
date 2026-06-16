#include "fs/providers/text_file_viewer.h"
#include "fs/file_provider.h"

#include <Arduino.h>

namespace {

static const int16_t kViewerTextY = 30;
static const int16_t kViewerLineH = 10;
static const uint8_t kTextColumns = 31;
static const uint8_t kTextRows = 16;

uint32_t clampOffset(uint32_t offset, uint32_t size) {
  return offset > size ? size : offset;
}

FileViewerOpenResult openTextViewer(const FileViewerRequest &request) {
  File file = openProviderPath(request.providerId, request.path);
  if (!file || file.isDirectory()) {
    return FileViewerOpenResult::Failed;
  }
  return FileViewerOpenResult::Opened;
}

void drawTextViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime) {
  if (runtime.size == 0) {
    gfx.setCursor(84, 92);
    gfx.print("EMPTY");
    return;
  }
  if (runtime.offset >= runtime.size) {
    gfx.setCursor(90, 92);
    gfx.print("END");
    return;
  }

  File file = openProviderPath(runtime.providerId, runtime.path);
  if (!file || file.isDirectory() ||
      !file.seek(clampOffset(runtime.offset, runtime.size))) {
    gfx.setCursor(58, 92);
    gfx.print("OPEN FAILED");
    return;
  }

  for (uint8_t row = 0; row < kTextRows; row++) {
    char line[kTextColumns + 1] = {};
    uint8_t len = 0;
    bool hadByte = false;

    while (len < kTextColumns) {
      const int raw = file.read();
      if (raw < 0) {
        break;
      }
      hadByte = true;
      const uint8_t c = static_cast<uint8_t>(raw);
      if (c == '\n') {
        break;
      }
      if (c == '\r') {
        continue;
      }
      if (c == '\t') {
        line[len++] = ' ';
      } else if (c >= 0x20 && c <= 0x7e) {
        line[len++] = static_cast<char>(c);
      } else if (c >= 0x80) {
        line[len++] = '?';
        while (file.available() > 0) {
          const int peeked = file.peek();
          if (peeked < 0 || (static_cast<uint8_t>(peeked) & 0xc0) != 0x80) {
            break;
          }
          file.read();
        }
      } else {
        line[len++] = '?';
      }
    }

    if (!hadByte && len == 0 && row == 0) {
      gfx.setCursor(84, 92);
      gfx.print("EMPTY");
      return;
    }
    if (!hadByte && len == 0) {
      return;
    }

    line[len] = '\0';
    gfx.setCursor(4, kViewerTextY + row * kViewerLineH);
    gfx.print(line);
  }
}

void scrollTextViewer(FileViewerRuntime &runtime, int8_t lines) {
  if (lines == 0) {
    return;
  }

  File file = openProviderPath(runtime.providerId, runtime.path);
  if (!file || file.isDirectory()) {
    return;
  }

  if (lines > 0) {
    uint32_t offset = clampOffset(runtime.offset, runtime.size);
    if (!file.seek(offset)) {
      return;
    }
    int8_t remaining = lines;
    while (remaining > 0 && offset < runtime.size) {
      const int raw = file.read();
      if (raw < 0) {
        offset = runtime.size;
        break;
      }
      offset++;
      if (raw == '\n') {
        remaining--;
      }
    }
    runtime.offset = clampOffset(offset, runtime.size);
    return;
  }

  static const uint16_t kBackScanBytes = 2048;
  const uint32_t start =
      runtime.offset > kBackScanBytes ? runtime.offset - kBackScanBytes : 0;
  if (!file.seek(start)) {
    return;
  }

  uint32_t lineStarts[8] = {};
  uint8_t count = 1;
  uint8_t next = 1;
  lineStarts[0] = start;

  uint32_t offset = start;
  while (offset < runtime.offset) {
    const int raw = file.read();
    if (raw < 0) {
      break;
    }
    offset++;
    if (raw == '\n' && offset < runtime.offset) {
      lineStarts[next] = offset;
      next = (next + 1) % 8;
      if (count < 8) {
        count++;
      }
    }
  }

  const uint8_t wanted =
      static_cast<uint8_t>((-lines) < count ? -lines : count - 1);
  const uint8_t newest = (next + 7) % 8;
  const uint8_t index = (newest + 8 - wanted) % 8;
  runtime.offset = lineStarts[index];
}

uint32_t textVisibleBytes(const FileViewerRuntime &) {
  return static_cast<uint32_t>(kTextRows) * kTextColumns;
}

const char *const TEXT_VIEWER_EXTENSIONS[] = {
    "txt",        "md",       "markdown", "json",  "jsonl", "cfg",
    "conf",       "config",   "ini",      "toml",  "yaml",  "yml",
    "xml",        "html",     "htm",      "css",   "scss",  "csv",
    "tsv",        "log",      "c",        "h",     "cpp",   "hpp",
    "cc",         "hh",       "ino",      "py",    "js",    "mjs",
    "cjs",        "ts",       "tsx",      "jsx",   "java",  "kt",
    "kts",        "go",       "rs",       "swift", "rb",    "php",
    "sh",         "bash",     "zsh",      "fish",  "ps1",   "bat",
    "cmd",        "lua",      "pl",       "pm",    "r",     "sql",
    "dockerfile", "makefile", "mk",       "gradle","cmake", "patch",
    "diff",
};

} // namespace

const FileViewerExtension TEXT_FILE_VIEWER = {
    "text",
    "TEXT",
    TEXT_VIEWER_EXTENSIONS,
    static_cast<uint8_t>(sizeof(TEXT_VIEWER_EXTENSIONS) /
                         sizeof(TEXT_VIEWER_EXTENSIONS[0])),
    openTextViewer,
    drawTextViewer,
    scrollTextViewer,
    textVisibleBytes,
};
