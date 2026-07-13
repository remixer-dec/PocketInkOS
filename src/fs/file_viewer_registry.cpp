#include "fs/file_viewer_registry.h"
#include "fs/providers/pink_audio_file_viewer.h"
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
#include "fs/providers/audio_file_viewer.h"
#endif
#include "fs/providers/epub_file_viewer.h"
#include "fs/providers/hex_file_viewer.h"
#include "fs/providers/midi_file_viewer.h"
#include "fs/providers/pdf_file_viewer.h"
#include "fs/providers/image_file_viewer.h"
#include "fs/providers/svg_file_viewer.h"
#include "fs/providers/text_file_viewer.h"

#include <cstring>

namespace {

char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c - 'A' + 'a');
  }
  return c;
}

bool equalsIgnoreCase(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  while (*left != '\0' && *right != '\0') {
    if (asciiLower(*left++) != asciiLower(*right++)) {
      return false;
    }
  }
  return *left == '\0' && *right == '\0';
}

const char *extensionForPath(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return "";
  }

  const char *name = strrchr(path, '/');
  name = name != nullptr ? name + 1 : path;
  const char *dot = strrchr(name, '.');
  if (dot == nullptr || dot == name || dot[1] == '\0') {
    return "";
  }
  return dot + 1;
}

bool extensionMatches(const FileViewerExtension &viewer, const char *extension,
                      bool wildcard) {
  if (viewer.extensions == nullptr) {
    return false;
  }

  for (uint8_t i = 0; i < viewer.extensionCount; i++) {
    const char *candidate = viewer.extensions[i];
    if (candidate == nullptr) {
      continue;
    }
    const bool isWildcard = candidate[0] == '*' && candidate[1] == '\0';
    if (wildcard) {
      if (isWildcard) {
        return true;
      }
    } else if (!isWildcard && equalsIgnoreCase(extension, candidate)) {
      return true;
    }
  }
  return false;
}

const FileViewerExtension *const VIEWERS[] = {
    &PINK_AUDIO_FILE_VIEWER,
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
    &AUDIO_FILE_VIEWER,
#endif
    &MIDI_FILE_VIEWER,
    &IMAGE_FILE_VIEWER,
    &SVG_FILE_VIEWER,
    &PDF_FILE_VIEWER,
    &EPUB_FILE_VIEWER,
    &TEXT_FILE_VIEWER,
    &HEX_FILE_VIEWER,
};

const size_t VIEWER_COUNT = sizeof(VIEWERS) / sizeof(VIEWERS[0]);

bool viewerMatches(const FileViewerExtension &viewer, const char *path,
                   const char *extension, bool wildcard) {
  if (!wildcard && viewer.matches != nullptr &&
      viewer.matches(path, extension)) {
    return true;
  }
  return extensionMatches(viewer, extension, wildcard);
}

} // namespace

const FileViewerExtension *findFileViewerForPath(const char *path) {
  const char *extension = extensionForPath(path);

  for (size_t i = 0; i < VIEWER_COUNT; i++) {
    if (VIEWERS[i] != nullptr &&
        viewerMatches(*VIEWERS[i], path, extension, false)) {
      return VIEWERS[i];
    }
  }

  for (size_t i = 0; i < VIEWER_COUNT; i++) {
    if (VIEWERS[i] != nullptr && extensionMatches(*VIEWERS[i], extension, true)) {
      return VIEWERS[i];
    }
  }

  return nullptr;
}

FileViewerOpenResult openFileWithViewer(const char *providerId,
                                        const char *path,
                                        const FileViewerExtension **viewer,
                                        FileViewerActivity *activity) {
  if (viewer != nullptr) {
    *viewer = nullptr;
  }
  if (providerId == nullptr || providerId[0] == '\0' || path == nullptr ||
      path[0] == '\0') {
    return FileViewerOpenResult::Failed;
  }

  const char *extension = extensionForPath(path);
  const FileViewerExtension *firstMatched = nullptr;
  FileViewerOpenResult best = FileViewerOpenResult::Unsupported;
  for (uint8_t pass = 0; pass < 2; pass++) {
    const bool wildcard = pass != 0;
    for (size_t i = 0; i < VIEWER_COUNT; i++) {
      const FileViewerExtension *candidate = VIEWERS[i];
      if (candidate == nullptr ||
          !viewerMatches(*candidate, path, extension, wildcard)) {
        continue;
      }
      if (firstMatched == nullptr) {
        firstMatched = candidate;
      }
      if (candidate->open == nullptr) {
        if (best == FileViewerOpenResult::Unsupported) {
          best = FileViewerOpenResult::NotImplemented;
        }
        continue;
      }
      FileViewerRequest request;
      request.providerId = providerId;
      request.path = path;
      request.extension = extension;
      request.activity = activity;
      const FileViewerOpenResult result = candidate->open(request);
      if (result == FileViewerOpenResult::Opened) {
        if (viewer != nullptr) {
          *viewer = candidate;
        }
        return result;
      }
      if (result == FileViewerOpenResult::Failed ||
          (result == FileViewerOpenResult::NotImplemented &&
           best == FileViewerOpenResult::Unsupported)) {
        best = result;
      }
    }
    // Wildcard viewers are a last resort only when no specific viewer claimed
    // the extension. A failed codec should report failure, not open as hex.
    if (!wildcard && firstMatched != nullptr) {
      break;
    }
  }
  if (viewer != nullptr) {
    *viewer = firstMatched;
  }
  return best;
}
