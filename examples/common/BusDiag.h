#pragma once

#ifndef MCP45HVX1_EXAMPLE_PLATFORM_IDF
#define MCP45HVX1_EXAMPLE_PLATFORM_IDF 0
#endif

#if MCP45HVX1_EXAMPLE_PLATFORM_IDF
#include "examples/common/IdfArduinoCompat.h"
#else
#include <Wire.h>
#endif

#include "I2cScanner.h"

namespace bus_diag {
inline void scan() {
  i2c_scanner::scan(Wire);
}
}
