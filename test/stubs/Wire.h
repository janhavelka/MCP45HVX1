/// @file Wire.h
/// @brief Minimal Wire stub for native testing
#pragma once

#include <cstdint>
#include <cstddef>

/// TX/RX buffer size, mirroring the macro the real Wire implementations expose
/// so transports can size their transfers against it.
#ifndef I2C_BUFFER_LENGTH
#define I2C_BUFFER_LENGTH 64
#endif

class TwoWire {
public:
  bool begin(int sda = -1, int scl = -1) { (void)sda; (void)scl; return true; }
  void setClock(uint32_t freq) { (void)freq; }
  void setTimeOut(uint32_t timeoutMs) { _timeoutMs = timeoutMs; }
  uint32_t getTimeOut() const { return _timeoutMs; }

  void beginTransmission(uint8_t addr) {
    _addr = addr;
    _txLen = 0;
    _overflow = false;
    ++_beginTransmissionCalls;
  }
  size_t write(uint8_t data) {
    if (_txLen >= sizeof(_txBuf)) {
      _overflow = true;
      return 0;
    }
    _txBuf[_txLen++] = data;
    return 1;
  }
  size_t write(const uint8_t* data, size_t len) {
    size_t written = 0;
    for (size_t i = 0; i < len && _txLen < sizeof(_txBuf); i++) {
      _txBuf[_txLen++] = data[i];
      written++;
    }
    if (written != len) {
      _overflow = true;
    }
    return written;
  }
  uint8_t endTransmission(bool stop = true) {
    _lastStop = stop;
    ++_endTransmissionCalls;
    // Real Wire reports 1 ("data too long to fit in transmit buffer").
    const uint8_t result = _overflow ? 1 : _endTransmissionResult;
    _repeatedStartPending = !stop && result == 0;
    return result;
  }

  size_t requestFrom(uint8_t addr, size_t len) {
    _readAddr = addr;
    ++_requestFromCalls;
    _lastReadRepeatedStart = _repeatedStartPending;
    _repeatedStartPending = false;
    _rxLen = len < _stagedRxLen ? len : _stagedRxLen;
    if (_requestFromOverrideEnabled && _requestFromOverride < _rxLen) {
      _rxLen = _requestFromOverride;
    }
    _rxIdx = 0;
    return _rxLen;
  }

  int available() { return static_cast<int>(_rxLen - _rxIdx); }
  int read() {
    if (_rxIdx < _rxLen) {
      return _rxBuf[_rxIdx++];
    }
    return -1;
  }

  // Test helper: set data to return on next read
  void _setReadData(const uint8_t* data, size_t len) {
    _stagedRxLen = len < sizeof(_rxBuf) ? len : sizeof(_rxBuf);
    _rxLen = 0;
    _rxIdx = 0;
    for (size_t i = 0; i < _stagedRxLen; i++) {
      _rxBuf[i] = data[i];
    }
  }

  void _setEndTransmissionResult(uint8_t result) { _endTransmissionResult = result; }
  void _clearEndTransmissionResult() { _endTransmissionResult = 0; }
  void _setRequestFromResult(size_t len) {
    _requestFromOverrideEnabled = true;
    _requestFromOverride = len;
  }
  void _clearRequestFromOverride() { _requestFromOverrideEnabled = false; }
  bool _overflowed() const { return _overflow; }
  bool lastStop() const { return _lastStop; }
  bool lastReadRepeatedStart() const { return _lastReadRepeatedStart; }
  uint8_t lastWriteAddress() const { return _addr; }
  uint8_t lastReadAddress() const { return _readAddr; }
  uint32_t beginTransmissionCalls() const { return _beginTransmissionCalls; }
  uint32_t endTransmissionCalls() const { return _endTransmissionCalls; }
  uint32_t requestFromCalls() const { return _requestFromCalls; }

  void end() {}

private:
  uint8_t _addr = 0;
  uint8_t _readAddr = 0;
  bool _lastStop = true;
  bool _repeatedStartPending = false;
  bool _lastReadRepeatedStart = false;
  uint32_t _beginTransmissionCalls = 0;
  uint32_t _endTransmissionCalls = 0;
  uint32_t _requestFromCalls = 0;
  uint8_t _txBuf[I2C_BUFFER_LENGTH] = {};
  size_t _txLen = 0;
  uint8_t _rxBuf[I2C_BUFFER_LENGTH] = {};
  size_t _rxLen = 0;
  size_t _stagedRxLen = 0;
  size_t _rxIdx = 0;
  uint32_t _timeoutMs = 0;
  uint8_t _endTransmissionResult = 0;
  bool _requestFromOverrideEnabled = false;
  size_t _requestFromOverride = 0;
  bool _overflow = false;
};

extern TwoWire Wire;
