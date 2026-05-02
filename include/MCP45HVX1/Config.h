/// @file Config.h
/// @brief Configuration structure for MCP45HVX1 driver
#pragma once

#include <cstddef>
#include <cstdint>

#include "MCP45HVX1/Status.h"

namespace MCP45HVX1 {

/// I2C write callback signature.
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C write-then-read callback signature.
///
/// txData may be null only when txLen == 0. A zero-length TX phase is used for
/// the MCP45HVX1 documented "last address" read format.
using I2cWriteReadFn = Status (*)(uint8_t addr, const uint8_t* txData, size_t txLen,
                                  uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                  void* user);

/// Millisecond timestamp callback.
using NowMsFn = uint32_t (*)(void* user);

/// MCP45HVX1 wiper resolution variant.
enum class Resolution : uint8_t {
  Bits7 = 7,  ///< MCP45HV31, valid wiper codes 0x00-0x7F
  Bits8 = 8   ///< MCP45HV51, valid wiper codes 0x00-0xFF
};

/// Configuration for MCP45HVX1 driver.
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;         ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C write-read/read function pointer
  void* i2cUser = nullptr;               ///< User context for callbacks

  // === Timing Hooks (optional) ===
  NowMsFn nowMs = nullptr;               ///< Monotonic millisecond source
  void* timeUser = nullptr;              ///< User context for timing hook

  // === Device Settings ===
  uint8_t i2cAddress = 0x3C;             ///< 0x3C-0x3F per DS20005304B Table 6-2
  uint32_t i2cTimeoutMs = 50;            ///< I2C transaction timeout in ms
  Resolution resolution = Resolution::Bits8; ///< Device variant, MCP45HV51 by default

  // === Optional Initialization Writes ===
  bool writeInitialWiper = false;        ///< Write initialWiperCode during begin()
  uint8_t initialWiperCode = 0x7F;       ///< Initial wiper code if writeInitialWiper is true
  bool writeInitialTcon = false;         ///< Write initialTcon during begin()
  uint8_t initialTcon = 0xFF;            ///< Initial TCON value if writeInitialTcon is true

  // === Startup Checks ===
  bool requirePowerOnDefaults = false;   ///< Require POR/BOR defaults during begin()
  bool requireReadMsbZero = true;        ///< Require readback MSB byte to be 0x00

  // === Health Tracking ===
  uint8_t offlineThreshold = 5;          ///< Consecutive failures before OFFLINE state
};

}  // namespace MCP45HVX1

