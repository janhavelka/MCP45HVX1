/**
 * @file main.cpp
 * @brief Interactive MCP45HVX1 bring-up CLI.
 */

#include <Arduino.h>
#include <Wire.h>

#include <cstdlib>
#include <cstring>

#include "MCP45HVX1/MCP45HVX1.h"
#include "examples/common/BoardConfig.h"
#include "examples/common/BusDiag.h"
#include "examples/common/CliShell.h"
#include "examples/common/CliStyle.h"
#include "examples/common/HealthDiag.h"
#include "examples/common/HealthView.h"
#include "examples/common/Log.h"
#include "examples/common/TransportAdapter.h"

namespace {

MCP45HVX1::MCP45HVX1 gDev;
MCP45HVX1::Config gConfig;
transport::BusResetContext gBusReset{board::I2C_SDA, board::I2C_SCL};
bool gVerbose = false;
bool gGeneralCallArmed = false;

struct StressStats {
  bool active = false;
  const char* testName = "";
  const char* mode = "";
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  uint32_t targetIterations = 0;
  uint32_t attempts = 0;
  uint32_t success = 0;
  uint32_t errors = 0;
  uint32_t successBefore = 0;
  uint32_t failBefore = 0;
  uint32_t minAttemptUs = 0xFFFFFFFFUL;
  uint32_t maxAttemptUs = 0;
  uint64_t sumAttemptUs = 0;
  bool hasFailure = false;
  bool aborted = false;
  MCP45HVX1::Status firstError = MCP45HVX1::Status::Ok();
  MCP45HVX1::Status lastError = MCP45HVX1::Status::Ok();
  MCP45HVX1::Status restoreStatus = MCP45HVX1::Status::Ok();
};

StressStats gStress;

const char* skipWs(const char* p) {
  while (p != nullptr && (*p == ' ' || *p == '\t')) {
    ++p;
  }
  return p == nullptr ? "" : p;
}

bool noMoreArgs(const char* p) {
  return *skipWs(p) == '\0';
}

bool requireNoArgs(const char* command, const char* args) {
  if (noMoreArgs(args)) {
    return true;
  }
  LOGE("usage: %s", command);
  return false;
}

bool readToken(const char*& p, char* out, size_t outLen) {
  p = skipWs(p);
  if (*p == '\0' || out == nullptr || outLen == 0) {
    return false;
  }

  size_t i = 0;
  while (*p != '\0' && *p != ' ' && *p != '\t') {
    if ((i + 1U) < outLen) {
      out[i++] = *p;
    }
    ++p;
  }
  out[i] = '\0';
  return i > 0U;
}

bool parseU32(const char*& p, uint32_t maxValue, uint32_t& out) {
  p = skipWs(p);
  if (*p == '\0' || *p == '-') {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = strtoul(p, &end, 0);
  if (end == p || parsed > maxValue) {
    return false;
  }
  p = end;
  out = static_cast<uint32_t>(parsed);
  return true;
}

bool parseFloat01(const char*& p, float& out) {
  p = skipWs(p);
  if (*p == '\0') {
    return false;
  }
  char* end = nullptr;
  const float parsed = strtof(p, &end);
  if (end == p || parsed != parsed || parsed < 0.0f || parsed > 1.0f) {
    return false;
  }
  p = end;
  out = parsed;
  return true;
}

bool parseBool(const char*& p, bool& out) {
  char token[8] = {};
  if (!readToken(p, token, sizeof(token))) {
    return false;
  }
  if (strcmp(token, "1") == 0 || strcmp(token, "on") == 0 ||
      strcmp(token, "yes") == 0 || strcmp(token, "true") == 0) {
    out = true;
    return true;
  }
  if (strcmp(token, "0") == 0 || strcmp(token, "off") == 0 ||
      strcmp(token, "no") == 0 || strcmp(token, "false") == 0) {
    out = false;
    return true;
  }
  return false;
}

bool isPrimaryAddress(uint8_t address) {
  return address >= MCP45HVX1::cmd::MIN_ADDRESS && address <= MCP45HVX1::cmd::MAX_ADDRESS;
}

bool isAlternateAddress(uint8_t address) {
  return address >= MCP45HVX1::cmd::ALT_MIN_ADDRESS &&
         address <= MCP45HVX1::cmd::ALT_MAX_ADDRESS;
}

const char* statusName(MCP45HVX1::Err code) {
  switch (code) {
    case MCP45HVX1::Err::OK:
      return "OK";
    case MCP45HVX1::Err::IN_PROGRESS:
      return "IN_PROGRESS";
    case MCP45HVX1::Err::INVALID_CONFIG:
      return "INVALID_CONFIG";
    case MCP45HVX1::Err::NOT_INITIALIZED:
      return "NOT_INITIALIZED";
    case MCP45HVX1::Err::INVALID_PARAM:
      return "INVALID_PARAM";
    case MCP45HVX1::Err::I2C_ERROR:
      return "I2C_ERROR";
    case MCP45HVX1::Err::TIMEOUT:
      return "TIMEOUT";
    case MCP45HVX1::Err::I2C_TIMEOUT:
      return "I2C_TIMEOUT";
    case MCP45HVX1::Err::I2C_NACK_ADDR:
      return "I2C_NACK_ADDR";
    case MCP45HVX1::Err::I2C_NACK_DATA:
      return "I2C_NACK_DATA";
    case MCP45HVX1::Err::I2C_BUS:
      return "I2C_BUS";
    case MCP45HVX1::Err::DEVICE_NOT_FOUND:
      return "DEVICE_NOT_FOUND";
    case MCP45HVX1::Err::REGISTER_MISMATCH:
      return "REGISTER_MISMATCH";
    case MCP45HVX1::Err::BUSY:
      return "BUSY";
    case MCP45HVX1::Err::UNSUPPORTED:
      return "UNSUPPORTED";
    default:
      return "UNKNOWN";
  }
}

const char* statusCause(MCP45HVX1::Err code) {
  switch (code) {
    case MCP45HVX1::Err::OK:
      return "operation completed";
    case MCP45HVX1::Err::NOT_INITIALIZED:
      return "driver has not been initialized";
    case MCP45HVX1::Err::INVALID_CONFIG:
      return "configuration is invalid";
    case MCP45HVX1::Err::INVALID_PARAM:
      return "argument is invalid or out of range";
    case MCP45HVX1::Err::I2C_NACK_ADDR:
    case MCP45HVX1::Err::DEVICE_NOT_FOUND:
      return "bus/device not found or address was not acknowledged";
    case MCP45HVX1::Err::I2C_NACK_DATA:
      return "I2C data byte was not acknowledged";
    case MCP45HVX1::Err::I2C_TIMEOUT:
    case MCP45HVX1::Err::TIMEOUT:
      return "operation timed out";
    case MCP45HVX1::Err::I2C_BUS:
      return "I2C bus error";
    case MCP45HVX1::Err::I2C_ERROR:
      return "I2C read/write failed";
    case MCP45HVX1::Err::REGISTER_MISMATCH:
      return "register/data validation failed";
    case MCP45HVX1::Err::BUSY:
      return "device or driver is busy";
    case MCP45HVX1::Err::IN_PROGRESS:
      return "operation is still in progress";
    case MCP45HVX1::Err::UNSUPPORTED:
      return "command is unsupported by this device/register";
    default:
      return "internal or unknown error";
  }
}

const char* statusColor(const MCP45HVX1::Status& st) {
  if (st.ok()) {
    return LOG_COLOR_GREEN;
  }
  return (st.code == MCP45HVX1::Err::BUSY || st.code == MCP45HVX1::Err::IN_PROGRESS)
             ? LOG_COLOR_YELLOW
             : LOG_COLOR_RED;
}

const char* modeName(MCP45HVX1::TerminalMode mode) {
  switch (mode) {
    case MCP45HVX1::TerminalMode::Potentiometer:
      return "pot";
    case MCP45HVX1::TerminalMode::RheostatBToW:
      return "bw";
    case MCP45HVX1::TerminalMode::RheostatAToW:
      return "aw";
    case MCP45HVX1::TerminalMode::WiperFloating:
      return "float";
    case MCP45HVX1::TerminalMode::Shutdown:
      return "shutdown";
    case MCP45HVX1::TerminalMode::Custom:
      return "custom";
    default:
      return "?";
  }
}

const char* terminalName(MCP45HVX1::Terminal terminal) {
  switch (terminal) {
    case MCP45HVX1::Terminal::A:
      return "A";
    case MCP45HVX1::Terminal::W:
      return "W";
    case MCP45HVX1::Terminal::B:
      return "B";
    default:
      return "?";
  }
}

const char* resistanceName(MCP45HVX1::ResistanceOption option) {
  switch (option) {
    case MCP45HVX1::ResistanceOption::R5K:
      return "5k";
    case MCP45HVX1::ResistanceOption::R10K:
      return "10k";
    case MCP45HVX1::ResistanceOption::R50K:
      return "50k";
    case MCP45HVX1::ResistanceOption::R100K:
      return "100k";
    default:
      return "?";
  }
}

bool parseResolution(const char*& p, MCP45HVX1::Resolution& out) {
  char token[8] = {};
  if (!readToken(p, token, sizeof(token))) {
    return false;
  }
  if (strcmp(token, "7") == 0 || strcmp(token, "7bit") == 0 ||
      strcmp(token, "bits7") == 0 || strcmp(token, "mcp45hv31") == 0) {
    out = MCP45HVX1::Resolution::Bits7;
    return true;
  }
  if (strcmp(token, "8") == 0 || strcmp(token, "8bit") == 0 ||
      strcmp(token, "bits8") == 0 || strcmp(token, "mcp45hv51") == 0) {
    out = MCP45HVX1::Resolution::Bits8;
    return true;
  }
  return false;
}

bool parseResistance(const char*& p, MCP45HVX1::ResistanceOption& out) {
  char token[8] = {};
  if (!readToken(p, token, sizeof(token))) {
    return false;
  }
  if (strcmp(token, "5k") == 0 || strcmp(token, "502") == 0) {
    out = MCP45HVX1::ResistanceOption::R5K;
    return true;
  }
  if (strcmp(token, "10k") == 0 || strcmp(token, "103") == 0) {
    out = MCP45HVX1::ResistanceOption::R10K;
    return true;
  }
  if (strcmp(token, "50k") == 0 || strcmp(token, "503") == 0) {
    out = MCP45HVX1::ResistanceOption::R50K;
    return true;
  }
  if (strcmp(token, "100k") == 0 || strcmp(token, "104") == 0) {
    out = MCP45HVX1::ResistanceOption::R100K;
    return true;
  }
  return false;
}

bool parseTerminal(const char*& p, MCP45HVX1::Terminal& out) {
  char token[4] = {};
  if (!readToken(p, token, sizeof(token))) {
    return false;
  }
  if (strcmp(token, "a") == 0) {
    out = MCP45HVX1::Terminal::A;
    return true;
  }
  if (strcmp(token, "w") == 0) {
    out = MCP45HVX1::Terminal::W;
    return true;
  }
  if (strcmp(token, "b") == 0) {
    out = MCP45HVX1::Terminal::B;
    return true;
  }
  return false;
}

bool parseMode(const char*& p, MCP45HVX1::TerminalMode& out) {
  char token[16] = {};
  if (!readToken(p, token, sizeof(token))) {
    return false;
  }
  if (strcmp(token, "pot") == 0 || strcmp(token, "potentiometer") == 0) {
    out = MCP45HVX1::TerminalMode::Potentiometer;
    return true;
  }
  if (strcmp(token, "bw") == 0 || strcmp(token, "b-w") == 0 ||
      strcmp(token, "rheostat-bw") == 0) {
    out = MCP45HVX1::TerminalMode::RheostatBToW;
    return true;
  }
  if (strcmp(token, "aw") == 0 || strcmp(token, "a-w") == 0 ||
      strcmp(token, "rheostat-aw") == 0) {
    out = MCP45HVX1::TerminalMode::RheostatAToW;
    return true;
  }
  if (strcmp(token, "float") == 0 || strcmp(token, "wiper-floating") == 0) {
    out = MCP45HVX1::TerminalMode::WiperFloating;
    return true;
  }
  if (strcmp(token, "shutdown") == 0) {
    out = MCP45HVX1::TerminalMode::Shutdown;
    return true;
  }
  return false;
}

MCP45HVX1::Config makeDefaultConfig() {
  MCP45HVX1::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  gBusReset.frequencyHz = board::I2C_FREQ_HZ;
  gBusReset.timeoutMs = board::I2C_TIMEOUT_MS;
  cfg.busReset = transport::wireBusReset;
  cfg.controlUser = &gBusReset;
  cfg.i2cAddress = MCP45HVX1::cmd::DEFAULT_ADDRESS;
  cfg.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  cfg.resolution = MCP45HVX1::Resolution::Bits8;
  cfg.resistance = MCP45HVX1::ResistanceOption::R10K;
  return cfg;
}

void printStatus(const MCP45HVX1::Status& st) {
  LOG_SERIAL.printf("  Status: %s%s%s (code=%u, detail=%ld)\n",
                    statusColor(st),
                    statusName(st.code),
                    LOG_COLOR_RESET,
                    static_cast<unsigned>(st.code),
                    static_cast<long>(st.detail));
  if (!st.ok()) {
    LOG_SERIAL.printf("  Cause: %s\n", statusCause(st.code));
    if (st.msg != nullptr && st.msg[0] != '\0') {
      LOG_SERIAL.printf("  Message: %s\n", st.msg);
    }
  }
}

MCP45HVX1::Status beginDevice() {
  gConfig.allowAlternateAddressRange = isAlternateAddress(gConfig.i2cAddress);
  if (gConfig.allowAlternateAddressRange) {
    LOGW("Using disputed alternate address range 0x5C-0x5F; docs favor 0x3C-0x3F");
  }

  gDev.end();
  MCP45HVX1::Status st = gDev.begin(gConfig);
  printStatus(st);
  return st;
}

void printVersion() {
  LOG_SERIAL.printf("MCP45HVX1 %s\n", MCP45HVX1::VERSION_FULL);
}

void printDeviceInfo() {
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  LOG_SERIAL.println("=== Device Info ===");
  LOG_SERIAL.printf("  Address: 0x%02X (%s range)\n",
                    info.i2cAddress,
                    info.usingAlternateAddressRange ? "alternate" : "primary");
  LOG_SERIAL.printf("  Resolution: %u-bit (max code 0x%02X, default 0x%02X)\n",
                    static_cast<unsigned>(info.resolution),
                    info.maxWiperCode,
                    info.defaultWiperCode);
  LOG_SERIAL.printf("  Resistance: %s (%u ohm nominal)\n",
                    resistanceName(info.resistance),
                    static_cast<unsigned>(info.nominalResistanceOhms));
  LOG_SERIAL.printf("  Wiper step: %.3f ohm\n", static_cast<double>(info.nominalStepOhms));
  LOG_SERIAL.printf("  Max terminal current: %.1f mA\n",
                    static_cast<double>(info.maxTerminalCurrentMilliAmps));
}

void printErrataInfo() {
  const MCP45HVX1::SiliconErrataInfo info =
      MCP45HVX1::MCP45HVX1::siliconErrataInfo();

  LOG_SERIAL.println("=== Errata ===");
  LOG_SERIAL.printf("  Document: %s rev %s\n", info.documentNumber, info.documentRevision);
  LOG_SERIAL.printf("  Title: %s\n", info.documentTitle);
  LOG_SERIAL.printf("  Marking: %s\n", info.markingSummary);
  LOG_SERIAL.printf("  Shared-bus I2C hazard: %s%s%s\n",
                    cli::warningIfNonZeroColor(info.sharedBusI2cHazard ? 1U : 0U),
                    log_bool_str(info.sharedBusI2cHazard),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  General Call address decode hazard: %s%s%s\n",
                    cli::warningIfNonZeroColor(info.generalCallAddressDecodeHazard ? 1U : 0U),
                    log_bool_str(info.generalCallAddressDecodeHazard),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Hardware General Call bit ignored: %s%s%s\n",
                    cli::warningIfNonZeroColor(info.hardwareGeneralCallBitIgnored ? 1U : 0U),
                    log_bool_str(info.hardwareGeneralCallBitIgnored),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Unique-bus workaround: %s%s%s\n",
                    cli::yesNoColor(info.uniqueBusWorkaroundForAffectedSilicon),
                    log_bool_str(info.uniqueBusWorkaroundForAffectedSilicon),
                    LOG_COLOR_RESET);
  LOG_SERIAL.println("  Source: docs/MCP45HVX1_Errata_DS80000649B.pdf");
}

void printTerminalStatus(uint8_t tcon) {
  const MCP45HVX1::TerminalStatus s = MCP45HVX1::MCP45HVX1::decodeTcon(tcon);
  LOG_SERIAL.printf("  TCON: 0x%02X\n", tcon);
  LOG_SERIAL.printf("  Mode: %s\n", modeName(s.mode));
  LOG_SERIAL.printf("  Shutdown: %s%s%s\n",
                    cli::yesNoColor(!s.softwareShutdown),
                    log_bool_str(s.softwareShutdown),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Terminals: A=%s%s%s W=%s%s%s B=%s%s%s\n",
                    cli::yesNoColor(s.terminalA),
                    log_bool_str(s.terminalA),
                    LOG_COLOR_RESET,
                    cli::yesNoColor(s.terminalW),
                    log_bool_str(s.terminalW),
                    LOG_COLOR_RESET,
                    cli::yesNoColor(s.terminalB),
                    log_bool_str(s.terminalB),
                    LOG_COLOR_RESET);
}

void readRegisters() {
  MCP45HVX1::RegisterSnapshot s;
  MCP45HVX1::Status st = gDev.readSnapshot(s);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  const float position = MCP45HVX1::MCP45HVX1::fractionFromCode(s.wiper, gConfig.resolution);
  const float rbw = MCP45HVX1::MCP45HVX1::resistanceBToWOhms(s.wiper, gConfig.resistance,
                                                            gConfig.resolution);
  const float rawOhms = MCP45HVX1::MCP45HVX1::resistanceAToWOhms(s.wiper, gConfig.resistance,
                                                                 gConfig.resolution);
  LOG_SERIAL.println("=== Register Snapshot ===");
  LOG_SERIAL.printf("  Wiper: 0x%02X (%u)\n", s.wiper, static_cast<unsigned>(s.wiper));
  LOG_SERIAL.printf("  Position: %.4f\n", static_cast<double>(position));
  LOG_SERIAL.printf("  R_BW: %.1f ohm\n", static_cast<double>(rbw));
  LOG_SERIAL.printf("  R_AW: %.1f ohm\n", static_cast<double>(rawOhms));
  printTerminalStatus(s.tcon);
}

void dumpRegisters() {
  readRegisters();
  uint8_t last = 0;
  MCP45HVX1::Status st = gDev.readLastAddress(last);
  if (st.ok()) {
    LOG_SERIAL.printf("  Last address: 0x%02X\n", last);
  } else {
    printStatus(st);
  }
}

void printHelp() {
  cli::printHelpHeader("MCP45HVX1 CLI Help");
  cli::printHelpSection("Common");
  cli::printHelpItem("help / ?", "Show this help");
  cli::printHelpItem("version / ver", "Print firmware and library version info");
  cli::printHelpItem("scan", "Scan I2C bus");
  cli::printHelpItem("begin [addr] [7|8]", "Apply config and begin driver");
  cli::printHelpItem("addr <0x3c..0x3f|0x5c..0x5f>", "Set I2C address and re-begin");
  cli::printHelpItem("res <7|8>", "Set wiper resolution and re-begin");
  cli::printHelpItem("rab <5k|10k|50k|100k>", "Set nominal resistance helper");
  cli::printHelpItem("read / rregs", "Read decoded wiper and TCON snapshot");
  cli::printHelpItem("dump / raw", "Read decoded snapshot plus raw address pointer");

  cli::printHelpSection("Wiper And Terminal");
  cli::printHelpItem("wiper [0..max]", "Read or write volatile wiper code");
  cli::printHelpItem("frac [0.0..1.0]", "Read or write normalized wiper position");
  cli::printHelpItem("pos [0.0..1.0]", "Alias for frac");
  cli::printHelpItem("zero / mid / max", "Move wiper to common positions");
  cli::printHelpItem("inc [n] / dec [n]", "Step wiper with clamp at endpoints");
  cli::printHelpItem("tcon [value]", "Read or write raw TCON byte");
  cli::printHelpItem("term a|w|b [0|1]", "Read or set one terminal bit");
  cli::printHelpItem("shutdown [0|1]", "Read or set software shutdown");
  cli::printHelpItem("mode [pot|bw|aw|float|shutdown]", "Read or apply terminal preset");

  cli::printHelpSection("Registers");
  cli::printHelpItem("last", "Read current device address pointer");
  cli::printHelpItem("rreg <0x00|0x04>", "Read one register");
  cli::printHelpItem("wreg <reg> <value>", "Write one register");
  cli::printHelpItem("wregs <reg> <value>", "Compatibility alias for wreg");

  cli::printHelpSection("Diagnostics");
  cli::printHelpItem("detail / cfg / settings", "Print detailed config, cache, and health");
  cli::printHelpItem("drv", "Show compact one-line health summary");
  cli::printHelpItem("probe", "Probe configured device (no health tracking)");
  cli::printHelpItem("recover", "Refresh cache and update health");
  cli::printHelpItem("iface_reset", "Run board I2C reset callback");
  cli::printHelpItem("defaults", "Restore volatile POR/BOR defaults");
  cli::printHelpItem("info", "Print variant/resistance helper info");
  cli::printHelpItem("errata", "Print DS80000649B I2C errata cautions");
  cli::printHelpItem("selftest", "Run safe command self-test report");
  cli::printHelpItem("stress [n]", "Run N inc/dec restore cycles (default 8)");
  cli::printHelpItem("stress_mix [n]", "Run N mixed write/mode cycles (default 8)");
  cli::printHelpItem("verbose [0|1]", "Get/set verbose command logging");

  cli::printHelpSection("General Call");
  cli::printHelpItem("gc arm | gc disarm", "Enable one broadcast command attempt");
  cli::printHelpItem("gc wiper <code>", "Broadcast wiper write");
  cli::printHelpItem("gc tcon <value>", "Broadcast TCON write");
  cli::printHelpItem("gc inc | gc dec", "Broadcast one wiper step");
  LOG_SERIAL.println();
}

void handleBegin(const char* args) {
  MCP45HVX1::Config next = gConfig;
  const char* p = args;
  char token[16] = {};
  if (!readToken(p, token, sizeof(token))) {
    beginDevice();
    return;
  }

  const char* tokenPtr = token;
  MCP45HVX1::Resolution resolution = next.resolution;
  if (parseResolution(tokenPtr, resolution) && noMoreArgs(tokenPtr)) {
    next.resolution = resolution;
  } else {
    tokenPtr = token;
    uint32_t address = 0;
    if (!parseU32(tokenPtr, 0x7F, address) || !noMoreArgs(tokenPtr) ||
        (!isPrimaryAddress(static_cast<uint8_t>(address)) &&
         !isAlternateAddress(static_cast<uint8_t>(address)))) {
      LOGE("usage: begin [addr] [7|8]");
      return;
    }
    next.i2cAddress = static_cast<uint8_t>(address);
  }

  if (!noMoreArgs(p)) {
    if (!parseResolution(p, resolution) || !noMoreArgs(p)) {
      LOGE("usage: begin [addr] [7|8]");
      return;
    }
    next.resolution = resolution;
  }
  gConfig = next;
  beginDevice();
}

void handleAddress(const char* args) {
  uint32_t address = 0;
  const char* p = args;
  if (!parseU32(p, 0x7F, address) || !noMoreArgs(p)) {
    LOGE("usage: addr <0x3c..0x3f|0x5c..0x5f>");
    return;
  }
  if (!isPrimaryAddress(static_cast<uint8_t>(address)) &&
      !isAlternateAddress(static_cast<uint8_t>(address))) {
    LOGE("address must be documented 0x3C-0x3F or opt-in disputed 0x5C-0x5F");
    return;
  }
  gConfig.i2cAddress = static_cast<uint8_t>(address);
  beginDevice();
}

void handleResolution(const char* args) {
  MCP45HVX1::Resolution resolution = gConfig.resolution;
  const char* p = args;
  if (!parseResolution(p, resolution) || !noMoreArgs(p)) {
    LOGE("usage: res <7|8>");
    return;
  }
  gConfig.resolution = resolution;
  beginDevice();
}

void handleResistance(const char* args) {
  MCP45HVX1::ResistanceOption option = gConfig.resistance;
  const char* p = args;
  if (!parseResistance(p, option) || !noMoreArgs(p)) {
    LOGE("usage: rab <5k|10k|50k|100k>");
    return;
  }
  gConfig.resistance = option;
  LOGI("Resistance helper set to %s", resistanceName(option));
  printDeviceInfo();
}

void handleWiper(const char* args) {
  uint32_t value = 0;
  const char* p = args;
  if (noMoreArgs(p)) {
    uint8_t code = 0;
    MCP45HVX1::Status st = gDev.readWiper(code);
    if (st.ok()) {
      LOG_SERIAL.printf("  Wiper: 0x%02X (%u)\n", code, static_cast<unsigned>(code));
    } else {
      printStatus(st);
    }
    return;
  }

  if (!parseU32(p, MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution), value) ||
      !noMoreArgs(p)) {
    LOGE("usage: wiper <0..%u>",
         static_cast<unsigned>(MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution)));
    return;
  }
  printStatus(gDev.writeWiper(static_cast<uint8_t>(value)));
}

void handleFraction(const char* args) {
  float fraction = 0.0f;
  const char* p = args;
  if (noMoreArgs(p)) {
    MCP45HVX1::Status st = gDev.readWiperFraction(fraction);
    if (st.ok()) {
      LOG_SERIAL.printf("  Fraction: %.4f\n", static_cast<double>(fraction));
    } else {
      printStatus(st);
    }
    return;
  }

  if (!parseFloat01(p, fraction) || !noMoreArgs(p)) {
    LOGE("usage: frac <0.0..1.0>");
    return;
  }
  printStatus(gDev.writeWiperFraction(fraction));
}

void handleStep(const char* args, bool increment) {
  const char* p = args;
  uint32_t steps = 1;
  if (!noMoreArgs(p) && (!parseU32(p, 255, steps) || !noMoreArgs(p))) {
    LOGE("usage: %s [1..255]", increment ? "inc" : "dec");
    return;
  }
  if (steps == 0) {
    LOGE("steps must be > 0");
    return;
  }
  printStatus(increment ? gDev.incrementWiper(static_cast<uint8_t>(steps))
                        : gDev.decrementWiper(static_cast<uint8_t>(steps)));
}

void handleTcon(const char* args) {
  const char* p = args;
  if (noMoreArgs(p)) {
    uint8_t tcon = 0;
    MCP45HVX1::Status st = gDev.readTcon(tcon);
    if (st.ok()) {
      printTerminalStatus(tcon);
    } else {
      printStatus(st);
    }
    return;
  }

  uint32_t value = 0;
  if (!parseU32(p, 0xFF, value) || !noMoreArgs(p)) {
    LOGE("usage: tcon [0x00..0xff]");
    return;
  }
  printStatus(gDev.writeTcon(static_cast<uint8_t>(value)));
}

void handleTerm(const char* args) {
  MCP45HVX1::Terminal terminal = MCP45HVX1::Terminal::A;
  const char* p = args;
  if (!parseTerminal(p, terminal)) {
    LOGE("usage: term a|w|b [0|1]");
    return;
  }
  if (noMoreArgs(p)) {
    bool enabled = false;
    MCP45HVX1::Status st = gDev.getTerminalEnabled(terminal, enabled);
    if (st.ok()) {
      LOG_SERIAL.printf("  Terminal %s: %s%s%s\n",
                        terminalName(terminal),
                        cli::yesNoColor(enabled),
                        enabled ? "enabled" : "disabled",
                        LOG_COLOR_RESET);
    } else {
      printStatus(st);
    }
    return;
  }

  bool enabled = false;
  if (!parseBool(p, enabled) || !noMoreArgs(p)) {
    LOGE("usage: term a|w|b [0|1]");
    return;
  }
  printStatus(gDev.setTerminalEnabled(terminal, enabled));
}

void handleShutdown(const char* args) {
  const char* p = args;
  if (noMoreArgs(p)) {
    bool enabled = false;
    MCP45HVX1::Status st = gDev.getSoftwareShutdown(enabled);
    if (st.ok()) {
      LOG_SERIAL.printf("  Shutdown: %s%s%s\n",
                        cli::yesNoColor(!enabled),
                        enabled ? "enabled" : "disabled",
                        LOG_COLOR_RESET);
    } else {
      printStatus(st);
    }
    return;
  }

  bool enabled = false;
  if (!parseBool(p, enabled) || !noMoreArgs(p)) {
    LOGE("usage: shutdown [0|1]");
    return;
  }
  printStatus(gDev.setSoftwareShutdown(enabled));
}

void handleMode(const char* args) {
  MCP45HVX1::TerminalMode mode = MCP45HVX1::TerminalMode::Potentiometer;
  const char* p = args;
  if (noMoreArgs(p)) {
    MCP45HVX1::Status st = gDev.getTerminalMode(mode);
    if (st.ok()) {
      LOG_SERIAL.printf("  Mode: %s\n", modeName(mode));
    } else {
      printStatus(st);
    }
    return;
  }

  if (!parseMode(p, mode) || !noMoreArgs(p)) {
    LOGE("usage: mode pot|bw|aw|float|shutdown");
    return;
  }
  printStatus(gDev.setTerminalMode(mode));
}

void handleReadRegister(const char* args) {
  uint32_t reg = 0;
  const char* p = args;
  if (!parseU32(p, 0xFF, reg) || !noMoreArgs(p)) {
    LOGE("usage: rreg <0x00|0x04>");
    return;
  }
  uint8_t value = 0;
  MCP45HVX1::Status st = gDev.readRegister(static_cast<uint8_t>(reg), value);
  if (st.ok()) {
    LOG_SERIAL.printf("  Register 0x%02X: 0x%02X\n", static_cast<unsigned>(reg), value);
  } else {
    printStatus(st);
  }
}

void handleWriteRegister(const char* args) {
  uint32_t reg = 0;
  uint32_t value = 0;
  const char* p = args;
  if (!parseU32(p, 0xFF, reg) || !parseU32(p, 0xFF, value) || !noMoreArgs(p)) {
    LOGE("usage: wreg <0x00|0x04> <0x00..0xff>");
    return;
  }
  printStatus(gDev.writeRegister(static_cast<uint8_t>(reg), static_cast<uint8_t>(value)));
}

void handleLast() {
  uint8_t value = 0;
  MCP45HVX1::Status st = gDev.readLastAddress(value);
  if (st.ok()) {
    LOG_SERIAL.printf("  Last address: 0x%02X\n", value);
  } else {
    printStatus(st);
  }
}

void handleVerbose(const char* args) {
  const char* p = args;
  if (noMoreArgs(p)) {
    LOG_SERIAL.printf("  Verbose: %s%s%s\n",
                      cli::yesNoColor(gVerbose),
                      gVerbose ? "enabled" : "disabled",
                      LOG_COLOR_RESET);
    return;
  }
  bool value = false;
  if (!parseBool(p, value) || !noMoreArgs(p)) {
    LOGE("usage: verbose [0|1]");
    return;
  }
  gVerbose = value;
  LOGI("Verbose output %s", gVerbose ? "enabled" : "disabled");
}

void handleGeneralCall(const char* args) {
  const char* p = args;
  char sub[12] = {};
  if (!readToken(p, sub, sizeof(sub))) {
    LOGE("usage: gc arm | gc disarm | gc wiper <code> | gc tcon <value> | gc inc | gc dec");
    gGeneralCallArmed = false;
    return;
  }

  if (strcmp(sub, "arm") == 0) {
    if (noMoreArgs(p)) {
      gGeneralCallArmed = true;
      LOGW("General Call armed for one broadcast command; it affects every enabled device");
      LOGW("DS80000649B: affected silicon has General Call decode anomalies");
    } else {
      LOGE("usage: gc arm");
      gGeneralCallArmed = false;
    }
    return;
  }
  if (strcmp(sub, "disarm") == 0) {
    gGeneralCallArmed = false;
    if (noMoreArgs(p)) {
      LOGI("General Call disarmed");
    } else {
      LOGE("usage: gc disarm");
    }
    return;
  }
  if (!gGeneralCallArmed) {
    LOGE("General Call is broadcast; run 'gc arm' first");
    return;
  }

  MCP45HVX1::Status st = MCP45HVX1::Status::Ok();
  if (strcmp(sub, "wiper") == 0) {
    uint32_t value = 0;
    if (!parseU32(p, MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution), value) ||
        !noMoreArgs(p)) {
      LOGE("usage: gc wiper <0..max>");
      gGeneralCallArmed = false;
      return;
    }
    st = gDev.generalCallWriteWiper(static_cast<uint8_t>(value));
  } else if (strcmp(sub, "tcon") == 0) {
    uint32_t value = 0;
    if (!parseU32(p, 0xFF, value) || !noMoreArgs(p)) {
      LOGE("usage: gc tcon <0x00..0xff>");
      gGeneralCallArmed = false;
      return;
    }
    st = gDev.generalCallWriteTcon(static_cast<uint8_t>(value));
  } else if (strcmp(sub, "inc") == 0 && noMoreArgs(p)) {
    st = gDev.generalCallIncrementWiper();
  } else if (strcmp(sub, "dec") == 0 && noMoreArgs(p)) {
    st = gDev.generalCallDecrementWiper();
  } else {
    LOGE("usage: gc arm | gc disarm | gc wiper <code> | gc tcon <value> | gc inc | gc dec");
    gGeneralCallArmed = false;
    return;
  }

  gGeneralCallArmed = false;
  printStatus(st);
  LOGI("General Call disarmed");
}

void runSelfTest() {
  struct Result {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t skip = 0;
  } result;

  enum class Outcome : uint8_t { PASS, FAIL, SKIP };
  auto report = [&](const char* name, Outcome outcome, const char* note) {
    const bool ok = (outcome == Outcome::PASS);
    const bool skip = (outcome == Outcome::SKIP);
    const char* color = skip ? LOG_COLOR_YELLOW : LOG_COLOR_RESULT(ok);
    const char* tag = skip ? "SKIP" : (ok ? "PASS" : "FAIL");
    LOG_SERIAL.printf("  [%s%s%s] %s", color, tag, LOG_COLOR_RESET, name);
    if (note != nullptr && note[0] != '\0') {
      LOG_SERIAL.printf(" - %s", note);
    }
    LOG_SERIAL.println();
    if (skip) {
      result.skip++;
    } else if (ok) {
      result.pass++;
    } else {
      result.fail++;
    }
  };
  auto reportCheck = [&](const char* name, bool ok, const char* note) {
    report(name, ok ? Outcome::PASS : Outcome::FAIL, note);
  };
  auto reportSkip = [&](const char* name, const char* note) {
    report(name, Outcome::SKIP, note);
  };

  LOG_SERIAL.println("=== MCP45HVX1 selftest (safe commands) ===");

  const uint32_t succBefore = gDev.totalSuccess();
  const uint32_t failBefore = gDev.totalFailures();
  const uint8_t consBefore = gDev.consecutiveFailures();

  MCP45HVX1::Status st = gDev.probe();
  if (st.code == MCP45HVX1::Err::NOT_INITIALIZED) {
    reportSkip("probe responds", "driver not initialized");
    reportSkip("remaining checks", "selftest aborted");
    LOG_SERIAL.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                      cli::nonZeroGoodColor(result.pass),
                      static_cast<unsigned long>(result.pass),
                      LOG_COLOR_RESET,
                      cli::zeroGoodColor(result.fail),
                      static_cast<unsigned long>(result.fail),
                      LOG_COLOR_RESET,
                      cli::warningIfNonZeroColor(result.skip),
                      static_cast<unsigned long>(result.skip),
                      LOG_COLOR_RESET);
    return;
  }
  reportCheck("probe responds", st.ok(), st.ok() ? "" : statusName(st.code));
  const bool probeNoTrack = gDev.totalSuccess() == succBefore &&
                            gDev.totalFailures() == failBefore &&
                            gDev.consecutiveFailures() == consBefore;
  reportCheck("probe no-health-side-effects", probeNoTrack, "");

  MCP45HVX1::RegisterSnapshot snapshot;
  st = gDev.readSnapshot(snapshot);
  reportCheck("readSnapshot", st.ok(), st.ok() ? "" : statusName(st.code));

  const uint8_t maxCode = MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution);
  reportCheck("wiper within configured resolution", st.ok() && snapshot.wiper <= maxCode, "");
  const MCP45HVX1::TerminalStatus terminalStatus =
      MCP45HVX1::MCP45HVX1::decodeTcon(snapshot.tcon);
  reportCheck("decodeTcon mode known", st.ok() && strcmp(modeName(terminalStatus.mode), "?") != 0, "");

  uint8_t last = 0;
  st = gDev.readLastAddress(last);
  reportCheck("readLastAddress", st.ok(), st.ok() ? "" : statusName(st.code));

  MCP45HVX1::TerminalMode mode = MCP45HVX1::TerminalMode::Potentiometer;
  st = gDev.getTerminalMode(mode);
  reportCheck("getTerminalMode", st.ok(), st.ok() ? "" : statusName(st.code));

  bool shutdown = false;
  st = gDev.getSoftwareShutdown(shutdown);
  reportCheck("getSoftwareShutdown", st.ok(), st.ok() ? "" : statusName(st.code));

  st = gDev.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : statusName(st.code));
  reportCheck("isOnline", gDev.isOnline(), "");

  LOG_SERIAL.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                    cli::nonZeroGoodColor(result.pass),
                    static_cast<unsigned long>(result.pass),
                    LOG_COLOR_RESET,
                    cli::zeroGoodColor(result.fail),
                    static_cast<unsigned long>(result.fail),
                    LOG_COLOR_RESET,
                    cli::warningIfNonZeroColor(result.skip),
                    static_cast<unsigned long>(result.skip),
                    LOG_COLOR_RESET);
}

MCP45HVX1::Status restoreSnapshot(const MCP45HVX1::RegisterSnapshot& snapshot) {
  MCP45HVX1::Status first = MCP45HVX1::Status::Ok();
  MCP45HVX1::Status st = gDev.writeWiper(snapshot.wiper);
  if (!st.ok()) {
    first = st;
    LOGW("restore wiper failed");
    printStatus(st);
  }
  st = gDev.writeTcon(snapshot.tcon);
  if (!st.ok()) {
    if (first.ok()) {
      first = st;
    }
    LOGW("restore TCON failed");
    printStatus(st);
  }
  return first;
}

uint32_t stressProgressStep(uint32_t total) {
  if (total <= 10U) {
    return 1U;
  }
  const uint32_t step = total / 10U;
  return step == 0U ? 1U : step;
}

void printStressProgress(uint32_t completed, uint32_t total, uint32_t okCount, uint32_t failCount) {
  if (total == 0U) {
    return;
  }
  const uint32_t step = stressProgressStep(total);
  if (completed < total && (completed % step) != 0U) {
    return;
  }

  const float pct = 100.0f * static_cast<float>(completed) / static_cast<float>(total);
  LOG_SERIAL.printf("  Progress: %lu/%lu (%s%.0f%%%s, ok=%s%lu%s, fail=%s%lu%s)\n",
                    static_cast<unsigned long>(completed),
                    static_cast<unsigned long>(total),
                    cli::successRateColor(pct),
                    static_cast<double>(pct),
                    LOG_COLOR_RESET,
                    cli::nonZeroGoodColor(okCount),
                    static_cast<unsigned long>(okCount),
                    LOG_COLOR_RESET,
                    cli::zeroGoodColor(failCount),
                    static_cast<unsigned long>(failCount),
                    LOG_COLOR_RESET);
}

void resetStressStats(const char* testName, const char* mode, uint32_t iterations) {
  gStress.active = true;
  gStress.testName = testName;
  gStress.mode = mode;
  gStress.startMs = millis();
  gStress.endMs = 0;
  gStress.targetIterations = iterations;
  gStress.attempts = 0;
  gStress.success = 0;
  gStress.errors = 0;
  gStress.successBefore = gDev.totalSuccess();
  gStress.failBefore = gDev.totalFailures();
  gStress.minAttemptUs = 0xFFFFFFFFUL;
  gStress.maxAttemptUs = 0;
  gStress.sumAttemptUs = 0;
  gStress.hasFailure = false;
  gStress.aborted = false;
  gStress.firstError = MCP45HVX1::Status::Ok();
  gStress.lastError = MCP45HVX1::Status::Ok();
  gStress.restoreStatus = MCP45HVX1::Status::Ok();
}

void recordStressResult(const MCP45HVX1::Status& st,
                        uint32_t durationUs,
                        const char* op,
                        uint32_t iteration) {
  gStress.attempts++;
  if (durationUs < gStress.minAttemptUs) {
    gStress.minAttemptUs = durationUs;
  }
  if (durationUs > gStress.maxAttemptUs) {
    gStress.maxAttemptUs = durationUs;
  }
  gStress.sumAttemptUs += durationUs;

  if (st.ok()) {
    gStress.success++;
  } else {
    gStress.errors++;
    if (!gStress.hasFailure) {
      gStress.firstError = st;
      gStress.hasFailure = true;
    }
    gStress.lastError = st;
  }

  if (gVerbose) {
    LOG_SERIAL.printf("  [%lu] %-14s %s%s%s %lu us",
                      static_cast<unsigned long>(iteration),
                      op,
                      LOG_COLOR_RESULT(st.ok()),
                      st.ok() ? "OK" : "FAIL",
                      LOG_COLOR_RESET,
                      static_cast<unsigned long>(durationUs));
    if (!st.ok()) {
      LOG_SERIAL.printf(" (%s)", statusName(st.code));
    }
    LOG_SERIAL.println();
  }
}

void printStressSummary(uint32_t expectedAttempts) {
  gStress.active = false;
  gStress.endMs = millis();
  const uint32_t durationMs = gStress.endMs - gStress.startMs;
  const uint32_t successDelta = gDev.totalSuccess() - gStress.successBefore;
  const uint32_t failDelta = gDev.totalFailures() - gStress.failBefore;
  const float successPct =
      (gStress.attempts > 0U)
          ? (100.0f * static_cast<float>(gStress.success) /
             static_cast<float>(gStress.attempts))
          : 0.0f;

  LOG_SERIAL.println(strcmp(gStress.testName, "stress_mix") == 0
                         ? "=== stress_mix summary ==="
                         : "=== Stress Summary ===");
  LOG_SERIAL.printf("  Test: %s\n", gStress.testName);
  LOG_SERIAL.printf("  Target: %lu iterations\n",
                    static_cast<unsigned long>(gStress.targetIterations));
  LOG_SERIAL.printf("  Mode: %s\n", gStress.mode);
  LOG_SERIAL.printf("  Attempts: %lu/%lu\n",
                    static_cast<unsigned long>(gStress.attempts),
                    static_cast<unsigned long>(expectedAttempts));
  LOG_SERIAL.printf("  Success: %s%lu%s\n",
                    cli::nonZeroGoodColor(gStress.success),
                    static_cast<unsigned long>(gStress.success),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Errors: %s%lu%s\n",
                    cli::zeroGoodColor(gStress.errors),
                    static_cast<unsigned long>(gStress.errors),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Success rate: %s%.2f%%%s\n",
                    cli::successRateColor(successPct),
                    static_cast<double>(successPct),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  if (durationMs > 0U) {
    const float rate = 1000.0f * static_cast<float>(gStress.attempts) /
                       static_cast<float>(durationMs);
    LOG_SERIAL.printf("  Rate: %.2f ops/s\n", static_cast<double>(rate));
  }
  if (gStress.attempts > 0U) {
    const uint32_t avgUs =
        static_cast<uint32_t>(gStress.sumAttemptUs / static_cast<uint64_t>(gStress.attempts));
    LOG_SERIAL.printf("  Attempt us: min=%lu avg=%lu max=%lu\n",
                      static_cast<unsigned long>(gStress.minAttemptUs),
                      static_cast<unsigned long>(avgUs),
                      static_cast<unsigned long>(gStress.maxAttemptUs));
  }
  LOG_SERIAL.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                    cli::nonZeroGoodColor(successDelta),
                    static_cast<unsigned long>(successDelta),
                    LOG_COLOR_RESET,
                    cli::zeroGoodColor(failDelta),
                    static_cast<unsigned long>(failDelta),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Restore: %s%s%s\n",
                    LOG_COLOR_RESULT(gStress.restoreStatus.ok()),
                    gStress.restoreStatus.ok() ? "PASS" : "FAIL",
                    LOG_COLOR_RESET);
  if (gStress.aborted) {
    LOG_SERIAL.printf("  Result: %sFAIL%s (aborted on first error)\n",
                      LOG_COLOR_RED,
                      LOG_COLOR_RESET);
  } else {
    const bool passed = gStress.errors == 0U && gStress.restoreStatus.ok();
    LOG_SERIAL.printf("  Result: %s%s%s\n",
                      LOG_COLOR_RESULT(passed),
                      passed ? "PASS" : "FAIL",
                      LOG_COLOR_RESET);
  }

  if (gStress.hasFailure) {
    LOG_SERIAL.println("  First failure:");
    printStatus(gStress.firstError);
    if (gStress.errors > 1U) {
      LOG_SERIAL.println("  Last failure:");
      printStatus(gStress.lastError);
    }
  }
  if (!gStress.restoreStatus.ok()) {
    LOG_SERIAL.println("  Restore failure:");
    printStatus(gStress.restoreStatus);
  }

  LOG_SERIAL.print("  Final ");
  health_view::printSummary(gDev);
}

void runStress(const char* args, bool mixed) {
  const char* p = args;
  uint32_t count = 8;
  if (!noMoreArgs(p) && (!parseU32(p, 255, count) || !noMoreArgs(p))) {
    LOGE("usage: %s [1..255]", mixed ? "stress_mix" : "stress");
    return;
  }
  if (count == 0) {
    LOGE("count must be > 0");
    return;
  }

  MCP45HVX1::RegisterSnapshot original;
  MCP45HVX1::Status st = gDev.readSnapshot(original);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  const uint32_t expectedAttempts = count * 2U;
  resetStressStats(mixed ? "stress_mix" : "stress",
                   mixed ? "write wiper plus terminal mode" : "increment/decrement restore",
                   count);
  LOGI("Starting %s: %lu iterations, %lu operations",
       mixed ? "stress_mix" : "stress test",
       static_cast<unsigned long>(count),
       static_cast<unsigned long>(expectedAttempts));

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t startUs = micros();
    st = mixed ? gDev.writeWiper(static_cast<uint8_t>(
                       (i * MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution)) /
                       (count - 1U > 0U ? count - 1U : 1U)))
               : gDev.incrementWiper();
    recordStressResult(st, micros() - startUs, mixed ? "writeWiper" : "increment", i + 1U);
    printStressProgress(gStress.attempts, expectedAttempts, gStress.success, gStress.errors);
    if (!st.ok()) {
      gStress.aborted = true;
      break;
    }

    if (mixed) {
      const MCP45HVX1::TerminalMode mode =
          (i % 3U) == 0U ? MCP45HVX1::TerminalMode::Potentiometer
                         : ((i % 3U) == 1U ? MCP45HVX1::TerminalMode::RheostatBToW
                                           : MCP45HVX1::TerminalMode::RheostatAToW);
      const uint32_t modeStartUs = micros();
      st = gDev.setTerminalMode(mode);
      recordStressResult(st, micros() - modeStartUs, "setMode", i + 1U);
      printStressProgress(gStress.attempts, expectedAttempts, gStress.success, gStress.errors);
      if (!st.ok()) {
        gStress.aborted = true;
        break;
      }
    }
  }

  if (!mixed && !gStress.aborted) {
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t startUs = micros();
      MCP45HVX1::Status st = gDev.decrementWiper();
      recordStressResult(st, micros() - startUs, "decrement", i + 1U);
      printStressProgress(gStress.attempts, expectedAttempts, gStress.success, gStress.errors);
      if (!st.ok()) {
        gStress.aborted = true;
        break;
      }
    }
  }

  gStress.restoreStatus = restoreSnapshot(original);
  printStressSummary(expectedAttempts);
}

void handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  const char* p = line.c_str();
  char command[20] = {};
  if (!readToken(p, command, sizeof(command))) {
    return;
  }

  if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
    if (requireNoArgs(command, p)) printHelp();
  } else if (strcmp(command, "version") == 0 || strcmp(command, "ver") == 0) {
    if (requireNoArgs(command, p)) printVersion();
  } else if (strcmp(command, "scan") == 0) {
    if (requireNoArgs(command, p)) bus_diag::scan();
  } else if (strcmp(command, "begin") == 0) {
    handleBegin(p);
  } else if (strcmp(command, "addr") == 0) {
    handleAddress(p);
  } else if (strcmp(command, "res") == 0) {
    handleResolution(p);
  } else if (strcmp(command, "rab") == 0) {
    handleResistance(p);
  } else if (strcmp(command, "probe") == 0) {
    if (requireNoArgs(command, p)) printStatus(gDev.probe());
  } else if (strcmp(command, "recover") == 0) {
    if (requireNoArgs(command, p)) printStatus(gDev.recover());
  } else if (strcmp(command, "iface_reset") == 0) {
    if (requireNoArgs(command, p)) printStatus(gDev.resetI2cState());
  } else if (strcmp(command, "defaults") == 0) {
    if (requireNoArgs(command, p)) printStatus(gDev.restorePowerOnDefaults());
  } else if (strcmp(command, "read") == 0 || strcmp(command, "rregs") == 0) {
    if (requireNoArgs(command, p)) readRegisters();
  } else if (strcmp(command, "dump") == 0 || strcmp(command, "raw") == 0) {
    if (requireNoArgs(command, p)) dumpRegisters();
  } else if (strcmp(command, "last") == 0) {
    if (requireNoArgs(command, p)) handleLast();
  } else if (strcmp(command, "rreg") == 0) {
    handleReadRegister(p);
  } else if (strcmp(command, "wreg") == 0 || strcmp(command, "wregs") == 0) {
    handleWriteRegister(p);
  } else if (strcmp(command, "wiper") == 0) {
    handleWiper(p);
  } else if (strcmp(command, "frac") == 0 || strcmp(command, "pos") == 0) {
    handleFraction(p);
  } else if (strcmp(command, "zero") == 0) {
    if (requireNoArgs(command, p)) printStatus(gDev.writeWiper(0));
  } else if (strcmp(command, "mid") == 0) {
    if (requireNoArgs(command, p)) printStatus(gDev.writeWiperFraction(0.5f));
  } else if (strcmp(command, "max") == 0) {
    if (requireNoArgs(command, p)) {
      printStatus(gDev.writeWiper(MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution)));
    }
  } else if (strcmp(command, "inc") == 0) {
    handleStep(p, true);
  } else if (strcmp(command, "dec") == 0) {
    handleStep(p, false);
  } else if (strcmp(command, "tcon") == 0) {
    handleTcon(p);
  } else if (strcmp(command, "term") == 0) {
    handleTerm(p);
  } else if (strcmp(command, "shutdown") == 0) {
    handleShutdown(p);
  } else if (strcmp(command, "mode") == 0) {
    handleMode(p);
  } else if (strcmp(command, "cfg") == 0 || strcmp(command, "settings") == 0 ||
             strcmp(command, "detail") == 0) {
    if (requireNoArgs(command, p)) health_diag::printSettings(gDev);
  } else if (strcmp(command, "drv") == 0) {
    if (requireNoArgs(command, p)) health_view::printSummary(gDev);
  } else if (strcmp(command, "info") == 0) {
    if (requireNoArgs(command, p)) printDeviceInfo();
  } else if (strcmp(command, "errata") == 0) {
    if (requireNoArgs(command, p)) printErrataInfo();
  } else if (strcmp(command, "selftest") == 0) {
    if (requireNoArgs(command, p)) runSelfTest();
  } else if (strcmp(command, "stress") == 0) {
    runStress(p, false);
  } else if (strcmp(command, "stress_mix") == 0) {
    runStress(p, true);
  } else if (strcmp(command, "verbose") == 0) {
    handleVerbose(p);
  } else if (strcmp(command, "gc") == 0) {
    handleGeneralCall(p);
  } else {
    cli::printUnknownCommand(command);
  }

  LOGV(gVerbose, "state=%s", health_view::stateName(gDev.state()));
}

}  // namespace

void setup() {
  log_begin();
  LOGI("MCP45HVX1 bring-up CLI");
  printVersion();

  if (!board::initI2c()) {
    LOGE("I2C init failed");
    return;
  }

  gConfig = makeDefaultConfig();
  beginDevice();
  printDeviceInfo();
  readRegisters();
  printHelp();
  cli::printPrompt();
}

void loop() {
  gDev.tick(millis());

  String line;
  if (cli_shell::readLine(line)) {
    handleCommand(line);
    cli::printPrompt();
  }
}
