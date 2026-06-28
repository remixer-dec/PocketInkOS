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
  int available() const { return 0; }
  int read() { return -1; }
  size_t read(uint8_t *, size_t) { return 0; }
  size_t write(const uint8_t *, size_t) { return 0; }
  int peek() { return -1; }
  bool seek(uint32_t) { return false; }
  void close() {}
  File openNextFile() { return File(); }
};

class FS {
public:
  File open(const char *) { return File(); }
  File open(const char *, const char *) { return File(); }
  bool mkdir(const char *) { return false; }
};

} // namespace fs

using fs::File;

#ifndef FILE_APPEND
#define FILE_APPEND "a"
#endif
