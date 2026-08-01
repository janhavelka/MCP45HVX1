/**
 * @file Log.h
 * @brief Simple serial logging macros for examples.
 *
 * NOT part of the library API. The library itself does not log.
 * These macros are for example/application code only.
 */

#pragma once

#include <Arduino.h>

#include "examples/common/BuildConfig.h"

// Compile-time validation
#if LOG_LEVEL < 0 || LOG_LEVEL > 4
#error "LOG_LEVEL must be 0-4 (0=off, 1=error, 2=info, 3=debug, 4=trace)"
#endif

#ifndef LOG_SERIAL
#define LOG_SERIAL Serial
#endif

namespace log_color {

inline bool& enabledFlag() {
#if MCP45HVX1_CLI_ENABLE_COLOR
  static bool enabled = (MCP45HVX1_CLI_COLOR_DEFAULT != 0);
#else
  static bool enabled = false;
#endif
  return enabled;
}

inline bool isEnabled() {
#if MCP45HVX1_CLI_ENABLE_COLOR
  return enabledFlag();
#else
  return false;
#endif
}

inline void setEnabled(bool enabled) {
#if MCP45HVX1_CLI_ENABLE_COLOR
  enabledFlag() = enabled;
#else
  (void)enabled;
#endif
}

inline const char* code(const char* sequence) {
#if MCP45HVX1_CLI_ENABLE_COLOR
  return enabledFlag() ? sequence : "";
#else
  (void)sequence;
  return "";
#endif
}

}  // namespace log_color

#define LOG_COLOR_RESET  log_color::code("\033[0m")
#define LOG_COLOR_RED    log_color::code("\033[31m")
#define LOG_COLOR_GREEN  log_color::code("\033[32m")
#define LOG_COLOR_YELLOW log_color::code("\033[33m")
#define LOG_COLOR_BLUE   log_color::code("\033[34m")
#define LOG_COLOR_MAGENTA log_color::code("\033[35m")
#define LOG_COLOR_CYAN   log_color::code("\033[36m")
#define LOG_COLOR_GRAY   log_color::code("\033[90m")
#define LOG_COLOR_RESULT(ok) ((ok) ? LOG_COLOR_GREEN : LOG_COLOR_RED)
#define LOG_COLOR_STATE(online, failures) \
  ((online) ? (((failures) > 0U) ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN) : LOG_COLOR_RED)

inline const char* log_bool_str(bool value) { return value ? "yes" : "no"; }

/**
 * @brief Initialize serial for logging.
 * @param baud Baud rate (default: 115200).
 */
inline void log_begin(unsigned long baud = 115200) {
  LOG_SERIAL.begin(baud);
  // Give USB CDC time to initialize on ESP32-S3
  #if defined(CONFIG_IDF_TARGET_ESP32S3) && ARDUINO_USB_CDC_ON_BOOT
  delay(100);
  #endif
}

// Colorize only the severity tag; keep message text in terminal default color.
// A variadic-only message argument keeps zero-format-argument calls valid C++17.
#define LOG_PRINT_WITH_TAG(tagColor, tag, ...) \
  do { \
    LOG_SERIAL.printf("%s[" tag "]%s ", tagColor, LOG_COLOR_RESET); \
    LOG_SERIAL.printf(__VA_ARGS__); \
    LOG_SERIAL.print("\n"); \
  } while (0)

/// @brief Log error message (level >= 1)
#define LOGE(...) \
  do { \
    if (LOG_LEVEL >= 1) LOG_PRINT_WITH_TAG(LOG_COLOR_RED, "E", __VA_ARGS__); \
  } while (0)

/// @brief Log warning message (level >= 2)
#define LOGW(...) \
  do { \
    if (LOG_LEVEL >= 2) LOG_PRINT_WITH_TAG(LOG_COLOR_YELLOW, "W", __VA_ARGS__); \
  } while (0)

/// @brief Log info message (level >= 2)
#define LOGI(...) \
  do { \
    if (LOG_LEVEL >= 2) LOG_PRINT_WITH_TAG(LOG_COLOR_CYAN, "I", __VA_ARGS__); \
  } while (0)

/// @brief Log debug message (level >= 3)
#define LOGD(...) \
  do { \
    if (LOG_LEVEL >= 3) LOG_PRINT_WITH_TAG(LOG_COLOR_BLUE, "D", __VA_ARGS__); \
  } while (0)

/// @brief Log trace message (level >= 4)
#define LOGT(...) \
  do { \
    if (LOG_LEVEL >= 4) LOG_PRINT_WITH_TAG(LOG_COLOR_GRAY, "T", __VA_ARGS__); \
  } while (0)

// Conditional verbose logging (runtime switch)
#define LOGV(verbose, ...) \
  do { \
    if (verbose) { \
      LOG_PRINT_WITH_TAG(LOG_COLOR_GRAY, "V", __VA_ARGS__); \
    } \
  } while (0)
