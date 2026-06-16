/// @file test_basic.cpp
/// @brief Native contract tests for MCP45HVX1 lifecycle, protocol, and health behavior.

#include <unity.h>

#include <limits>
#include <type_traits>

#include "MCP45HVX1/MCP45HVX1.h"

#include "Arduino.h"
#include "Wire.h"

SerialClass Serial;
TwoWire Wire;

#include "examples/common/I2cTransport.h"

using namespace MCP45HVX1;

namespace {

using Driver = ::MCP45HVX1::MCP45HVX1;

static_assert(!std::is_copy_constructible<Driver>::value,
              "MCP45HVX1 must not be copy constructible");
static_assert(!std::is_copy_assignable<Driver>::value,
              "MCP45HVX1 must not be copy assignable");
static_assert(!std::is_move_constructible<Driver>::value,
              "MCP45HVX1 must not be move constructible");
static_assert(!std::is_move_assignable<Driver>::value,
              "MCP45HVX1 must not be move assignable");

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
  int writeErrorAfterMutationRemaining = 0;
  uint8_t writeErrorAfterMutationSkip = 0;
  uint8_t failAfterAppliedCommands = 0;
  uint32_t failReadCall = 0;
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
  return Driver::maxWiperCode(bus->resolution);
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
        bus->tcon = Driver::sanitizeTcon(data[index]);
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
      bus->tcon = Driver::sanitizeTcon(data[1]);
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
    Status st = applyGeneralCall(bus, data, len);
    if (!st.ok()) {
      return st;
    }
    if (bus->writeErrorAfterMutationRemaining > 0) {
      if (bus->writeErrorAfterMutationSkip > 0) {
        bus->writeErrorAfterMutationSkip--;
        return Status::Ok();
      }
      bus->writeErrorAfterMutationRemaining--;
      return bus->writeError;
    }
    return Status::Ok();
  }

  uint8_t appliedCommands = 0;
  for (size_t i = 0; i < len; ++i) {
    Status st = applyCommand(bus, data[i], data, i, len);
    if (!st.ok()) {
      return st;
    }
    ++appliedCommands;
    if (bus->writeErrorAfterMutationRemaining > 0 &&
        bus->failAfterAppliedCommands > 0 &&
        appliedCommands >= bus->failAfterAppliedCommands) {
      if (bus->writeErrorAfterMutationSkip > 0) {
        bus->writeErrorAfterMutationSkip--;
        continue;
      }
      bus->writeErrorAfterMutationRemaining--;
      return bus->writeError;
    }
  }
  if (bus->writeErrorAfterMutationRemaining > 0) {
    if (bus->writeErrorAfterMutationSkip > 0) {
      bus->writeErrorAfterMutationSkip--;
      return Status::Ok();
    }
    bus->writeErrorAfterMutationRemaining--;
    return bus->writeError;
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
  if (bus->failReadCall != 0U && bus->readCalls == bus->failReadCall) {
    bus->failReadCall = 0;
    return bus->readError;
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

Config makeGeneralCallConfig(FakeBus& bus) {
  Config cfg = makeConfig(bus);
  cfg.allowGeneralCall = true;
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

  Status pending = Status::InProgress();
  TEST_ASSERT_TRUE(pending.inProgress());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::IN_PROGRESS),
                          static_cast<uint8_t>(pending.code));
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
  TEST_ASSERT_FALSE(cfg.allowGeneralCall);
  TEST_ASSERT_FALSE(cfg.writeInitialWiper);
  TEST_ASSERT_FALSE(cfg.writeInitialTcon);
  TEST_ASSERT_FALSE(cfg.requirePowerOnDefaults);
  TEST_ASSERT_TRUE(cfg.requireReadMsbZero);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.offlineThreshold);
}

void test_begin_rejects_invalid_config() {
  Driver dev;
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

void test_begin_address_matrix() {
  struct AddressCase {
    uint8_t address;
    bool allowAlternate;
    bool expectOk;
    bool expectAlternate;
  };

  const AddressCase cases[] = {
      {0x3C, false, true, false},
      {0x3D, false, true, false},
      {0x3E, false, true, false},
      {0x3F, false, true, false},
      {0x3B, false, false, false},
      {0x40, false, false, false},
      {0x5C, false, false, false},
      {0x5D, false, false, false},
      {0x5E, false, false, false},
      {0x5F, false, false, false},
      {0x5C, true, true, true},
      {0x5D, true, true, true},
      {0x5E, true, true, true},
      {0x5F, true, true, true},
      {0x5B, true, false, false},
      {0x60, true, false, false},
  };

  for (const AddressCase& tc : cases) {
    FakeBus bus;
    Driver dev;
    Config cfg = makeConfig(bus);
    cfg.i2cAddress = tc.address;
    cfg.allowAlternateAddressRange = tc.allowAlternate;

    Status st = dev.begin(cfg);
    if (tc.expectOk) {
      TEST_ASSERT_TRUE(st.ok());
      TEST_ASSERT_TRUE(dev.isInitialized());
      TEST_ASSERT_EQUAL_HEX8(tc.address, dev.getDeviceInfo().i2cAddress);
      TEST_ASSERT_EQUAL(tc.expectAlternate, dev.getDeviceInfo().usingAlternateAddressRange);
      TEST_ASSERT_EQUAL_UINT32(2u, bus.readCalls);
    } else {
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                              static_cast<uint8_t>(st.code));
      TEST_ASSERT_FALSE(dev.isInitialized());
      TEST_ASSERT_EQUAL_UINT32(0u, bus.readCalls);
      TEST_ASSERT_EQUAL_UINT32(0u, bus.writeCalls);
    }
  }
}

void test_begin_reads_and_caches_registers() {
  FakeBus bus;
  bus.wiper = 0x42;
  bus.tcon = 0xFB;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  SettingsSnapshot s;
  TEST_ASSERT_TRUE(dev.getSettings(s).ok());
  TEST_ASSERT_TRUE(s.initialized);
  TEST_ASSERT_TRUE(s.cachedWiperKnown);
  TEST_ASSERT_TRUE(s.cachedTconKnown);
  TEST_ASSERT_EQUAL_HEX8(0x42, s.cachedWiper);
  TEST_ASSERT_EQUAL_HEX8(0xFB, s.cachedTcon);
  TEST_ASSERT_EQUAL_UINT32(0u, s.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(2u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(0u, bus.writeLogCount);

  const SettingsSnapshot byValue = dev.getSettings();
  TEST_ASSERT_EQUAL_HEX8(s.cachedWiper, byValue.cachedWiper);
  TEST_ASSERT_EQUAL_HEX8(s.cachedTcon, byValue.cachedTcon);
}

void test_read_snapshot_does_not_partially_overwrite_output_on_failure() {
  FakeBus bus;
  bus.wiper = 0x42;
  bus.tcon = 0xFB;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  RegisterSnapshot snapshot{0xAA, 0x55};
  bus.wiper = 0x11;
  bus.tcon = 0xF7;
  bus.failReadCall = bus.readCalls + 2U;
  bus.readError = Status::Error(Err::I2C_BUS, "snapshot TCON read failed", -81);

  Status st = dev.readSnapshot(snapshot);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-81, st.detail);
  TEST_ASSERT_EQUAL_HEX8(0xAA, snapshot.wiper);
  TEST_ASSERT_EQUAL_HEX8(0x55, snapshot.tcon);

  SettingsSnapshot settings = dev.getSettings();
  TEST_ASSERT_TRUE(settings.cachedWiperKnown);
  TEST_ASSERT_EQUAL_HEX8(0x11, settings.cachedWiper);
  TEST_ASSERT_TRUE(settings.cachedTconKnown);
  TEST_ASSERT_EQUAL_HEX8(0xFB, settings.cachedTcon);
}

void test_begin_preserves_transport_status_except_address_nack() {
  {
    FakeBus bus;
    Driver dev;
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_TIMEOUT, "baseline timeout", -31);
    Status st = dev.begin(makeConfig(bus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(-31, st.detail);
    TEST_ASSERT_FALSE(dev.isInitialized());
  }

  {
    FakeBus bus;
    Driver dev;
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_BUS, "baseline bus error", -32);
    Status st = dev.begin(makeConfig(bus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(-32, st.detail);
  }

  {
    FakeBus bus;
    Driver dev;
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_NACK_DATA, "baseline data NACK", -33);
    Status st = dev.begin(makeConfig(bus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(-33, st.detail);
  }

  {
    FakeBus bus;
    Driver dev;
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_NACK_ADDR, "baseline address NACK", -34);
    Status st = dev.begin(makeConfig(bus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(-34, st.detail);
  }
}

void test_begin_without_initial_writes_is_read_only() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.initialized);
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.cachedTconKnown);
  TEST_ASSERT_FALSE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT32(2u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(0u, bus.writeLogCount);
}

void test_device_info_and_resistance_helpers() {
  FakeBus bus;
  Driver dev;
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
                           Driver::nominalResistanceOhms(ResistanceOption::R10K));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f,
                           Driver::maxTerminalCurrentMilliAmps(ResistanceOption::R5K));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.5f,
                           Driver::maxTerminalCurrentMilliAmps(ResistanceOption::R10K));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f,
                           Driver::maxTerminalCurrentMilliAmps(ResistanceOption::R100K));
  TEST_ASSERT_EQUAL_UINT32(100000u,
                           Driver::nominalResistanceOhms(ResistanceOption::R100K));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 392.16f,
                           Driver::stepResistanceOhms(ResistanceOption::R100K,
                                                         Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 50196.0f,
                           Driver::resistanceBToWOhms(0x80, ResistanceOption::R100K,
                                                         Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 39.22f,
                           Driver::stepResistanceOhms(ResistanceOption::R10K,
                                                         Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 5019.6f,
                           Driver::resistanceBToWOhms(0x80, ResistanceOption::R10K,
                                                         Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 4980.4f,
                           Driver::resistanceAToWOhms(0x80, ResistanceOption::R10K,
                                                         Resolution::Bits8));
}

void test_silicon_errata_info() {
  const SiliconErrataInfo info = Driver::siliconErrataInfo();
  TEST_ASSERT_EQUAL_STRING("DS80000649B", info.documentNumber);
  TEST_ASSERT_EQUAL_STRING("B (July 2015)", info.documentRevision);
  TEST_ASSERT_EQUAL_STRING("MCP45HVX1 Rev. A1 Silicon/Data Sheet Errata",
                           info.documentTitle);
  TEST_ASSERT_TRUE(info.sharedBusI2cHazard);
  TEST_ASSERT_TRUE(info.generalCallAddressDecodeHazard);
  TEST_ASSERT_TRUE(info.hardwareGeneralCallBitIgnored);
  TEST_ASSERT_TRUE(info.uniqueBusWorkaroundForAffectedSilicon);
  TEST_ASSERT_TRUE(info.productionReleaseGateRequired);
  TEST_ASSERT_TRUE(info.sharedBusRiskAcceptanceRequired);
  TEST_ASSERT_TRUE(info.generalCallRequiresIsolatedBusEvidence);
  TEST_ASSERT_TRUE(info.markingSummary != nullptr && info.markingSummary[0] != '\0');
}

void test_device_model_defaults_for_variants_and_registers() {
  TEST_ASSERT_EQUAL_HEX8(0x00, cmd::REG_WIPER0);
  TEST_ASSERT_EQUAL_HEX8(0x04, cmd::REG_TCON0);
  TEST_ASSERT_EQUAL_HEX8(0x3C, cmd::MIN_ADDRESS);
  TEST_ASSERT_EQUAL_HEX8(0x3F, cmd::MAX_ADDRESS);
  TEST_ASSERT_EQUAL_HEX8(0x5C, cmd::ALT_MIN_ADDRESS);
  TEST_ASSERT_EQUAL_HEX8(0x5F, cmd::ALT_MAX_ADDRESS);

  TEST_ASSERT_EQUAL_HEX8(0x7F, Driver::maxWiperCode(Resolution::Bits7));
  TEST_ASSERT_EQUAL_HEX8(0xFF, Driver::maxWiperCode(Resolution::Bits8));
  TEST_ASSERT_EQUAL_HEX8(0x3F, Driver::defaultWiperCode(Resolution::Bits7));
  TEST_ASSERT_EQUAL_HEX8(0x7F, Driver::defaultWiperCode(Resolution::Bits8));
  TEST_ASSERT_EQUAL_HEX8(0xFF, cmd::TCON_DEFAULT);
  TEST_ASSERT_EQUAL_HEX8(0xF0, cmd::TCON_RESERVED_MASK);
  TEST_ASSERT_EQUAL_HEX8(0x0F, cmd::TCON_IMPLEMENTED_MASK);

  FakeBus bus7;
  bus7.resolution = Resolution::Bits7;
  bus7.wiper = cmd::WIPER_DEFAULT_7BIT;
  Driver dev7;
  TEST_ASSERT_TRUE(dev7.begin(makeConfig(bus7)).ok());
  DeviceInfo info7 = dev7.getDeviceInfo();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Resolution::Bits7),
                          static_cast<uint8_t>(info7.resolution));
  TEST_ASSERT_EQUAL_HEX8(cmd::WIPER_MAX_7BIT, info7.maxWiperCode);
  TEST_ASSERT_EQUAL_HEX8(cmd::WIPER_DEFAULT_7BIT, info7.defaultWiperCode);

  FakeBus bus8;
  Driver dev8;
  TEST_ASSERT_TRUE(dev8.begin(makeConfig(bus8)).ok());
  DeviceInfo info8 = dev8.getDeviceInfo();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Resolution::Bits8),
                          static_cast<uint8_t>(info8.resolution));
  TEST_ASSERT_EQUAL_HEX8(cmd::WIPER_MAX_8BIT, info8.maxWiperCode);
  TEST_ASSERT_EQUAL_HEX8(cmd::WIPER_DEFAULT_8BIT, info8.defaultWiperCode);
}

void test_begin_require_power_on_defaults() {
  FakeBus bus;
  bus.wiper = 0x22;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.requirePowerOnDefaults = true;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));

  FakeBus bus7;
  bus7.resolution = Resolution::Bits7;
  bus7.wiper = cmd::WIPER_DEFAULT_7BIT + 1u;
  Driver dev7;
  Config cfg7 = makeConfig(bus7);
  cfg7.requirePowerOnDefaults = true;
  st = dev7.begin(cfg7);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));

  FakeBus tconBus;
  tconBus.tcon = 0xF7;
  Driver tconDev;
  Config tconCfg = makeConfig(tconBus);
  tconCfg.requirePowerOnDefaults = true;
  st = tconDev.begin(tconCfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));

  FakeBus ok7;
  ok7.resolution = Resolution::Bits7;
  ok7.wiper = cmd::WIPER_DEFAULT_7BIT;
  Driver ok7Dev;
  Config ok7Cfg = makeConfig(ok7);
  ok7Cfg.requirePowerOnDefaults = true;
  TEST_ASSERT_TRUE(ok7Dev.begin(ok7Cfg).ok());
}

void test_begin_optional_initial_writes() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.writeInitialWiper = true;
  cfg.initialWiperCode = 0x20;
  cfg.writeInitialTcon = true;
  cfg.initialTcon = 0x07;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_HEX8(0x20, bus.wiper);
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.tcon);
}

void test_begin_initial_wiper_write_is_explicit_opt_in() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.writeInitialWiper = true;
  cfg.initialWiperCode = 0x20;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  TEST_ASSERT_EQUAL_UINT32(1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(1u, bus.writeLogCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, bus.writeAddrLog[0]);
  TEST_ASSERT_EQUAL_UINT8(2u, bus.writeLenLog[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::makeCommand(cmd::REG_WIPER0, cmd::Command::WriteData),
                         bus.writeByte0Log[0]);
  TEST_ASSERT_EQUAL_HEX8(0x20, bus.writeByte1Log[0]);
  TEST_ASSERT_EQUAL_HEX8(0x20, bus.wiper);
  TEST_ASSERT_EQUAL_HEX8(cmd::TCON_DEFAULT, bus.tcon);
}

void test_begin_initial_tcon_success_then_wiper_mutate_fail_is_recoverable() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.writeInitialTcon = true;
  cfg.initialTcon = 0x07;
  cfg.writeInitialWiper = true;
  cfg.initialWiperCode = 0x20;
  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeErrorAfterMutationSkip = 1;
  bus.writeError = Status::Error(Err::I2C_BUS, "startup wiper failed after mutation", -21);

  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-21, st.detail);
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.tcon);
  TEST_ASSERT_EQUAL_HEX8(0x20, bus.wiper);

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_TRUE(snap.cachedTconKnown);
  TEST_ASSERT_EQUAL_HEX8(0xF7, snap.cachedTcon);
  TEST_ASSERT_FALSE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(snap.hardwareStateUncertainError.code));
  TEST_ASSERT_EQUAL_UINT8(1u, snap.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1u, snap.totalFailures);

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x20, value);
  snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.cachedTconKnown);
  TEST_ASSERT_FALSE(snap.hardwareStateUncertain);
}

void test_begin_initial_tcon_mutate_fail_is_recoverable() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.writeInitialTcon = true;
  cfg.initialTcon = 0x07;
  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "startup TCON timeout", -22);

  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.tcon);

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.initialized);
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(snap.hardwareStateUncertainError.code));
}

void test_begin_initial_write_pre_mutation_failure_does_not_set_uncertainty() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.writeInitialWiper = true;
  cfg.initialWiperCode = 0x20;
  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_ADDR, "startup address NACK", -23);

  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(cmd::WIPER_DEFAULT_8BIT, bus.wiper);

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.initialized);
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.cachedTconKnown);
  TEST_ASSERT_FALSE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(snap.state));
}

void test_recover_after_failed_startup_write_reads_volatile_state() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.writeInitialWiper = true;
  cfg.initialWiperCode = 0x22;
  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_BUS, "startup write failed after mutation", -24);

  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_TRUE(dev.hardwareStateUncertain());
  const uint32_t readsAfterFailedBegin = bus.readCalls;

  TEST_ASSERT_TRUE(dev.recover().ok());
  TEST_ASSERT_EQUAL_UINT32(readsAfterFailedBegin + 2u, bus.readCalls);
  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.cachedTconKnown);
  TEST_ASSERT_EQUAL_HEX8(0x22, snap.cachedWiper);
  TEST_ASSERT_FALSE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(snap.state));
}

void test_failed_begin_clears_stale_runtime_snapshot() {
  FakeBus bus;
  Driver dev;

  Config good = makeConfig(bus);
  good.i2cAddress = 0x3F;
  good.initialWiperCode = 0x20;
  TEST_ASSERT_TRUE(dev.begin(good).ok());
  TEST_ASSERT_TRUE(dev.writeWiper(0x55).ok());

  Config bad = makeConfig(bus);
  bad.i2cWrite = nullptr;
  bad.i2cWriteRead = nullptr;
  Status st = dev.begin(bad);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, snap.config.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, snap.config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, snap.config.offlineThreshold);
  TEST_ASSERT_FALSE(snap.cachedWiperKnown);
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.totalFailures);
  TEST_ASSERT_EQUAL_UINT8(0u, snap.consecutiveFailures);
}

void test_end_resets_lifecycle_without_device_io() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(dev.writeWiper(0x55).ok());
  const uint32_t readsBeforeEnd = bus.readCalls;
  const uint32_t writesBeforeEnd = bus.writeCalls;

  dev.end();

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_FALSE(snap.cachedWiperKnown);
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_FALSE(snap.hardwareStateUncertain);

  uint8_t value = 0;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readWiper(value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeRegister(cmd::REG_WIPER0, 0x22).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.generalCallWriteTcon(0xFF).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT32(readsBeforeEnd, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(writesBeforeEnd, bus.writeCalls);
}

void test_read_write_wiper() {
  FakeBus bus;
  Driver dev;
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
  Driver dev;
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

void test_variant_wiper_code_matrix() {
  struct VariantCase {
    Resolution resolution;
    const uint8_t* accepted;
    size_t acceptedCount;
    uint8_t rejected;
    bool hasRejected;
  };

  const uint8_t hv31Accepted[] = {0x00, 0x3F, 0x7F};
  const uint8_t hv51Accepted[] = {0x00, 0x7F, 0x80, 0xFF};
  const VariantCase cases[] = {
      {Resolution::Bits7, hv31Accepted, sizeof(hv31Accepted) / sizeof(hv31Accepted[0]), 0x80,
       true},
      {Resolution::Bits8, hv51Accepted, sizeof(hv51Accepted) / sizeof(hv51Accepted[0]), 0x00,
       false},
  };

  for (const VariantCase& tc : cases) {
    FakeBus bus;
    bus.resolution = tc.resolution;
    bus.wiper = Driver::defaultWiperCode(tc.resolution);
    Driver dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    for (size_t i = 0; i < tc.acceptedCount; ++i) {
      const uint8_t code = tc.accepted[i];
      TEST_ASSERT_TRUE(dev.writeWiper(code).ok());
      TEST_ASSERT_EQUAL_HEX8(code, bus.wiper);
      SettingsSnapshot snap = dev.getSettings();
      TEST_ASSERT_TRUE(snap.cachedWiperKnown);
      TEST_ASSERT_EQUAL_HEX8(code, snap.cachedWiper);
      TEST_ASSERT_FALSE(snap.hardwareStateUncertain);
    }

    if (tc.hasRejected) {
      const uint32_t writesBeforeInvalid = bus.writeCalls;
      const uint8_t wiperBeforeInvalid = bus.wiper;
      Status st = dev.writeWiper(tc.rejected);
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                              static_cast<uint8_t>(st.code));
      TEST_ASSERT_EQUAL_HEX8(wiperBeforeInvalid, bus.wiper);
      TEST_ASSERT_EQUAL_UINT32(writesBeforeInvalid, bus.writeCalls);
      TEST_ASSERT_TRUE(dev.getSettings().cachedWiperKnown);
      TEST_ASSERT_FALSE(dev.hardwareStateUncertain());
    }
  }
}

void test_increment_decrement_clamp_and_cache() {
  FakeBus bus;
  bus.wiper = 0xFE;
  Driver dev;
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
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_EQUAL_HEX8(cmd::TCON_DEFAULT, Driver::sanitizeTcon(cmd::TCON_DEFAULT));
  TEST_ASSERT_EQUAL_HEX8(0xF7, Driver::sanitizeTcon(0x07));

  TEST_ASSERT_TRUE(dev.writeTcon(0x00).ok());
  TEST_ASSERT_EQUAL_UINT8(1u, bus.writeLogCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::makeCommand(cmd::REG_TCON0, cmd::Command::WriteData),
                         bus.writeByte0Log[0]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.writeByte1Log[0]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.tcon);
  TEST_ASSERT_EQUAL_HEX8(0xF0, dev.getSettings().cachedTcon);

  TerminalStatus decoded = Driver::decodeTcon(cmd::TCON_DEFAULT);
  TEST_ASSERT_FALSE(decoded.softwareShutdown);
  TEST_ASSERT_TRUE(decoded.terminalA);
  TEST_ASSERT_TRUE(decoded.terminalW);
  TEST_ASSERT_TRUE(decoded.terminalB);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TerminalMode::Potentiometer),
                          static_cast<uint8_t>(decoded.mode));
}

void test_terminal_helpers() {
  FakeBus bus;
  Driver dev;
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

  status = Driver::decodeTcon(cmd::TCON_SHUTDOWN);
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

void test_job_set_wiper_is_one_instruction_and_blocks_interleaving() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  const uint32_t readsAfterBegin = bus.readCalls;

  Status st = dev.startSetWiperJob(0x44);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(dev.jobActive());

  uint8_t value = 0;
  st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsAfterBegin, bus.readCalls);

  st = dev.pollJob(bus.nowMs, 5);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(0x44, bus.wiper);

  JobSnapshot job = dev.getJobSnapshot();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(JobType::SetWiper),
                          static_cast<uint8_t>(job.type));
  TEST_ASSERT_FALSE(job.active);
  TEST_ASSERT_TRUE(job.outputChanging);
  TEST_ASSERT_EQUAL_UINT8(1u, job.instructionsPlanned);
  TEST_ASSERT_EQUAL_UINT8(1u, job.instructionsCompleted);
  TEST_ASSERT_EQUAL_UINT8(1u, job.lastPollInstructions);
  TEST_ASSERT_TRUE(job.status.ok());

  SettingsSnapshot settings = dev.getSettings();
  TEST_ASSERT_TRUE(settings.cachedWiperKnown);
  TEST_ASSERT_EQUAL_HEX8(0x44, settings.cachedWiper);
  TEST_ASSERT_FALSE(settings.hardwareStateUncertain);
}

void test_job_snapshot_respects_instruction_budget() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  const uint32_t readsAfterBegin = bus.readCalls;
  bus.wiper = 0x22;
  bus.tcon = 0xF7;

  TEST_ASSERT_TRUE(dev.startReadSnapshotJob().ok());

  Status st = dev.pollJob(bus.nowMs, 0);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(readsAfterBegin, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(0u, dev.getJobSnapshot().lastPollInstructions);

  st = dev.pollJob(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(readsAfterBegin + 1u, bus.readCalls);
  JobSnapshot job = dev.getJobSnapshot();
  TEST_ASSERT_TRUE(job.active);
  TEST_ASSERT_FALSE(job.outputChanging);
  TEST_ASSERT_FALSE(job.registersValid);
  TEST_ASSERT_EQUAL_UINT8(2u, job.instructionsPlanned);
  TEST_ASSERT_EQUAL_UINT8(1u, job.instructionsCompleted);
  TEST_ASSERT_EQUAL_UINT8(1u, job.lastPollInstructions);
  TEST_ASSERT_EQUAL_HEX8(0x22, job.registers.wiper);

  st = dev.pollJob(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(readsAfterBegin + 2u, bus.readCalls);
  job = dev.getJobSnapshot();
  TEST_ASSERT_FALSE(job.active);
  TEST_ASSERT_TRUE(job.registersValid);
  TEST_ASSERT_EQUAL_UINT8(2u, job.instructionsCompleted);
  TEST_ASSERT_EQUAL_HEX8(0x22, job.registers.wiper);
  TEST_ASSERT_EQUAL_HEX8(0xF7, job.registers.tcon);
}

void test_job_terminal_setter_is_visible_read_modify_write() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  const uint32_t readsAfterBegin = bus.readCalls;
  const uint32_t writesAfterBegin = bus.writeCalls;

  TEST_ASSERT_TRUE(dev.startSetTerminalJob(Terminal::A, false).ok());
  Status st = dev.pollJob(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(readsAfterBegin + 1u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(writesAfterBegin, bus.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(cmd::TCON_DEFAULT, bus.tcon);

  st = dev.pollJob(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(writesAfterBegin + 1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.tcon);

  JobSnapshot job = dev.getJobSnapshot();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(JobType::SetTerminal),
                          static_cast<uint8_t>(job.type));
  TEST_ASSERT_TRUE(job.outputChanging);
  TEST_ASSERT_EQUAL_UINT8(2u, job.instructionsPlanned);
  TEST_ASSERT_EQUAL_UINT8(2u, job.instructionsCompleted);
  TEST_ASSERT_EQUAL_UINT8(1u, job.lastPollInstructions);
}

void test_job_terminal_setter_noop_consumes_read_only() {
  FakeBus bus;
  bus.tcon = 0xFB;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  const uint32_t readsAfterBegin = bus.readCalls;
  const uint32_t writesAfterBegin = bus.writeCalls;

  TEST_ASSERT_TRUE(dev.startSetTerminalJob(Terminal::A, false).ok());
  Status st = dev.pollJob(bus.nowMs, 2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(readsAfterBegin + 1u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(writesAfterBegin, bus.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.tcon);

  JobSnapshot job = dev.getJobSnapshot();
  TEST_ASSERT_EQUAL_UINT8(1u, job.instructionsPlanned);
  TEST_ASSERT_EQUAL_UINT8(1u, job.instructionsCompleted);
  TEST_ASSERT_EQUAL_UINT8(1u, job.lastPollInstructions);
}

void test_job_step_sequence_runs_one_chunk_per_poll() {
  FakeBus bus;
  bus.wiper = 0x00;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint8_t steps = static_cast<uint8_t>(cmd::MAX_COMMAND_CHUNK + 2U);
  TEST_ASSERT_TRUE(dev.startIncrementWiperJob(steps).ok());
  Status st = dev.pollJob(bus.nowMs, 5);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(cmd::MAX_COMMAND_CHUNK), bus.writeLenLog[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::MAX_COMMAND_CHUNK), bus.wiper);

  JobSnapshot job = dev.getJobSnapshot();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(JobType::IncrementWiper),
                          static_cast<uint8_t>(job.type));
  TEST_ASSERT_TRUE(job.outputChanging);
  TEST_ASSERT_EQUAL_UINT8(2u, job.instructionsPlanned);
  TEST_ASSERT_EQUAL_UINT8(1u, job.instructionsCompleted);
  TEST_ASSERT_EQUAL_UINT8(1u, job.lastPollInstructions);

  st = dev.pollJob(bus.nowMs, 5);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(2u, bus.writeLenLog[1]);
  TEST_ASSERT_EQUAL_HEX8(steps, bus.wiper);
  TEST_ASSERT_EQUAL_UINT8(2u, dev.getJobSnapshot().instructionsCompleted);
}

void test_job_step_failure_stops_remaining_chunks_and_marks_uncertain() {
  FakeBus bus;
  bus.wiper = 0x00;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.startIncrementWiperJob(130).ok());
  Status st = dev.pollJob(bus.nowMs, 5);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::MAX_COMMAND_CHUNK), bus.wiper);

  bus.writeErrorAfterMutationRemaining = 1;
  bus.failAfterAppliedCommands = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "timeout after chunk mutation", -130);
  st = dev.pollJob(bus.nowMs, 5);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-130, st.detail);
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::MAX_COMMAND_CHUNK + 1U), bus.wiper);

  JobSnapshot job = dev.getJobSnapshot();
  TEST_ASSERT_EQUAL_UINT8(3u, job.instructionsPlanned);
  TEST_ASSERT_EQUAL_UINT8(1u, job.instructionsCompleted);
  TEST_ASSERT_EQUAL_UINT8(1u, job.lastPollInstructions);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(job.status.code));

  SettingsSnapshot settings = dev.getSettings();
  TEST_ASSERT_FALSE(settings.cachedWiperKnown);
  TEST_ASSERT_TRUE(settings.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(settings.hardwareStateUncertainError.code));
}

void test_recover_job_keeps_offline_latch_until_full_readback() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  uint8_t value = 0;
  Status st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t readsAfterOffline = bus.readCalls;
  bus.wiper = 0x33;
  bus.tcon = 0xF7;
  TEST_ASSERT_TRUE(dev.startRecoverJob().ok());

  st = dev.pollJob(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(readsAfterOffline + 1u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));

  st = dev.pollJob(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(readsAfterOffline + 2u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));

  JobSnapshot job = dev.getJobSnapshot();
  TEST_ASSERT_TRUE(job.registersValid);
  TEST_ASSERT_EQUAL_HEX8(0x33, job.registers.wiper);
  TEST_ASSERT_EQUAL_HEX8(0xF7, job.registers.tcon);
}

void test_direct_register_access_rejects_reserved() {
  FakeBus bus;
  Driver dev;
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
  Driver dev;
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
  Driver dev;
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
  Driver dev;
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
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeGeneralCallConfig(bus)).ok());

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
  Driver dev7;
  TEST_ASSERT_TRUE(dev7.begin(makeGeneralCallConfig(bus)).ok());
  Status st = dev7.generalCallWriteWiper(0x80);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
}

void test_general_call_disabled_by_default_without_bus_io() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t generalCallsBefore = bus.generalCallWrites;

  Status st = dev.generalCallWriteWiper(0x44);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::UNSUPPORTED),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("General Call disabled by Config::allowGeneralCall", st.msg);

  st = dev.generalCallWriteTcon(0x07);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::UNSUPPORTED),
                          static_cast<uint8_t>(st.code));

  st = dev.generalCallIncrementWiper();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::UNSUPPORTED),
                          static_cast<uint8_t>(st.code));

  st = dev.generalCallDecrementWiper();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::UNSUPPORTED),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(generalCallsBefore, bus.generalCallWrites);
  TEST_ASSERT_TRUE(dev.getSettings().cachedWiperKnown);
  TEST_ASSERT_TRUE(dev.getSettings().cachedTconKnown);
  TEST_ASSERT_FALSE(dev.hardwareStateUncertain());
}

void test_i2c_reset_ok_from_degraded_does_not_mark_ready_without_device_read() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  uint8_t value = 0;
  Status st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  const uint32_t readsBeforeReset = bus.readCalls;

  st = dev.resetI2cState();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(readsBeforeReset, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.resetCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());

  bus.readErrorRemaining = 1;
  st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
}

void test_i2c_reset_ok_then_device_read_success_returns_ready() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  uint8_t value = 0;
  TEST_ASSERT_FALSE(dev.readWiper(value).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  const uint32_t readsBeforeReset = bus.readCalls;

  TEST_ASSERT_TRUE(dev.resetI2cState().ok());
  TEST_ASSERT_EQUAL_UINT32(readsBeforeReset, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));

  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
  TEST_ASSERT_EQUAL_HEX8(bus.wiper, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalSuccess());
}

void test_write_wiper_pre_mutation_failure_does_not_set_uncertainty() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_ADDR, "address not acknowledged", -7);
  Status st = dev.writeWiper(0x55);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(cmd::WIPER_DEFAULT_8BIT, bus.wiper);

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_FALSE(snap.hardwareStateUncertain);
  TEST_ASSERT_FALSE(dev.hardwareStateUncertain());
}

void test_write_wiper_mutate_then_fail_marks_uncertain() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "write timed out after mutation", -11);
  Status st = dev.writeWiper(0x55);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x55, bus.wiper);

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(snap.hardwareStateUncertainError.code));
  TEST_ASSERT_TRUE(dev.hardwareStateUncertain());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(dev.hardwareStateUncertainError().code));
}

void test_write_tcon_mutate_then_fail_marks_uncertain() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "data NACK after mutation", -12);
  Status st = dev.writeTcon(0x07);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.tcon);

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(snap.hardwareStateUncertainError.code));
}

void test_write_register_mutate_then_fail_marks_target_cache_uncertain() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "raw wiper timeout after mutation", -51);
  Status st = dev.writeRegister(cmd::REG_WIPER0, 0x66);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x66, bus.wiper);
  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.cachedTconKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x66, value);
  TEST_ASSERT_FALSE(dev.hardwareStateUncertain());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "raw TCON data NACK after mutation", -52);
  st = dev.writeRegister(cmd::REG_TCON0, 0x07);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.tcon);
  snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(snap.hardwareStateUncertainError.code));
}

void test_increment_decrement_partial_failure_marks_uncertain() {
  FakeBus bus;
  bus.wiper = 0x10;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.failAfterAppliedCommands = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "timeout after first increment", -13);
  Status st = dev.incrementWiper(3);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x11, bus.wiper);
  TEST_ASSERT_FALSE(dev.getSettings().cachedWiperKnown);
  TEST_ASSERT_TRUE(dev.hardwareStateUncertain());

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
  TEST_ASSERT_FALSE(dev.hardwareStateUncertain());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.failAfterAppliedCommands = 1;
  bus.writeError = Status::Error(Err::I2C_ERROR, "failure after first decrement", -14);
  st = dev.decrementWiper(3);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x10, bus.wiper);
  TEST_ASSERT_FALSE(dev.getSettings().cachedWiperKnown);
  TEST_ASSERT_TRUE(dev.hardwareStateUncertain());
}

void test_general_call_mutate_then_fail_marks_uncertain() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeGeneralCallConfig(bus)).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_BUS, "general call failed after mutation", -15);
  Status st = dev.generalCallWriteWiper(0x44);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x44, bus.wiper);

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(snap.hardwareStateUncertainError.code));
}

void test_successful_readback_clears_uncertainty_only_after_all_unknown_verified() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_ERROR, "wiper fail after mutation", -16);
  TEST_ASSERT_FALSE(dev.writeWiper(0x33).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_ERROR, "tcon fail after mutation", -17);
  TEST_ASSERT_FALSE(dev.writeTcon(0x07).ok());

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.cachedWiperKnown);
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
  snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);

  TEST_ASSERT_TRUE(dev.readTcon(value).ok());
  snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_TRUE(snap.cachedTconKnown);
  TEST_ASSERT_FALSE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK),
                          static_cast<uint8_t>(snap.hardwareStateUncertainError.code));
}

void test_probe_does_not_update_health_but_recover_does() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  Status st = dev.probe();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));

  bus.readErrorRemaining = 1;
  st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));

  bus.readErrorRemaining = 1;
  bus.nowMs = 2000;
  st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(2000u, dev.lastErrorMs());
}

void test_probe_preserves_transport_status_except_address_nack() {
  {
    FakeBus bus;
    Driver dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_TIMEOUT, "probe timeout", -41);
    Status st = dev.probe();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(-41, st.detail);
    TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  }

  {
    FakeBus bus;
    Driver dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_BUS, "probe bus error", -42);
    Status st = dev.probe();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(-42, st.detail);
    TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  }

  {
    FakeBus bus;
    Driver dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_NACK_DATA, "probe data NACK", -43);
    Status st = dev.probe();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(-43, st.detail);
    TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  }

  {
    FakeBus bus;
    Driver dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_NACK_ADDR, "probe address NACK", -44);
    Status st = dev.probe();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(-44, st.detail);
    TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  }
}

void test_probe_preserves_register_mismatch() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readMsb = 0x01;
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
}

void test_recover_preserves_transport_status_detail() {
  struct RecoverErrorCase {
    Err injected;
    int32_t detail;
  };

  const RecoverErrorCase cases[] = {
      {Err::I2C_TIMEOUT, -61},
      {Err::I2C_BUS, -62},
      {Err::I2C_NACK_DATA, -63},
      {Err::I2C_NACK_ADDR, -64},
  };

  for (const RecoverErrorCase& tc : cases) {
    FakeBus bus;
    Driver dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(tc.injected, "recover injected read failure", tc.detail);

    Status st = dev.recover();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tc.injected),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(tc.detail, st.detail);
    TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                            static_cast<uint8_t>(dev.state()));
  }
}

void test_recover_keeps_uncertainty_until_all_unknown_registers_verified() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "wiper timeout after mutation", -71);
  TEST_ASSERT_FALSE(dev.writeWiper(0x33).ok());

  bus.writeErrorAfterMutationRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "tcon data NACK after mutation", -72);
  TEST_ASSERT_FALSE(dev.writeTcon(0x07).ok());

  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.cachedWiperKnown);
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);

  bus.failReadCall = bus.readCalls + 2U;
  bus.readError = Status::Error(Err::I2C_BUS, "recover TCON read failed", -73);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-73, st.detail);

  snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.cachedWiperKnown);
  TEST_ASSERT_EQUAL_HEX8(0x33, snap.cachedWiper);
  TEST_ASSERT_FALSE(snap.cachedTconKnown);
  TEST_ASSERT_TRUE(snap.hardwareStateUncertain);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(snap.state));

  TEST_ASSERT_TRUE(dev.readTcon(snap.cachedTcon).ok());
  TEST_ASSERT_FALSE(dev.hardwareStateUncertain());
}

void test_offline_threshold() {
  FakeBus bus;
  Driver dev;
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

void test_offline_blocks_normal_operations_without_bus_io() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  uint8_t value = 0;
  Status st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t readsAfterOffline = bus.readCalls;
  const uint32_t writesAfterOffline = bus.writeCalls;

  st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);

  st = dev.writeWiper(0x44);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));

  st = dev.incrementWiper();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));

  st = dev.readLastAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));

  st = dev.generalCallWriteWiper(0x44);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));

  st = dev.restorePowerOnDefaults();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));

  TEST_ASSERT_EQUAL_UINT32(readsAfterOffline, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(writesAfterOffline, bus.writeCalls);
}

void test_recover_is_explicit_path_out_of_offline() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  uint8_t value = 0;
  TEST_ASSERT_FALSE(dev.readWiper(value).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t readsAfterOffline = bus.readCalls;
  const uint32_t resetsAfterOffline = bus.resetCalls;
  TEST_ASSERT_TRUE(dev.resetI2cState().ok());
  TEST_ASSERT_EQUAL_UINT32(resetsAfterOffline + 1u, bus.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(readsAfterOffline, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  TEST_ASSERT_TRUE(dev.recover().ok());
  TEST_ASSERT_EQUAL_UINT32(readsAfterOffline + 2u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));

  TEST_ASSERT_TRUE(dev.writeWiper(0x22).ok());
  TEST_ASSERT_EQUAL_HEX8(0x22, bus.wiper);
}

void test_failed_recover_from_offline_reasserts_latch_after_partial_success() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 3;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = cfg.offlineThreshold;
  uint8_t value = 0;
  for (uint8_t i = 0; i < cfg.offlineThreshold; ++i) {
    Status st = dev.readWiper(value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                            static_cast<uint8_t>(st.code));
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.consecutiveFailures() >= cfg.offlineThreshold);

  bus.failReadCall = bus.readCalls + 2U;
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.consecutiveFailures() >= cfg.offlineThreshold);

  const uint32_t readsAfterRecover = bus.readCalls;
  st = dev.readWiper(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsAfterRecover, bus.readCalls);
}

void test_offline_threshold_zero_normalizes_to_one() {
  FakeBus bus;
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 0;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_UINT8(1u, dev.getConfig().offlineThreshold);
}

void test_read_msb_mismatch_is_reported() {
  FakeBus bus;
  bus.readMsb = 0x01;
  Driver dev;
  Status st = dev.begin(makeConfig(bus));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::REGISTER_MISMATCH),
                          static_cast<uint8_t>(st.code));
}

void test_tracked_read_msb_mismatch_updates_health() {
  FakeBus bus;
  Driver dev;
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
  Driver dev;
  Config cfg = makeConfig(bus);
  cfg.requireReadMsbZero = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readWiper(value).ok());
}

void test_zero_step_commands_are_noops() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  const uint32_t writesBefore = bus.writeCalls;

  TEST_ASSERT_TRUE(dev.incrementWiper(0).ok());
  TEST_ASSERT_TRUE(dev.decrementWiper(0).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
}

void test_failed_bus_reset_updates_health() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.resetStatus = Status::Error(Err::I2C_BUS, "forced reset fail", -3);
  bus.nowMs = 4000;
  Status st = dev.resetI2cState();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(4000u, dev.lastErrorMs());
}

void test_conversions() {
  TEST_ASSERT_EQUAL_HEX8(0x80, Driver::codeFromFraction(0.5f, Resolution::Bits8));
  TEST_ASSERT_EQUAL_HEX8(0x40, Driver::codeFromFraction(0.5f, Resolution::Bits7));
  TEST_ASSERT_EQUAL_HEX8(0x00, Driver::codeFromFraction(-0.1f, Resolution::Bits8));
  TEST_ASSERT_EQUAL_HEX8(0xFF, Driver::codeFromFraction(1.1f, Resolution::Bits8));
  TEST_ASSERT_EQUAL_HEX8(0x7F, Driver::codeFromFraction(1.1f, Resolution::Bits7));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, Driver::fractionFromCode(0xFF, Resolution::Bits8));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, Driver::fractionFromCode(0x7F, Resolution::Bits7));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, Driver::fractionFromCode(0xFF, Resolution::Bits7));
}

void test_fraction_endpoint_matrix_by_variant() {
  struct FractionCase {
    Resolution resolution;
    float fraction;
    uint8_t expectedCode;
    float expectedReadback;
  };

  const FractionCase cases[] = {
      {Resolution::Bits7, 0.0f, 0x00, 0.0f},
      {Resolution::Bits7, 0.5f, 0x40, 0.504f},
      {Resolution::Bits7, 1.0f, 0x7F, 1.0f},
      {Resolution::Bits8, 0.0f, 0x00, 0.0f},
      {Resolution::Bits8, 0.5f, 0x80, 0.502f},
      {Resolution::Bits8, 1.0f, 0xFF, 1.0f},
  };

  for (const FractionCase& tc : cases) {
    TEST_ASSERT_EQUAL_HEX8(tc.expectedCode, Driver::codeFromFraction(tc.fraction, tc.resolution));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, tc.expectedReadback,
                             Driver::fractionFromCode(tc.expectedCode, tc.resolution));

    FakeBus bus;
    bus.resolution = tc.resolution;
    Driver dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    TEST_ASSERT_TRUE(dev.writeWiperFraction(tc.fraction).ok());
    TEST_ASSERT_EQUAL_HEX8(tc.expectedCode, bus.wiper);

    float readback = -1.0f;
    TEST_ASSERT_TRUE(dev.readWiperFraction(readback).ok());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, tc.expectedReadback, readback);
  }

  for (const Resolution resolution : {Resolution::Bits7, Resolution::Bits8}) {
    FakeBus bus;
    bus.resolution = resolution;
    Driver dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    const uint32_t writesBeforeInvalid = bus.writeCalls;
    const uint8_t wiperBeforeInvalid = bus.wiper;

    Status st = dev.writeWiperFraction(-0.01f);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                            static_cast<uint8_t>(st.code));
    st = dev.writeWiperFraction(1.01f);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                            static_cast<uint8_t>(st.code));
    st = dev.writeWiperFraction(std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_UINT32(writesBeforeInvalid, bus.writeCalls);
    TEST_ASSERT_EQUAL_HEX8(wiperBeforeInvalid, bus.wiper);
    TEST_ASSERT_FALSE(dev.hardwareStateUncertain());
  }
}

void test_fraction_read_write_helpers() {
  FakeBus bus;
  Driver dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeWiperFraction(0.5f).ok());
  TEST_ASSERT_EQUAL_HEX8(0x80, bus.wiper);

  float fraction = 0.0f;
  TEST_ASSERT_TRUE(dev.readWiperFraction(fraction).ok());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.502f, fraction);

  const uint32_t writesBeforeInvalid = bus.writeCalls;
  Status st = dev.writeWiperFraction(1.1f);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x80, bus.wiper);
  TEST_ASSERT_EQUAL_UINT32(writesBeforeInvalid, bus.writeCalls);
  TEST_ASSERT_FALSE(dev.hardwareStateUncertain());
  TEST_ASSERT_TRUE(dev.getSettings().cachedWiperKnown);

  st = dev.writeWiperFraction(-0.1f);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x80, bus.wiper);
  TEST_ASSERT_EQUAL_UINT32(writesBeforeInvalid, bus.writeCalls);
  TEST_ASSERT_FALSE(dev.hardwareStateUncertain());
  TEST_ASSERT_TRUE(dev.getSettings().cachedWiperKnown);

  st = dev.writeWiperFraction(std::numeric_limits<float>::quiet_NaN());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x80, bus.wiper);
  TEST_ASSERT_EQUAL_UINT32(writesBeforeInvalid, bus.writeCalls);
  TEST_ASSERT_FALSE(dev.hardwareStateUncertain());
  TEST_ASSERT_TRUE(dev.getSettings().cachedWiperKnown);
}

void test_operations_reject_before_begin() {
  Driver dev;
  uint8_t value = 0;
  bool flag = false;
  float fraction = 0.0f;
  TerminalStatus terminalStatus;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readWiper(value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeWiper(0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeTcon(0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readWiperFraction(fraction).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeWiperFraction(0.0f).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.incrementWiper().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.decrementWiper().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readTcon(value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.setTerminalEnabled(Terminal::A, true).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.getSoftwareShutdown(flag).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readTerminalStatus(terminalStatus).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readRegister(cmd::REG_WIPER0, value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeRegister(cmd::REG_TCON0, 0xFF).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readLastAddress(value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.generalCallWriteWiper(0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.generalCallWriteTcon(0xFF).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.generalCallIncrementWiper().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.generalCallDecrementWiper().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.resetI2cState().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.restorePowerOnDefaults().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.resetToDefaults().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.probe().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startSetWiperJob(0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startReadSnapshotJob().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startSetTerminalJob(Terminal::A, true).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startIncrementWiperJob().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startDecrementWiperJob().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startRecoverJob().code));
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
  RUN_TEST(test_begin_preserves_transport_status_except_address_nack);
  RUN_TEST(test_begin_address_matrix);
  RUN_TEST(test_begin_reads_and_caches_registers);
  RUN_TEST(test_read_snapshot_does_not_partially_overwrite_output_on_failure);
  RUN_TEST(test_begin_without_initial_writes_is_read_only);
  RUN_TEST(test_device_info_and_resistance_helpers);
  RUN_TEST(test_silicon_errata_info);
  RUN_TEST(test_device_model_defaults_for_variants_and_registers);
  RUN_TEST(test_begin_require_power_on_defaults);
  RUN_TEST(test_begin_optional_initial_writes);
  RUN_TEST(test_begin_initial_wiper_write_is_explicit_opt_in);
  RUN_TEST(test_begin_initial_tcon_success_then_wiper_mutate_fail_is_recoverable);
  RUN_TEST(test_begin_initial_tcon_mutate_fail_is_recoverable);
  RUN_TEST(test_begin_initial_write_pre_mutation_failure_does_not_set_uncertainty);
  RUN_TEST(test_recover_after_failed_startup_write_reads_volatile_state);
  RUN_TEST(test_failed_begin_clears_stale_runtime_snapshot);
  RUN_TEST(test_end_resets_lifecycle_without_device_io);
  RUN_TEST(test_read_write_wiper);
  RUN_TEST(test_wire_protocol_frames);
  RUN_TEST(test_variant_wiper_code_matrix);
  RUN_TEST(test_increment_decrement_clamp_and_cache);
  RUN_TEST(test_tcon_write_sanitizes_reserved_bits);
  RUN_TEST(test_terminal_helpers);
  RUN_TEST(test_job_set_wiper_is_one_instruction_and_blocks_interleaving);
  RUN_TEST(test_job_snapshot_respects_instruction_budget);
  RUN_TEST(test_job_terminal_setter_is_visible_read_modify_write);
  RUN_TEST(test_job_terminal_setter_noop_consumes_read_only);
  RUN_TEST(test_job_step_sequence_runs_one_chunk_per_poll);
  RUN_TEST(test_job_step_failure_stops_remaining_chunks_and_marks_uncertain);
  RUN_TEST(test_recover_job_keeps_offline_latch_until_full_readback);
  RUN_TEST(test_direct_register_access_rejects_reserved);
  RUN_TEST(test_read_last_address);
  RUN_TEST(test_i2c_reset_and_restore_defaults);
  RUN_TEST(test_i2c_reset_ok_from_degraded_does_not_mark_ready_without_device_read);
  RUN_TEST(test_i2c_reset_ok_then_device_read_success_returns_ready);
  RUN_TEST(test_i2c_reset_reports_unsupported_without_callback);
  RUN_TEST(test_general_call_helpers);
  RUN_TEST(test_general_call_disabled_by_default_without_bus_io);
  RUN_TEST(test_write_wiper_pre_mutation_failure_does_not_set_uncertainty);
  RUN_TEST(test_write_wiper_mutate_then_fail_marks_uncertain);
  RUN_TEST(test_write_tcon_mutate_then_fail_marks_uncertain);
  RUN_TEST(test_write_register_mutate_then_fail_marks_target_cache_uncertain);
  RUN_TEST(test_increment_decrement_partial_failure_marks_uncertain);
  RUN_TEST(test_general_call_mutate_then_fail_marks_uncertain);
  RUN_TEST(test_successful_readback_clears_uncertainty_only_after_all_unknown_verified);
  RUN_TEST(test_probe_does_not_update_health_but_recover_does);
  RUN_TEST(test_probe_preserves_transport_status_except_address_nack);
  RUN_TEST(test_probe_preserves_register_mismatch);
  RUN_TEST(test_recover_preserves_transport_status_detail);
  RUN_TEST(test_recover_keeps_uncertainty_until_all_unknown_registers_verified);
  RUN_TEST(test_offline_threshold);
  RUN_TEST(test_offline_blocks_normal_operations_without_bus_io);
  RUN_TEST(test_recover_is_explicit_path_out_of_offline);
  RUN_TEST(test_failed_recover_from_offline_reasserts_latch_after_partial_success);
  RUN_TEST(test_offline_threshold_zero_normalizes_to_one);
  RUN_TEST(test_read_msb_mismatch_is_reported);
  RUN_TEST(test_tracked_read_msb_mismatch_updates_health);
  RUN_TEST(test_require_read_msb_zero_can_be_disabled);
  RUN_TEST(test_zero_step_commands_are_noops);
  RUN_TEST(test_failed_bus_reset_updates_health);
  RUN_TEST(test_conversions);
  RUN_TEST(test_fraction_endpoint_matrix_by_variant);
  RUN_TEST(test_fraction_read_write_helpers);
  RUN_TEST(test_operations_reject_before_begin);
  RUN_TEST(test_example_transport_maps_wire_errors_and_read_only_transactions);

  return UNITY_END();
}
