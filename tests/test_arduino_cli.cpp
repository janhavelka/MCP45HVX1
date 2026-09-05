// Host regression tests compile the real example with stubbed Arduino I/O.
#include <cassert>
#include <cstdio>

#include "examples/01_basic_bringup_cli/main.cpp"

SerialClass Serial;
TwoWire Wire;

namespace {
using MCP45HVX1::Err;
using MCP45HVX1::Status;
using MCP45HVX1::TerminalMode;

struct CliBus {
  uint8_t wiper = 83;
  uint8_t tcon = 0xFF;
  unsigned writes = 0;
  unsigned reads = 0;
  unsigned failWrite = 0;
  unsigned failRead = 0;
  int ignoreRegister = -1;

  static Status write(uint8_t address, const uint8_t* bytes, size_t length,
                      uint32_t timeout, void* context) {
    auto& bus = *static_cast<CliBus*>(context);
    assert(address == 0x3C && length == 2 && timeout > 0);
    ++bus.writes;
    if (bus.writes == bus.failWrite) {
      return Status::Error(Err::I2C_TIMEOUT, "injected write failure", 42);
    }
    const uint8_t reg = bytes[0] >> 4;
    assert(reg == 0 || reg == 4);
    if (reg != bus.ignoreRegister) {
      (reg == 0 ? bus.wiper : bus.tcon) = bytes[1];
    }
    return Status::Ok();
  }

  static Status read(uint8_t address, const uint8_t* tx, size_t txLength,
                     uint8_t* rx, size_t rxLength, uint32_t timeout, void* context) {
    auto& bus = *static_cast<CliBus*>(context);
    assert(address == 0x3C && txLength == 1 && rxLength == 2 && timeout > 0);
    ++bus.reads;
    if (bus.reads == bus.failRead) {
      return Status::Error(Err::I2C_TIMEOUT, "injected read failure", 43);
    }
    const uint8_t reg = tx[0] >> 4;
    assert(reg == 0 || reg == 4);
    rx[0] = 0;
    rx[1] = reg == 0 ? bus.wiper : bus.tcon;
    return Status::Ok();
  }
};

void start(CliBus& bus) {
  gDev.end();
  gConfig = MCP45HVX1::Config{};
  gConfig.i2cWrite = CliBus::write;
  gConfig.i2cWriteRead = CliBus::read;
  gConfig.i2cUser = &bus;
  gConfig.offlineThreshold = 255;
  assert(gDev.begin(gConfig).ok());
  bus.writes = bus.reads = 0;
  gOutputStateUncertain = true;
}

void testRestore() {
  MCP45HVX1::RegisterSnapshot baseline;
  baseline.wiper = 21;
  baseline.tcon = 0xFB;
  {
    CliBus bus;
    start(bus);
    assert(restoreSnapshot(baseline).ok());
    assert(bus.wiper == 21 && bus.tcon == 0xFB);
    assert(bus.writes == 2 && bus.reads == 2);
    assert(!gOutputStateUncertain);
  }
  for (int ignored : {0, 4}) {
    CliBus bus;
    start(bus);
    bus.ignoreRegister = ignored;
    assert(restoreSnapshot(baseline).code == Err::REGISTER_MISMATCH);
    assert(gOutputStateUncertain);
    assert(bus.writes == 2 && bus.reads == 2);
  }
  for (unsigned phase : {1U, 2U}) {
    CliBus bus;
    start(bus);
    bus.failRead = phase;
    const Status st = restoreSnapshot(baseline);
    assert(st.code == Err::I2C_TIMEOUT && st.detail == 43);
    assert(gOutputStateUncertain && bus.reads == phase);
  }
  for (unsigned phase : {1U, 2U}) {
    CliBus bus;
    start(bus);
    bus.failWrite = phase;
    const Status st = restoreSnapshot(baseline);
    assert(st.code == Err::I2C_TIMEOUT && st.detail == 42);
    assert(gOutputStateUncertain);
    assert(bus.writes == 2 && bus.reads == 0);
  }
  gDev.end();
}

void testArguments() {
  struct Alias { const char* text; TerminalMode mode; };
  const Alias aliases[] = {
      {"pot", TerminalMode::Potentiometer}, {"potentiometer", TerminalMode::Potentiometer},
      {"bw", TerminalMode::RheostatBToW}, {"b-w", TerminalMode::RheostatBToW},
      {"rheostat-bw", TerminalMode::RheostatBToW}, {"rheostat_bw", TerminalMode::RheostatBToW},
      {"aw", TerminalMode::RheostatAToW}, {"a-w", TerminalMode::RheostatAToW},
      {"rheostat-aw", TerminalMode::RheostatAToW}, {"rheostat_aw", TerminalMode::RheostatAToW},
      {"float", TerminalMode::WiperFloating}, {"floating", TerminalMode::WiperFloating},
      {"wiper-floating", TerminalMode::WiperFloating}, {"shutdown", TerminalMode::Shutdown},
  };
  for (const auto& alias : aliases) {
    const char* args = alias.text;
    TerminalMode mode{};
    assert(parseMode(args, mode) && noMoreArgs(args) && mode == alias.mode);
  }
  const char* oversized = "shutdownxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  char token[9] = {};
  assert(!readToken(oversized, token, sizeof(token)));

  CliBus bus;
  start(bus);
  const char* rejected[] = {
      "zero junk", "mid junk", "max junk", "defaults junk", "recover junk",
      "probe junk", "read junk", "version junk", "scan junk", "info junk", "errata junk",
      "addr 0x3d junk", "res 7 junk", "rab 5k junk", "frac 0.5 junk",
      "shutdown on junk", "mode bw junk", "inc 1 junk", "dec 1 junk", "stress 1 junk",
      "begin 0x3d invalid", "begin 8 7", "begin 0x000000000000000000000000000000003d",
      "raw xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
      "addr 0x5c", "begin 0x5c", "addr_alt 0x5c",
  };
  for (const char* command : rejected) {
    handleCommand(String(command));
    assert(bus.writes == 0 && bus.reads == 0);
    assert(gConfig.i2cAddress == 0x3C);
    assert(gConfig.resolution == MCP45HVX1::Resolution::Bits8);
    assert(gConfig.resistance == MCP45HVX1::ResistanceOption::R10K);
  }
  assert(gOutputStateUncertain);
  handleCommand(String("mid"));
  assert(bus.wiper == 0x7F && bus.writes == 1 && bus.reads == 2);
  gConfig.resolution = MCP45HVX1::Resolution::Bits7;
  assert(gDev.begin(gConfig).ok());
  handleCommand(String("mid"));
  assert(bus.wiper == 0x3F && bus.writes == 2);
  gDev.end();
}
}  // namespace

int main() {
  testRestore();
  testArguments();
  puts("Arduino CLI restore and argument regression tests passed");
}
