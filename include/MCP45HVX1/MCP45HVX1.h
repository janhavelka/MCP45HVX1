/// @file MCP45HVX1.h
/// @brief Main driver class for MCP45HVX1 high-voltage I2C digital potentiometer
#pragma once

#include <cstddef>
#include <cstdint>

#include "MCP45HVX1/CommandTable.h"
#include "MCP45HVX1/Config.h"
#include "MCP45HVX1/Status.h"
#include "MCP45HVX1/Version.h"

namespace MCP45HVX1 {

/// Driver state for health monitoring.
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// Terminal selector for TCON helpers.
enum class Terminal : uint8_t {
  A,  ///< P0A terminal
  W,  ///< P0W wiper terminal
  B   ///< P0B terminal
};

/// Common terminal connection presets.
enum class TerminalMode : uint8_t {
  Potentiometer, ///< A, W, and B connected
  RheostatBToW,  ///< P0A disconnected; P0B and P0W connected
  RheostatAToW,  ///< P0B disconnected; P0A and P0W connected
  WiperFloating, ///< P0W disconnected; P0A and P0B connected
  Shutdown       ///< Software shutdown via R0HW=0
};

/// Snapshot of current device registers.
struct RegisterSnapshot {
  uint8_t wiper = 0; ///< Volatile Wiper 0 register
  uint8_t tcon = 0;  ///< Volatile TCON0 register
};

/// Decoded terminal-control register fields.
struct TerminalStatus {
  bool softwareShutdown = false; ///< true when R0HW=0
  bool terminalA = false;        ///< P0A connection bit
  bool terminalW = false;        ///< P0W connection bit
  bool terminalB = false;        ///< P0B connection bit
  TerminalMode mode = TerminalMode::Potentiometer; ///< Best-fit decoded mode
};

/// Static device/configuration information derived from Config.
struct DeviceInfo {
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS;
  Resolution resolution = Resolution::Bits8;
  ResistanceOption resistance = ResistanceOption::R10K;
  uint8_t maxWiperCode = cmd::WIPER_MAX_8BIT;
  uint8_t defaultWiperCode = cmd::WIPER_DEFAULT_8BIT;
  uint16_t nominalResistanceOhms = 10000;
  float nominalStepOhms = 0.0f;
  bool usingAlternateAddressRange = false;
};

/// Snapshot of the current driver settings and health state.
struct SettingsSnapshot {
  Config config;                 ///< Active runtime configuration snapshot
  DriverState state = DriverState::UNINIT;
  bool initialized = false;
  uint32_t lastOkMs = 0;
  uint32_t lastErrorMs = 0;
  Status lastError = Status::Ok();
  uint8_t consecutiveFailures = 0;
  uint32_t totalFailures = 0;
  uint32_t totalSuccess = 0;
  bool cachedWiperKnown = false;
  uint8_t cachedWiper = 0;
  bool cachedTconKnown = false;
  uint8_t cachedTcon = 0;
  bool addressPointerKnown = false;
  uint8_t addressPointer = cmd::REG_WIPER0;
};

/// MCP45HVX1 driver class.
class MCP45HVX1 {
public:
  // =========================================================================
  // Lifecycle
  // =========================================================================

  /// Initialize the driver with configuration.
  /// By default this only reads Wiper/TCON to verify presence and cache state.
  /// Initial register writes are opt-in via Config::writeInitialWiper/Tcon.
  Status begin(const Config& config);

  /// Process pending operations (currently a no-op).
  void tick(uint32_t nowMs);

  /// Shutdown the driver object. Does not modify the analog terminal state.
  void end();

  // =========================================================================
  // Diagnostics
  // =========================================================================

  /// Check if device is present on the bus (no health tracking).
  Status probe();

  /// Attempt to recover from DEGRADED/OFFLINE state by tracked register reads.
  Status recover();

  /// Run the optional board-provided I2C bus/software-reset callback.
  /// This resets only the I2C interface state, not Wiper/TCON registers.
  Status resetI2cState();

  /// Restore documented POR/BOR volatile register defaults for the configured resolution.
  Status restorePowerOnDefaults();

  /// Backward-friendly alias for restorePowerOnDefaults().
  Status resetToDefaults() { return restorePowerOnDefaults(); }

  // =========================================================================
  // Driver State
  // =========================================================================

  DriverState state() const { return _driverState; }
  bool isInitialized() const { return _initialized; }
  bool isOnline() const {
    return _driverState == DriverState::READY || _driverState == DriverState::DEGRADED;
  }
  const Config& getConfig() const { return _config; }
  SettingsSnapshot getSettings() const;
  DeviceInfo getDeviceInfo() const;

  // =========================================================================
  // Health Tracking
  // =========================================================================

  uint32_t lastOkMs() const { return _lastOkMs; }
  uint32_t lastErrorMs() const { return _lastErrorMs; }
  Status lastError() const { return _lastError; }
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  uint32_t totalFailures() const { return _totalFailures; }
  uint32_t totalSuccess() const { return _totalSuccess; }

  // =========================================================================
  // Wiper API
  // =========================================================================

  /// Read volatile Wiper 0.
  Status readWiper(uint8_t& code);

  /// Write volatile Wiper 0. Rejects codes outside the configured resolution.
  Status writeWiper(uint8_t code);

  /// Increment volatile Wiper 0 by one or more steps.
  Status incrementWiper(uint8_t steps = 1);

  /// Decrement volatile Wiper 0 by one or more steps.
  Status decrementWiper(uint8_t steps = 1);

  /// Convert a normalized 0.0-1.0 position to the nearest valid wiper code.
  static uint8_t codeFromFraction(float fraction, Resolution resolution);

  /// Convert a wiper code to normalized 0.0-1.0 position.
  static float fractionFromCode(uint8_t code, Resolution resolution);

  /// Read volatile Wiper 0 as a normalized 0.0-1.0 position.
  Status readWiperFraction(float& fraction);

  /// Write volatile Wiper 0 from a normalized 0.0-1.0 position.
  Status writeWiperFraction(float fraction);

  /// Maximum valid wiper code for a resolution.
  static constexpr uint8_t maxWiperCode(Resolution resolution) {
    return resolution == Resolution::Bits7 ? cmd::WIPER_MAX_7BIT : cmd::WIPER_MAX_8BIT;
  }

  /// POR/BOR default wiper code for a resolution.
  static constexpr uint8_t defaultWiperCode(Resolution resolution) {
    return resolution == Resolution::Bits7 ? cmd::WIPER_DEFAULT_7BIT : cmd::WIPER_DEFAULT_8BIT;
  }

  /// Nominal end-to-end resistance in ohms for an ordering option.
  static constexpr uint16_t nominalResistanceOhms(ResistanceOption option) {
    return option == ResistanceOption::R5K
               ? 5000
               : (option == ResistanceOption::R10K
                      ? 10000
                      : (option == ResistanceOption::R50K ? 50000 : 100000));
  }

  /// Ideal step resistance in ohms. Excludes tolerance, wiper resistance, INL/DNL, and leakage.
  static float stepResistanceOhms(ResistanceOption option, Resolution resolution);

  /// Ideal B-to-W resistance in ohms for a wiper code.
  static float resistanceBToWOhms(uint8_t code, ResistanceOption option, Resolution resolution);

  /// Ideal A-to-W resistance in ohms for a wiper code.
  static float resistanceAToWOhms(uint8_t code, ResistanceOption option, Resolution resolution);

  // =========================================================================
  // Terminal Control API
  // =========================================================================

  /// Read volatile TCON0.
  Status readTcon(uint8_t& value);

  /// Write volatile TCON0. Reserved upper bits are forced to 1.
  Status writeTcon(uint8_t value);

  /// Configure one terminal's connection bit.
  Status setTerminalEnabled(Terminal terminal, bool enabled);

  /// Read one terminal's connection bit from TCON0.
  Status getTerminalEnabled(Terminal terminal, bool& enabled);

  /// Enable or disable software shutdown via R0HW.
  Status setSoftwareShutdown(bool enabled);

  /// Read software shutdown bit state from TCON0.
  Status getSoftwareShutdown(bool& enabled);

  /// Apply a common terminal connection preset.
  Status setTerminalMode(TerminalMode mode);

  /// Decode the current TCON0 register into a terminal mode.
  Status getTerminalMode(TerminalMode& mode);

  /// Read and decode TCON0.
  Status readTerminalStatus(TerminalStatus& status);

  /// Read both implemented registers in sequence.
  Status readSnapshot(RegisterSnapshot& snapshot);

  /// Sanitize TCON for writes by forcing reserved bits [7:4] to 1.
  static constexpr uint8_t sanitizeTcon(uint8_t value) {
    return static_cast<uint8_t>(value | cmd::TCON_RESERVED_MASK);
  }

  /// Decode a sanitized TCON value.
  static TerminalStatus decodeTcon(uint8_t value);

  // =========================================================================
  // Register Access (Direct)
  // =========================================================================

  Status readRegister(uint8_t reg, uint8_t& value);
  Status writeRegister(uint8_t reg, uint8_t value);

  /// Read the register addressed by the device's current address pointer.
  Status readLastAddress(uint8_t& value);

  // =========================================================================
  // General Call Helpers
  // =========================================================================

  /// Broadcast a Wiper 0 write using the General Call address.
  Status generalCallWriteWiper(uint8_t code);

  /// Broadcast a TCON0 write using the General Call address.
  Status generalCallWriteTcon(uint8_t value);

  /// Broadcast a Wiper 0 increment using the General Call address.
  Status generalCallIncrementWiper();

  /// Broadcast a Wiper 0 decrement using the General Call address.
  Status generalCallDecrementWiper();

private:
  // Transport wrappers
  Status _i2cWriteReadRaw(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                          uint8_t* rxBuf, size_t rxLen);
  Status _i2cWriteRaw(uint8_t addr, const uint8_t* buf, size_t len);
  Status _i2cWriteReadTracked(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                              uint8_t* rxBuf, size_t rxLen);
  Status _i2cWriteTracked(uint8_t addr, const uint8_t* buf, size_t len);

  // Register helpers
  Status _readRegisterRaw(uint8_t reg, uint8_t& value);
  Status _readRegisterTracked(uint8_t reg, uint8_t& value);
  Status _readLastAddressTracked(uint8_t& value);
  Status _writeRegisterRaw(uint8_t reg, uint8_t value);
  Status _writeRegisterTracked(uint8_t reg, uint8_t value);
  Status _sendWiperStepCommand(cmd::Command command, uint8_t steps);
  Status _generalCallWrite(uint8_t commandByte, const uint8_t* data, size_t len);

  // Validation and state helpers
  static bool _isValidAddress(uint8_t address);
  static bool _isPrimaryAddress(uint8_t address);
  static bool _isAlternateAddress(uint8_t address);
  static bool _isValidRegister(uint8_t reg);
  static bool _isWritableRegister(uint8_t reg);
  static bool _isValidWiperCode(uint8_t code, Resolution resolution);
  static uint8_t _terminalMask(Terminal terminal);
  static uint8_t _tconForMode(TerminalMode mode);
  void _syncRegister(uint8_t reg, uint8_t value);
  void _clearCachedRegisters();

  // Health management
  Status _updateHealth(const Status& st);
  Status _recordFailure(const Status& st);
  uint32_t _nowMs() const;

  Config _config;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;

  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;

  bool _cachedWiperKnown = false;
  uint8_t _cachedWiper = 0;
  bool _cachedTconKnown = false;
  uint8_t _cachedTcon = cmd::TCON_DEFAULT;
  bool _addressPointerKnown = true;
  uint8_t _addressPointer = cmd::REG_WIPER0;
};

}  // namespace MCP45HVX1
