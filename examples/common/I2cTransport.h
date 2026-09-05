/**
 * @file I2cTransport.h
 * @brief Wire-based I2C transport adapter for MCP45HVX1 examples.
 *
 * NOT part of the library API. Example-only.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "MCP45HVX1/CommandTable.h"
#include "MCP45HVX1/Status.h"

namespace transport {

/// Largest transfer this adapter will hand to Wire.
///
/// Derived from the Wire implementation's own buffer macro where one is
/// exposed, so the limit tracks the platform instead of a magic number.
#if defined(I2C_BUFFER_LENGTH)
static constexpr size_t WIRE_BUFFER_LIMIT = I2C_BUFFER_LENGTH;
#elif defined(BUFFER_LENGTH)
static constexpr size_t WIRE_BUFFER_LIMIT = BUFFER_LENGTH;
#else
static constexpr size_t WIRE_BUFFER_LIMIT = 32;
#endif

// The core sends up to MAX_COMMAND_CHUNK command bytes in one INC/DEC write.
// A transport that cannot carry that in a single transaction would fail every
// multi-step wiper helper, so catch it at compile time rather than at runtime.
static_assert(WIRE_BUFFER_LIMIT >= MCP45HVX1::cmd::MAX_COMMAND_CHUNK,
              "Wire TX buffer is smaller than MCP45HVX1::cmd::MAX_COMMAND_CHUNK");

struct BusResetContext {
  int sda = -1;
  int scl = -1;
  uint32_t frequencyHz = 400000;
  uint16_t timeoutMs = 50;
};

inline MCP45HVX1::Status mapWireResult(uint8_t result, const char* context) {
  switch (result) {
    case 0:
      return MCP45HVX1::Status::Ok();
    case 1:
      return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, context, result);
    case 2:
      return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_NACK_ADDR, context, result);
    case 3:
      return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_NACK_DATA, context, result);
    case 4:
      return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_BUS, context, result);
    case 5:
      return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_TIMEOUT, context, result);
    default:
      return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_ERROR, context, result);
  }
}

/// Apply a per-transaction timeout, clamped to the width Wire accepts.
/// TwoWire::setTimeOut() takes a uint16_t, so a large Config::i2cTimeoutMs
/// would otherwise wrap to a very short timeout.
inline void setWireTimeout(TwoWire& wire, uint32_t timeoutMs) {
  wire.setTimeOut(static_cast<uint16_t>(timeoutMs > 0xFFFFU ? 0xFFFFU : timeoutMs));
}

inline MCP45HVX1::Status wireWrite(uint8_t addr, const uint8_t* data, size_t len,
                                   uint32_t timeoutMs, void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_CONFIG, "Wire instance is null");
  }
  if (timeoutMs == 0) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "I2C timeout must be > 0");
  }
  if (data == nullptr || len == 0) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "Invalid I2C write params");
  }
  if (len > WIRE_BUFFER_LIMIT) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "Write exceeds I2C buffer",
                                    static_cast<int32_t>(len));
  }

  setWireTimeout(*wire, timeoutMs);
  wire->beginTransmission(addr);
  const size_t written = wire->write(data, len);
  if (written != len) {
    (void)wire->endTransmission(true);  // release the bus and the Wire lock
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_ERROR, "I2C write incomplete",
                                    static_cast<int32_t>(written));
  }

  return mapWireResult(wire->endTransmission(true), "I2C write failed");
}

inline MCP45HVX1::Status wireWriteRead(uint8_t addr, const uint8_t* tx, size_t txLen,
                                       uint8_t* rx, size_t rxLen, uint32_t timeoutMs,
                                       void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_CONFIG, "Wire instance is null");
  }
  if (timeoutMs == 0) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "I2C timeout must be > 0");
  }
  if ((txLen > 0 && tx == nullptr) || rx == nullptr || rxLen == 0) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "Invalid I2C read params");
  }
  if (txLen > WIRE_BUFFER_LIMIT || rxLen > WIRE_BUFFER_LIMIT) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "I2C read exceeds buffer");
  }

  setWireTimeout(*wire, timeoutMs);
  if (txLen > 0) {
    wire->beginTransmission(addr);
    const size_t written = wire->write(tx, txLen);
    if (written != txLen) {
      (void)wire->endTransmission(true);  // release the bus and the Wire lock
      return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_ERROR, "I2C write incomplete",
                                      static_cast<int32_t>(written));
    }

    // On arduino-esp32 endTransmission(false) only marks the transfer as
    // repeated-start; the bytes go out inside requestFrom() below, so a zero
    // return here does not yet mean the device acknowledged.
    const uint8_t result = wire->endTransmission(false);
    if (result != 0) {
      return mapWireResult(result, "I2C write phase failed");
    }
  }

  const size_t read = wire->requestFrom(addr, static_cast<uint8_t>(rxLen));
  if (read == 0) {
    // No byte at all came back: the client did not acknowledge. Reporting this
    // as a generic I2C error would make Err::DEVICE_NOT_FOUND unreachable from
    // begin() and probe(), which is the primary bring-up diagnostic.
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_NACK_ADDR, "I2C read not acknowledged");
  }
  if (read != rxLen) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_ERROR, "I2C read length mismatch",
                                    static_cast<int32_t>(read));
  }

  for (size_t i = 0; i < rxLen; ++i) {
    if (wire->available()) {
      rx[i] = static_cast<uint8_t>(wire->read());
    } else {
      return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_ERROR, "I2C data not available");
    }
  }

  return MCP45HVX1::Status::Ok();
}

inline bool initWire(int sda, int scl, uint32_t freq = 400000, uint16_t timeoutMs = 50) {
#if defined(ARDUINO_ARCH_ESP32)
  pinMode(scl, OUTPUT);
  pinMode(sda, INPUT_PULLUP);
  for (int i = 0; i < 9; i++) {
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
  }
  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);
  pinMode(sda, INPUT_PULLUP);  // release SDA before Wire takes the pin
#endif

  const bool ok = Wire.begin(sda, scl);
  Wire.setClock(freq);
  setWireTimeout(Wire, timeoutMs);
  return ok;
}

inline MCP45HVX1::Status wireBusReset(void* user) {
#if defined(ARDUINO_ARCH_ESP32)
  BusResetContext* ctx = static_cast<BusResetContext*>(user);
  if (ctx == nullptr || ctx->sda < 0 || ctx->scl < 0) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_CONFIG,
                                    "Bus reset pins not configured");
  }

  Wire.end();
  pinMode(ctx->sda, INPUT_PULLUP);
  pinMode(ctx->scl, OUTPUT);
  for (int i = 0; i < 9; ++i) {
    digitalWrite(ctx->scl, LOW);
    delayMicroseconds(5);
    digitalWrite(ctx->scl, HIGH);
    delayMicroseconds(5);
  }
  pinMode(ctx->sda, OUTPUT);
  digitalWrite(ctx->sda, LOW);
  delayMicroseconds(5);
  digitalWrite(ctx->scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(ctx->sda, HIGH);
  delayMicroseconds(5);
  pinMode(ctx->sda, INPUT_PULLUP);

  Wire.begin(ctx->sda, ctx->scl);
  Wire.setClock(ctx->frequencyHz);
  setWireTimeout(Wire, ctx->timeoutMs);
  return MCP45HVX1::Status::Ok();
#else
  (void)user;
  return MCP45HVX1::Status::Error(MCP45HVX1::Err::UNSUPPORTED,
                                  "Wire bus reset requires Arduino ESP32 GPIO");
#endif
}

}  // namespace transport
