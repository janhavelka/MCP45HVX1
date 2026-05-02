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

const char* skipWs(const char* p) {
  while (p != nullptr && (*p == ' ' || *p == '\t')) {
    ++p;
  }
  return p == nullptr ? "" : p;
}

bool noMoreArgs(const char* p) {
  return *skipWs(p) == '\0';
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
  if (end == p || parsed < 0.0f || parsed > 1.0f) {
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
  cfg.busReset = transport::wireBusReset;
  cfg.controlUser = &gBusReset;
  cfg.i2cAddress = MCP45HVX1::cmd::DEFAULT_ADDRESS;
  cfg.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  cfg.resolution = MCP45HVX1::Resolution::Bits8;
  cfg.resistance = MCP45HVX1::ResistanceOption::R10K;
  return cfg;
}

void printStatus(const MCP45HVX1::Status& st) {
  if (st.ok()) {
    LOG_SERIAL.printf("%sOK%s\n", LOG_COLOR_GREEN, LOG_COLOR_RESET);
    return;
  }
  LOG_SERIAL.printf("%sERR%s code=%s(%u) detail=%ld msg=%s\n",
                    LOG_COLOR_RED,
                    LOG_COLOR_RESET,
                    statusName(st.code),
                    static_cast<unsigned>(st.code),
                    static_cast<long>(st.detail),
                    st.msg);
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
  LOG_SERIAL.printf("device addr=0x%02X resolution=%u-bit rab=%s (%u ohm) step=%.3f ohm",
                    info.i2cAddress,
                    static_cast<unsigned>(info.resolution),
                    resistanceName(info.resistance),
                    static_cast<unsigned>(info.nominalResistanceOhms),
                    static_cast<double>(info.nominalStepOhms));
  if (info.usingAlternateAddressRange) {
    LOG_SERIAL.print(" alternate-address");
  }
  LOG_SERIAL.println();
}

void printTerminalStatus(uint8_t tcon) {
  const MCP45HVX1::TerminalStatus s = MCP45HVX1::MCP45HVX1::decodeTcon(tcon);
  LOG_SERIAL.printf("tcon=0x%02X mode=%s shdn=%s A=%s W=%s B=%s\n",
                    tcon,
                    modeName(s.mode),
                    log_bool_str(s.softwareShutdown),
                    log_bool_str(s.terminalA),
                    log_bool_str(s.terminalW),
                    log_bool_str(s.terminalB));
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
  LOG_SERIAL.printf("wiper=0x%02X pos=%.4f r_bw=%.1f ohm r_aw=%.1f ohm\n",
                    s.wiper,
                    static_cast<double>(position),
                    static_cast<double>(rbw),
                    static_cast<double>(rawOhms));
  printTerminalStatus(s.tcon);
}

void dumpRegisters() {
  readRegisters();
  uint8_t last = 0;
  MCP45HVX1::Status st = gDev.readLastAddress(last);
  if (st.ok()) {
    LOG_SERIAL.printf("last-address-read=0x%02X\n", last);
  } else {
    printStatus(st);
  }
}

void printHelp() {
  cli::printHelpHeader("MCP45HVX1 CLI");
  cli::printHelpSection("Device");
  cli::printHelpItem("help | ?", "Show this help");
  cli::printHelpItem("version | ver", "Print library version");
  cli::printHelpItem("scan", "Scan I2C bus");
  cli::printHelpItem("begin [addr] [7|8]", "Apply config and begin driver");
  cli::printHelpItem("addr <0x3c..0x3f|0x5c..0x5f>", "Set I2C address and re-begin");
  cli::printHelpItem("res <7|8>", "Set wiper resolution and re-begin");
  cli::printHelpItem("rab <5k|10k|50k|100k>", "Set nominal resistance helper");
  cli::printHelpItem("probe", "Probe configured device");
  cli::printHelpItem("recover", "Refresh cache and update health");
  cli::printHelpItem("iface_reset", "Run board I2C reset callback");
  cli::printHelpItem("defaults", "Restore volatile POR/BOR defaults");

  cli::printHelpSection("Read/Write");
  cli::printHelpItem("read | rregs", "Read wiper and TCON");
  cli::printHelpItem("dump", "Read registers and last-address value");
  cli::printHelpItem("last", "Read current device address pointer");
  cli::printHelpItem("rreg <0x00|0x04>", "Read one register");
  cli::printHelpItem("wreg <reg> <value>", "Write one register");
  cli::printHelpItem("wregs <reg> <value>", "Compatibility alias for wreg");
  cli::printHelpItem("wiper <0..max>", "Write volatile wiper code");
  cli::printHelpItem("frac <0.0..1.0>", "Write normalized wiper position");
  cli::printHelpItem("pos <0.0..1.0>", "Alias for frac");
  cli::printHelpItem("zero | mid | max", "Move wiper to common positions");
  cli::printHelpItem("inc [n] | dec [n]", "Step wiper with clamp at endpoints");

  cli::printHelpSection("Terminal Control");
  cli::printHelpItem("tcon [value]", "Read or write TCON");
  cli::printHelpItem("term a|w|b [0|1]", "Read or set one terminal bit");
  cli::printHelpItem("shutdown [0|1]", "Read or set software shutdown");
  cli::printHelpItem("mode pot|bw|aw|float|shutdown", "Apply terminal preset");

  cli::printHelpSection("Diagnostics");
  cli::printHelpItem("cfg | settings", "Print config, cache, and health");
  cli::printHelpItem("drv", "Print compact health line");
  cli::printHelpItem("info", "Print variant/resistance helper info");
  cli::printHelpItem("selftest", "Probe, read, and decode all practical state");
  cli::printHelpItem("stress [n]", "Run inc/dec exercise");
  cli::printHelpItem("stress_mix [n]", "Exercise writes, modes, and reads");
  cli::printHelpItem("verbose [0|1]", "Get/set verbose command logging");

  cli::printHelpSection("General Call");
  cli::printHelpItem("gc wiper <code>", "Broadcast wiper write");
  cli::printHelpItem("gc tcon <value>", "Broadcast TCON write");
  cli::printHelpItem("gc inc | gc dec", "Broadcast one wiper step");
  LOG_SERIAL.println();
}

void handleBegin(const char* args) {
  uint32_t address = 0;
  const char* p = args;
  if (parseU32(p, 0x7F, address)) {
    gConfig.i2cAddress = static_cast<uint8_t>(address);
  }
  MCP45HVX1::Resolution resolution = gConfig.resolution;
  if (!noMoreArgs(p)) {
    if (!parseResolution(p, resolution) || !noMoreArgs(p)) {
      LOGE("usage: begin [addr] [7|8]");
      return;
    }
    gConfig.resolution = resolution;
  }
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
  LOGI("rab=%s", resistanceName(option));
  printDeviceInfo();
}

void handleWiper(const char* args) {
  uint32_t value = 0;
  const char* p = args;
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
      LOG_SERIAL.printf("enabled=%s\n", log_bool_str(enabled));
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
      LOG_SERIAL.printf("shutdown=%s\n", log_bool_str(enabled));
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
    LOG_SERIAL.printf("reg[0x%02X]=0x%02X\n", static_cast<unsigned>(reg), value);
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
    LOG_SERIAL.printf("last=0x%02X\n", value);
  } else {
    printStatus(st);
  }
}

void handleVerbose(const char* args) {
  const char* p = args;
  if (noMoreArgs(p)) {
    LOG_SERIAL.printf("verbose=%s\n", log_bool_str(gVerbose));
    return;
  }
  bool value = false;
  if (!parseBool(p, value) || !noMoreArgs(p)) {
    LOGE("usage: verbose [0|1]");
    return;
  }
  gVerbose = value;
  LOGI("verbose=%s", log_bool_str(gVerbose));
}

void handleGeneralCall(const char* args) {
  const char* p = args;
  char sub[12] = {};
  if (!readToken(p, sub, sizeof(sub))) {
    LOGE("usage: gc wiper <code> | gc tcon <value> | gc inc | gc dec");
    return;
  }

  if (strcmp(sub, "wiper") == 0) {
    uint32_t value = 0;
    if (!parseU32(p, MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution), value) ||
        !noMoreArgs(p)) {
      LOGE("usage: gc wiper <0..max>");
      return;
    }
    printStatus(gDev.generalCallWriteWiper(static_cast<uint8_t>(value)));
  } else if (strcmp(sub, "tcon") == 0) {
    uint32_t value = 0;
    if (!parseU32(p, 0xFF, value) || !noMoreArgs(p)) {
      LOGE("usage: gc tcon <0x00..0xff>");
      return;
    }
    printStatus(gDev.generalCallWriteTcon(static_cast<uint8_t>(value)));
  } else if (strcmp(sub, "inc") == 0 && noMoreArgs(p)) {
    printStatus(gDev.generalCallIncrementWiper());
  } else if (strcmp(sub, "dec") == 0 && noMoreArgs(p)) {
    printStatus(gDev.generalCallDecrementWiper());
  } else {
    LOGE("usage: gc wiper <code> | gc tcon <value> | gc inc | gc dec");
  }
}

void runSelfTest() {
  printStatus(gDev.probe());
  readRegisters();
  handleLast();
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

  for (uint32_t i = 0; i < count; ++i) {
    MCP45HVX1::Status st = mixed ? gDev.writeWiper(static_cast<uint8_t>(
                                      (i * MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution)) /
                                      (count - 1U > 0U ? count - 1U : 1U)))
                                  : gDev.incrementWiper();
    if (!st.ok()) {
      printStatus(st);
      return;
    }

    if (mixed) {
      const MCP45HVX1::TerminalMode mode =
          (i % 3U) == 0U ? MCP45HVX1::TerminalMode::Potentiometer
                         : ((i % 3U) == 1U ? MCP45HVX1::TerminalMode::RheostatBToW
                                           : MCP45HVX1::TerminalMode::RheostatAToW);
      st = gDev.setTerminalMode(mode);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
    }
  }

  if (!mixed) {
    for (uint32_t i = 0; i < count; ++i) {
      MCP45HVX1::Status st = gDev.decrementWiper();
      if (!st.ok()) {
        printStatus(st);
        return;
      }
    }
  }
  readRegisters();
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
    printHelp();
  } else if (strcmp(command, "version") == 0 || strcmp(command, "ver") == 0) {
    printVersion();
  } else if (strcmp(command, "scan") == 0) {
    bus_diag::scan();
  } else if (strcmp(command, "begin") == 0) {
    handleBegin(p);
  } else if (strcmp(command, "addr") == 0) {
    handleAddress(p);
  } else if (strcmp(command, "res") == 0) {
    handleResolution(p);
  } else if (strcmp(command, "rab") == 0) {
    handleResistance(p);
  } else if (strcmp(command, "probe") == 0) {
    printStatus(gDev.probe());
  } else if (strcmp(command, "recover") == 0) {
    printStatus(gDev.recover());
  } else if (strcmp(command, "iface_reset") == 0) {
    printStatus(gDev.resetI2cState());
  } else if (strcmp(command, "defaults") == 0) {
    printStatus(gDev.restorePowerOnDefaults());
  } else if (strcmp(command, "read") == 0 || strcmp(command, "rregs") == 0) {
    readRegisters();
  } else if (strcmp(command, "dump") == 0) {
    dumpRegisters();
  } else if (strcmp(command, "last") == 0) {
    handleLast();
  } else if (strcmp(command, "rreg") == 0) {
    handleReadRegister(p);
  } else if (strcmp(command, "wreg") == 0 || strcmp(command, "wregs") == 0) {
    handleWriteRegister(p);
  } else if (strcmp(command, "wiper") == 0) {
    handleWiper(p);
  } else if (strcmp(command, "frac") == 0 || strcmp(command, "pos") == 0) {
    handleFraction(p);
  } else if (strcmp(command, "zero") == 0) {
    printStatus(gDev.writeWiper(0));
  } else if (strcmp(command, "mid") == 0) {
    printStatus(gDev.writeWiperFraction(0.5f));
  } else if (strcmp(command, "max") == 0) {
    printStatus(gDev.writeWiper(MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution)));
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
  } else if (strcmp(command, "cfg") == 0 || strcmp(command, "settings") == 0) {
    health_diag::printSettings(gDev);
  } else if (strcmp(command, "drv") == 0) {
    health_view::printSummary(gDev);
  } else if (strcmp(command, "info") == 0) {
    printDeviceInfo();
  } else if (strcmp(command, "selftest") == 0) {
    runSelfTest();
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
