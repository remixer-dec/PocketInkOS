#include "fs/file_viewer_registry.h"
#include "fs/providers/hex_file_viewer.h"
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
    &IMAGE_FILE_VIEWER,
    &SVG_FILE_VIEWER,
    &PDF_FILE_VIEWER,
    &TEXT_FILE_VIEWER,
    &HEX_FILE_VIEWER,
};

const size_t VIEWER_COUNT = sizeof(VIEWERS) / sizeof(VIEWERS[0]);

} // namespace

const FileViewerExtension *findFileViewerForPath(const char *path) {
  const char *extension = extensionForPath(path);

  for (size_t i = 0; i < VIEWER_COUNT; i++) {
    if (VIEWERS[i] != nullptr && extensionMatches(*VIEWERS[i], extension, false)) {
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

  const FileViewerExtension *matched = findFileViewerForPath(path);
  if (viewer != nullptr) {
    *viewer = matched;
  }
  if (matched == nullptr) {
    return FileViewerOpenResult::Unsupported;
  }
  if (matched->open == nullptr) {
    return FileViewerOpenResult::NotImplemented;
  }

  FileViewerRequest request;
  request.providerId = providerId;
  request.path = path;
  request.extension = extensionForPath(path);
  request.activity = activity;
  return matched->open(request);
}
