/// @file MCP45HVX1.h
/// @brief Public API for the MCP45HVX1 high-voltage I2C digital potentiometer.
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
  Shutdown,      ///< Software shutdown via R0HW=0
  Custom         ///< Valid TCON state that does not match a named preset
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
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< Active 7-bit I2C address.
  Resolution resolution = Resolution::Bits8; ///< Configured wiper-code resolution.
  ResistanceOption resistance = ResistanceOption::R10K; ///< Nominal RAB option.
  uint8_t maxWiperCode = cmd::WIPER_MAX_8BIT; ///< Largest valid volatile wiper code.
  uint8_t defaultWiperCode = cmd::WIPER_DEFAULT_8BIT; ///< POR/BOR wiper code.
  uint32_t nominalResistanceOhms = 10000; ///< Nominal A-to-B resistance in ohms.
  float nominalStepOhms = 0.0f; ///< Ideal code-to-code resistance step in ohms.
  float maxTerminalCurrentMilliAmps = 12.5f; ///< Datasheet terminal-current limit.
  bool usingAlternateAddressRange = false; ///< True when using disputed 0x5C-0x5F range.
};

/// Static summary of the published silicon errata that affects bus planning.
struct SiliconErrataInfo {
  const char* documentNumber = "DS80000649B"; ///< Microchip errata document number.
  const char* documentRevision = "B (July 2015)"; ///< Errata revision used by this driver.
  const char* documentTitle = "MCP45HVX1 Rev. A1 Silicon/Data Sheet Errata"; ///< Source title.
  const char* markingSummary =
      "Rev A1 through 1518NNN; Rev A2/current sample starts 1520NNN"; ///< Marking guide.
  bool sharedBusI2cHazard = true; ///< Device may accept traffic addressed to another client.
  bool generalCallAddressDecodeHazard = true; ///< Invalid GC command address bits can alias.
  bool hardwareGeneralCallBitIgnored = true; ///< Hardware GC bit is not decoded as specified.
  bool uniqueBusWorkaroundForAffectedSilicon = true; ///< Errata workaround uses an isolated bus.
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
  ///
  /// By default this only reads Wiper/TCON to verify presence and cache state.
  /// Initial register writes are opt-in via Config::writeInitialWiper and
  /// Config::writeInitialTcon.
  ///
  /// @param config Transport callbacks, device address, variant, and startup policy.
  /// @return Status::Ok() when the device is present and the runtime cache is valid.
  Status begin(const Config& config);

  /// Process pending operations (currently a no-op).
  /// @param nowMs Current monotonic time in milliseconds.
  void tick(uint32_t nowMs);

  /// Shutdown the driver object. Does not modify the analog terminal state.
  void end();

  // =========================================================================
  // Diagnostics
  // =========================================================================

  /// Check if device is present on the bus without changing health counters.
  /// @return Status::Ok() on ACK/read success, otherwise transport or presence error.
  Status probe();

  /// Attempt to recover from DEGRADED/OFFLINE state by tracked register reads.
  /// @return Status::Ok() if Wiper/TCON were read and health returned online.
  Status recover();

  /// Run the optional board-provided I2C bus/software-reset callback.
  /// This resets only the I2C interface state, not Wiper/TCON registers.
  /// @return Status::Ok() when the callback succeeds, or UNSUPPORTED if none was provided.
  Status resetI2cState();

  /// Restore documented POR/BOR volatile register defaults for the configured resolution.
  /// @return Status::Ok() after Wiper/TCON defaults are written and cached.
  Status restorePowerOnDefaults();

  /// Backward-friendly alias for restorePowerOnDefaults().
  /// @return Result from restorePowerOnDefaults().
  Status resetToDefaults() { return restorePowerOnDefaults(); }

  // =========================================================================
  // Driver State
  // =========================================================================

  /// @return Current driver health state.
  DriverState state() const { return _driverState; }

  /// @return true after a successful begin() and before end().
  bool isInitialized() const { return _initialized; }

  /// @return true when the driver is READY or DEGRADED.
  bool isOnline() const {
    return _driverState == DriverState::READY || _driverState == DriverState::DEGRADED;
  }

  /// @return Active configuration snapshot.
  const Config& getConfig() const { return _config; }

  /// @return Copy of current settings, health counters, and register-cache state.
  SettingsSnapshot getSettings() const;

  /// Copy current settings, health counters, and register-cache state.
  /// @param out Receives the settings snapshot.
  /// @return Status::Ok(); this form matches the sibling driver snapshot API.
  Status getSettings(SettingsSnapshot& out) const;

  /// @return Static helper information for the configured variant.
  DeviceInfo getDeviceInfo() const;

  /// Published silicon errata summary for board-level and CLI diagnostics.
  /// @return Static errata summary derived from DS80000649B.
  static constexpr SiliconErrataInfo siliconErrataInfo() { return SiliconErrataInfo{}; }

  // =========================================================================
  // Health Tracking
  // =========================================================================

  /// @return Timestamp of the last tracked successful bus operation.
  uint32_t lastOkMs() const { return _lastOkMs; }

  /// @return Timestamp of the last tracked failure.
  uint32_t lastErrorMs() const { return _lastErrorMs; }

  /// @return Last tracked error status.
  Status lastError() const { return _lastError; }

  /// @return Current consecutive tracked failure count.
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }

  /// @return Total tracked failures since the last successful begin().
  uint32_t totalFailures() const { return _totalFailures; }

  /// @return Total tracked successes since the last successful begin().
  uint32_t totalSuccess() const { return _totalSuccess; }

  // =========================================================================
  // Wiper API
  // =========================================================================

  /// Read volatile Wiper 0.
  /// @param code Receives the raw wiper code.
  /// @return Status::Ok() on a valid readback.
  Status readWiper(uint8_t& code);

  /// Write volatile Wiper 0. Rejects codes outside the configured resolution.
  /// @param code New raw wiper code.
  /// @return Status::Ok() after the write is ACKed and cached.
  Status writeWiper(uint8_t code);

  /// Increment volatile Wiper 0 by one or more steps.
  /// @param steps Number of increment commands to send; zero is a no-op.
  /// @return Status::Ok() when all required step commands complete.
  Status incrementWiper(uint8_t steps = 1);

  /// Decrement volatile Wiper 0 by one or more steps.
  /// @param steps Number of decrement commands to send; zero is a no-op.
  /// @return Status::Ok() when all required step commands complete.
  Status decrementWiper(uint8_t steps = 1);

  /// Convert a normalized 0.0-1.0 position to the nearest valid wiper code.
  /// @param fraction Normalized target position; values outside range are clamped.
  /// @param resolution Device resolution variant.
  /// @return Nearest valid raw wiper code.
  static uint8_t codeFromFraction(float fraction, Resolution resolution);

  /// Convert a wiper code to normalized 0.0-1.0 position.
  /// @param code Raw wiper code.
  /// @param resolution Device resolution variant.
  /// @return Normalized position in the range 0.0 to 1.0 after code clamping.
  static float fractionFromCode(uint8_t code, Resolution resolution);

  /// Read volatile Wiper 0 as a normalized 0.0-1.0 position.
  /// @param fraction Receives the normalized position.
  /// @return Status::Ok() on a valid wiper read.
  Status readWiperFraction(float& fraction);

  /// Write volatile Wiper 0 from a normalized 0.0-1.0 position.
  /// @param fraction Normalized target position; values outside range are clamped.
  /// @return Status::Ok() after the resulting code is written.
  Status writeWiperFraction(float fraction);

  /// Maximum valid wiper code for a resolution.
  /// @param resolution Device resolution variant.
  /// @return 0x7F for 7-bit devices, 0xFF for 8-bit devices.
  static constexpr uint8_t maxWiperCode(Resolution resolution) {
    return resolution == Resolution::Bits7 ? cmd::WIPER_MAX_7BIT : cmd::WIPER_MAX_8BIT;
  }

  /// POR/BOR default wiper code for a resolution.
  /// @param resolution Device resolution variant.
  /// @return Documented volatile Wiper 0 reset code.
  static constexpr uint8_t defaultWiperCode(Resolution resolution) {
    return resolution == Resolution::Bits7 ? cmd::WIPER_DEFAULT_7BIT : cmd::WIPER_DEFAULT_8BIT;
  }

  /// Nominal end-to-end resistance in ohms for an ordering option.
  /// @param option Resistance ordering code.
  /// @return Nominal A-to-B resistance in ohms.
  static constexpr uint32_t nominalResistanceOhms(ResistanceOption option) {
    return option == ResistanceOption::R5K
               ? 5000
               : (option == ResistanceOption::R10K
                      ? 10000
                      : (option == ResistanceOption::R50K ? 50000 : 100000));
  }

  /// Ideal step resistance in ohms. Excludes tolerance, wiper resistance, INL/DNL, and leakage.
  /// @param option Resistance ordering code.
  /// @param resolution Device resolution variant.
  /// @return Ideal end-to-end resistance divided by the maximum code.
  static float stepResistanceOhms(ResistanceOption option, Resolution resolution);

  /// Datasheet maximum terminal current in mA for a nominal RAB option.
  /// @param option Resistance ordering code.
  /// @return Terminal-current limit in milliamps.
  static constexpr float maxTerminalCurrentMilliAmps(ResistanceOption option) {
    return option == ResistanceOption::R5K
               ? 25.0f
               : (option == ResistanceOption::R10K ? 12.5f : 6.5f);
  }

  /// Ideal B-to-W resistance in ohms for a wiper code.
  /// @param code Raw wiper code.
  /// @param option Resistance ordering code.
  /// @param resolution Device resolution variant.
  /// @return Ideal B-to-W resistance in ohms.
  static float resistanceBToWOhms(uint8_t code, ResistanceOption option, Resolution resolution);

  /// Ideal A-to-W resistance in ohms for a wiper code.
  /// @param code Raw wiper code.
  /// @param option Resistance ordering code.
  /// @param resolution Device resolution variant.
  /// @return Ideal A-to-W resistance in ohms.
  static float resistanceAToWOhms(uint8_t code, ResistanceOption option, Resolution resolution);

  // =========================================================================
  // Terminal Control API
  // =========================================================================

  /// Read volatile TCON0.
  /// @param value Receives the raw TCON0 byte.
  /// @return Status::Ok() on a valid readback.
  Status readTcon(uint8_t& value);

  /// Write volatile TCON0. Reserved upper bits are forced to 1.
  /// @param value Requested TCON0 byte; reserved bits are sanitized before write.
  /// @return Status::Ok() after the sanitized value is written and cached.
  Status writeTcon(uint8_t value);

  /// Configure one terminal's connection bit.
  /// @param terminal Terminal selector.
  /// @param enabled true to connect the terminal switch, false to open it.
  /// @return Status::Ok() after TCON0 is updated.
  Status setTerminalEnabled(Terminal terminal, bool enabled);

  /// Read one terminal's connection bit from TCON0.
  /// @param terminal Terminal selector.
  /// @param enabled Receives the decoded terminal switch state.
  /// @return Status::Ok() on a valid TCON0 read.
  Status getTerminalEnabled(Terminal terminal, bool& enabled);

  /// Enable or disable software shutdown via R0HW.
  /// @param enabled true to enter software shutdown, false to keep R0HW connected.
  /// @return Status::Ok() after TCON0 is updated.
  Status setSoftwareShutdown(bool enabled);

  /// Read software shutdown bit state from TCON0.
  /// @param enabled Receives true when software shutdown is active.
  /// @return Status::Ok() on a valid TCON0 read.
  Status getSoftwareShutdown(bool& enabled);

  /// Apply a common terminal connection preset.
  /// @param mode Named terminal preset. TerminalMode::Custom is rejected.
  /// @return Status::Ok() after TCON0 is updated.
  Status setTerminalMode(TerminalMode mode);

  /// Decode the current TCON0 register into a terminal mode.
  /// @param mode Receives the best-fit decoded terminal preset.
  /// @return Status::Ok() on a valid TCON0 read.
  Status getTerminalMode(TerminalMode& mode);

  /// Read and decode TCON0.
  /// @param status Receives terminal bit states and decoded mode.
  /// @return Status::Ok() on a valid TCON0 read.
  Status readTerminalStatus(TerminalStatus& status);

  /// Read both implemented registers in sequence.
  /// @param snapshot Receives Wiper 0 and TCON0 values.
  /// @return Status::Ok() only if both reads succeed.
  Status readSnapshot(RegisterSnapshot& snapshot);

  /// Sanitize TCON for writes by forcing reserved bits [7:4] to 1.
  /// @param value Requested raw TCON0 value.
  /// @return TCON0 value with reserved upper bits set.
  static constexpr uint8_t sanitizeTcon(uint8_t value) {
    return static_cast<uint8_t>(value | cmd::TCON_RESERVED_MASK);
  }

  /// Decode a sanitized TCON value.
  /// @param value Raw or sanitized TCON0 value.
  /// @return Decoded terminal status and best-fit terminal mode.
  static TerminalStatus decodeTcon(uint8_t value);

  // =========================================================================
  // Register Access (Direct)
  // =========================================================================

  /// Read a documented volatile register directly.
  /// @param reg Register address; only implemented volatile registers are accepted.
  /// @param value Receives the raw register byte.
  /// @return Status::Ok() on a valid register read.
  Status readRegister(uint8_t reg, uint8_t& value);

  /// Write a documented volatile register directly.
  /// @param reg Register address; only writable volatile registers are accepted.
  /// @param value Raw value. TCON0 writes are sanitized.
  /// @return Status::Ok() after the write is ACKed and cached.
  Status writeRegister(uint8_t reg, uint8_t value);

  /// Read the register addressed by the device's current address pointer.
  /// @param value Receives the raw byte read using the last-address format.
  /// @return Status::Ok() on a valid readback.
  Status readLastAddress(uint8_t& value);

  // =========================================================================
  // General Call Helpers
  // =========================================================================

  /// Broadcast a Wiper 0 write using the General Call address.
  /// On ACK, the local Wiper cache is marked unknown because ACK is not device-specific.
  /// @param code Raw wiper code to broadcast.
  /// @return Status::Ok() if the broadcast write was ACKed by the bus.
  Status generalCallWriteWiper(uint8_t code);

  /// Broadcast a TCON0 write using the General Call address.
  /// On ACK, the local TCON cache is marked unknown because ACK is not device-specific.
  /// @param value Raw TCON0 value; reserved bits are sanitized before broadcast.
  /// @return Status::Ok() if the broadcast write was ACKed by the bus.
  Status generalCallWriteTcon(uint8_t value);

  /// Broadcast a Wiper 0 increment using the General Call address.
  /// On ACK, the local Wiper cache is marked unknown because ACK is not device-specific.
  /// @return Status::Ok() if the broadcast command was ACKed by the bus.
  Status generalCallIncrementWiper();

  /// Broadcast a Wiper 0 decrement using the General Call address.
  /// On ACK, the local Wiper cache is marked unknown because ACK is not device-specific.
  /// @return Status::Ok() if the broadcast command was ACKed by the bus.
  Status generalCallDecrementWiper();

private:
  // Transport wrappers
  Status _i2cWriteReadRaw(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                          uint8_t* rxBuf, size_t rxLen);
  Status _i2cWriteRaw(uint8_t addr, const uint8_t* buf, size_t len);
  Status _i2cWriteReadTracked(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                              uint8_t* rxBuf, size_t rxLen);
  Status _i2cWriteTracked(uint8_t addr, const uint8_t* buf, size_t len);
  Status _busResetTracked(bool trackSuccess);

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
  static bool _isValidResistanceOption(ResistanceOption option);
  static bool _isValidRegister(uint8_t reg);
  static bool _isWritableRegister(uint8_t reg);
  static bool _isValidWiperCode(uint8_t code, Resolution resolution);
  static uint8_t _terminalMask(Terminal terminal);
  static uint8_t _tconForMode(TerminalMode mode);
  void _syncRegister(uint8_t reg, uint8_t value);
  void _clearCachedRegisters();

  // Health management
  Status _offlineStatus() const;
  void _reassertOfflineLatch();
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
