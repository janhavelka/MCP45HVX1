/**
 * @file MCP45HVX1.cpp
 * @brief MCP45HVX1 high-voltage I2C digital potentiometer driver implementation.
 */

#include "MCP45HVX1/MCP45HVX1.h"

#include <Arduino.h>
#include <cstring>
#include <limits>

namespace MCP45HVX1 {
namespace {

static constexpr uint8_t INVALID_TERMINAL_MASK = 0x00;

uint8_t boundedAdd(uint8_t value, uint8_t delta, uint8_t maxValue) {
  const uint16_t sum = static_cast<uint16_t>(value) + delta;
  return sum > maxValue ? maxValue : static_cast<uint8_t>(sum);
}

uint8_t boundedSub(uint8_t value, uint8_t delta) {
  return delta > value ? 0 : static_cast<uint8_t>(value - delta);
}

}  // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

Status MCP45HVX1::begin(const Config& config) {
  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;

  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _clearCachedRegisters();
  _addressPointerKnown = true;
  _addressPointer = cmd::REG_WIPER0;

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (config.i2cTimeoutMs == 0) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout must be > 0");
  }
  if (!_isValidAddress(config.i2cAddress) ||
      (_isAlternateAddress(config.i2cAddress) && !config.allowAlternateAddressRange)) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C address");
  }
  if (config.resolution != Resolution::Bits7 && config.resolution != Resolution::Bits8) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid resolution");
  }
  if (config.writeInitialWiper &&
      !_isValidWiperCode(config.initialWiperCode, config.resolution)) {
    return Status::Error(Err::INVALID_CONFIG, "Initial wiper code exceeds resolution");
  }

  _config = config;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }

  uint8_t wiper = 0;
  Status st = _readRegisterRaw(cmd::REG_WIPER0, wiper);
  if (!st.ok()) {
    if (st.code == Err::REGISTER_MISMATCH) {
      return st;
    }
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }

  uint8_t tcon = 0;
  st = _readRegisterRaw(cmd::REG_TCON0, tcon);
  if (!st.ok()) {
    if (st.code == Err::REGISTER_MISMATCH) {
      return st;
    }
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }

  if (_config.requirePowerOnDefaults) {
    if (wiper != defaultWiperCode(_config.resolution)) {
      return Status::Error(Err::REGISTER_MISMATCH, "Wiper not at POR/BOR default", wiper);
    }
    if (tcon != cmd::TCON_DEFAULT) {
      return Status::Error(Err::REGISTER_MISMATCH, "TCON not at POR/BOR default", tcon);
    }
  }

  _syncRegister(cmd::REG_WIPER0, wiper);
  _syncRegister(cmd::REG_TCON0, tcon);

  if (_config.writeInitialTcon) {
    st = _writeRegisterRaw(cmd::REG_TCON0, _config.initialTcon);
    if (!st.ok()) {
      return st;
    }
    _syncRegister(cmd::REG_TCON0, sanitizeTcon(_config.initialTcon));
  }

  if (_config.writeInitialWiper) {
    st = _writeRegisterRaw(cmd::REG_WIPER0, _config.initialWiperCode);
    if (!st.ok()) {
      return st;
    }
    _syncRegister(cmd::REG_WIPER0, _config.initialWiperCode);
  }

  _initialized = true;
  _driverState = DriverState::READY;
  return Status::Ok();
}

void MCP45HVX1::tick(uint32_t nowMs) {
  (void)nowMs;
  // MCP45HVX1 has no documented write-cycle delay or async operation.
}

void MCP45HVX1::end() {
  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _clearCachedRegisters();
  _addressPointerKnown = true;
  _addressPointer = cmd::REG_WIPER0;
}

SettingsSnapshot MCP45HVX1::getSettings() const {
  SettingsSnapshot snapshot;
  snapshot.config = _config;
  snapshot.state = _driverState;
  snapshot.initialized = _initialized;
  snapshot.lastOkMs = _lastOkMs;
  snapshot.lastErrorMs = _lastErrorMs;
  snapshot.lastError = _lastError;
  snapshot.consecutiveFailures = _consecutiveFailures;
  snapshot.totalFailures = _totalFailures;
  snapshot.totalSuccess = _totalSuccess;
  snapshot.cachedWiperKnown = _cachedWiperKnown;
  snapshot.cachedWiper = _cachedWiper;
  snapshot.cachedTconKnown = _cachedTconKnown;
  snapshot.cachedTcon = _cachedTcon;
  snapshot.addressPointerKnown = _addressPointerKnown;
  snapshot.addressPointer = _addressPointer;
  return snapshot;
}

DeviceInfo MCP45HVX1::getDeviceInfo() const {
  DeviceInfo info;
  info.i2cAddress = _config.i2cAddress;
  info.resolution = _config.resolution;
  info.resistance = _config.resistance;
  info.maxWiperCode = maxWiperCode(_config.resolution);
  info.defaultWiperCode = defaultWiperCode(_config.resolution);
  info.nominalResistanceOhms = nominalResistanceOhms(_config.resistance);
  info.nominalStepOhms = stepResistanceOhms(_config.resistance, _config.resolution);
  info.usingAlternateAddressRange = _isAlternateAddress(_config.i2cAddress);
  return info;
}

// ===========================================================================
// Diagnostics
// ===========================================================================

Status MCP45HVX1::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t value = 0;
  Status st = _readRegisterRaw(cmd::REG_WIPER0, value);
  if (!st.ok()) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }
  return Status::Ok();
}

Status MCP45HVX1::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  RegisterSnapshot snapshot;
  Status st = readSnapshot(snapshot);
  if (!st.ok()) {
    return st;
  }
  return Status::Ok();
}

Status MCP45HVX1::resetI2cState() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_config.busReset == nullptr) {
    return Status::Error(Err::UNSUPPORTED, "I2C bus reset callback not configured");
  }

  Status st = _config.busReset(_config.controlUser);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM ||
      st.code == Err::UNSUPPORTED) {
    return st;
  }
  if (!st.ok()) {
    return _updateHealth(st);
  }

  _addressPointerKnown = false;
  _addressPointer = cmd::REG_WIPER0;
  return _updateHealth(st);
}

Status MCP45HVX1::restorePowerOnDefaults() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  Status st = writeTcon(cmd::TCON_DEFAULT);
  if (!st.ok()) {
    return st;
  }
  return writeWiper(defaultWiperCode(_config.resolution));
}

// ===========================================================================
// Wiper API
// ===========================================================================

Status MCP45HVX1::readWiper(uint8_t& code) {
  return readRegister(cmd::REG_WIPER0, code);
}

Status MCP45HVX1::writeWiper(uint8_t code) {
  return writeRegister(cmd::REG_WIPER0, code);
}

Status MCP45HVX1::incrementWiper(uint8_t steps) {
  return _sendWiperStepCommand(cmd::Command::Increment, steps);
}

Status MCP45HVX1::decrementWiper(uint8_t steps) {
  return _sendWiperStepCommand(cmd::Command::Decrement, steps);
}

uint8_t MCP45HVX1::codeFromFraction(float fraction, Resolution resolution) {
  const uint8_t maxCode = maxWiperCode(resolution);
  if (!(fraction > 0.0f)) {
    return 0;
  }
  if (fraction >= 1.0f) {
    return maxCode;
  }
  return static_cast<uint8_t>((fraction * static_cast<float>(maxCode)) + 0.5f);
}

float MCP45HVX1::fractionFromCode(uint8_t code, Resolution resolution) {
  const uint8_t maxCode = maxWiperCode(resolution);
  const uint8_t bounded = code > maxCode ? maxCode : code;
  return static_cast<float>(bounded) / static_cast<float>(maxCode);
}

Status MCP45HVX1::readWiperFraction(float& fraction) {
  uint8_t code = 0;
  Status st = readWiper(code);
  if (!st.ok()) {
    return st;
  }
  fraction = fractionFromCode(code, _config.resolution);
  return Status::Ok();
}

Status MCP45HVX1::writeWiperFraction(float fraction) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!(fraction >= 0.0f && fraction <= 1.0f)) {
    return Status::Error(Err::INVALID_PARAM, "Wiper fraction must be 0.0-1.0");
  }
  return writeWiper(codeFromFraction(fraction, _config.resolution));
}

float MCP45HVX1::stepResistanceOhms(ResistanceOption option, Resolution resolution) {
  const uint8_t maxCode = maxWiperCode(resolution);
  return static_cast<float>(nominalResistanceOhms(option)) / static_cast<float>(maxCode);
}

float MCP45HVX1::resistanceBToWOhms(uint8_t code, ResistanceOption option,
                                    Resolution resolution) {
  return fractionFromCode(code, resolution) * static_cast<float>(nominalResistanceOhms(option));
}

float MCP45HVX1::resistanceAToWOhms(uint8_t code, ResistanceOption option,
                                    Resolution resolution) {
  return (1.0f - fractionFromCode(code, resolution)) *
         static_cast<float>(nominalResistanceOhms(option));
}

// ===========================================================================
// Terminal Control API
// ===========================================================================

Status MCP45HVX1::readTcon(uint8_t& value) {
  return readRegister(cmd::REG_TCON0, value);
}

Status MCP45HVX1::writeTcon(uint8_t value) {
  return writeRegister(cmd::REG_TCON0, sanitizeTcon(value));
}

Status MCP45HVX1::setTerminalEnabled(Terminal terminal, bool enabled) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  const uint8_t mask = _terminalMask(terminal);
  if (mask == INVALID_TERMINAL_MASK) {
    return Status::Error(Err::INVALID_PARAM, "Invalid terminal");
  }

  uint8_t tcon = _cachedTcon;
  if (!_cachedTconKnown) {
    Status st = readTcon(tcon);
    if (!st.ok()) {
      return st;
    }
  }

  uint8_t next = tcon;
  if (enabled) {
    next = static_cast<uint8_t>(next | mask);
  } else {
    next = static_cast<uint8_t>(next & ~mask);
  }
  next = sanitizeTcon(next);

  if (next == tcon) {
    return Status::Ok();
  }
  return writeTcon(next);
}

Status MCP45HVX1::getTerminalEnabled(Terminal terminal, bool& enabled) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  const uint8_t mask = _terminalMask(terminal);
  if (mask == INVALID_TERMINAL_MASK) {
    return Status::Error(Err::INVALID_PARAM, "Invalid terminal");
  }

  uint8_t tcon = 0;
  Status st = readTcon(tcon);
  if (!st.ok()) {
    return st;
  }
  enabled = (tcon & mask) != 0;
  return Status::Ok();
}

Status MCP45HVX1::setSoftwareShutdown(bool enabled) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t tcon = _cachedTcon;
  if (!_cachedTconKnown) {
    Status st = readTcon(tcon);
    if (!st.ok()) {
      return st;
    }
  }

  uint8_t next = tcon;
  if (enabled) {
    next = static_cast<uint8_t>(next & ~cmd::TCON_R0HW);
  } else {
    next = static_cast<uint8_t>(next | cmd::TCON_R0HW);
  }
  next = sanitizeTcon(next);

  if (next == tcon) {
    return Status::Ok();
  }
  return writeTcon(next);
}

Status MCP45HVX1::getSoftwareShutdown(bool& enabled) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t tcon = 0;
  Status st = readTcon(tcon);
  if (!st.ok()) {
    return st;
  }
  enabled = (tcon & cmd::TCON_R0HW) == 0;
  return Status::Ok();
}

Status MCP45HVX1::setTerminalMode(TerminalMode mode) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  return writeTcon(_tconForMode(mode));
}

Status MCP45HVX1::getTerminalMode(TerminalMode& mode) {
  TerminalStatus status;
  Status st = readTerminalStatus(status);
  if (!st.ok()) {
    return st;
  }
  mode = status.mode;
  return Status::Ok();
}

Status MCP45HVX1::readTerminalStatus(TerminalStatus& status) {
  uint8_t tcon = 0;
  Status st = readTcon(tcon);
  if (!st.ok()) {
    return st;
  }
  status = decodeTcon(tcon);
  return Status::Ok();
}

Status MCP45HVX1::readSnapshot(RegisterSnapshot& snapshot) {
  Status st = readWiper(snapshot.wiper);
  if (!st.ok()) {
    return st;
  }
  return readTcon(snapshot.tcon);
}

// ===========================================================================
// Register Access (Direct)
// ===========================================================================

Status MCP45HVX1::readRegister(uint8_t reg, uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_isValidRegister(reg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address is reserved");
  }

  Status st = _readRegisterTracked(reg, value);
  if (!st.ok()) {
    return st;
  }
  _syncRegister(reg, value);
  return Status::Ok();
}

Status MCP45HVX1::writeRegister(uint8_t reg, uint8_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_isWritableRegister(reg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address is not writable");
  }
  if (reg == cmd::REG_WIPER0 && !_isValidWiperCode(value, _config.resolution)) {
    return Status::Error(Err::INVALID_PARAM, "Wiper code exceeds configured resolution", value);
  }

  const uint8_t sanitized = (reg == cmd::REG_TCON0) ? sanitizeTcon(value) : value;
  Status st = _writeRegisterTracked(reg, sanitized);
  if (!st.ok()) {
    return st;
  }
  _syncRegister(reg, sanitized);
  return Status::Ok();
}

Status MCP45HVX1::readLastAddress(uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  return _readLastAddressTracked(value);
}

// ===========================================================================
// General Call Helpers
// ===========================================================================

Status MCP45HVX1::generalCallWriteWiper(uint8_t code) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_isValidWiperCode(code, _config.resolution)) {
    return Status::Error(Err::INVALID_PARAM, "Wiper code exceeds configured resolution", code);
  }

  Status st = _generalCallWrite(cmd::GC_WRITE_WIPER0, &code, 1);
  if (st.ok()) {
    _syncRegister(cmd::REG_WIPER0, code);
  }
  return st;
}

Status MCP45HVX1::generalCallWriteTcon(uint8_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  const uint8_t sanitized = sanitizeTcon(value);
  Status st = _generalCallWrite(cmd::GC_WRITE_TCON0, &sanitized, 1);
  if (st.ok()) {
    _syncRegister(cmd::REG_TCON0, sanitized);
  }
  return st;
}

Status MCP45HVX1::generalCallIncrementWiper() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  Status st = _generalCallWrite(cmd::GC_INCREMENT_WIPER0, nullptr, 0);
  if (st.ok() && _cachedWiperKnown) {
    _cachedWiper = boundedAdd(_cachedWiper, 1, maxWiperCode(_config.resolution));
  }
  return st;
}

Status MCP45HVX1::generalCallDecrementWiper() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  Status st = _generalCallWrite(cmd::GC_DECREMENT_WIPER0, nullptr, 0);
  if (st.ok() && _cachedWiperKnown) {
    _cachedWiper = boundedSub(_cachedWiper, 1);
  }
  return st;
}

// ===========================================================================
// Transport Wrappers
// ===========================================================================

Status MCP45HVX1::_i2cWriteReadRaw(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                                   uint8_t* rxBuf, size_t rxLen) {
  if ((txLen > 0 && txBuf == nullptr) || rxBuf == nullptr || rxLen == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C read buffer");
  }
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write-read not set");
  }
  return _config.i2cWriteRead(addr, txBuf, txLen, rxBuf, rxLen,
                              _config.i2cTimeoutMs, _config.i2cUser);
}

Status MCP45HVX1::_i2cWriteRaw(uint8_t addr, const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C write buffer");
  }
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  return _config.i2cWrite(addr, buf, len, _config.i2cTimeoutMs, _config.i2cUser);
}

Status MCP45HVX1::_i2cWriteReadTracked(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                                       uint8_t* rxBuf, size_t rxLen) {
  if ((txLen > 0 && txBuf == nullptr) || rxBuf == nullptr || rxLen == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C read buffer");
  }

  Status st = _i2cWriteReadRaw(addr, txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status MCP45HVX1::_i2cWriteTracked(uint8_t addr, const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C write buffer");
  }

  Status st = _i2cWriteRaw(addr, buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

// ===========================================================================
// Register Helpers
// ===========================================================================

Status MCP45HVX1::_readRegisterRaw(uint8_t reg, uint8_t& value) {
  if (!_isValidRegister(reg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address is reserved");
  }

  const uint8_t command = cmd::makeCommand(reg, cmd::Command::ReadData);
  uint8_t rx[cmd::READ_RESPONSE_LEN] = {};
  Status st = _i2cWriteReadRaw(_config.i2cAddress, &command, 1, rx, sizeof(rx));
  if (!st.ok()) {
    return st;
  }
  if (_config.requireReadMsbZero && rx[cmd::READ_MSB_INDEX] != 0x00) {
    return Status::Error(Err::REGISTER_MISMATCH, "Read MSB byte is not zero",
                         rx[cmd::READ_MSB_INDEX]);
  }
  value = rx[cmd::READ_LSB_INDEX];
  _addressPointerKnown = true;
  _addressPointer = reg;
  return Status::Ok();
}

Status MCP45HVX1::_readRegisterTracked(uint8_t reg, uint8_t& value) {
  if (!_isValidRegister(reg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address is reserved");
  }

  const uint8_t command = cmd::makeCommand(reg, cmd::Command::ReadData);
  uint8_t rx[cmd::READ_RESPONSE_LEN] = {};
  Status st = _i2cWriteReadRaw(_config.i2cAddress, &command, 1, rx, sizeof(rx));
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  if (!st.ok()) {
    return _updateHealth(st);
  }
  if (_config.requireReadMsbZero && rx[cmd::READ_MSB_INDEX] != 0x00) {
    return _recordFailure(Status::Error(Err::REGISTER_MISMATCH, "Read MSB byte is not zero",
                                        rx[cmd::READ_MSB_INDEX]));
  }
  st = _updateHealth(st);
  if (!st.ok()) {
    return st;
  }
  value = rx[cmd::READ_LSB_INDEX];
  _addressPointerKnown = true;
  _addressPointer = reg;
  return Status::Ok();
}

Status MCP45HVX1::_readLastAddressTracked(uint8_t& value) {
  uint8_t rx[cmd::READ_RESPONSE_LEN] = {};
  Status st = _i2cWriteReadRaw(_config.i2cAddress, nullptr, 0, rx, sizeof(rx));
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  if (!st.ok()) {
    return _updateHealth(st);
  }
  if (_config.requireReadMsbZero && rx[cmd::READ_MSB_INDEX] != 0x00) {
    return _recordFailure(Status::Error(Err::REGISTER_MISMATCH, "Read MSB byte is not zero",
                                        rx[cmd::READ_MSB_INDEX]));
  }
  st = _updateHealth(st);
  if (!st.ok()) {
    return st;
  }
  value = rx[cmd::READ_LSB_INDEX];
  if (_addressPointerKnown && _isValidRegister(_addressPointer)) {
    _syncRegister(_addressPointer, value);
  }
  return Status::Ok();
}

Status MCP45HVX1::_writeRegisterRaw(uint8_t reg, uint8_t value) {
  if (!_isWritableRegister(reg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address is not writable");
  }
  if (reg == cmd::REG_WIPER0 && !_isValidWiperCode(value, _config.resolution)) {
    return Status::Error(Err::INVALID_PARAM, "Wiper code exceeds configured resolution", value);
  }

  const uint8_t sanitized = (reg == cmd::REG_TCON0) ? sanitizeTcon(value) : value;
  const uint8_t payload[2] = {cmd::makeCommand(reg, cmd::Command::WriteData), sanitized};
  Status st = _i2cWriteRaw(_config.i2cAddress, payload, sizeof(payload));
  if (st.ok()) {
    _addressPointerKnown = true;
    _addressPointer = reg;
  }
  return st;
}

Status MCP45HVX1::_writeRegisterTracked(uint8_t reg, uint8_t value) {
  if (!_isWritableRegister(reg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address is not writable");
  }

  const uint8_t sanitized = (reg == cmd::REG_TCON0) ? sanitizeTcon(value) : value;
  const uint8_t payload[2] = {cmd::makeCommand(reg, cmd::Command::WriteData), sanitized};
  Status st = _i2cWriteTracked(_config.i2cAddress, payload, sizeof(payload));
  if (st.ok()) {
    _addressPointerKnown = true;
    _addressPointer = reg;
  }
  return st;
}

Status MCP45HVX1::_sendWiperStepCommand(cmd::Command command, uint8_t steps) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (command != cmd::Command::Increment && command != cmd::Command::Decrement) {
    return Status::Error(Err::INVALID_PARAM, "Invalid wiper step command");
  }
  if (steps == 0) {
    return Status::Ok();
  }

  uint8_t remaining = steps;
  while (remaining > 0) {
    const uint8_t chunk = remaining > cmd::MAX_COMMAND_CHUNK
                              ? static_cast<uint8_t>(cmd::MAX_COMMAND_CHUNK)
                              : remaining;
    uint8_t payload[cmd::MAX_COMMAND_CHUNK] = {};
    const uint8_t commandByte = cmd::makeCommand(cmd::REG_WIPER0, command);
    std::memset(payload, commandByte, chunk);

    Status st = _i2cWriteTracked(_config.i2cAddress, payload, chunk);
    if (!st.ok()) {
      return st;
    }

    if (_cachedWiperKnown) {
      if (command == cmd::Command::Increment) {
        _cachedWiper = boundedAdd(_cachedWiper, chunk, maxWiperCode(_config.resolution));
      } else {
        _cachedWiper = boundedSub(_cachedWiper, chunk);
      }
    }
    _addressPointerKnown = true;
    _addressPointer = cmd::REG_WIPER0;
    remaining = static_cast<uint8_t>(remaining - chunk);
  }

  return Status::Ok();
}

Status MCP45HVX1::_generalCallWrite(uint8_t commandByte, const uint8_t* data, size_t len) {
  if (len > 0 && data == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid General Call payload");
  }
  if (len > 1) {
    return Status::Error(Err::INVALID_PARAM, "General Call payload too long");
  }

  uint8_t payload[2] = {commandByte, 0};
  if (len == 1) {
    payload[1] = data[0];
  }
  return _i2cWriteTracked(cmd::GENERAL_CALL_ADDRESS, payload, len + 1);
}

// ===========================================================================
// Validation and State Helpers
// ===========================================================================

bool MCP45HVX1::_isValidAddress(uint8_t address) {
  return _isPrimaryAddress(address) || _isAlternateAddress(address);
}

bool MCP45HVX1::_isPrimaryAddress(uint8_t address) {
  return address >= cmd::MIN_ADDRESS && address <= cmd::MAX_ADDRESS;
}

bool MCP45HVX1::_isAlternateAddress(uint8_t address) {
  return address >= cmd::ALT_MIN_ADDRESS && address <= cmd::ALT_MAX_ADDRESS;
}

bool MCP45HVX1::_isValidRegister(uint8_t reg) {
  return reg == cmd::REG_WIPER0 || reg == cmd::REG_TCON0;
}

bool MCP45HVX1::_isWritableRegister(uint8_t reg) {
  return _isValidRegister(reg);
}

bool MCP45HVX1::_isValidWiperCode(uint8_t code, Resolution resolution) {
  return code <= maxWiperCode(resolution);
}

uint8_t MCP45HVX1::_terminalMask(Terminal terminal) {
  switch (terminal) {
    case Terminal::A:
      return cmd::TCON_R0A;
    case Terminal::W:
      return cmd::TCON_R0W;
    case Terminal::B:
      return cmd::TCON_R0B;
    default:
      return INVALID_TERMINAL_MASK;
  }
}

uint8_t MCP45HVX1::_tconForMode(TerminalMode mode) {
  switch (mode) {
    case TerminalMode::Potentiometer:
      return cmd::TCON_POTENTIOMETER;
    case TerminalMode::RheostatBToW:
      return cmd::TCON_RHEOSTAT_B_TO_W;
    case TerminalMode::RheostatAToW:
      return cmd::TCON_RHEOSTAT_A_TO_W;
    case TerminalMode::WiperFloating:
      return cmd::TCON_WIPER_FLOATING;
    case TerminalMode::Shutdown:
      return cmd::TCON_SHUTDOWN;
    default:
      return cmd::TCON_POTENTIOMETER;
  }
}

TerminalStatus MCP45HVX1::decodeTcon(uint8_t value) {
  const uint8_t sanitized = sanitizeTcon(value);
  TerminalStatus status;
  status.softwareShutdown = (sanitized & cmd::TCON_R0HW) == 0;
  status.terminalA = (sanitized & cmd::TCON_R0A) != 0;
  status.terminalW = (sanitized & cmd::TCON_R0W) != 0;
  status.terminalB = (sanitized & cmd::TCON_R0B) != 0;

  const uint8_t implemented = static_cast<uint8_t>(sanitized & cmd::TCON_IMPLEMENTED_MASK);
  switch (implemented) {
    case cmd::TCON_IMPLEMENTED_MASK:
      status.mode = TerminalMode::Potentiometer;
      break;
    case static_cast<uint8_t>(cmd::TCON_R0B | cmd::TCON_R0W | cmd::TCON_R0HW):
      status.mode = TerminalMode::RheostatBToW;
      break;
    case static_cast<uint8_t>(cmd::TCON_R0A | cmd::TCON_R0W | cmd::TCON_R0HW):
      status.mode = TerminalMode::RheostatAToW;
      break;
    case static_cast<uint8_t>(cmd::TCON_R0A | cmd::TCON_R0B | cmd::TCON_R0HW):
      status.mode = TerminalMode::WiperFloating;
      break;
    default:
      status.mode = status.softwareShutdown ? TerminalMode::Shutdown
                                            : TerminalMode::Potentiometer;
      break;
  }
  return status;
}

void MCP45HVX1::_syncRegister(uint8_t reg, uint8_t value) {
  if (reg == cmd::REG_WIPER0) {
    _cachedWiper = value;
    _cachedWiperKnown = true;
  } else if (reg == cmd::REG_TCON0) {
    _cachedTcon = sanitizeTcon(value);
    _cachedTconKnown = true;
  }
}

void MCP45HVX1::_clearCachedRegisters() {
  _cachedWiperKnown = false;
  _cachedWiper = 0;
  _cachedTconKnown = false;
  _cachedTcon = cmd::TCON_DEFAULT;
}

// ===========================================================================
// Health Management
// ===========================================================================

Status MCP45HVX1::_updateHealth(const Status& st) {
  if (!_initialized) {
    return st;
  }
  if (st.inProgress()) {
    return st;
  }

  const uint32_t now = _nowMs();
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

  if (st.ok()) {
    _lastOkMs = now;
    if (_totalSuccess < maxU32) {
      _totalSuccess++;
    }
    _consecutiveFailures = 0;
    _driverState = DriverState::READY;
    return st;
  }

  _lastError = st;
  _lastErrorMs = now;
  if (_totalFailures < maxU32) {
    _totalFailures++;
  }
  if (_consecutiveFailures < maxU8) {
    _consecutiveFailures++;
  }

  if (_consecutiveFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }

  return st;
}

Status MCP45HVX1::_recordFailure(const Status& st) {
  if (st.ok() || st.inProgress()) {
    return st;
  }
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM ||
      st.code == Err::NOT_INITIALIZED || st.code == Err::UNSUPPORTED) {
    return st;
  }

  const uint32_t now = _nowMs();
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

  _lastError = st;
  _lastErrorMs = now;
  if (_totalFailures < maxU32) {
    _totalFailures++;
  }
  if (_consecutiveFailures < maxU8) {
    _consecutiveFailures++;
  }
  if (_consecutiveFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }
  return st;
}

uint32_t MCP45HVX1::_nowMs() const {
  if (_config.nowMs != nullptr) {
    return _config.nowMs(_config.timeUser);
  }
  return millis();
}

}  // namespace MCP45HVX1
