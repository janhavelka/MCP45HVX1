/// @file Arduino.h
/// @brief Minimal Arduino stub for native testing
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <cstdlib>
#include <cctype>

// Basic types
using byte = uint8_t;
static constexpr int INPUT = 0;
static constexpr int OUTPUT = 1;
static constexpr int INPUT_PULLUP = 2;
static constexpr int LOW = 0;
static constexpr int HIGH = 1;

inline void pinMode(int pin, int mode) { (void)pin; (void)mode; }
inline void digitalWrite(int pin, int value) { (void)pin; (void)value; }
inline int digitalRead(int pin) { (void)pin; return HIGH; }

// Timing stubs
inline uint32_t gMillisValue = 0;
inline void setMillis(uint32_t value) { gMillisValue = value; }
inline uint32_t millis() { return gMillisValue; }
inline uint32_t micros() { return 0; }
inline void delay(uint32_t ms) { (void)ms; }
inline void delayMicroseconds(uint32_t us) { (void)us; }
inline void yield() {}

// Serial stub
class SerialClass {
public:
  void begin(uint32_t baud) { (void)baud; }
  void print(const char* s) { (void)s; }
  void println(const char* s = "") { (void)s; }
  void printf(const char* fmt, ...) { (void)fmt; }
  void flush() {}
  int available() { return 0; }
  int read() { return -1; }
  operator bool() { return true; }
};

using Print = SerialClass;

extern SerialClass Serial;

// String class (minimal stub)
class String {
public:
  String() = default;
  String(const char* s) : _data(s ? s : "") {}
  const char* c_str() const { return _data.c_str(); }
  size_t length() const { return _data.length(); }
  bool reserve(size_t size) { _data.reserve(size); return true; }
  void trim() {}
  void remove(size_t index) {
    if (index < _data.size()) {
      _data.erase(index);
    }
  }
  void toLowerCase() {
    for (char& c : _data) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  bool startsWith(const char* prefix) const {
    return _data.find(prefix) == 0;
  }
  String substring(size_t start) const {
    return String(_data.substr(start).c_str());
  }
  int toInt() const { return std::stoi(_data); }
  String& operator+=(char c) { _data += c; return *this; }
private:
  std::string _data;
};
