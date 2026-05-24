#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define GPIO_NUM_0 0
#define GPIO_NUM_18 18
using gpio_num_t = int;
class String {
  std::string s;
public:
  String() = default;
  String(const char *v): s(v ? v : "") {}
  String(const std::string &v): s(v) {}
  size_t length() const { return s.length(); }
  String substring(size_t from) const { return from < s.size() ? s.substr(from) : ""; }
  void remove(size_t pos) { if (pos < s.size()) s.erase(pos); }
  const char *c_str() const { return s.c_str(); }
  String &operator+=(char c) { s += c; return *this; }
  operator const char *() const { return s.c_str(); }
};
struct SerialStub {
  void begin(int) {}
  void println(const char *) {}
  void println(const String &) {}
  void print(const char *) {}
  void print(const String &) {}
  void print(int) {}
  void printf(const char *, ...) {}
};
inline SerialStub Serial;
inline void delay(unsigned long) {}
inline unsigned long millis() { static unsigned long t = 0; return t += 16; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
