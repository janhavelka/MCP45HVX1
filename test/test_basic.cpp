/// @file test_basic.cpp
/// @brief Native contract tests for MCP45HVX1 lifecycle, protocol, and health behavior.

#include <unity.h>

#include "Arduino.h"
#include "Wire.h"

SerialClass Serial;
TwoWire Wire;

#include "MCP45HVX1/MCP45HVX1.h"
#include "examples/common/I2cTransport.h"

using namespace MCP45HVX1;

namespace {

struct FakeBus {
  static constexpr size_t MAX_LOG = 16;

  uint32_t nowMs = 1000;
  uint32_t writeCalls = 0;
  uint32_t readCalls = 0;
  uint32_t generalCallWrites = 0;
  uint32_t resetCalls = 0;
  uint8_t writeLogCount = 0;
  uint8_t readLogCount = 0;
  uint8_t writeAddrLog[MAX_LOG] = {};
  uint8_t writeLenLog[MAX_LOG] = {};
  uint8_t writeByte0Log[MAX_LOG] = {};
  uint8_t writeByte1Log[MAX_LOG] = {};
  uint8_t readAddrLog[MAX_LOG] = {};
  uint8_t readTxLenLog[MAX_LOG] = {};
  uint8_t readRxLenLog[MAX_LOG] = {};
  uint8_t readTxByte0Log[MAX_LOG] = {};

  uint8_t wiper = cmd::WIPER_DEFAULT_8BIT;
  uint8_t tcon = cmd::TCON_DEFAULT;
  uint8_t lastReg = cmd::REG_WIPER0;
  uint8_t readMsb = 0x00;
  Resolution resolution = Resolution::Bits8;

  int readErrorRemaining = 0;
  int writeErrorRemaining = 0;
  Status readError = Status::Error(Err::I2C_ERROR, "forced read error", -1);
  Status writeError = Status::Error(Err::I2C_ERROR, "forced write error", -2);
  Status resetStatus = Status::Ok();
};

void logWrite(FakeBus* bus, uint8_t addr, const uint8_t* data, size_t len) {
  if (bus->writeLogCount >= FakeBus::MAX_LOG) {
    return;
  }
  const uint8_t index = bus->writeLogCount++;
  bus->writeAddrLog[index] = addr;
  bus->writeLenLog[index] = static_cast<uint8_t>(len);
  bus->writeByte0Log[index] = (data != nullptr && len > 0) ? data[0] : 0;
  bus->writeByte1Log[index] = (data != nullptr && len > 1) ? data[1] : 0;
}

void logRead(FakeBus* bus, uint8_t addr, const uint8_t* txData, size_t txLen, size_t rxLen) {
  if (bus->readLogCount >= FakeBus::MAX_LOG) {
    return;
  }
  const uint8_t index = bus->readLogCount++;
  bus->readAddrLog[index] = addr;
  bus->readTxLenLog[index] = static_cast<uint8_t>(txLen);
  bus->readRxLenLog[index] = static_cast<uint8_t>(rxLen);
  bus->readTxByte0Log[index] = (txData != nullptr && txLen > 0) ? txData[0] : 0;
}

bool isValidFakeReg(uint8_t reg) {
  return reg == cmd::REG_WIPER0 || reg == cmd::REG_TCON0;
}

uint8_t maxCode(FakeBus* bus) {
  return MCP45HVX1::maxWiperCode(bus->resolution);
}

uint8_t readFakeReg(FakeBus* bus, uint8_t reg) {
  return reg == cmd::REG_TCON0 ? bus->tcon : bus->wiper;
}

Status applyCommand(FakeBus* bus, uint8_t commandByte, const uint8_t* data,
                    size_t& index, size_t len) {
  const uint8_t reg = static_cast<uint8_t>((commandByte >> 4) & 0x0F);
  const auto op = static_cast<cmd::Command>((commandByte >> 2) & 0x03);

  if (!isValidFakeReg(reg)) {
    return Status::Error(Err::I2C_NACK_DATA, "reserved register", reg);
  }

  bus->lastReg = reg;

  switch (op) {
    case cmd::Command::WriteData:
      if ((index + 1) >= len || data == nullptr) {
        return Status::Error(Err::I2C_NACK_DATA, "missing data byte");
      }
      ++index;
      if (reg == cmd::REG_WIPER0) {
        bus->wiper = data[index] > maxCode(bus) ? maxCode(bus) : data[index];
      } else {
        bus->tcon = MCP45HVX1::sanitizeTcon(data[index]);
      }
      return Status::Ok();

    case cmd::Command::Increment:
      if (reg != cmd::REG_WIPER0) {
        return Status::Error(Err::I2C_NACK_DATA, "INC invalid for register", reg);
      }
      if (bus->wiper < maxCode(bus)) {
        ++bus->wiper;
      }
      return Status::Ok();

    case cmd::Command::Decrement:
      if (reg != cmd::REG_WIPER0) {
        return Status::Error(Err::I2C_NACK_DATA, "DEC invalid for register", reg);
      }
      if (bus->wiper > 0) {
        --bus->wiper;
      }
      return Status::Ok();

    case cmd::Command::ReadData:
      return Status::Ok();
  }

  return Status::Error(Err::I2C_ERROR, "unknown command");
}

Status applyGeneralCall(FakeBus* bus, const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "invalid general call");
  }

  switch (data[0]) {
    case cmd::GC_WRITE_WIPER0:
      if (len < 2) {
        return Status::Error(Err::I2C_NACK_DATA, "missing GC wiper data");
      }
      bus->wiper = data[1] > maxCode(bus) ? maxCode(bus) : data[1];
      return Status::Ok();
    case cmd::GC_WRITE_TCON0:
      if (len < 2) {
        return Status::Error(Err::I2C_NACK_DATA, "missing GC TCON data");
      }
      bus->tcon = MCP45HVX1::sanitizeTcon(data[1]);
      return Status::Ok();
    case cmd::GC_INCREMENT_WIPER0:
      if (bus->wiper < maxCode(bus)) {
        ++bus->wiper;
      }
      return Status::Ok();
    case cmd::GC_DECREMENT_WIPER0:
      if (bus->wiper > 0) {
        --bus->wiper;
      }
      return Status::Ok();
    default:
      return Status::Error(Err::I2C_NACK_DATA, "unknown GC command", data[0]);
  }
}

Status fakeWrite(uint8_t addr, const uint8_t* data, size_t len, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->writeCalls++;
  if (data == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write args");
  }
  if (bus->writeErrorRemaining > 0) {
    bus->writeErrorRemaining--;
    return bus->writeError;
  }
  logWrite(bus, addr, data, len);

  if (addr == cmd::GENERAL_CALL_ADDRESS) {
    bus->generalCallWrites++;
    return applyGeneralCall(bus, data, len);
  }

  for (size_t i = 0; i < len; ++i) {
    Status st = applyCommand(bus, data[i], data, i, len);
    if (!st.ok()) {
      return st;
    }
  }
  return Status::Ok();
}

Status fakeWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen, uint8_t* rxData,
                     size_t rxLen, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->readCalls++;
  if ((txLen > 0 && txData == nullptr) || rxData == nullptr || rxLen == 0) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write-read args");
  }
  if (bus->readErrorRemaining > 0) {
    bus->readErrorRemaining--;
    return bus->readError;
  }
  logRead(bus, addr, txData, txLen, rxLen);

  if (txLen > 0) {
    for (size_t i = 0; i < txLen; ++i) {
      Status st = applyCommand(bus, txData[i], txData, i, txLen);
      if (!st.ok()) {
        return st;
      }
    }
  }

  if (!isValidFakeReg(bus->lastReg)) {
    return Status::Error(Err::I2C_NACK_DATA, "invalid last register", bus->lastReg);
  }

  if (rxLen >= 1) {
    rxData[0] = bus->readMsb;
  }
  if (rxLen >= 2) {
    rxData[1] = readFakeReg(bus, bus->lastReg);
  }
  for (size_t i = 2; i < rxLen; ++i) {
    rxData[i] = 0;
  }
  return Status::Ok();
}

uint32_t fakeNowMs(void* user) {
  return static_cast<FakeBus*>(user)->nowMs;
}

Status fakeBusReset(void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->resetCalls++;
  return bus->resetStatus;
}

Config makeConfig(FakeBus& bus) {
  Config cfg;
  cfg.i2cWrite = fakeWrite;
  cfg.i2cWriteRead = fakeWriteRead;
  cfg.i2cUser = &bus;
  cfg.busReset = fakeBusReset;
  cfg.controlUser = &bus;
  cfg.nowMs = fakeNowMs;
  cfg.timeUser = &bus;
  cfg.i2cTimeoutMs = 10;
  cfg.offlineThreshold = 3;
  cfg.i2cAddress = cmd::DEFAULT_ADDRESS;
  cfg.resolution = bus.resolution;
  return cfg;
}

}  // namespace

void setUp() {
  setMillis(0);
  Wire._clearEndTransmissionResult();
  Wire._clearRequestFromOverride();
}

void tearDown() {}

void test_status_ok_and_error() {
  Status ok = Status::Ok();
  TEST_ASSERT_TRUE(ok.ok());
  Status st = Status::Error(Err::I2C_ERROR, "Test error", 42);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(42, st.detail);
}

void test_command_constants() {
  TEST_ASSERT_EQUAL_HEX8(0x3C, cmd::DEFAULT_ADDRESS);
  TEST_ASSERT_EQUAL_HEX8(0x00, cmd::REG_WIPER0);
  TEST_ASSERT_EQUAL_HEX8(0x04, cmd::REG_TCON0);
  TEST_ASSERT_EQUAL_HEX8(0x0C, cmd::makeCommand(cmd::REG_WIPER0, cmd::Command::ReadData));
  TEST_ASSERT_EQUAL_HEX8(0x40, cmd::makeCommand(cmd::REG_TCON0, cmd::Command::WriteData));
  TEST_ASSERT_EQUAL_HEX8(0xF7, cmd::TCON_SHUTDOWN);
}

void test_config_defaults() {
  Config cfg;
  TEST_ASSERT_NULL(cfg.i2cWrite);
  TEST_ASSERT_NULL(cfg.i2cWriteRead);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, cfg.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50, cfg.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Resolution::Bits8),
                          static_cast<uint8_t>(cfg.resolution));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResistanceOption::R10K),
                          static_cast<uint8_t>(cfg.resistance));
  TEST_ASSERT_FALSE(cfg.allowAlternateAddressRange);
  TEST_ASSERT_FALSE(cfg.writeInitialWiper);
  TEST_ASSERT_FALSE(cfg.writeInitialTcon);
  TEST_ASSERT_FALSE(cfg.requirePowerOnDefaults);
  TEST_ASSERT_TRUE(cfg.requireReadMsbZero);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.offlineThreshold);
}

void test_begin_rejects_invalid_config() {
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));

  FakeBus bus;
  cfg = makeConfig(bus);
  cfg.i2cTimeoutMs = 0;
  st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));

  cfg = makeConfig(bus);
  cfg.i2cAddress = 0x60;
  st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));

  cfg = makeConfig(bus);
  cfg.resistance = static_cast<ResistanceOption>(99);
  st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));
}

void test_begin_accepts_documented_and_alternate_address_ranges() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cAddress = 0x3F;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  dev.end();
  cfg = makeConfig(bus);
  cfg.i2cAddress = 0x5C;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));

  cfg.allowAlternateAddressRange = true;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_TRUE(dev.getDeviceInfo().usingAlternateAddressRange);
}

void test_begin_reads_and_caches_registers() {
  FakeBus bus;
  bus.wiper = 0x42;
  bus.tcon = 0xFB;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  SettingsSnapshot s = dev.getSettings();
  TEST_ASSERT_TRUE(s.initialized);
  TEST_ASSERT_TRUE(s.cachedWiperKnown);
  TEST_ASSERT_TRUE(s.cachedTconKnown);
  TEST_ASSERT_EQUAL_HEX8(0x42, s.cachedWiper);
  TEST_ASSERT_EQUAL_HEX8(0xFB, s.cachedTcon);
  TEST_ASSERT_EQUAL_UINT32(0u, s.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(2u, bus.readCalls);
}

void test_device_info_and_resistance_helpers() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg = makeConfig(bus);
  cfg.resistance = ResistanceOption::R50K;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  DeviceInfo info = dev.getDeviceInfo();
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, info.i2cAddress);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Resolution::Bits8),
                          static_cast<uint8_t>(info.resolution));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResistanceOption::R50K),
                          static_cast<uint8_t>(info.resistance));
  TEST_ASSERT_EQUAL_UINT32(50000u, info.nominalResistanceOhms);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 196.08f, info.nominalStepOhms);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, info.maxTerminalCurrentMilliAmps);

  TEST_ASSERT_EQUAL_UINT32(10000u,
                           MCP45HVX1::nominalResistanceOhms(ResistanceOption::R10K));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f,
                           MCP45HVX1::maxTerminalCurrentMilliAmps(ResistanceOption::R5K));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.5f,
                           MCP45HVX1::maxTerminalCurrentMilliAmps(ResistanceOption::R10K));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f,
                           MCP45HVX1::maxTerminalCurrentMilliAmps(ResistanceOption::R100K));
  TEST_ASSERT_EQUAL_UINT32(100000u,
                           MCP45HVX1::nominalResistanceOhms(ResistanceOption::R100K));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 392.16f,
                           MCP45HVX1::stepResistanceOhms(ResistanceOption::R100K,
                                                         Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 50196.0f,
                           MCP45HVX1::resistanceBToWOhms(0x80, ResistanceOption::R100K,
                                                         Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 39.22f,
                           MCP45HVX1::stepResistanceOhms(ResistanceOption::R10K,
                                                         Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 5019.6f,
                           MCP45HVX1::resistanceBToWOhms(0x80, ResistanceOption::R10K,
                                                         Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 4980.4f,
                           MCP45HVX1::resistanceAToWOhms(0x80, ResistanceOption::R10K,
                                                         Resolution::Bits8));
}

void test_begin_require_power_on_defaults() {
  FakeBus bus;
  bus.wiper = 0x22;
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg = makeConfig(bus);
  cfg.requirePowerOnDefaults = true;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));
}

void test_begin_optional_initial_writes() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg = makeConfig(bus);
  cfg.writeInitialWiper = true;
  cfg.initialWiperCode = 0x20;
  cfg.writeInitialTcon = true;
  cfg.initialTcon = 0x07;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_HEX8(0x20, bus.wiper);
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.tcon);
}

void test_read_write_wiper() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeWiper(0x80).ok());
  TEST_ASSERT_EQUAL_HEX8(0x80, bus.wiper);

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x80, value);
  TEST_ASSERT_EQUAL_UINT32(2u, dev.totalSuccess());
}

void test_wire_protocol_frames() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
  TEST_ASSERT_EQUAL_UINT8(3u, bus.readLogCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, bus.readAddrLog[2]);
  TEST_ASSERT_EQUAL_UINT8(1u, bus.readTxLenLog[2]);
  TEST_ASSERT_EQUAL_UINT8(cmd::READ_RESPONSE_LEN, bus.readRxLenLog[2]);
  TEST_ASSERT_EQUAL_HEX8(cmd::makeCommand(cmd::REG_WIPER0, cmd::Command::ReadData),
                         bus.readTxByte0Log[2]);

  TEST_ASSERT_TRUE(dev.writeWiper(0x33).ok());
  TEST_ASSERT_EQUAL_UINT8(1u, bus.writeLogCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, bus.writeAddrLog[0]);
  TEST_ASSERT_EQUAL_UINT8(2u, bus.writeLenLog[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::makeCommand(cmd::REG_WIPER0, cmd::Command::WriteData),
                         bus.writeByte0Log[0]);
  TEST_ASSERT_EQUAL_HEX8(0x33, bus.writeByte1Log[0]);

  TEST_ASSERT_TRUE(dev.incrementWiper(70).ok());
  TEST_ASSERT_EQUAL_UINT8(3u, bus.writeLogCount);
  TEST_ASSERT_EQUAL_UINT8(cmd::MAX_COMMAND_CHUNK, bus.writeLenLog[1]);
  TEST_ASSERT_EQUAL_UINT8(6u, bus.writeLenLog[2]);
  TEST_ASSERT_EQUAL_HEX8(cmd::makeCommand(cmd::REG_WIPER0, cmd::Command::Increment),
                         bus.writeByte0Log[1]);
  TEST_ASSERT_EQUAL_HEX8(cmd::makeCommand(cmd::REG_WIPER0, cmd::Command::Increment),
                         bus.writeByte0Log[2]);

  TEST_ASSERT_TRUE(dev.readLastAddress(value).ok());
  TEST_ASSERT_EQUAL_UINT8(4u, bus.readLogCount);
  TEST_ASSERT_EQUAL_UINT8(0u, bus.readTxLenLog[3]);
  TEST_ASSERT_EQUAL_UINT8(cmd::READ_RESPONSE_LEN, bus.readRxLenLog[3]);
}

void test_7bit_wiper_rejects_out_of_range() {
  FakeBus bus;
  bus.resolution = Resolution::Bits7;
  bus.wiper = cmd::WIPER_DEFAULT_7BIT;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  Status st = dev.writeWiper(0x80);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(cmd::WIPER_DEFAULT_7BIT, bus.wiper);
}

void test_increment_decrement_clamp_and_cache() {
  FakeBus bus;
  bus.wiper = 0xFE;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.incrementWiper(3).ok());
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.wiper);
  TEST_ASSERT_EQUAL_HEX8(0xFF, dev.getSettings().cachedWiper);

  TEST_ASSERT_TRUE(dev.decrementWiper(4).ok());
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.wiper);
  TEST_ASSERT_EQUAL_HEX8(0xFB, dev.getSettings().cachedWiper);
}

void test_tcon_write_sanitizes_reserved_bits() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeTcon(0x00).ok());
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.tcon);
  TEST_ASSERT_EQUAL_HEX8(0xF0, dev.getSettings().cachedTcon);
}

void test_terminal_helpers() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.setTerminalEnabled(Terminal::A, false).ok());
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.tcon);

  bool enabled = true;
  TEST_ASSERT_TRUE(dev.getTerminalEnabled(Terminal::A, enabled).ok());
  TEST_ASSERT_FALSE(enabled);

  TEST_ASSERT_TRUE(dev.setSoftwareShutdown(true).ok());
  TEST_ASSERT_EQUAL_HEX8(0xF3, bus.tcon);

  bool shutdown = false;
  TEST_ASSERT_TRUE(dev.getSoftwareShutdown(shutdown).ok());
  TEST_ASSERT_TRUE(shutdown);

  TEST_ASSERT_TRUE(dev.setTerminalMode(TerminalMode::RheostatAToW).ok());
  TEST_ASSERT_EQUAL_HEX8(cmd::TCON_RHEOSTAT_A_TO_W, bus.tcon);

  TerminalMode mode = TerminalMode::Potentiometer;
  TEST_ASSERT_TRUE(dev.getTerminalMode(mode).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TerminalMode::RheostatAToW),
                          static_cast<uint8_t>(mode));

  TerminalStatus status;
  TEST_ASSERT_TRUE(dev.readTerminalStatus(status).ok());
  TEST_ASSERT_FALSE(status.softwareShutdown);
  TEST_ASSERT_TRUE(status.terminalA);
  TEST_ASSERT_TRUE(status.terminalW);
  TEST_ASSERT_FALSE(status.terminalB);

  status = MCP45HVX1::decodeTcon(cmd::TCON_SHUTDOWN);
  TEST_ASSERT_TRUE(status.softwareShutdown);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TerminalMode::Shutdown),
                          static_cast<uint8_t>(status.mode));

  TEST_ASSERT_TRUE(dev.writeTcon(0xFA).ok());
  TEST_ASSERT_TRUE(dev.readTerminalStatus(status).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TerminalMode::Custom),
                          static_cast<uint8_t>(status.mode));

  Status st = dev.setTerminalMode(static_cast<TerminalMode>(99));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  st = dev.setTerminalMode(TerminalMode::Custom);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
}

void test_direct_register_access_rejects_reserved() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t value = 0;
  Status st = dev.readRegister(0x01, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));

  st = dev.writeRegister(0x05, 0xAA);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
}

void test_read_last_address() {
  FakeBus bus;
  bus.wiper = 0x55;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeTcon(0xFB).ok());
  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readLastAddress(value).ok());
  TEST_ASSERT_EQUAL_HEX8(0xFB, value);
}

void test_i2c_reset_and_restore_defaults() {
  FakeBus bus;
  bus.wiper = 0x11;
  bus.tcon = 0xF3;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.resetI2cState().ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.resetCalls);
  TEST_ASSERT_FALSE(dev.getSettings().addressPointerKnown);

  TEST_ASSERT_TRUE(dev.restorePowerOnDefaults().ok());
  TEST_ASSERT_EQUAL_HEX8(cmd::WIPER_DEFAULT_8BIT, bus.wiper);
  TEST_ASSERT_EQUAL_HEX8(cmd::TCON_DEFAULT, bus.tcon);
}

void test_i2c_reset_reports_unsupported_without_callback() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg = makeConfig(bus);
  cfg.busReset = nullptr;
  cfg.controlUser = nullptr;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  Status st = dev.resetI2cState();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::UNSUPPORTED), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.resetCalls);
}

void test_general_call_helpers() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.generalCallWriteWiper(0x44).ok());
  TEST_ASSERT_EQUAL_HEX8(0x44, bus.wiper);
  TEST_ASSERT_FALSE(dev.getSettings().cachedWiperKnown);

  TEST_ASSERT_TRUE(dev.generalCallIncrementWiper().ok());
  TEST_ASSERT_EQUAL_HEX8(0x45, bus.wiper);
  TEST_ASSERT_FALSE(dev.getSettings().cachedWiperKnown);

  TEST_ASSERT_TRUE(dev.generalCallDecrementWiper().ok());
  TEST_ASSERT_EQUAL_HEX8(0x44, bus.wiper);
  TEST_ASSERT_FALSE(dev.getSettings().cachedWiperKnown);

  TEST_ASSERT_TRUE(dev.generalCallWriteTcon(0x07).ok());
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.tcon);
  TEST_ASSERT_FALSE(dev.getSettings().cachedTconKnown);
  TEST_ASSERT_EQUAL_UINT32(4u, bus.generalCallWrites);

  bus.resolution = Resolution::Bits7;
  MCP45HVX1::MCP45HVX1 dev7;
  TEST_ASSERT_TRUE(dev7.begin(makeConfig(bus)).ok());
  Status st = dev7.generalCallWriteWiper(0x80);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
}

void test_probe_does_not_update_health_but_recover_does() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());

  bus.readErrorRemaining = 1;
  bus.nowMs = 2000;
  st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(2000u, dev.lastErrorMs());
}

void test_probe_preserves_register_mismatch() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readMsb = 0x01;
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
}

void test_offline_threshold() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  uint8_t value = 0;
  Status st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
}

void test_offline_threshold_zero_normalizes_to_one() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 0;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_UINT8(1u, dev.getConfig().offlineThreshold);
}

void test_read_msb_mismatch_is_reported() {
  FakeBus bus;
  bus.readMsb = 0x01;
  MCP45HVX1::MCP45HVX1 dev;
  Status st = dev.begin(makeConfig(bus));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));
}

void test_tracked_read_msb_mismatch_updates_health() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readMsb = 0x01;
  bus.nowMs = 3000;
  uint8_t value = 0;
  Status st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(3000u, dev.lastErrorMs());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
}

void test_require_read_msb_zero_can_be_disabled() {
  FakeBus bus;
  bus.readMsb = 0x01;
  MCP45HVX1::MCP45HVX1 dev;
  Config cfg = makeConfig(bus);
  cfg.requireReadMsbZero = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
}

void test_zero_step_commands_are_noops() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  const uint32_t writesBefore = bus.writeCalls;

  TEST_ASSERT_TRUE(dev.incrementWiper(0).ok());
  TEST_ASSERT_TRUE(dev.decrementWiper(0).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
}

void test_failed_bus_reset_updates_health() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.resetStatus = Status::Error(Err::I2C_BUS, "forced reset fail", -3);
  bus.nowMs = 4000;
  Status st = dev.resetI2cState();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(4000u, dev.lastErrorMs());
}

void test_conversions() {
  TEST_ASSERT_EQUAL_HEX8(0x80, MCP45HVX1::codeFromFraction(0.5f, Resolution::Bits8));
  TEST_ASSERT_EQUAL_HEX8(0x40, MCP45HVX1::codeFromFraction(0.5f, Resolution::Bits7));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, MCP45HVX1::fractionFromCode(0xFF, Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, MCP45HVX1::fractionFromCode(0x7F, Resolution::Bits7));
}

void test_fraction_read_write_helpers() {
  FakeBus bus;
  MCP45HVX1::MCP45HVX1 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeWiperFraction(0.5f).ok());
  TEST_ASSERT_EQUAL_HEX8(0x80, bus.wiper);

  float fraction = 0.0f;
  TEST_ASSERT_TRUE(dev.readWiperFraction(fraction).ok());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.502f, fraction);

  Status st = dev.writeWiperFraction(1.1f);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
}

void test_operations_reject_before_begin() {
  MCP45HVX1::MCP45HVX1 dev;
  uint8_t value = 0;
  bool flag = false;
  float fraction = 0.0f;
  TerminalStatus terminalStatus;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readWiper(value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeWiper(0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readWiperFraction(fraction).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeWiperFraction(0.0f).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.incrementWiper().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readTcon(value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.setTerminalEnabled(Terminal::A, true).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.getSoftwareShutdown(flag).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readTerminalStatus(terminalStatus).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.resetI2cState().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.restorePowerOnDefaults().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.probe().code));
}

void test_example_transport_maps_wire_errors_and_read_only_transactions() {
  Wire._clearEndTransmissionResult();
  Wire._clearRequestFromOverride();
  TEST_ASSERT_TRUE(transport::initWire(8, 9, 400000, 77));
  TEST_ASSERT_EQUAL_UINT32(77u, Wire.getTimeOut());

  const uint8_t byte = 0x55;
  Wire._setEndTransmissionResult(2);
  Status st = transport::wireWrite(0x3C, &byte, 1, 123, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));

  uint8_t rx[2] = {};
  const uint8_t readData[2] = {0x00, 0x7F};
  Wire._clearEndTransmissionResult();
  Wire._setReadData(readData, sizeof(readData));
  st = transport::wireWriteRead(0x3C, nullptr, 0, rx, sizeof(rx), 123, &Wire);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x7F, rx[1]);

  Wire._setRequestFromResult(1);
  st = transport::wireWriteRead(0x3C, nullptr, 0, rx, sizeof(rx), 123, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  Wire._clearRequestFromOverride();

  uint8_t tooLargeForStub[65] = {};
  st = transport::wireWrite(0x3C, tooLargeForStub, sizeof(tooLargeForStub), 123, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_status_ok_and_error);
  RUN_TEST(test_command_constants);
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_begin_rejects_invalid_config);
  RUN_TEST(test_begin_accepts_documented_and_alternate_address_ranges);
  RUN_TEST(test_begin_reads_and_caches_registers);
  RUN_TEST(test_device_info_and_resistance_helpers);
  RUN_TEST(test_begin_require_power_on_defaults);
  RUN_TEST(test_begin_optional_initial_writes);
  RUN_TEST(test_read_write_wiper);
  RUN_TEST(test_wire_protocol_frames);
  RUN_TEST(test_7bit_wiper_rejects_out_of_range);
  RUN_TEST(test_increment_decrement_clamp_and_cache);
  RUN_TEST(test_tcon_write_sanitizes_reserved_bits);
  RUN_TEST(test_terminal_helpers);
  RUN_TEST(test_direct_register_access_rejects_reserved);
  RUN_TEST(test_read_last_address);
  RUN_TEST(test_i2c_reset_and_restore_defaults);
  RUN_TEST(test_i2c_reset_reports_unsupported_without_callback);
  RUN_TEST(test_general_call_helpers);
  RUN_TEST(test_probe_does_not_update_health_but_recover_does);
  RUN_TEST(test_probe_preserves_register_mismatch);
  RUN_TEST(test_offline_threshold);
  RUN_TEST(test_offline_threshold_zero_normalizes_to_one);
  RUN_TEST(test_read_msb_mismatch_is_reported);
  RUN_TEST(test_tracked_read_msb_mismatch_updates_health);
  RUN_TEST(test_require_read_msb_zero_can_be_disabled);
  RUN_TEST(test_zero_step_commands_are_noops);
  RUN_TEST(test_failed_bus_reset_updates_health);
  RUN_TEST(test_conversions);
  RUN_TEST(test_fraction_read_write_helpers);
  RUN_TEST(test_operations_reject_before_begin);
  RUN_TEST(test_example_transport_maps_wire_errors_and_read_only_transactions);

  return UNITY_END();
}
