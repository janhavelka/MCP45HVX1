/// @file CommandTable.h
/// @brief Register addresses, commands, masks, and constants for MCP45HVX1
#pragma once

#include <cstddef>
#include <cstdint>

namespace MCP45HVX1 {
namespace cmd {

// ============================================================================
// I2C Addressing
// ============================================================================

/// Default 7-bit I2C address for A1=A0=0.
static constexpr uint8_t DEFAULT_ADDRESS = 0x3C;

/// Minimum documented 7-bit I2C address (fixed bits 01111 + A1:A0).
static constexpr uint8_t MIN_ADDRESS = 0x3C;

/// Maximum documented 7-bit I2C address.
static constexpr uint8_t MAX_ADDRESS = 0x3F;

/// Alternate range called out in extracted markdown as a figure/OCR conflict.
/// The Rev B PDF text and Table 6-2 show 0x3C-0x3F; this range is accepted only
/// to let hardware-verification builds override the address without patching.
static constexpr uint8_t ALT_MIN_ADDRESS = 0x5C;
static constexpr uint8_t ALT_MAX_ADDRESS = 0x5F;

/// General Call address.
static constexpr uint8_t GENERAL_CALL_ADDRESS = 0x00;

// ============================================================================
// Register Addresses
// ============================================================================

/// Volatile Wiper 0 register. R/W/INC/DEC.
static constexpr uint8_t REG_WIPER0 = 0x00;

/// Volatile TCON0 terminal-control register. R/W only.
static constexpr uint8_t REG_TCON0 = 0x04;

/// Lowest address in the documented memory map.
static constexpr uint8_t MIN_REGISTER = 0x00;

/// Highest address in the documented memory map.
static constexpr uint8_t MAX_REGISTER = 0xFF;

// ============================================================================
// Command Byte Format
// ============================================================================

enum class Command : uint8_t {
  WriteData = 0x00,
  Increment = 0x01,
  Decrement = 0x02,
  ReadData = 0x03
};

/// Build command byte: AD3:AD0[7:4], C1:C0[3:2], D9:D8[1:0].
static constexpr uint8_t makeCommand(uint8_t reg, Command command, uint8_t compatBits = 0) {
  return static_cast<uint8_t>(((reg & 0x0F) << 4) |
                              ((static_cast<uint8_t>(command) & 0x03) << 2) |
                              (compatBits & 0x03));
}

/// Number of bytes returned by the MCP45HVX1 read protocol.
static constexpr uint8_t READ_RESPONSE_LEN = 2;

/// Index of readback MSB compatibility byte.
static constexpr uint8_t READ_MSB_INDEX = 0;

/// Index of actual 8-bit register data in readback payload.
static constexpr uint8_t READ_LSB_INDEX = 1;

// ============================================================================
// Defaults and Limits
// ============================================================================

static constexpr uint8_t WIPER_MAX_7BIT = 0x7F;
static constexpr uint8_t WIPER_MAX_8BIT = 0xFF;
static constexpr uint8_t WIPER_DEFAULT_7BIT = 0x3F;
static constexpr uint8_t WIPER_DEFAULT_8BIT = 0x7F;
static constexpr uint8_t WIPER_ZERO = 0x00;

static constexpr uint8_t TCON_DEFAULT = 0xFF;
static constexpr uint8_t TCON_RESERVED_MASK = 0xF0;
static constexpr uint8_t TCON_R0HW = 0x08;
static constexpr uint8_t TCON_R0A = 0x04;
static constexpr uint8_t TCON_R0W = 0x02;
static constexpr uint8_t TCON_R0B = 0x01;
static constexpr uint8_t TCON_TERMINAL_MASK = static_cast<uint8_t>(TCON_R0A | TCON_R0W | TCON_R0B);
static constexpr uint8_t TCON_SHUTDOWN = static_cast<uint8_t>(TCON_RESERVED_MASK | TCON_TERMINAL_MASK);
static constexpr uint8_t TCON_POTENTIOMETER = TCON_DEFAULT;
static constexpr uint8_t TCON_RHEOSTAT_B_TO_W =
    static_cast<uint8_t>(TCON_RESERVED_MASK | TCON_R0HW | TCON_R0W | TCON_R0B);
static constexpr uint8_t TCON_RHEOSTAT_A_TO_W =
    static_cast<uint8_t>(TCON_RESERVED_MASK | TCON_R0HW | TCON_R0A | TCON_R0W);
static constexpr uint8_t TCON_WIPER_FLOATING =
    static_cast<uint8_t>(TCON_RESERVED_MASK | TCON_R0HW | TCON_R0A | TCON_R0B);

/// Maximum command bytes in a single continuous INC/DEC transaction.
static constexpr size_t MAX_COMMAND_CHUNK = 64;

// ============================================================================
// General Call Commands
// ============================================================================

static constexpr uint8_t GC_WRITE_WIPER0 = 0x80;
static constexpr uint8_t GC_WRITE_TCON0 = 0xC0;
static constexpr uint8_t GC_INCREMENT_WIPER0 = 0x84;
static constexpr uint8_t GC_DECREMENT_WIPER0 = 0x88;

}  // namespace cmd
}  // namespace MCP45HVX1

