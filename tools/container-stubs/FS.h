#pragma once

#include "Arduino.h"

namespace fs {

class File {
public:
  File() = default;
  explicit operator bool() const { return false; }
  bool isDirectory() const { return false; }
  const char *name() const { return ""; }
  const char *path() const { return ""; }
  size_t size() const { return 0; }
  File openNextFile() { return File(); }
};

class FS {
public:
  File open(const char *) { return File(); }
};

} // namespace fs

using fs::File;
