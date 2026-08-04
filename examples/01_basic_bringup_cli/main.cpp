/**
 * @file main.cpp
 * @brief Interactive MCP45HVX1 bring-up CLI.
 */

#include <Arduino.h>
#include <Wire.h>
#if defined(ESP_PLATFORM)
#include "esp_system.h"
#endif

#include <cstdlib>
#include <cstring>

#include "MCP45HVX1/MCP45HVX1.h"
#include "examples/common/BoardConfig.h"
#include "examples/common/CliShell.h"
#include "examples/common/CliStyle.h"
#include "examples/common/HealthView.h"
#include "examples/common/I2cScanner.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/Log.h"

namespace {

MCP45HVX1::MCP45HVX1 gDev;
MCP45HVX1::Config gConfig;
transport::BusResetContext gBusReset{board::I2C_SDA, board::I2C_SCL};
bool gVerbose = false;
bool gGeneralCallArmed = false;
bool gOutputStateUncertain = false;

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
  bool outputChanging = false;
};

StressStats gStress;

void printStateLine();
void printHelp();

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
      strcmp(token, "bits7") == 0 || strcmp(token, "hv31") == 0 ||
      strcmp(token, "mcp45hv31") == 0) {
    out = MCP45HVX1::Resolution::Bits7;
    return true;
  }
  if (strcmp(token, "8") == 0 || strcmp(token, "8bit") == 0 ||
      strcmp(token, "bits8") == 0 || strcmp(token, "hv51") == 0 ||
      strcmp(token, "mcp45hv51") == 0) {
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
  if (strcmp(token, "5k") == 0 || strcmp(token, "5000") == 0 || strcmp(token, "502") == 0) {
    out = MCP45HVX1::ResistanceOption::R5K;
    return true;
  }
  if (strcmp(token, "10k") == 0 || strcmp(token, "10000") == 0 || strcmp(token, "103") == 0) {
    out = MCP45HVX1::ResistanceOption::R10K;
    return true;
  }
  if (strcmp(token, "50k") == 0 || strcmp(token, "50000") == 0 || strcmp(token, "503") == 0) {
    out = MCP45HVX1::ResistanceOption::R50K;
    return true;
  }
  if (strcmp(token, "100k") == 0 || strcmp(token, "100000") == 0 || strcmp(token, "104") == 0) {
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

uint32_t exampleNowMs(void*) {
  return millis();
}

const char* variantName(MCP45HVX1::Resolution resolution) {
  return resolution == MCP45HVX1::Resolution::Bits7 ? "MCP45HV31" : "MCP45HV51";
}

uint16_t tapCount(MCP45HVX1::Resolution resolution) {
  return resolution == MCP45HVX1::Resolution::Bits7 ? 128U : 256U;
}

uint8_t porWiperCode(MCP45HVX1::Resolution resolution) {
  return MCP45HVX1::MCP45HVX1::defaultWiperCode(resolution);
}

const char* frameworkName() {
  return "Arduino";
}

const char* targetName() {
#if defined(CONFIG_IDF_TARGET)
  return CONFIG_IDF_TARGET;
#elif defined(ARDUINO_ARCH_ESP32)
  return "esp32";
#else
  return "unknown";
#endif
}

const char* resetReasonName() {
#if defined(ESP_PLATFORM)
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "other watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
#else
  return "unknown";
#endif
}

bool parseFloatRange(const char*& p, float minValue, float maxValue, float& out) {
  p = skipWs(p);
  if (*p == '\0') {
    return false;
  }
  char* end = nullptr;
  const float parsed = strtof(p, &end);
  if (end == p || parsed != parsed || parsed < minValue || parsed > maxValue) {
    return false;
  }
  p = end;
  out = parsed;
  return true;
}

void printActiveConfigLine() {
  LOG_SERIAL.printf("  Active: addr=0x%02X range=%s variant=%s resolution=%u-bit/%u taps rab=%u ohm\n",
                    gConfig.i2cAddress,
                    isAlternateAddress(gConfig.i2cAddress) ? "alternate" : "standard",
                    variantName(gConfig.resolution),
                    static_cast<unsigned>(gConfig.resolution),
                    static_cast<unsigned>(tapCount(gConfig.resolution)),
                    static_cast<unsigned>(
                        MCP45HVX1::MCP45HVX1::nominalResistanceOhms(gConfig.resistance)));
}

void warnOutputChanging(const char* command) {
  LOG_SERIAL.printf("%s[WARN]%s %s changes the MCP45HVX1 analog output state.\n",
                    LOG_COLOR_YELLOW,
                    LOG_COLOR_RESET,
                    command);
  LOG_SERIAL.printf("%s[WARN]%s Confirm that the connected analog/high-voltage circuit is safe.\n",
                    LOG_COLOR_YELLOW,
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("%s[WARN]%s Register readback does not prove physical output movement if WLAT/SHDN pins override it.\n",
                    LOG_COLOR_YELLOW,
                    LOG_COLOR_RESET);
}

void warnDangerous(const char* command) {
  LOG_SERIAL.printf("%s[DANGER]%s %s is bus-wide or raw and may affect live hardware.\n",
                    LOG_COLOR_MAGENTA,
                    LOG_COLOR_RESET,
                    command);
  warnOutputChanging(command);
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
  cfg.nowMs = exampleNowMs;
  cfg.resolution = MCP45HVX1::Resolution::Bits8;
  cfg.resistance = MCP45HVX1::ResistanceOption::R10K;
  cfg.allowGeneralCall = true;  // Diagnostic CLI has its own one-shot `gc arm` operator gate.
  return cfg;
}

void printStatus(const MCP45HVX1::Status& st) {
  LOG_SERIAL.printf("  Status: %s%s%s (code=%u, detail=%ld)\n",
                    statusColor(st),
                    health_view::errName(st.code),
                    LOG_COLOR_RESET,
                    static_cast<unsigned>(st.code),
                    static_cast<long>(st.detail));
  if (!st.ok()) {
    LOG_SERIAL.printf("  Detail: %ld\n", static_cast<long>(st.detail));
    LOG_SERIAL.printf("  Cause: %s\n", statusCause(st.code));
    if (st.msg != nullptr && st.msg[0] != '\0') {
      LOG_SERIAL.printf("  Message: %s\n", st.msg);
    }
    LOG_SERIAL.printf("  Address: 0x%02X\n", gConfig.i2cAddress);
    LOG_SERIAL.printf("  Last OK: %lu ms\n", static_cast<unsigned long>(gDev.lastOkMs()));
    LOG_SERIAL.printf("  Consecutive failures: %u\n",
                      static_cast<unsigned>(gDev.consecutiveFailures()));
    LOG_SERIAL.println("  Suggested next step: run `probe`, check wiring/pullups/address, then `recover`");
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
  if (st.ok()) {
    gOutputStateUncertain = false;
    printActiveConfigLine();
  } else if (gDev.hardwareStateUncertain()) {
    LOGW("begin failed after a possible output-changing startup write; verify state");
    printStateLine();
  }
  return st;
}

void printVersion() {
  LOG_SERIAL.printf("%s=== MCP45HVX1 Bring-up CLI ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  LOG_SERIAL.println("  Firmware: MCP45HVX1 bring-up CLI");
  LOG_SERIAL.printf("  Library: %s\n", MCP45HVX1::VERSION_FULL);
  LOG_SERIAL.printf("  Framework: %s\n", frameworkName());
#if defined(ARDUINO_ARCH_ESP32)
  LOG_SERIAL.printf("  Arduino-ESP32: %s\n", ESP.getCoreVersion());
  LOG_SERIAL.printf("  ESP-IDF: %s\n", ESP.getSdkVersion());
#endif
  LOG_SERIAL.printf("  Target: %s\n", targetName());
  LOG_SERIAL.printf("  Reset reason: %s\n", resetReasonName());
  LOG_SERIAL.printf("  CLI color: %s%s%s\n",
                    cli::yesNoColor(log_color::isEnabled()),
                    log_color::isEnabled() ? "on" : "off",
                    LOG_COLOR_RESET);
  LOG_SERIAL.println("  CLI mode: safe by default");
}

void printDeviceInfo() {
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  LOG_SERIAL.println("=== Device Info ===");
  LOG_SERIAL.printf("  Address: 0x%02X (%s range)\n",
                    info.i2cAddress,
                    info.usingAlternateAddressRange ? "alternate" : "primary");
  LOG_SERIAL.printf("  Variant: %s (configured, not auto-detected)\n",
                    variantName(info.resolution));
  LOG_SERIAL.printf("  Resolution: %u-bit / %u taps (max code 0x%02X, POR 0x%02X)\n",
                    static_cast<unsigned>(info.resolution),
                    static_cast<unsigned>(tapCount(info.resolution)),
                    info.maxWiperCode,
                    info.defaultWiperCode);
  LOG_SERIAL.printf("  RAB nominal: %s (%u ohm)\n",
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
  LOG_SERIAL.printf("  Production release gate required: %s%s%s\n",
                    cli::warningIfNonZeroColor(info.productionReleaseGateRequired ? 1U : 0U),
                    log_bool_str(info.productionReleaseGateRequired),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Shared-bus risk acceptance required: %s%s%s\n",
                    cli::warningIfNonZeroColor(info.sharedBusRiskAcceptanceRequired ? 1U : 0U),
                    log_bool_str(info.sharedBusRiskAcceptanceRequired),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  General Call isolated-bus evidence required: %s%s%s\n",
                    cli::warningIfNonZeroColor(info.generalCallRequiresIsolatedBusEvidence ? 1U : 0U),
                    log_bool_str(info.generalCallRequiresIsolatedBusEvidence),
                    LOG_COLOR_RESET);
  LOG_SERIAL.println("  Source: docs/reference-pdfs/MCP45HVX1_Errata_DS80000649B.pdf");
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
    LOG_SERIAL.println("=== MCP45HVX1 Wiper/TCON Read ===");
    printStatus(st);
    return;
  }

  gOutputStateUncertain = false;
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  const float position = MCP45HVX1::MCP45HVX1::fractionFromCode(s.wiper, gConfig.resolution);
  const float rbw = MCP45HVX1::MCP45HVX1::resistanceBToWOhms(s.wiper, gConfig.resistance,
                                                            gConfig.resolution);
  const float rawOhms = MCP45HVX1::MCP45HVX1::resistanceAToWOhms(s.wiper, gConfig.resistance,
                                                                 gConfig.resolution);
  LOG_SERIAL.println("=== MCP45HVX1 Wiper/TCON Read ===");
  LOG_SERIAL.printf("  Address: 0x%02X\n", info.i2cAddress);
  LOG_SERIAL.printf("  Variant: %s\n", variantName(info.resolution));
  LOG_SERIAL.printf("  Resolution: %u-bit / %u taps\n",
                    static_cast<unsigned>(info.resolution),
                    static_cast<unsigned>(tapCount(info.resolution)));
  LOG_SERIAL.printf("  RAB nominal: %u ohm\n",
                    static_cast<unsigned>(info.nominalResistanceOhms));
  LOG_SERIAL.printf("  Raw code: 0x%02X / %u\n", s.wiper, static_cast<unsigned>(s.wiper));
  LOG_SERIAL.printf("  Position: %.1f %%\n", static_cast<double>(position * 100.0f));
  LOG_SERIAL.printf("  Nominal resistance A-W: %.1f ohm\n", static_cast<double>(rawOhms));
  LOG_SERIAL.printf("  Nominal resistance W-B: %.1f ohm\n", static_cast<double>(rbw));
  printTerminalStatus(s.tcon);
  printStatus(st);
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

bool hardwareStateUncertain(const MCP45HVX1::SettingsSnapshot& s) {
  return s.hardwareStateUncertain || gOutputStateUncertain ||
         !s.cachedWiperKnown || !s.cachedTconKnown || !gDev.isOnline();
}

void printStateLine() {
  MCP45HVX1::SettingsSnapshot s;
  (void)gDev.getSettings(s);
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  const bool dirty = hardwareStateUncertain(s);
  LOG_SERIAL.printf("MCP45HVX1 state: %s%s%s online=%s%s%s addr=0x%02X variant=%s ",
                    LOG_COLOR_STATE(gDev.isOnline(), s.consecutiveFailures),
                    health_view::stateName(s.state),
                    LOG_COLOR_RESET,
                    cli::yesNoColor(gDev.isOnline()),
                    gDev.isOnline() ? "yes" : "no",
                    LOG_COLOR_RESET,
                    info.i2cAddress,
                    info.resolution == MCP45HVX1::Resolution::Bits7 ? "HV31" : "HV51");
  if (s.cachedWiperKnown) {
    LOG_SERIAL.printf("wiper=0x%02X ", s.cachedWiper);
  } else {
    LOG_SERIAL.print("wiper=unknown ");
  }
  if (s.cachedTconKnown) {
    LOG_SERIAL.printf("tcon=0x%02X ", s.cachedTcon);
  } else {
    LOG_SERIAL.print("tcon=unknown ");
  }
  LOG_SERIAL.printf("failures=%u uncertain=%s%s%s dirty=%s%s%s\n",
                    static_cast<unsigned>(s.consecutiveFailures),
                    dirty ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    dirty ? "yes" : "no",
                    LOG_COLOR_RESET,
                    dirty ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    dirty ? "yes" : "no",
                    LOG_COLOR_RESET);
}

void printDriverHealth() {
  MCP45HVX1::SettingsSnapshot s;
  const MCP45HVX1::Status snapStatus = gDev.getSettings(s);
  if (!snapStatus.ok()) {
    printStatus(snapStatus);
    return;
  }
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  const uint32_t total = s.totalSuccess + s.totalFailures;
  const float successRate =
      total > 0U ? (100.0f * static_cast<float>(s.totalSuccess) /
                    static_cast<float>(total))
                 : 0.0f;
  const uint32_t now = millis();

  LOG_SERIAL.println("=== Driver Health ===");
  LOG_SERIAL.printf("  State: %s%s%s\n",
                    LOG_COLOR_STATE(gDev.isOnline(), s.consecutiveFailures),
                    health_view::stateName(s.state),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Online: %s%s%s\n",
                    cli::yesNoColor(gDev.isOnline()),
                    gDev.isOnline() ? "yes" : "no",
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Active address: 0x%02X\n", info.i2cAddress);
  LOG_SERIAL.printf("  Variant: %s\n", variantName(info.resolution));
  LOG_SERIAL.printf("  Resolution: %u-bit / %u taps\n",
                    static_cast<unsigned>(info.resolution),
                    static_cast<unsigned>(tapCount(info.resolution)));
  LOG_SERIAL.printf("  RAB nominal: %u ohm\n",
                    static_cast<unsigned>(info.nominalResistanceOhms));
  LOG_SERIAL.printf("  Consecutive failures: %s%u%s\n",
                    health_view::failureColor(s.consecutiveFailures),
                    static_cast<unsigned>(s.consecutiveFailures),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Total success: %s%lu%s\n",
                    health_view::successColor(s.totalSuccess),
                    static_cast<unsigned long>(s.totalSuccess),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Total failures: %s%lu%s\n",
                    health_view::failureColor(s.totalFailures),
                    static_cast<unsigned long>(s.totalFailures),
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Success rate: %s%.1f%%%s\n",
                    cli::successRateColor(successRate),
                    static_cast<double>(successRate),
                    LOG_COLOR_RESET);
  if (s.lastOkMs > 0U) {
    LOG_SERIAL.printf("  Last OK: %lu ms ago (at %lu ms)\n",
                      static_cast<unsigned long>(now - s.lastOkMs),
                      static_cast<unsigned long>(s.lastOkMs));
  } else {
    LOG_SERIAL.println("  Last OK: never");
  }
  if (s.lastErrorMs > 0U) {
    LOG_SERIAL.printf("  Last error: %lu ms ago (at %lu ms)\n",
                      static_cast<unsigned long>(now - s.lastErrorMs),
                      static_cast<unsigned long>(s.lastErrorMs));
  } else {
    LOG_SERIAL.println("  Last error: never");
  }
  LOG_SERIAL.printf("  Last status code/detail: %s%s%s / %ld\n",
                    s.lastError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                    health_view::errName(s.lastError.code),
                    LOG_COLOR_RESET,
                    static_cast<long>(s.lastError.detail));
  if (!s.lastError.ok() && s.lastError.msg != nullptr && s.lastError.msg[0] != '\0') {
    LOG_SERIAL.printf("  Last message: %s\n", s.lastError.msg);
  }
  LOG_SERIAL.printf("  Hardware state dirty/uncertain: %s%s%s\n",
                    hardwareStateUncertain(s) ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    hardwareStateUncertain(s) ? "yes" : "no",
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Core hardware uncertainty: %s%s%s\n",
                    s.hardwareStateUncertain ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    s.hardwareStateUncertain ? "yes" : "no",
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Last uncertainty code/detail: %s%s%s / %ld\n",
                    s.hardwareStateUncertainError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                    health_view::errName(s.hardwareStateUncertainError.code),
                    LOG_COLOR_RESET,
                    static_cast<long>(s.hardwareStateUncertainError.detail));
  if (!s.hardwareStateUncertainError.ok() &&
      s.hardwareStateUncertainError.msg != nullptr &&
      s.hardwareStateUncertainError.msg[0] != '\0') {
    LOG_SERIAL.printf("  Last uncertainty message: %s\n", s.hardwareStateUncertainError.msg);
  }
}

void printConfigSnapshot() {
  MCP45HVX1::SettingsSnapshot s;
  const MCP45HVX1::Status snapStatus = gDev.getSettings(s);
  if (!snapStatus.ok()) {
    printStatus(snapStatus);
    return;
  }
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();

  LOG_SERIAL.println("=== MCP45HVX1 Configuration ===");
  LOG_SERIAL.printf("  Active address: 0x%02X\n", info.i2cAddress);
  LOG_SERIAL.println("  Address policy: standard 0x3C..0x3F");
  LOG_SERIAL.printf("  Alternate address range enabled: %s%s%s\n",
                    s.config.allowAlternateAddressRange ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    s.config.allowAlternateAddressRange ? "yes" : "no",
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  General Call core opt-in: %s%s%s\n",
                    s.config.allowGeneralCall ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    s.config.allowGeneralCall ? "yes" : "no",
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Variant: %s (configured, not auto-detected)\n",
                    variantName(info.resolution));
  LOG_SERIAL.printf("  Resolution: %u-bit / %u taps\n",
                    static_cast<unsigned>(info.resolution),
                    static_cast<unsigned>(tapCount(info.resolution)));
  LOG_SERIAL.printf("  POR wiper default: 0x%02X\n", porWiperCode(info.resolution));
  LOG_SERIAL.printf("  RAB nominal: %u ohm\n",
                    static_cast<unsigned>(info.nominalResistanceOhms));
  LOG_SERIAL.printf("  TCON cached: %s0x%02X%s\n",
                    s.cachedTconKnown ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                    s.cachedTcon,
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  TCON cache known: %s%s%s\n",
                    s.cachedTconKnown ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                    s.cachedTconKnown ? "yes" : "no",
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Wiper cached: %s0x%02X%s\n",
                    s.cachedWiperKnown ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                    s.cachedWiper,
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Wiper cache known: %s%s%s\n",
                    s.cachedWiperKnown ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                    s.cachedWiperKnown ? "yes" : "no",
                    LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Hardware state uncertain: %s%s%s\n",
                    hardwareStateUncertain(s) ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    hardwareStateUncertain(s) ? "yes" : "no",
                    LOG_COLOR_RESET);
  LOG_SERIAL.println("  SHDN pin controlled by core: no");
  LOG_SERIAL.println("  WLAT pin controlled by core: no");
  LOG_SERIAL.println("  Safe mode default: yes");
  LOG_SERIAL.println("  Analog safety enforced by software: no");
}

enum class HelpSection : uint8_t {
  Common,
  DeviceSelection,
  Diagnostics,
  Tests,
  OutputControl,
  Dangerous,
};

enum class HelpSafety : uint8_t {
  Safe,
  Lifecycle,
  QueryOrWrite,
  OutputChanging,
  Dangerous,
};

struct CommandHelpSpec {
  const char* canonical;
  const char* aliases;
  const char* synopsis;
  const char* description;
  HelpSection section;
  HelpSafety safety;
  const char* syntax;
  const char* examples;
};

static constexpr CommandHelpSpec COMMAND_HELP[] = {
    {"help", "?", "help / ? [command]", "Show all commands or detailed help for one command.",
     HelpSection::Common, HelpSafety::Safe, "help\nhelp <command>\n? <command>",
     "help\nhelp wiper\n? gc"},
    {"version", "ver", "version / ver", "Print firmware, library, framework, target, and reset provenance.",
     HelpSection::Common, HelpSafety::Safe, "version\nver", "version"},
    {"scan", "", "scan", "Scan the application-owned I2C bus for responding addresses.",
     HelpSection::Common, HelpSafety::Safe, "scan", "scan"},
    {"color", "", "color [on|off]", "Show or change ANSI color output.",
     HelpSection::Common, HelpSafety::Safe, "color\ncolor <on|off>", "color off\ncolor on"},
    {"verbose", "", "verbose [on|off]", "Show or change per-operation diagnostic logging.",
     HelpSection::Common, HelpSafety::Safe, "verbose\nverbose <on|off>", "verbose on\nstress 10"},

    {"begin", "", "begin [addr] [7|8]", "Apply the selected address/variant and initialize the driver.",
     HelpSection::DeviceSelection, HelpSafety::Lifecycle,
     "begin\nbegin <addr> [7|8]\nbegin <7|8>", "begin\nbegin 0x3c 8"},
    {"addr", "", "addr [0x3c..0x3f]", "Show or select a documented I2C address, then reinitialize.",
     HelpSection::DeviceSelection, HelpSafety::Lifecycle, "addr\naddr <0x3c..0x3f>",
     "addr\naddr 0x3c"},
    {"addr_alt", "", "addr_alt <0x5c..0x5f>", "Opt in to the disputed alternate address range.",
     HelpSection::DeviceSelection, HelpSafety::Lifecycle, "addr_alt <0x5c..0x5f>",
     "addr_alt 0x5c"},
    {"variant", "res", "variant / res [hv31|hv51|7|8]", "Show or select the configured device resolution.",
     HelpSection::DeviceSelection, HelpSafety::Lifecycle,
     "variant\nvariant <hv31|hv51>\nres <7|8>", "variant hv51\nres 7"},
    {"rab", "", "rab [5k|10k|50k|100k]", "Show or select nominal RAB for resistance helpers.",
     HelpSection::DeviceSelection, HelpSafety::Lifecycle,
     "rab\nrab <5k|10k|50k|100k|502|103|503|104>", "rab 10k\nrab 103"},

    {"probe", "", "probe", "Check the configured device without changing health counters.",
     HelpSection::Diagnostics, HelpSafety::Safe, "probe", "probe"},
    {"cfg", "settings detail", "cfg / settings / detail", "Print configuration and cache state; detail also prints health.",
     HelpSection::Diagnostics, HelpSafety::Safe, "cfg\nsettings\ndetail", "detail"},
    {"drv", "health", "drv / health", "Print driver state, counters, last error, and hardware uncertainty.",
     HelpSection::Diagnostics, HelpSafety::Safe, "drv\nhealth", "drv"},
    {"state", "", "state", "Print one compact machine-parseable state line.",
     HelpSection::Diagnostics, HelpSafety::Safe, "state", "state"},
    {"read", "rregs", "read / rregs", "Read and decode Wiper and TCON as one snapshot.",
     HelpSection::Diagnostics, HelpSafety::Safe, "read\nrregs", "read"},
    {"readwiper", "", "readwiper", "Read the volatile Wiper 0 code.",
     HelpSection::Diagnostics, HelpSafety::Safe, "readwiper", "readwiper"},
    {"readtcon", "", "readtcon", "Read and decode the volatile TCON0 register.",
     HelpSection::Diagnostics, HelpSafety::Safe, "readtcon", "readtcon"},
    {"last", "", "last", "Read using the device's current address pointer.",
     HelpSection::Diagnostics, HelpSafety::Safe, "last", "reg 0x04\nlast"},
    {"reg", "rreg", "reg / rreg <0x00|0x04>", "Read one implemented volatile register.",
     HelpSection::Diagnostics, HelpSafety::Safe, "reg <0x00|0x04>\nrreg <0x00|0x04>",
     "reg 0x00\nrreg 0x04"},
    {"dump", "", "dump", "Read the decoded snapshot and current address pointer.",
     HelpSection::Diagnostics, HelpSafety::Safe, "dump", "dump"},
    {"info", "", "info", "Print variant, tap count, nominal resistance, step, and current helpers.",
     HelpSection::Diagnostics, HelpSafety::Safe, "info", "info"},
    {"errata", "", "errata", "Print DS80000649B risks and required board-level gates.",
     HelpSection::Diagnostics, HelpSafety::Safe, "errata", "errata"},
    {"recover", "", "recover", "Refresh Wiper/TCON caches and tracked health using readback.",
     HelpSection::Diagnostics, HelpSafety::Safe, "recover", "recover\nstate"},
    {"iface_reset", "", "iface_reset", "Run the board-owned I2C reset callback; this does not reset Wiper/TCON.",
     HelpSection::Diagnostics, HelpSafety::Lifecycle, "iface_reset", "iface_reset\nrecover"},

    {"selftest", "", "selftest [safe|output]", "Run the safe diagnostic suite or an explicit restoring output test.",
     HelpSection::Tests, HelpSafety::QueryOrWrite, "selftest\nselftest safe\nselftest output",
     "selftest safe\nselftest output"},
    {"stress", "", "stress [n]", "Run bounded read-only probe/Wiper/TCON cycles.",
     HelpSection::Tests, HelpSafety::Safe, "stress [1..255]", "stress 100"},

    {"wiper", "", "wiper [code|percent|fraction]", "Read or set the volatile Wiper 0 position.",
     HelpSection::OutputControl, HelpSafety::QueryOrWrite,
     "wiper\nwiper <0..max>\nwiper percent <0..100>\nwiper fraction <0.0..1.0>",
     "wiper\nwiper 0x7f\nwiper percent 50"},
    {"frac", "pos", "frac / pos [0.0..1.0]", "Read or set normalized Wiper position.",
     HelpSection::OutputControl, HelpSafety::QueryOrWrite, "frac\nfrac <0.0..1.0>\npos <0.0..1.0>",
     "frac\nfrac 0.5"},
    {"zero", "mid max", "zero / mid / max", "Move the Wiper to a common code preset.",
     HelpSection::OutputControl, HelpSafety::OutputChanging, "zero\nmid\nmax", "mid"},
    {"inc", "dec", "inc / dec [n]", "Step Wiper 0 with endpoint clamping.",
     HelpSection::OutputControl, HelpSafety::OutputChanging, "inc [1..255]\ndec [1..255]",
     "inc\ndec 4"},
    {"tcon", "", "tcon [value|default]", "Read or write the volatile TCON0 register.",
     HelpSection::OutputControl, HelpSafety::QueryOrWrite, "tcon\ntcon <0x00..0xff>\ntcon default",
     "tcon\ntcon 0xff"},
    {"term", "terminal", "term / terminal a|w|b [on|off]", "Read or change one terminal connection bit.",
     HelpSection::OutputControl, HelpSafety::QueryOrWrite,
     "term <a|w|b>\nterm <a|w|b> <on|off>\nterminal <a|w|b> <on|off>",
     "term a\nterminal a off"},
    {"shutdown", "software-shutdown", "shutdown / software-shutdown [on|off]", "Read or set TCON software shutdown; not the SHDN pin.",
     HelpSection::OutputControl, HelpSafety::QueryOrWrite,
     "shutdown\nshutdown <on|off>\nsoftware-shutdown <on|off>", "shutdown\nshutdown on"},
    {"mode", "", "mode [pot|bw|aw|float|shutdown]", "Read or apply a named terminal connection preset.",
     HelpSection::OutputControl, HelpSafety::QueryOrWrite,
     "mode\nmode <pot|bw|aw|float|shutdown>", "mode\nmode bw"},
    {"defaults", "", "defaults", "Restore documented volatile Wiper/TCON POR/BOR defaults.",
     HelpSection::OutputControl, HelpSafety::OutputChanging, "defaults", "defaults"},

    {"stress_mix", "", "stress_mix [n]", "Run bounded Wiper/mode writes and restore the captured baseline.",
     HelpSection::Dangerous, HelpSafety::OutputChanging, "stress_mix [1..255]", "stress_mix 10"},
    {"raw", "", "raw [write <reg> <value>]", "Read the raw snapshot or write one implemented volatile register.",
     HelpSection::Dangerous, HelpSafety::Dangerous,
     "raw\nraw write <0x00|0x04> <0x00..0xff>", "raw\nraw write 0x04 0xff"},
    {"wreg", "wregs", "wreg / wregs <reg> <value>", "Raw volatile register-write aliases.",
     HelpSection::Dangerous, HelpSafety::Dangerous,
     "wreg <0x00|0x04> <0x00..0xff>\nwregs <0x00|0x04> <0x00..0xff>",
     "wreg 0x04 0xff"},
    {"gc", "", "gc arm|disarm|wiper|tcon|inc|dec", "Issue one armed General Call broadcast operation.",
     HelpSection::Dangerous, HelpSafety::Dangerous,
     "gc arm\ngc disarm\ngc wiper <code>\ngc tcon <value>\ngc inc\ngc dec",
     "errata\ngc arm\ngc inc"},
};

const char* helpSectionName(HelpSection section) {
  switch (section) {
    case HelpSection::Common: return "Common";
    case HelpSection::DeviceSelection: return "Device Selection";
    case HelpSection::Diagnostics: return "Diagnostics";
    case HelpSection::Tests: return "Tests";
    case HelpSection::OutputControl: return "Output Control";
    case HelpSection::Dangerous: return "Dangerous / Operator-Gated";
  }
  return "Commands";
}

const char* helpSafetyName(HelpSafety safety) {
  switch (safety) {
    case HelpSafety::Safe: return "SAFE / READ-ONLY";
    case HelpSafety::Lifecycle: return "LIFECYCLE / BUS CONTROL";
    case HelpSafety::QueryOrWrite: return "READ-ONLY QUERY; OUTPUT-CHANGING WHEN SET";
    case HelpSafety::OutputChanging: return "OUTPUT-CHANGING";
    case HelpSafety::Dangerous: return "DANGEROUS / RAW OR BUS-WIDE";
  }
  return "UNKNOWN";
}

const char* helpSafetyColor(HelpSafety safety) {
  switch (safety) {
    case HelpSafety::Safe: return LOG_COLOR_GREEN;
    case HelpSafety::Lifecycle: return LOG_COLOR_CYAN;
    case HelpSafety::QueryOrWrite:
    case HelpSafety::OutputChanging: return LOG_COLOR_YELLOW;
    case HelpSafety::Dangerous: return LOG_COLOR_MAGENTA;
  }
  return LOG_COLOR_RESET;
}

bool helpNameMatches(const char* names, const char* command) {
  if (names == nullptr || command == nullptr || command[0] == '\0') {
    return false;
  }
  const size_t commandLen = strlen(command);
  const char* cursor = names;
  while (*cursor != '\0') {
    while (*cursor == ' ') ++cursor;
    const char* start = cursor;
    while (*cursor != '\0' && *cursor != ' ') ++cursor;
    if (static_cast<size_t>(cursor - start) == commandLen &&
        strncmp(start, command, commandLen) == 0) {
      return true;
    }
  }
  return false;
}

const CommandHelpSpec* findCommandHelp(const char* command) {
  for (const CommandHelpSpec& spec : COMMAND_HELP) {
    if (strcmp(spec.canonical, command) == 0 || helpNameMatches(spec.aliases, command)) {
      return &spec;
    }
  }
  return nullptr;
}

void printHelpLines(const char* lines) {
  const char* cursor = lines;
  while (cursor != nullptr && *cursor != '\0') {
    const char* end = strchr(cursor, '\n');
    const size_t length = end == nullptr ? strlen(cursor) : static_cast<size_t>(end - cursor);
    LOG_SERIAL.printf("    %.*s\n", static_cast<int>(length), cursor);
    cursor = end == nullptr ? nullptr : end + 1;
  }
}

void printCommandHelp(const CommandHelpSpec& spec) {
  LOG_SERIAL.printf("%s=== Help: %s ===%s\n", LOG_COLOR_CYAN, spec.canonical, LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Section: %s\n", helpSectionName(spec.section));
  LOG_SERIAL.printf("  Aliases: %s\n", spec.aliases[0] == '\0' ? "none" : spec.aliases);
  LOG_SERIAL.printf("  Safety: %s%s%s\n", helpSafetyColor(spec.safety),
                    helpSafetyName(spec.safety), LOG_COLOR_RESET);
  LOG_SERIAL.printf("  Purpose: %s\n", spec.description);
  LOG_SERIAL.printf("  Syntax:\n");
  printHelpLines(spec.syntax);
  LOG_SERIAL.printf("  Examples:\n");
  printHelpLines(spec.examples);
}

void handleHelp(const char* args) {
  const char* p = args;
  if (noMoreArgs(p)) {
    printHelp();
    return;
  }
  char command[24] = {};
  if (!readToken(p, command, sizeof(command)) || !noMoreArgs(p)) {
    LOGE("usage: help [command]");
    return;
  }
  const CommandHelpSpec* spec = findCommandHelp(command);
  if (spec == nullptr) {
    LOGW("No help for '%s'; run `help` to list commands", command);
    return;
  }
  printCommandHelp(*spec);
}

void printHelp() {
  cli::printHelpHeader("MCP45HVX1 CLI Help");
  const HelpSection sections[] = {
      HelpSection::Common,
      HelpSection::DeviceSelection,
      HelpSection::Diagnostics,
      HelpSection::Tests,
      HelpSection::OutputControl,
      HelpSection::Dangerous,
  };
  for (HelpSection section : sections) {
    cli::printHelpSection(helpSectionName(section));
    for (const CommandHelpSpec& spec : COMMAND_HELP) {
      if (spec.section == section) {
        cli::printHelpItem(spec.synopsis, spec.description);
      }
    }
  }
  LOG_SERIAL.println("\nUse `help <command>` for syntax, safety, aliases, and examples.\n");
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
  if (noMoreArgs(p)) {
    printActiveConfigLine();
    LOG_SERIAL.println("  Standard address range: 0x3C..0x3F");
    LOG_SERIAL.println("  Alternate 0x5C..0x5F is disputed and requires addr_alt");
    return;
  }
  if (!parseU32(p, 0x7F, address) || !noMoreArgs(p)) {
    LOGE("usage: addr [0x3c..0x3f]");
    return;
  }
  if (!isPrimaryAddress(static_cast<uint8_t>(address))) {
    LOGE("address must be documented 0x3C-0x3F; use addr_alt for disputed 0x5C-0x5F");
    return;
  }
  gConfig.i2cAddress = static_cast<uint8_t>(address);
  gConfig.allowAlternateAddressRange = false;
  beginDevice();
}

void handleAlternateAddress(const char* args) {
  uint32_t address = 0;
  const char* p = args;
  if (!parseU32(p, 0x7F, address) || !noMoreArgs(p) ||
      !isAlternateAddress(static_cast<uint8_t>(address))) {
    LOGE("usage: addr_alt <0x5c..0x5f>");
    return;
  }
  LOGW("Using disputed alternate address range 0x5C-0x5F; not claimed hardware-validated");
  gConfig.i2cAddress = static_cast<uint8_t>(address);
  gConfig.allowAlternateAddressRange = true;
  beginDevice();
}

void handleResolution(const char* args) {
  MCP45HVX1::Resolution resolution = gConfig.resolution;
  const char* p = args;
  if (noMoreArgs(p)) {
    printActiveConfigLine();
    return;
  }
  if (!parseResolution(p, resolution) || !noMoreArgs(p)) {
    LOGE("usage: variant [hv31|hv51] or res <7|8>");
    return;
  }
  gConfig.resolution = resolution;
  beginDevice();
}

void handleResistance(const char* args) {
  MCP45HVX1::ResistanceOption option = gConfig.resistance;
  const char* p = args;
  if (noMoreArgs(p)) {
    printActiveConfigLine();
    return;
  }
  if (!parseResistance(p, option) || !noMoreArgs(p)) {
    LOGE("usage: rab [5000|10000|50000|100000]");
    return;
  }
  gConfig.resistance = option;
  LOGI("RAB helper set to %s", resistanceName(option));
  beginDevice();
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

  char sub[12] = {};
  const char* subStart = p;
  if (readToken(p, sub, sizeof(sub)) &&
      (strcmp(sub, "percent") == 0 || strcmp(sub, "fraction") == 0)) {
    float fraction = 0.0f;
    if (strcmp(sub, "percent") == 0) {
      float percent = 0.0f;
      if (!parseFloatRange(p, 0.0f, 100.0f, percent) || !noMoreArgs(p)) {
        LOGE("usage: wiper percent <0..100>");
        return;
      }
      fraction = percent / 100.0f;
    } else if (!parseFloat01(p, fraction) || !noMoreArgs(p)) {
      LOGE("usage: wiper fraction <0.0..1.0>");
      return;
    }
    warnOutputChanging("wiper");
    const MCP45HVX1::Status st = gDev.writeWiperFraction(fraction);
    printStatus(st);
    if (st.ok()) {
      uint8_t code = 0;
      if (gDev.readWiper(code).ok()) {
        LOG_SERIAL.printf("  Verified register wiper: 0x%02X (%u)\n",
                          code,
                          static_cast<unsigned>(code));
      }
    }
    return;
  }
  p = subStart;

  if (!parseU32(p, MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution), value) ||
      !noMoreArgs(p)) {
    LOGE("usage: wiper <0..%u>",
         static_cast<unsigned>(MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution)));
    return;
  }
  warnOutputChanging("wiper");
  const MCP45HVX1::Status st = gDev.writeWiper(static_cast<uint8_t>(value));
  printStatus(st);
  if (st.ok()) {
    readRegisters();
  }
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
  warnOutputChanging("frac");
  const MCP45HVX1::Status st = gDev.writeWiperFraction(fraction);
  printStatus(st);
  if (st.ok()) {
    readRegisters();
  }
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
  warnOutputChanging(increment ? "inc" : "dec");
  const MCP45HVX1::Status st = increment ? gDev.incrementWiper(static_cast<uint8_t>(steps))
                                         : gDev.decrementWiper(static_cast<uint8_t>(steps));
  printStatus(st);
  if (st.ok()) {
    readRegisters();
  }
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

  char token[12] = {};
  const char* tokenStart = p;
  if (readToken(p, token, sizeof(token)) && strcmp(token, "default") == 0 && noMoreArgs(p)) {
    warnOutputChanging("tcon default");
    const MCP45HVX1::Status st = gDev.writeTcon(MCP45HVX1::cmd::TCON_DEFAULT);
    printStatus(st);
    if (st.ok()) {
      readRegisters();
    }
    return;
  }
  p = tokenStart;

  uint32_t value = 0;
  if (!parseU32(p, 0xFF, value) || !noMoreArgs(p)) {
    LOGE("usage: tcon [0x00..0xff]");
    return;
  }
  warnOutputChanging("tcon");
  const MCP45HVX1::Status st = gDev.writeTcon(static_cast<uint8_t>(value));
  printStatus(st);
  if (st.ok()) {
    readRegisters();
  }
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
  warnOutputChanging("term");
  const MCP45HVX1::Status st = gDev.setTerminalEnabled(terminal, enabled);
  printStatus(st);
  if (st.ok()) {
    readRegisters();
  }
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
  warnOutputChanging("TCON software shutdown");
  const MCP45HVX1::Status st = gDev.setSoftwareShutdown(enabled);
  printStatus(st);
  if (st.ok()) {
    readRegisters();
  }
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
  warnOutputChanging("mode");
  const MCP45HVX1::Status st = gDev.setTerminalMode(mode);
  printStatus(st);
  if (st.ok()) {
    readRegisters();
  }
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
  warnDangerous("raw register write");
  const MCP45HVX1::Status st =
      gDev.writeRegister(static_cast<uint8_t>(reg), static_cast<uint8_t>(value));
  printStatus(st);
  if (st.ok()) {
    readRegisters();
  }
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

void handleColor(const char* args) {
  const char* p = args;
  if (noMoreArgs(p)) {
    LOG_SERIAL.printf("  Color: %s%s%s\n",
                      cli::yesNoColor(log_color::isEnabled()),
                      log_color::isEnabled() ? "on" : "off",
                      LOG_COLOR_RESET);
    return;
  }
  bool value = false;
  if (!parseBool(p, value) || !noMoreArgs(p)) {
    LOGE("usage: color [on|off]");
    return;
  }
  log_color::setEnabled(value);
  LOGI("Color output %s", log_color::isEnabled() ? "enabled" : "disabled");
}

void handleRaw(const char* args) {
  const char* p = args;
  char sub[8] = {};
  if (!readToken(p, sub, sizeof(sub))) {
    if (requireNoArgs("raw", p)) {
      dumpRegisters();
    }
    return;
  }
  if (strcmp(sub, "write") != 0) {
    LOGE("usage: raw write <0x00|0x04> <0x00..0xff>");
    return;
  }
  handleWriteRegister(p);
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
      LOG_SERIAL.printf("%s[DANGER]%s General Call armed for one broadcast command.\n",
                        LOG_COLOR_MAGENTA,
                        LOG_COLOR_RESET);
      LOGW("It affects every enabled device on the bus and cache will become uncertain");
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

  warnDangerous("General Call");
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
  if (st.ok()) {
    gOutputStateUncertain = true;
  }
  printStatus(st);
  LOGI("General Call disarmed");
}

void runSelfTestSafe() {
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

  LOG_SERIAL.println("=== MCP45HVX1 selftest (safe) ===");

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
  reportCheck("probe responds", st.ok(), st.ok() ? "" : health_view::errName(st.code));
  const bool probeNoTrack = gDev.totalSuccess() == succBefore &&
                            gDev.totalFailures() == failBefore &&
                            gDev.consecutiveFailures() == consBefore;
  reportCheck("probe no-health-side-effects", probeNoTrack, "");

  uint8_t wiper = 0;
  st = gDev.readWiper(wiper);
  reportCheck("read wiper", st.ok(), st.ok() ? "" : health_view::errName(st.code));
  const bool wiperReadOk = st.ok();

  uint8_t tcon = 0;
  st = gDev.readTcon(tcon);
  reportCheck("read TCON", st.ok(), st.ok() ? "" : health_view::errName(st.code));
  const bool tconReadOk = st.ok();

  const uint8_t maxCode = MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution);
  reportCheck("wiper within configured resolution", wiperReadOk && wiper <= maxCode, "");
  const MCP45HVX1::TerminalStatus terminalStatus =
      MCP45HVX1::MCP45HVX1::decodeTcon(tcon);
  reportCheck("decodeTcon mode known", tconReadOk && strcmp(modeName(terminalStatus.mode), "?") != 0, "");
  reportCheck("variant config valid",
              gConfig.resolution == MCP45HVX1::Resolution::Bits7 ||
                  gConfig.resolution == MCP45HVX1::Resolution::Bits8,
              "");
  reportCheck("address policy valid",
              isPrimaryAddress(gConfig.i2cAddress) ||
                  (gConfig.allowAlternateAddressRange && isAlternateAddress(gConfig.i2cAddress)),
              "");

  uint8_t last = 0;
  st = gDev.readLastAddress(last);
  reportCheck("readLastAddress", st.ok(), st.ok() ? "" : health_view::errName(st.code));

  MCP45HVX1::TerminalMode mode = MCP45HVX1::TerminalMode::Potentiometer;
  st = gDev.getTerminalMode(mode);
  reportCheck("getTerminalMode", st.ok(), st.ok() ? "" : health_view::errName(st.code));

  bool shutdown = false;
  st = gDev.getSoftwareShutdown(shutdown);
  reportCheck("getSoftwareShutdown", st.ok(), st.ok() ? "" : health_view::errName(st.code));

  st = gDev.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : health_view::errName(st.code));
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

MCP45HVX1::Status restoreSnapshot(const MCP45HVX1::RegisterSnapshot& snapshot);

void runOutputSelfTest() {
  struct Result {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t skip = 0;
  } result;

  auto reportCheck = [&](const char* name, bool ok, const char* note) {
    LOG_SERIAL.printf("  [%s%s%s] %s",
                      LOG_COLOR_RESULT(ok),
                      ok ? "PASS" : "FAIL",
                      LOG_COLOR_RESET,
                      name);
    if (note != nullptr && note[0] != '\0') {
      LOG_SERIAL.printf(" - %s", note);
    }
    LOG_SERIAL.println();
    if (ok) {
      result.pass++;
    } else {
      result.fail++;
    }
  };

  LOG_SERIAL.println("=== MCP45HVX1 selftest (output-changing) ===");
  warnOutputChanging("selftest output");

  MCP45HVX1::RegisterSnapshot baseline;
  MCP45HVX1::Status st = gDev.readSnapshot(baseline);
  if (!st.ok()) {
    reportCheck("capture baseline", false, health_view::errName(st.code));
    printStatus(st);
    LOG_SERIAL.printf("Selftest result: pass=%lu fail=%lu skip=%lu\n",
                      static_cast<unsigned long>(result.pass),
                      static_cast<unsigned long>(result.fail),
                      static_cast<unsigned long>(result.skip));
    return;
  }
  reportCheck("capture baseline", true, "");

  const uint8_t maxCode = MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution);
  const uint8_t testCodes[3] = {0U, static_cast<uint8_t>(maxCode / 2U), maxCode};
  for (uint8_t i = 0; i < 3U; ++i) {
    st = gDev.writeWiper(testCodes[i]);
    if (!st.ok()) {
      reportCheck("write test wiper", false, health_view::errName(st.code));
      break;
    }
    uint8_t readback = 0;
    st = gDev.readWiper(readback);
    reportCheck("verify test wiper", st.ok() && readback == testCodes[i],
                st.ok() ? "" : health_view::errName(st.code));
    if (!st.ok()) {
      break;
    }
  }

  st = gDev.writeTcon(MCP45HVX1::cmd::TCON_DEFAULT);
  reportCheck("write TCON default", st.ok(), st.ok() ? "" : health_view::errName(st.code));
  if (st.ok()) {
    uint8_t tcon = 0;
    st = gDev.readTcon(tcon);
    reportCheck("verify TCON default", st.ok() && tcon == MCP45HVX1::cmd::TCON_DEFAULT,
                st.ok() ? "" : health_view::errName(st.code));
  }

  const MCP45HVX1::Status restore = restoreSnapshot(baseline);
  reportCheck("restore baseline", restore.ok(),
              restore.ok() ? "" : health_view::errName(restore.code));
  if (!restore.ok()) {
    gOutputStateUncertain = true;
    LOG_SERIAL.printf("%s[FAIL]%s Restore failure is high severity; verify analog output before continuing.\n",
                      LOG_COLOR_RED,
                      LOG_COLOR_RESET);
  }

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

void handleSelfTest(const char* args) {
  const char* p = args;
  if (noMoreArgs(p)) {
    runSelfTestSafe();
    return;
  }
  char mode[12] = {};
  if (!readToken(p, mode, sizeof(mode)) || !noMoreArgs(p)) {
    LOGE("usage: selftest [safe|output]");
    return;
  }
  if (strcmp(mode, "safe") == 0) {
    runSelfTestSafe();
  } else if (strcmp(mode, "output") == 0) {
    runOutputSelfTest();
  } else {
    LOGE("usage: selftest [safe|output]");
  }
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

void resetStressStats(const char* testName,
                      const char* mode,
                      uint32_t iterations,
                      bool outputChanging) {
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
  gStress.outputChanging = outputChanging;
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
      LOG_SERIAL.printf(" (%s)", health_view::errName(st.code));
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
  LOG_SERIAL.printf("  Output changes: %s%s%s\n",
                    gStress.outputChanging ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    gStress.outputChanging ? "yes" : "no",
                    LOG_COLOR_RESET);
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
  if (gStress.outputChanging) {
    LOG_SERIAL.printf("  Restore: %s%s%s\n",
                      LOG_COLOR_RESULT(gStress.restoreStatus.ok()),
                      gStress.restoreStatus.ok() ? "PASS" : "FAIL",
                      LOG_COLOR_RESET);
  } else {
    LOG_SERIAL.printf("  Restore: %snot needed%s\n", LOG_COLOR_GRAY, LOG_COLOR_RESET);
  }
  if (gStress.aborted) {
    LOG_SERIAL.printf("  Result: %sFAIL%s (aborted on first error)\n",
                      LOG_COLOR_RED,
                      LOG_COLOR_RESET);
  } else {
    const bool passed = gStress.errors == 0U &&
                        (!gStress.outputChanging || gStress.restoreStatus.ok());
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

  MCP45HVX1::SettingsSnapshot finalSnapshot;
  (void)gDev.getSettings(finalSnapshot);
  LOG_SERIAL.printf("  Hardware uncertainty: %s%s%s\n",
                    hardwareStateUncertain(finalSnapshot) ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
                    hardwareStateUncertain(finalSnapshot) ? "yes" : "no",
                    LOG_COLOR_RESET);
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

  if (!mixed) {
    const uint32_t expectedAttempts = count * 3U;
    resetStressStats("stress", "read-only probe/readWiper/readTCON", count, false);
    LOGI("Starting read-only stress: %lu iterations, %lu operations",
         static_cast<unsigned long>(count),
         static_cast<unsigned long>(expectedAttempts));

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t startUs = micros();
      MCP45HVX1::Status st = gDev.probe();
      recordStressResult(st, micros() - startUs, "probe", i + 1U);
      printStressProgress(gStress.attempts, expectedAttempts, gStress.success, gStress.errors);
      if (!st.ok()) {
        gStress.aborted = true;
        break;
      }

      uint8_t value = 0;
      startUs = micros();
      st = gDev.readWiper(value);
      recordStressResult(st, micros() - startUs, "readWiper", i + 1U);
      printStressProgress(gStress.attempts, expectedAttempts, gStress.success, gStress.errors);
      if (!st.ok()) {
        gStress.aborted = true;
        break;
      }

      startUs = micros();
      st = gDev.readTcon(value);
      recordStressResult(st, micros() - startUs, "readTCON", i + 1U);
      printStressProgress(gStress.attempts, expectedAttempts, gStress.success, gStress.errors);
      if (!st.ok()) {
        gStress.aborted = true;
        break;
      }
    }

    printStressSummary(expectedAttempts);
    return;
  }

  warnOutputChanging("stress_mix");
  MCP45HVX1::RegisterSnapshot original;
  MCP45HVX1::Status st = gDev.readSnapshot(original);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  const uint32_t expectedAttempts = count * 2U;
  resetStressStats("stress_mix", "write wiper plus terminal mode", count, true);
  LOGI("Starting %s: %lu iterations, %lu operations",
       "stress_mix",
       static_cast<unsigned long>(count),
       static_cast<unsigned long>(expectedAttempts));

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t startUs = micros();
    st = gDev.writeWiper(static_cast<uint8_t>(
        (i * MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution)) /
        (count - 1U > 0U ? count - 1U : 1U)));
    recordStressResult(st, micros() - startUs, "writeWiper", i + 1U);
    printStressProgress(gStress.attempts, expectedAttempts, gStress.success, gStress.errors);
    if (!st.ok()) {
      gStress.aborted = true;
      break;
    }

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

  gStress.restoreStatus = restoreSnapshot(original);
  if (!gStress.restoreStatus.ok()) {
    gOutputStateUncertain = true;
  }
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
    handleHelp(p);
  } else if (strcmp(command, "version") == 0 || strcmp(command, "ver") == 0) {
    if (requireNoArgs(command, p)) printVersion();
  } else if (strcmp(command, "scan") == 0) {
    if (requireNoArgs(command, p)) i2c_scanner::scan(Wire);
  } else if (strcmp(command, "color") == 0) {
    handleColor(p);
  } else if (strcmp(command, "begin") == 0) {
    handleBegin(p);
  } else if (strcmp(command, "addr") == 0) {
    handleAddress(p);
  } else if (strcmp(command, "addr_alt") == 0) {
    handleAlternateAddress(p);
  } else if (strcmp(command, "res") == 0 || strcmp(command, "variant") == 0) {
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
    if (requireNoArgs(command, p)) {
      warnOutputChanging("defaults");
      const MCP45HVX1::Status st = gDev.restorePowerOnDefaults();
      printStatus(st);
      if (st.ok()) {
        readRegisters();
      }
    }
  } else if (strcmp(command, "read") == 0 || strcmp(command, "rregs") == 0) {
    if (requireNoArgs(command, p)) readRegisters();
  } else if (strcmp(command, "readwiper") == 0) {
    if (requireNoArgs(command, p)) handleWiper("");
  } else if (strcmp(command, "readtcon") == 0) {
    if (requireNoArgs(command, p)) handleTcon("");
  } else if (strcmp(command, "dump") == 0) {
    if (requireNoArgs(command, p)) dumpRegisters();
  } else if (strcmp(command, "raw") == 0) {
    handleRaw(p);
  } else if (strcmp(command, "last") == 0) {
    if (requireNoArgs(command, p)) handleLast();
  } else if (strcmp(command, "rreg") == 0 || strcmp(command, "reg") == 0) {
    handleReadRegister(p);
  } else if (strcmp(command, "wreg") == 0 || strcmp(command, "wregs") == 0) {
    handleWriteRegister(p);
  } else if (strcmp(command, "wiper") == 0) {
    handleWiper(p);
  } else if (strcmp(command, "frac") == 0 || strcmp(command, "pos") == 0) {
    handleFraction(p);
  } else if (strcmp(command, "zero") == 0) {
    if (requireNoArgs(command, p)) {
      warnOutputChanging("zero");
      const MCP45HVX1::Status st = gDev.writeWiper(0);
      printStatus(st);
      if (st.ok()) readRegisters();
    }
  } else if (strcmp(command, "mid") == 0) {
    if (requireNoArgs(command, p)) {
      warnOutputChanging("mid");
      const MCP45HVX1::Status st = gDev.writeWiperFraction(0.5f);
      printStatus(st);
      if (st.ok()) readRegisters();
    }
  } else if (strcmp(command, "max") == 0) {
    if (requireNoArgs(command, p)) {
      warnOutputChanging("max");
      const MCP45HVX1::Status st =
          gDev.writeWiper(MCP45HVX1::MCP45HVX1::maxWiperCode(gConfig.resolution));
      printStatus(st);
      if (st.ok()) readRegisters();
    }
  } else if (strcmp(command, "inc") == 0) {
    handleStep(p, true);
  } else if (strcmp(command, "dec") == 0) {
    handleStep(p, false);
  } else if (strcmp(command, "tcon") == 0) {
    handleTcon(p);
  } else if (strcmp(command, "term") == 0 || strcmp(command, "terminal") == 0) {
    handleTerm(p);
  } else if (strcmp(command, "shutdown") == 0 || strcmp(command, "software-shutdown") == 0) {
    handleShutdown(p);
  } else if (strcmp(command, "mode") == 0) {
    handleMode(p);
  } else if (strcmp(command, "cfg") == 0 || strcmp(command, "settings") == 0 ||
             strcmp(command, "detail") == 0) {
    if (requireNoArgs(command, p)) {
      printConfigSnapshot();
      if (strcmp(command, "detail") == 0) {
        printDriverHealth();
      }
    }
  } else if (strcmp(command, "drv") == 0 || strcmp(command, "health") == 0) {
    if (requireNoArgs(command, p)) printDriverHealth();
  } else if (strcmp(command, "state") == 0) {
    if (requireNoArgs(command, p)) printStateLine();
  } else if (strcmp(command, "info") == 0) {
    if (requireNoArgs(command, p)) printDeviceInfo();
  } else if (strcmp(command, "errata") == 0) {
    if (requireNoArgs(command, p)) printErrataInfo();
  } else if (strcmp(command, "selftest") == 0) {
    handleSelfTest(p);
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
