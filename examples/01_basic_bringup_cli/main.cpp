/**
 * @file main.cpp
 * @brief Interactive MCP45HVX1 bring-up CLI.
 */

#include <Arduino.h>
#include <Wire.h>

#include <cstdlib>

#include "MCP45HVX1/MCP45HVX1.h"
#include "examples/common/BoardConfig.h"
#include "examples/common/BusDiag.h"
#include "examples/common/CliShell.h"
#include "examples/common/HealthDiag.h"
#include "examples/common/HealthView.h"
#include "examples/common/Log.h"
#include "examples/common/TransportAdapter.h"

namespace {

MCP45HVX1::MCP45HVX1 gDev;
bool gVerbose = false;

MCP45HVX1::Config makeConfig() {
  MCP45HVX1::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  cfg.i2cAddress = MCP45HVX1::cmd::DEFAULT_ADDRESS;
  cfg.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  cfg.resolution = MCP45HVX1::Resolution::Bits8;
  return cfg;
}

void printStatus(const MCP45HVX1::Status& st) {
  if (st.ok()) {
    LOGI("OK");
  } else {
    LOGE("ERR code=%u detail=%ld msg=%s",
         static_cast<unsigned>(st.code),
         static_cast<long>(st.detail),
         st.msg);
  }
}

bool parseHexArg(const String& line, const char* prefix, uint32_t& out) {
  if (!line.startsWith(prefix)) {
    return false;
  }
  const char* p = line.c_str() + strlen(prefix);
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  if (*p == '\0') {
    return false;
  }
  char* end = nullptr;
  out = static_cast<uint32_t>(strtoul(p, &end, 0));
  return end != p;
}

void printHelp() {
  LOG_SERIAL.println();
  LOG_SERIAL.println("MCP45HVX1 commands:");
  LOG_SERIAL.println("  help                 Show this help");
  LOG_SERIAL.println("  scan                 I2C bus scan");
  LOG_SERIAL.println("  probe                Probe configured device");
  LOG_SERIAL.println("  recover              Recover and refresh register cache");
  LOG_SERIAL.println("  drv                  Compact driver health");
  LOG_SERIAL.println("  cfg | settings       Full driver settings");
  LOG_SERIAL.println("  read                 Read wiper and TCON");
  LOG_SERIAL.println("  rregs                Alias for read");
  LOG_SERIAL.println("  wiper <value>        Write wiper code, e.g. wiper 0x80");
  LOG_SERIAL.println("  inc [n]              Increment wiper n steps");
  LOG_SERIAL.println("  dec [n]              Decrement wiper n steps");
  LOG_SERIAL.println("  tcon <value>         Write TCON, reserved bits forced high");
  LOG_SERIAL.println("  wregs <reg> <value>  Write register 0x00 or 0x04");
  LOG_SERIAL.println("  mode pot|bw|aw|float|shutdown");
  LOG_SERIAL.println("  verbose              Toggle verbose logging");
  LOG_SERIAL.println("  stress               Run simple inc/dec stress pass");
  LOG_SERIAL.println();
}

void readRegisters() {
  MCP45HVX1::RegisterSnapshot s;
  MCP45HVX1::Status st = gDev.readSnapshot(s);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("wiper=0x%02X tcon=0x%02X", s.wiper, s.tcon);
}

void handleWriteRegister(const String& line) {
  const char* p = line.c_str() + strlen("wregs");
  char* end = nullptr;
  const uint32_t reg = strtoul(p, &end, 0);
  if (end == p) {
    LOGE("usage: wregs <reg> <value>");
    return;
  }
  const uint32_t value = strtoul(end, nullptr, 0);
  printStatus(gDev.writeRegister(static_cast<uint8_t>(reg), static_cast<uint8_t>(value)));
}

void handleMode(const String& line) {
  MCP45HVX1::TerminalMode mode = MCP45HVX1::TerminalMode::Potentiometer;
  if (line.endsWith("bw")) {
    mode = MCP45HVX1::TerminalMode::RheostatBToW;
  } else if (line.endsWith("aw")) {
    mode = MCP45HVX1::TerminalMode::RheostatAToW;
  } else if (line.endsWith("float")) {
    mode = MCP45HVX1::TerminalMode::WiperFloating;
  } else if (line.endsWith("shutdown")) {
    mode = MCP45HVX1::TerminalMode::Shutdown;
  } else if (!line.endsWith("pot")) {
    LOGE("usage: mode pot|bw|aw|float|shutdown");
    return;
  }
  printStatus(gDev.setTerminalMode(mode));
}

void runStress() {
  for (uint8_t i = 0; i < 8; ++i) {
    MCP45HVX1::Status st = gDev.incrementWiper();
    if (!st.ok()) {
      printStatus(st);
      return;
    }
  }
  for (uint8_t i = 0; i < 8; ++i) {
    MCP45HVX1::Status st = gDev.decrementWiper();
    if (!st.ok()) {
      printStatus(st);
      return;
    }
  }
  readRegisters();
}

void handleCommand(const String& line) {
  uint32_t value = 0;

  if (line == "help") {
    printHelp();
  } else if (line == "scan") {
    bus_diag::scan();
  } else if (line == "probe") {
    printStatus(gDev.probe());
  } else if (line == "recover") {
    printStatus(gDev.recover());
  } else if (line == "drv") {
    health_view::printSummary(gDev);
  } else if (line == "cfg" || line == "settings") {
    health_diag::printSettings(gDev);
  } else if (line == "read" || line == "rregs") {
    readRegisters();
  } else if (parseHexArg(line, "wiper", value)) {
    printStatus(gDev.writeWiper(static_cast<uint8_t>(value)));
  } else if (parseHexArg(line, "tcon", value)) {
    printStatus(gDev.writeTcon(static_cast<uint8_t>(value)));
  } else if (line.startsWith("inc")) {
    const int steps = line.length() > 3 ? line.substring(3).toInt() : 1;
    printStatus(gDev.incrementWiper(static_cast<uint8_t>(steps <= 0 ? 1 : steps)));
  } else if (line.startsWith("dec")) {
    const int steps = line.length() > 3 ? line.substring(3).toInt() : 1;
    printStatus(gDev.decrementWiper(static_cast<uint8_t>(steps <= 0 ? 1 : steps)));
  } else if (line.startsWith("wregs")) {
    handleWriteRegister(line);
  } else if (line.startsWith("mode")) {
    handleMode(line);
  } else if (line == "verbose") {
    gVerbose = !gVerbose;
    LOGI("verbose=%s", log_bool_str(gVerbose));
  } else if (line == "stress") {
    runStress();
  } else {
    LOGE("unknown command: %s", line.c_str());
  }

  LOGV(gVerbose, "state=%s", health_view::stateName(gDev.state()));
}

}  // namespace

void setup() {
  log_begin();
  LOGI("MCP45HVX1 bring-up CLI");

  if (!board::initI2c()) {
    LOGE("I2C init failed");
    return;
  }

  const MCP45HVX1::Config cfg = makeConfig();
  MCP45HVX1::Status st = gDev.begin(cfg);
  if (!st.ok()) {
    LOGE("begin failed: code=%u detail=%ld msg=%s",
         static_cast<unsigned>(st.code),
         static_cast<long>(st.detail),
         st.msg);
  } else {
    readRegisters();
  }

  printHelp();
}

void loop() {
  gDev.tick(millis());

  String line;
  if (cli_shell::readLine(line)) {
    handleCommand(line);
  }
}

