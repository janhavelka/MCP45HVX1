/**
 * @file main.cpp
 * @brief Native ESP-IDF bring-up CLI for MCP45HVX1.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "MCP45HVX1/MCP45HVX1.h"

namespace {

static constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
static constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
static constexpr uint32_t I2C_FREQ_HZ = 400000U;
static constexpr uint32_t I2C_TIMEOUT_MS = 50U;
static constexpr size_t LINE_LEN = 160U;
static constexpr uint8_t MAX_STEP_COUNT = 255U;
static constexpr uint32_t MAX_STRESS_COUNT = 255U;
static constexpr uint32_t DEFAULT_STRESS_COUNT = 8U;

struct NativeBus {
  i2c_master_bus_handle_t bus = nullptr;
  i2c_master_dev_handle_t device = nullptr;
  uint8_t deviceAddress = 0;
  i2c_master_dev_handle_t manual = nullptr;
  uint32_t freqHz = I2C_FREQ_HZ;
};

NativeBus gBus;
MCP45HVX1::MCP45HVX1 gDev;
MCP45HVX1::Config gCfg;

#ifndef MCP45HVX1_CLI_ENABLE_COLOR
#define MCP45HVX1_CLI_ENABLE_COLOR 1
#endif

#ifndef MCP45HVX1_CLI_COLOR_DEFAULT
#define MCP45HVX1_CLI_COLOR_DEFAULT 1
#endif

bool gVerbose = false;
bool gGcArmed = false;
bool gColor = MCP45HVX1_CLI_ENABLE_COLOR && MCP45HVX1_CLI_COLOR_DEFAULT;
bool gOutputStateUncertain = false;

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

int timeoutArg(uint32_t timeoutMs) {
  return timeoutMs > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeoutMs);
}

MCP45HVX1::Status mapI2c(esp_err_t err, const char* msg) {
  if (err == ESP_OK) {
    return MCP45HVX1::Status::Ok();
  }
  if (err == ESP_ERR_TIMEOUT) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_TIMEOUT, msg, err);
  }
  if (err == ESP_ERR_INVALID_ARG) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, msg, err);
  }
  if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_FOUND) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_NACK_ADDR, msg, err);
  }
  return MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_BUS, msg, err);
}

esp_err_t addDevice(NativeBus& bus, uint8_t addr, i2c_master_dev_handle_t* out) {
  if (bus.bus == nullptr || out == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = addr;
  dev.scl_speed_hz = bus.freqHz;
  return i2c_master_bus_add_device(bus.bus, &dev, out);
}

esp_err_t ensureDevice(NativeBus& bus, uint8_t addr) {
  if (bus.device != nullptr && bus.deviceAddress == addr) {
    return ESP_OK;
  }
  if (bus.device != nullptr) {
    (void)i2c_master_bus_rm_device(bus.device);
    bus.device = nullptr;
    bus.deviceAddress = 0;
  }
  esp_err_t err = addDevice(bus, addr, &bus.device);
  if (err == ESP_OK) {
    bus.deviceAddress = addr;
  }
  return err;
}

esp_err_t ensureManualDevice(NativeBus& bus) {
  if (bus.bus == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  if (bus.manual != nullptr) {
    return ESP_OK;
  }
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = I2C_DEVICE_ADDRESS_NOT_USED;
  dev.scl_speed_hz = bus.freqHz;
  return i2c_master_bus_add_device(bus.bus, &dev, &bus.manual);
}

esp_err_t transmitGeneralCall(NativeBus& bus, const uint8_t* data, size_t len,
                              uint32_t timeoutMs) {
  if (data == nullptr || len == 0U) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = ensureManualDevice(bus);
  if (err != ESP_OK) {
    return err;
  }
  uint8_t addressByte = 0x00U;
  i2c_operation_job_t ops[4] = {};
  ops[0].command = I2C_MASTER_CMD_START;
  ops[1].command = I2C_MASTER_CMD_WRITE;
  ops[1].write.ack_check = true;
  ops[1].write.data = &addressByte;
  ops[1].write.total_bytes = 1U;
  ops[2].command = I2C_MASTER_CMD_WRITE;
  ops[2].write.ack_check = true;
  ops[2].write.data = const_cast<uint8_t*>(data);
  ops[2].write.total_bytes = len;
  ops[3].command = I2C_MASTER_CMD_STOP;
  return i2c_master_execute_defined_operations(bus.manual, ops, 4U, timeoutArg(timeoutMs));
}

MCP45HVX1::Status i2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                           uint32_t timeoutMs, void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_CONFIG, "I2C bus not initialized");
  }
  if (addr == 0x00U) {
    return mapI2c(transmitGeneralCall(*bus, data, len, timeoutMs), "I2C general call failed");
  }
  esp_err_t err = ensureDevice(*bus, addr);
  if (err == ESP_OK) {
    err = i2c_master_transmit(bus->device, data, len, timeoutArg(timeoutMs));
  }
  return mapI2c(err, "I2C write failed");
}

MCP45HVX1::Status i2cWriteRead(uint8_t addr, const uint8_t* tx, size_t txLen,
                               uint8_t* rx, size_t rxLen, uint32_t timeoutMs,
                               void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_CONFIG, "I2C bus not initialized");
  }
  esp_err_t err = ensureDevice(*bus, addr);
  if (err == ESP_OK) {
    if (txLen == 0U) {
      err = i2c_master_receive(bus->device, rx, rxLen, timeoutArg(timeoutMs));
    } else {
      err = i2c_master_transmit_receive(bus->device, tx, txLen, rx, rxLen, timeoutArg(timeoutMs));
    }
  }
  return mapI2c(err, "I2C write-read failed");
}

bool initBus() {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = I2C_SDA;
  cfg.scl_io_num = I2C_SCL;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&cfg, &gBus.bus) == ESP_OK;
}

MCP45HVX1::Status resetBus(void*) {
  if (gBus.device != nullptr) {
    (void)i2c_master_bus_rm_device(gBus.device);
    gBus.device = nullptr;
    gBus.deviceAddress = 0;
  }
  if (gBus.manual != nullptr) {
    (void)i2c_master_bus_rm_device(gBus.manual);
    gBus.manual = nullptr;
  }
  if (gBus.bus != nullptr) {
    (void)i2c_del_master_bus(gBus.bus);
    gBus.bus = nullptr;
  }
  gpio_set_direction(I2C_SDA, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode(I2C_SDA, GPIO_PULLUP_ONLY);
  gpio_set_direction(I2C_SCL, GPIO_MODE_OUTPUT_OD);
  gpio_set_pull_mode(I2C_SCL, GPIO_PULLUP_ONLY);
  gpio_set_level(I2C_SDA, 1);
  for (int i = 0; i < 9; ++i) {
    gpio_set_level(I2C_SCL, 0);
    esp_rom_delay_us(5);
    gpio_set_level(I2C_SCL, 1);
    esp_rom_delay_us(5);
  }
  gpio_set_level(I2C_SDA, 0);
  esp_rom_delay_us(5);
  gpio_set_level(I2C_SCL, 1);
  esp_rom_delay_us(5);
  gpio_set_level(I2C_SDA, 1);
  esp_rom_delay_us(5);
  return initBus() ? MCP45HVX1::Status::Ok()
                   : MCP45HVX1::Status::Error(MCP45HVX1::Err::I2C_BUS, "I2C reinit failed");
}

char* trim(char* text) {
  while (*text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  char* end = text + strlen(text);
  while (end > text && isspace(static_cast<unsigned char>(end[-1]))) {
    *--end = '\0';
  }
  return text;
}

void lowerAscii(char* text) {
  if (text == nullptr) {
    return;
  }
  while (*text != '\0') {
    *text = static_cast<char>(tolower(static_cast<unsigned char>(*text)));
    ++text;
  }
}

char* splitWhitespace(char* text) {
  if (text == nullptr) {
    return nullptr;
  }
  while (*text != '\0' && !isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  if (*text == '\0') {
    return nullptr;
  }
  *text++ = '\0';
  return trim(text);
}

bool parseU32Bounded(const char* text, uint32_t maxValue, uint32_t* out) {
  if (text == nullptr || *text == '\0' || out == nullptr) {
    return false;
  }
  while (*text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  if (*text == '-' || *text == '+' || *text == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long v = strtoul(text, &end, 0);
  if (end == text || errno == ERANGE || v > static_cast<unsigned long>(maxValue)) {
    return false;
  }
  while (*end != '\0' && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }
  *out = static_cast<uint32_t>(v);
  return true;
}

bool parseU8Bounded(const char* text, uint8_t maxValue, uint8_t* out) {
  uint32_t parsed = 0;
  if (!parseU32Bounded(text, maxValue, &parsed)) {
    return false;
  }
  *out = static_cast<uint8_t>(parsed);
  return true;
}

bool parseFloatArg(const char* text, float* out) {
  if (text == nullptr || *text == '\0' || out == nullptr) {
    return false;
  }
  while (*text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  if (*text == '-' || *text == '+' || *text == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const float v = strtof(text, &end);
  if (end == text || errno == ERANGE || v != v) {
    return false;
  }
  while (*end != '\0' && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }
  *out = v;
  return true;
}

bool parseFloatRangeArg(const char* text, float minValue, float maxValue, float* out) {
  float value = 0.0f;
  if (!parseFloatArg(text, &value) || value < minValue || value > maxValue) {
    return false;
  }
  *out = value;
  return true;
}

bool parseBoolArg(const char* text, bool* out) {
  if (text == nullptr || out == nullptr) {
    return false;
  }
  if (strcmp(text, "1") == 0 || strcmp(text, "on") == 0 ||
      strcmp(text, "true") == 0 || strcmp(text, "yes") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(text, "0") == 0 || strcmp(text, "off") == 0 ||
      strcmp(text, "false") == 0 || strcmp(text, "no") == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool isBlankArg(const char* text) {
  if (text == nullptr) {
    return true;
  }
  while (*text != '\0') {
    if (!isspace(static_cast<unsigned char>(*text))) {
      return false;
    }
    ++text;
  }
  return true;
}

bool requireNoArgs(const char* args, const char* usage) {
  if (isBlankArg(args)) {
    return true;
  }
  puts(usage);
  return false;
}

const char* styleCode(const char* code) {
#if MCP45HVX1_CLI_ENABLE_COLOR
  return gColor ? code : "";
#else
  (void)code;
  return "";
#endif
}

void styleSetEnabled(bool enabled) {
#if MCP45HVX1_CLI_ENABLE_COLOR
  gColor = enabled;
#else
  (void)enabled;
  gColor = false;
#endif
}

bool styleIsEnabled() {
#if MCP45HVX1_CLI_ENABLE_COLOR
  return gColor;
#else
  return false;
#endif
}

const char* COLOR_RESET = "\x1B[0m";
const char* COLOR_RED = "\x1B[31m";
const char* COLOR_GREEN = "\x1B[32m";
const char* COLOR_YELLOW = "\x1B[33m";
const char* COLOR_MAGENTA = "\x1B[35m";
const char* COLOR_CYAN = "\x1B[36m";
const char* COLOR_GRAY = "\x1B[90m";

const char* cReset() { return styleCode(COLOR_RESET); }
const char* cRed() { return styleCode(COLOR_RED); }
const char* cGreen() { return styleCode(COLOR_GREEN); }
const char* cYellow() { return styleCode(COLOR_YELLOW); }
const char* cMagenta() { return styleCode(COLOR_MAGENTA); }
const char* cCyan() { return styleCode(COLOR_CYAN); }
const char* cGray() { return styleCode(COLOR_GRAY); }

const char* passFailColor(bool ok) {
  return ok ? cGreen() : cRed();
}

void printHeader(const char* title) {
  printf("%s=== %s ===%s\n", cCyan(), title, cReset());
}

void printWarning(const char* msg) {
  printf("%s[WARN]%s %s\n", cYellow(), cReset(), msg);
}

void printDanger(const char* msg) {
  printf("%s[DANGER]%s %s\n", cMagenta(), cReset(), msg);
}

bool isPrimaryAddress(uint8_t address) {
  return address >= MCP45HVX1::cmd::MIN_ADDRESS && address <= MCP45HVX1::cmd::MAX_ADDRESS;
}

bool isAlternateAddress(uint8_t address) {
  return address >= MCP45HVX1::cmd::ALT_MIN_ADDRESS &&
         address <= MCP45HVX1::cmd::ALT_MAX_ADDRESS;
}

uint8_t activeMaxWiperCode() {
  return MCP45HVX1::MCP45HVX1::maxWiperCode(gCfg.resolution);
}

bool parsePrimaryAddressArg(const char* text, uint8_t* out) {
  uint8_t address = 0;
  if (!parseU8Bounded(text, MCP45HVX1::cmd::MAX_ADDRESS, &address) ||
      !isPrimaryAddress(address)) {
    return false;
  }
  *out = address;
  return true;
}

bool parseAlternateAddressArg(const char* text, uint8_t* out) {
  uint8_t address = 0;
  if (!parseU8Bounded(text, MCP45HVX1::cmd::ALT_MAX_ADDRESS, &address) ||
      !isAlternateAddress(address)) {
    return false;
  }
  *out = address;
  return true;
}

bool parseAnySupportedAddressArg(const char* text, uint8_t* out, bool* alternate) {
  uint8_t address = 0;
  if (!parseU8Bounded(text, MCP45HVX1::cmd::ALT_MAX_ADDRESS, &address)) {
    return false;
  }
  if (isPrimaryAddress(address)) {
    *out = address;
    *alternate = false;
    return true;
  }
  if (isAlternateAddress(address)) {
    *out = address;
    *alternate = true;
    return true;
  }
  return false;
}

bool isImplementedRegister(uint8_t reg) {
  return reg == MCP45HVX1::cmd::REG_WIPER0 || reg == MCP45HVX1::cmd::REG_TCON0;
}

bool parseRegisterArg(const char* text, uint8_t* out) {
  uint8_t reg = 0;
  if (!parseU8Bounded(text, 0xFFU, &reg) || !isImplementedRegister(reg)) {
    return false;
  }
  *out = reg;
  return true;
}

bool parseTconArg(const char* text, uint8_t* out) {
  return parseU8Bounded(text, 0xFFU, out);
}

bool parseWiperCodeArg(const char* text, uint8_t* out) {
  return parseU8Bounded(text, activeMaxWiperCode(), out);
}

bool parseRawRegisterValueArg(uint8_t reg, const char* text, uint8_t* out) {
  if (reg == MCP45HVX1::cmd::REG_WIPER0) {
    return parseWiperCodeArg(text, out);
  }
  if (reg == MCP45HVX1::cmd::REG_TCON0) {
    return parseTconArg(text, out);
  }
  return false;
}

bool parseStepArg(const char* text, uint8_t* out) {
  if (isBlankArg(text)) {
    *out = 1U;
    return true;
  }
  uint8_t steps = 0;
  if (!parseU8Bounded(text, MAX_STEP_COUNT, &steps) || steps == 0U) {
    return false;
  }
  *out = steps;
  return true;
}

bool parseStressCountArg(const char* text, uint32_t* out) {
  if (isBlankArg(text)) {
    *out = DEFAULT_STRESS_COUNT;
    return true;
  }
  uint32_t count = 0;
  if (!parseU32Bounded(text, MAX_STRESS_COUNT, &count) || count == 0U) {
    return false;
  }
  *out = count;
  return true;
}

const char* stateName(MCP45HVX1::DriverState state) {
  switch (state) {
    case MCP45HVX1::DriverState::UNINIT: return "UNINIT";
    case MCP45HVX1::DriverState::READY: return "READY";
    case MCP45HVX1::DriverState::DEGRADED: return "DEGRADED";
    case MCP45HVX1::DriverState::OFFLINE: return "OFFLINE";
    default: return "?";
  }
}

const char* errName(MCP45HVX1::Err code) {
  switch (code) {
    case MCP45HVX1::Err::OK: return "OK";
    case MCP45HVX1::Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case MCP45HVX1::Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case MCP45HVX1::Err::I2C_ERROR: return "I2C_ERROR";
    case MCP45HVX1::Err::TIMEOUT: return "TIMEOUT";
    case MCP45HVX1::Err::INVALID_PARAM: return "INVALID_PARAM";
    case MCP45HVX1::Err::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
    case MCP45HVX1::Err::REGISTER_MISMATCH: return "REGISTER_MISMATCH";
    case MCP45HVX1::Err::BUSY: return "BUSY";
    case MCP45HVX1::Err::IN_PROGRESS: return "IN_PROGRESS";
    case MCP45HVX1::Err::UNSUPPORTED: return "UNSUPPORTED";
    case MCP45HVX1::Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
    case MCP45HVX1::Err::I2C_NACK_DATA: return "I2C_NACK_DATA";
    case MCP45HVX1::Err::I2C_TIMEOUT: return "I2C_TIMEOUT";
    case MCP45HVX1::Err::I2C_BUS: return "I2C_BUS";
    default: return "UNKNOWN";
  }
}

void printStatus(const char* op, MCP45HVX1::Status st) {
  printf("%s: %s%s%s (%s code=%u detail=%ld)\n", op, passFailColor(st.ok()),
         st.ok() ? "OK" : "FAIL", cReset(), errName(st.code),
         static_cast<unsigned>(st.code), static_cast<long>(st.detail));
  if (!st.ok() && st.msg != nullptr) {
    printf("  %s\n", st.msg);
  }
}

const char* variantName(MCP45HVX1::Resolution resolution) {
  return resolution == MCP45HVX1::Resolution::Bits7 ? "MCP45HV31" : "MCP45HV51";
}

bool hardwareStateUncertain(const MCP45HVX1::SettingsSnapshot& s) {
  return gOutputStateUncertain || s.hardwareStateUncertain || !s.cachedWiperKnown ||
         !s.cachedTconKnown || !gDev.isOnline();
}

bool parseResolutionText(const char* text, MCP45HVX1::Resolution* out) {
  if (text == nullptr || out == nullptr) {
    return false;
  }
  if (strcmp(text, "7") == 0 || strcmp(text, "hv31") == 0 ||
      strcmp(text, "mcp45hv31") == 0) {
    *out = MCP45HVX1::Resolution::Bits7;
    return true;
  }
  if (strcmp(text, "8") == 0 || strcmp(text, "hv51") == 0 ||
      strcmp(text, "mcp45hv51") == 0) {
    *out = MCP45HVX1::Resolution::Bits8;
    return true;
  }
  return false;
}

const char* resistanceName(MCP45HVX1::ResistanceOption option) {
  switch (option) {
    case MCP45HVX1::ResistanceOption::R5K: return "5k";
    case MCP45HVX1::ResistanceOption::R10K: return "10k";
    case MCP45HVX1::ResistanceOption::R50K: return "50k";
    case MCP45HVX1::ResistanceOption::R100K: return "100k";
    default: return "?";
  }
}

bool parseResistance(const char* text, MCP45HVX1::ResistanceOption* out) {
  if (strcmp(text, "5k") == 0 || strcmp(text, "5000") == 0 || strcmp(text, "502") == 0) {
    *out = MCP45HVX1::ResistanceOption::R5K;
    return true;
  }
  if (strcmp(text, "10k") == 0 || strcmp(text, "10000") == 0 || strcmp(text, "103") == 0) {
    *out = MCP45HVX1::ResistanceOption::R10K;
    return true;
  }
  if (strcmp(text, "50k") == 0 || strcmp(text, "50000") == 0 || strcmp(text, "503") == 0) {
    *out = MCP45HVX1::ResistanceOption::R50K;
    return true;
  }
  if (strcmp(text, "100k") == 0 || strcmp(text, "100000") == 0 || strcmp(text, "104") == 0) {
    *out = MCP45HVX1::ResistanceOption::R100K;
    return true;
  }
  return false;
}

const char* terminalModeName(MCP45HVX1::TerminalMode mode) {
  switch (mode) {
    case MCP45HVX1::TerminalMode::Potentiometer: return "pot";
    case MCP45HVX1::TerminalMode::RheostatBToW: return "bw";
    case MCP45HVX1::TerminalMode::RheostatAToW: return "aw";
    case MCP45HVX1::TerminalMode::WiperFloating: return "float";
    case MCP45HVX1::TerminalMode::Shutdown: return "shutdown";
    case MCP45HVX1::TerminalMode::Custom: return "custom";
    default: return "?";
  }
}

bool parseTerminalMode(const char* text, MCP45HVX1::TerminalMode* out) {
  if (strcmp(text, "pot") == 0 || strcmp(text, "potentiometer") == 0) {
    *out = MCP45HVX1::TerminalMode::Potentiometer;
    return true;
  }
  if (strcmp(text, "bw") == 0 || strcmp(text, "rheostat_bw") == 0) {
    *out = MCP45HVX1::TerminalMode::RheostatBToW;
    return true;
  }
  if (strcmp(text, "aw") == 0 || strcmp(text, "rheostat_aw") == 0) {
    *out = MCP45HVX1::TerminalMode::RheostatAToW;
    return true;
  }
  if (strcmp(text, "float") == 0 || strcmp(text, "floating") == 0) {
    *out = MCP45HVX1::TerminalMode::WiperFloating;
    return true;
  }
  if (strcmp(text, "shutdown") == 0) {
    *out = MCP45HVX1::TerminalMode::Shutdown;
    return true;
  }
  return false;
}

bool parseTerminal(const char* text, MCP45HVX1::Terminal* out) {
  if (strcmp(text, "a") == 0 || strcmp(text, "A") == 0) {
    *out = MCP45HVX1::Terminal::A;
    return true;
  }
  if (strcmp(text, "w") == 0 || strcmp(text, "W") == 0) {
    *out = MCP45HVX1::Terminal::W;
    return true;
  }
  if (strcmp(text, "b") == 0 || strcmp(text, "B") == 0) {
    *out = MCP45HVX1::Terminal::B;
    return true;
  }
  return false;
}

void printStateLine();
MCP45HVX1::Status restoreSnapshot(const MCP45HVX1::RegisterSnapshot& snapshot);

void beginDriver() {
  gCfg.i2cWrite = i2cWrite;
  gCfg.i2cWriteRead = i2cWriteRead;
  gCfg.i2cUser = &gBus;
  gCfg.busReset = resetBus;
  gCfg.nowMs = nowMs;
  gCfg.i2cTimeoutMs = I2C_TIMEOUT_MS;
  gCfg.allowGeneralCall = true;  // Diagnostic CLI has its own one-shot `gc arm` operator gate.
  const MCP45HVX1::Status st = gDev.begin(gCfg);
  printStatus("begin", st);
  if (st.ok()) {
    gOutputStateUncertain = false;
  } else if (gDev.hardwareStateUncertain()) {
    gOutputStateUncertain = true;
    printWarning("begin failed after a possible output-changing startup write.");
    printStateLine();
  }
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
    {"version", "ver", "version / ver", "Show firmware and library provenance.",
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
    {"cfg", "settings detail", "cfg / settings / detail", "Show configuration and cache state; detail also prints health.",
     HelpSection::Diagnostics, HelpSafety::Safe, "cfg\nsettings\ndetail", "detail"},
    {"drv", "health", "drv / health", "Show driver state, counters, last error, and hardware uncertainty.",
     HelpSection::Diagnostics, HelpSafety::Safe, "drv\nhealth", "drv"},
    {"state", "", "state", "Show compact machine-parseable state output.",
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
    {"info", "", "info", "Show variant, tap count, nominal resistance, step, and current helpers.",
     HelpSection::Diagnostics, HelpSafety::Safe, "info", "info"},
    {"errata", "", "errata", "Show DS80000649B risks and required board-level gates.",
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
    case HelpSafety::Safe: return cGreen();
    case HelpSafety::Lifecycle: return cCyan();
    case HelpSafety::QueryOrWrite:
    case HelpSafety::OutputChanging: return cYellow();
    case HelpSafety::Dangerous: return cMagenta();
  }
  return cReset();
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
    printf("    %.*s\n", static_cast<int>(length), cursor);
    cursor = end == nullptr ? nullptr : end + 1;
  }
}

void printCommandHelp(const CommandHelpSpec& spec) {
  printf("%s=== Help: %s ===%s\n", cCyan(), spec.canonical, cReset());
  printf("  Section: %s\n", helpSectionName(spec.section));
  printf("  Aliases: %s\n", spec.aliases[0] == '\0' ? "none" : spec.aliases);
  printf("  Safety: %s%s%s\n", helpSafetyColor(spec.safety),
         helpSafetyName(spec.safety), cReset());
  printf("  Purpose: %s\n", spec.description);
  puts("  Syntax:");
  printHelpLines(spec.syntax);
  puts("  Examples:");
  printHelpLines(spec.examples);
}

void printHelp() {
  printHeader("Native ESP-IDF MCP45HVX1 CLI");
  const HelpSection sections[] = {
      HelpSection::Common,
      HelpSection::DeviceSelection,
      HelpSection::Diagnostics,
      HelpSection::Tests,
      HelpSection::OutputControl,
      HelpSection::Dangerous,
  };
  for (HelpSection section : sections) {
    printf("\n%s[%s]%s\n", cGreen(), helpSectionName(section), cReset());
    for (const CommandHelpSpec& spec : COMMAND_HELP) {
      if (spec.section == section) {
        printf("  %s%-42s%s - %s\n", cCyan(), spec.synopsis, cReset(), spec.description);
      }
    }
  }
  puts("\nUse `help <command>` for syntax, safety, aliases, and examples.\n");
}

void handleHelp(const char* args) {
  if (isBlankArg(args)) {
    printHelp();
    return;
  }
  char local[LINE_LEN];
  snprintf(local, sizeof(local), "%s", args);
  char* command = trim(local);
  char* extra = splitWhitespace(command);
  if (extra != nullptr && !isBlankArg(extra)) {
    puts("Usage: help [command]");
    return;
  }
  const CommandHelpSpec* spec = findCommandHelp(command);
  if (spec == nullptr) {
    printf("No help for '%s'; run `help` to list commands.\n", command);
    return;
  }
  printCommandHelp(*spec);
}

void scanBus() {
  if (gBus.bus == nullptr) {
    printStatus("scan", MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_CONFIG,
                                                  "I2C bus not initialized"));
    return;
  }
  printHeader("I2C scan");
  for (uint8_t addr = 0x08U; addr <= 0x77U; ++addr) {
    if (i2c_master_probe(gBus.bus, addr, timeoutArg(I2C_TIMEOUT_MS)) == ESP_OK) {
      printf("  0x%02X\n", addr);
    }
  }
}

void printDrv() {
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  MCP45HVX1::SettingsSnapshot s{};
  (void)gDev.getSettings(s);
  printf("state=%s initialized=%d online=%d addr=0x%02X variant=%s resolution=%u rab=%s nominal=%lu uncertain=%d dirty=%d ok=%lu fail=%lu consecutive=%u\n",
         stateName(s.state), s.initialized ? 1 : 0,
         gDev.isOnline() ? 1 : 0, info.i2cAddress, variantName(info.resolution),
         static_cast<unsigned>(info.resolution), resistanceName(info.resistance),
         static_cast<unsigned long>(info.nominalResistanceOhms),
         hardwareStateUncertain(s) ? 1 : 0, gOutputStateUncertain ? 1 : 0,
         static_cast<unsigned long>(s.totalSuccess),
         static_cast<unsigned long>(s.totalFailures),
         static_cast<unsigned>(s.consecutiveFailures));
  printf("cache_known wiper=%d tcon=%d pointer=%d cached_wiper=0x%02X cached_tcon=0x%02X pointer=0x%02X\n",
         s.cachedWiperKnown ? 1 : 0, s.cachedTconKnown ? 1 : 0,
         s.addressPointerKnown ? 1 : 0, s.cachedWiper, s.cachedTcon, s.addressPointer);
  printf("last_status=%s code=%u detail=%ld message=%s\n",
         errName(s.lastError.code), static_cast<unsigned>(s.lastError.code),
         static_cast<long>(s.lastError.detail),
         s.lastError.msg != nullptr ? s.lastError.msg : "");
  printf("last_uncertain=%s code=%u detail=%ld message=%s\n",
         errName(s.hardwareStateUncertainError.code),
         static_cast<unsigned>(s.hardwareStateUncertainError.code),
         static_cast<long>(s.hardwareStateUncertainError.detail),
         s.hardwareStateUncertainError.msg != nullptr ? s.hardwareStateUncertainError.msg : "");
}

void printStateLine() {
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  MCP45HVX1::SettingsSnapshot s{};
  (void)gDev.getSettings(s);
  printf("state=%s initialized=%d online=%d uncertain=%d dirty=%d addr=0x%02X variant=%s resolution=%u rab=%s ok=%lu fail=%lu consecutive=%u last=%s detail=%ld\n",
         stateName(s.state), s.initialized ? 1 : 0, gDev.isOnline() ? 1 : 0,
         hardwareStateUncertain(s) ? 1 : 0, gOutputStateUncertain ? 1 : 0,
         info.i2cAddress, variantName(info.resolution),
         static_cast<unsigned>(info.resolution), resistanceName(info.resistance),
         static_cast<unsigned long>(s.totalSuccess),
         static_cast<unsigned long>(s.totalFailures),
         static_cast<unsigned>(s.consecutiveFailures),
         errName(s.lastError.code), static_cast<long>(s.lastError.detail));
  printf("cache_known wiper=%d tcon=%d pointer=%d cached_wiper=0x%02X cached_tcon=0x%02X pointer=0x%02X core_uncertain=%d last_uncertain=%s detail=%ld\n",
         s.cachedWiperKnown ? 1 : 0, s.cachedTconKnown ? 1 : 0,
         s.addressPointerKnown ? 1 : 0, s.cachedWiper, s.cachedTcon, s.addressPointer,
         s.hardwareStateUncertain ? 1 : 0, errName(s.hardwareStateUncertainError.code),
         static_cast<long>(s.hardwareStateUncertainError.detail));
}

void printDriverHealth() {
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  MCP45HVX1::SettingsSnapshot s{};
  (void)gDev.getSettings(s);
  printf("health state=%s initialized=%d online=%d addr=0x%02X variant=%s resolution=%u rab=%s nominal=%lu\n",
         stateName(s.state), s.initialized ? 1 : 0, gDev.isOnline() ? 1 : 0,
         info.i2cAddress, variantName(info.resolution), static_cast<unsigned>(info.resolution),
         resistanceName(info.resistance), static_cast<unsigned long>(info.nominalResistanceOhms));
  printf("  ok=%lu fail=%lu consecutive=%u threshold=%u\n",
         static_cast<unsigned long>(s.totalSuccess),
         static_cast<unsigned long>(s.totalFailures),
         static_cast<unsigned>(s.consecutiveFailures),
         static_cast<unsigned>(s.config.offlineThreshold));
  printf("  last_ok_ms=%lu last_error_ms=%lu last_status=%s last_code=%u last_detail=%ld\n",
         static_cast<unsigned long>(s.lastOkMs),
         static_cast<unsigned long>(s.lastErrorMs),
         errName(s.lastError.code),
         static_cast<unsigned>(s.lastError.code),
         static_cast<long>(s.lastError.detail));
  if (s.lastError.msg != nullptr && s.lastError.msg[0] != '\0') {
    printf("  last_message=%s\n", s.lastError.msg);
  }
  printf("  uncertainty aggregate=%d core=%d last_uncertain=%s code=%u detail=%ld\n",
         hardwareStateUncertain(s) ? 1 : 0,
         s.hardwareStateUncertain ? 1 : 0,
         errName(s.hardwareStateUncertainError.code),
         static_cast<unsigned>(s.hardwareStateUncertainError.code),
         static_cast<long>(s.hardwareStateUncertainError.detail));
  if (s.hardwareStateUncertainError.msg != nullptr &&
      s.hardwareStateUncertainError.msg[0] != '\0') {
    printf("  last_uncertain_message=%s\n", s.hardwareStateUncertainError.msg);
  }
  printf("  cache wiper=%s 0x%02X tcon=%s 0x%02X pointer=%s 0x%02X\n",
         s.cachedWiperKnown ? "known" : "unknown", s.cachedWiper,
         s.cachedTconKnown ? "known" : "unknown", s.cachedTcon,
         s.addressPointerKnown ? "known" : "unknown", s.addressPointer);
}

void printInfo() {
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  printf("addr=0x%02X variant=%s resolution=%u rab=%s nominal=%lu step=%.3f max_code=0x%02X default=0x%02X alt_range=%d\n",
         info.i2cAddress, variantName(info.resolution), static_cast<unsigned>(info.resolution),
         resistanceName(info.resistance), static_cast<unsigned long>(info.nominalResistanceOhms),
         info.nominalStepOhms, info.maxWiperCode, info.defaultWiperCode,
         info.usingAlternateAddressRange ? 1 : 0);
}

void printConfigSnapshot() {
  MCP45HVX1::SettingsSnapshot s{};
  (void)gDev.getSettings(s);
  printInfo();
  printf("timeout_ms=%lu alternate_allowed=%d general_call_allowed=%d require_por=%d require_msb_zero=%d color=%d verbose=%d\n",
         static_cast<unsigned long>(s.config.i2cTimeoutMs),
         s.config.allowAlternateAddressRange ? 1 : 0,
         s.config.allowGeneralCall ? 1 : 0,
         s.config.requirePowerOnDefaults ? 1 : 0,
         s.config.requireReadMsbZero ? 1 : 0,
         styleIsEnabled() ? 1 : 0,
         gVerbose ? 1 : 0);
  printf("initial_wiper_write=%d initial_wiper=0x%02X initial_tcon_write=%d initial_tcon=0x%02X\n",
         s.config.writeInitialWiper ? 1 : 0,
         s.config.initialWiperCode,
         s.config.writeInitialTcon ? 1 : 0,
         s.config.initialTcon);
  printf("cache_known wiper=%d tcon=%d uncertain=%d core_uncertain=%d\n",
         s.cachedWiperKnown ? 1 : 0,
         s.cachedTconKnown ? 1 : 0,
         hardwareStateUncertain(s) ? 1 : 0,
         s.hardwareStateUncertain ? 1 : 0);
  printf("last_status=%s code=%u detail=%ld message=%s\n",
         errName(s.lastError.code), static_cast<unsigned>(s.lastError.code),
         static_cast<long>(s.lastError.detail),
         s.lastError.msg != nullptr ? s.lastError.msg : "");
  printf("last_uncertain=%s code=%u detail=%ld message=%s dirty=%d\n",
         errName(s.hardwareStateUncertainError.code),
         static_cast<unsigned>(s.hardwareStateUncertainError.code),
         static_cast<long>(s.hardwareStateUncertainError.detail),
         s.hardwareStateUncertainError.msg != nullptr ? s.hardwareStateUncertainError.msg : "",
         gOutputStateUncertain ? 1 : 0);
}

void printErrata() {
  const MCP45HVX1::SiliconErrataInfo errata = MCP45HVX1::MCP45HVX1::siliconErrataInfo();
  printf("%s rev %s: %s\n", errata.documentNumber, errata.documentRevision,
         errata.documentTitle);
  printf("shared_bus_hazard=%d general_call_decode_hazard=%d isolated_bus_workaround=%d\n",
         errata.sharedBusI2cHazard ? 1 : 0,
         errata.generalCallAddressDecodeHazard ? 1 : 0,
         errata.uniqueBusWorkaroundForAffectedSilicon ? 1 : 0);
  printf("release_gate=%d shared_bus_risk_acceptance=%d gc_isolated_bus_evidence=%d\n",
         errata.productionReleaseGateRequired ? 1 : 0,
         errata.sharedBusRiskAcceptanceRequired ? 1 : 0,
         errata.generalCallRequiresIsolatedBusEvidence ? 1 : 0);
  puts(errata.markingSummary);
}

void readRegisters(bool includeLastAddress) {
  MCP45HVX1::RegisterSnapshot s;
  MCP45HVX1::Status st = gDev.readSnapshot(s);
  printStatus("read", st);
  if (st.ok()) {
    gOutputStateUncertain = false;
    const MCP45HVX1::TerminalStatus terminal = MCP45HVX1::MCP45HVX1::decodeTcon(s.tcon);
    printf("wiper=0x%02X tcon=0x%02X mode=%s shutdown=%d A=%d W=%d B=%d\n",
           s.wiper, s.tcon, terminalModeName(terminal.mode),
           terminal.softwareShutdown ? 1 : 0, terminal.terminalA ? 1 : 0,
           terminal.terminalW ? 1 : 0, terminal.terminalB ? 1 : 0);
  }
  if (includeLastAddress) {
    uint8_t last = 0;
    st = gDev.readLastAddress(last);
    printStatus("last", st);
    if (st.ok()) {
      printf("last=0x%02X\n", last);
    }
  }
}

void readWiperCommand(const char* label) {
  uint8_t code = 0;
  MCP45HVX1::Status st = gDev.readWiper(code);
  printStatus(label, st);
  if (st.ok()) {
    printf("wiper=0x%02X\n", code);
  }
}

void readTconCommand(const char* label) {
  uint8_t tcon = 0;
  MCP45HVX1::Status st = gDev.readTcon(tcon);
  printStatus(label, st);
  if (st.ok()) {
    const MCP45HVX1::TerminalStatus terminal = MCP45HVX1::MCP45HVX1::decodeTcon(tcon);
    printf("tcon=0x%02X mode=%s shutdown=%d A=%d W=%d B=%d\n",
           tcon, terminalModeName(terminal.mode), terminal.softwareShutdown ? 1 : 0,
           terminal.terminalA ? 1 : 0, terminal.terminalW ? 1 : 0,
           terminal.terminalB ? 1 : 0);
  }
}

void runStress(uint32_t count, bool mixed) {
  if (count == 0U) {
    puts("stress count must be > 0");
    return;
  }
  uint32_t ok = 0;
  uint32_t fail = 0;
  if (mixed) {
    printWarning("stress_mix changes Wiper/TCON repeatedly, then restores the starting snapshot.");
    MCP45HVX1::RegisterSnapshot original;
    MCP45HVX1::Status st = gDev.readSnapshot(original);
    if (!st.ok()) {
      printStatus("stress_mix baseline", st);
      return;
    }
    const uint8_t maxCode = activeMaxWiperCode();
    for (uint32_t i = 0; i < count; ++i) {
      const uint8_t code =
          count > 1U ? static_cast<uint8_t>((i * maxCode) / (count - 1U))
                     : static_cast<uint8_t>(maxCode / 2U);
      st = gDev.writeWiper(code);
      if (st.ok()) {
        ++ok;
      } else {
        ++fail;
        if (gVerbose) {
          printStatus("stress_mix writeWiper", st);
        }
        break;
      }
      const MCP45HVX1::TerminalMode mode =
          (i % 3U) == 0U ? MCP45HVX1::TerminalMode::Potentiometer
                         : ((i % 3U) == 1U ? MCP45HVX1::TerminalMode::RheostatBToW
                                           : MCP45HVX1::TerminalMode::RheostatAToW);
      st = gDev.setTerminalMode(mode);
      if (st.ok()) {
        ++ok;
      } else {
        ++fail;
        if (gVerbose) {
          printStatus("stress_mix setTerminalMode", st);
        }
        break;
      }
    }
    const MCP45HVX1::Status restore = restoreSnapshot(original);
    printf("stress_mix: ok=%s%lu%s fail=%s%lu%s restore=%s%s%s\n",
           cGreen(), static_cast<unsigned long>(ok), cReset(),
           fail == 0U ? cGreen() : cRed(), static_cast<unsigned long>(fail), cReset(),
           passFailColor(restore.ok()), restore.ok() ? "OK" : "FAIL", cReset());
    MCP45HVX1::SettingsSnapshot s{};
    (void)gDev.getSettings(s);
    printf("stress_mix_uncertain=%d core_uncertain=%d last_uncertain=%s code=%u\n",
           hardwareStateUncertain(s) ? 1 : 0,
           s.hardwareStateUncertain ? 1 : 0,
           errName(s.hardwareStateUncertainError.code),
           static_cast<unsigned>(s.hardwareStateUncertainError.code));
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    MCP45HVX1::Status st = gDev.probe();
    if (st.ok()) {
      ++ok;
    } else {
      ++fail;
      if (gVerbose) {
        printStatus("stress probe", st);
      }
      break;
    }
    uint8_t value = 0;
    st = gDev.readWiper(value);
    if (st.ok()) {
      ++ok;
    } else {
      ++fail;
      if (gVerbose) {
        printStatus("stress readWiper", st);
      }
      break;
    }
    st = gDev.readTcon(value);
    if (st.ok()) {
      ++ok;
    } else {
      ++fail;
      if (gVerbose) {
        printStatus("stress readTcon", st);
      }
      break;
    }
  }
  printf("stress: ok=%lu fail=%lu\n", static_cast<unsigned long>(ok),
         static_cast<unsigned long>(fail));
  MCP45HVX1::SettingsSnapshot s{};
  (void)gDev.getSettings(s);
  printf("stress_uncertain=%d core_uncertain=%d\n",
         hardwareStateUncertain(s) ? 1 : 0,
         s.hardwareStateUncertain ? 1 : 0);
}

void printSelftestResult(uint32_t pass, uint32_t fail, uint32_t skip) {
  printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
         pass > 0U ? cGreen() : cGray(), static_cast<unsigned long>(pass), cReset(),
         fail == 0U ? cGreen() : cRed(), static_cast<unsigned long>(fail), cReset(),
         skip == 0U ? cGreen() : cYellow(), static_cast<unsigned long>(skip), cReset());
}

void reportSelftest(const char* name, const char* tag, const char* color,
                    const char* note, uint32_t* pass, uint32_t* fail, uint32_t* skip) {
  printf("  [%s%s%s] %s", color, tag, cReset(), name);
  if (note != nullptr && note[0] != '\0') {
    printf(" - %s", note);
  }
  puts("");
  if (strcmp(tag, "PASS") == 0) {
    ++(*pass);
  } else if (strcmp(tag, "SKIP") == 0) {
    ++(*skip);
  } else {
    ++(*fail);
  }
}

void reportSelftestCheck(const char* name, bool ok, const char* note,
                         uint32_t* pass, uint32_t* fail, uint32_t* skip) {
  reportSelftest(name, ok ? "PASS" : "FAIL", passFailColor(ok), note, pass, fail, skip);
}

void reportSelftestSkip(const char* name, const char* note,
                        uint32_t* pass, uint32_t* fail, uint32_t* skip) {
  reportSelftest(name, "SKIP", cYellow(), note, pass, fail, skip);
}

MCP45HVX1::Status restoreSnapshot(const MCP45HVX1::RegisterSnapshot& snapshot) {
  MCP45HVX1::Status first = MCP45HVX1::Status::Ok();
  MCP45HVX1::Status st = gDev.writeWiper(snapshot.wiper);
  if (!st.ok()) {
    first = st;
    printWarning("restore wiper failed");
    printStatus("restore wiper", st);
  }
  st = gDev.writeTcon(snapshot.tcon);
  if (!st.ok()) {
    if (first.ok()) {
      first = st;
    }
    printWarning("restore TCON failed");
    printStatus("restore tcon", st);
  }
  if (!first.ok()) {
    gOutputStateUncertain = true;
    return first;
  }

  MCP45HVX1::RegisterSnapshot verified{};
  st = gDev.readSnapshot(verified);
  if (!st.ok()) {
    gOutputStateUncertain = true;
    return st;
  }
  if (verified.wiper != snapshot.wiper || verified.tcon != snapshot.tcon) {
    gOutputStateUncertain = true;
    return MCP45HVX1::Status::Error(MCP45HVX1::Err::REGISTER_MISMATCH,
                                    "restore readback mismatch");
  }
  gOutputStateUncertain = false;
  return MCP45HVX1::Status::Ok();
}

void runSelftestSafe() {
  uint32_t pass = 0;
  uint32_t fail = 0;
  uint32_t skip = 0;
  printHeader("MCP45HVX1 selftest (safe)");

  const uint32_t succBefore = gDev.totalSuccess();
  const uint32_t failBefore = gDev.totalFailures();
  const uint8_t consBefore = gDev.consecutiveFailures();

  MCP45HVX1::Status st = gDev.probe();
  if (st.code == MCP45HVX1::Err::NOT_INITIALIZED) {
    reportSelftestSkip("probe responds", "driver not initialized", &pass, &fail, &skip);
    reportSelftestSkip("remaining checks", "selftest aborted", &pass, &fail, &skip);
    printSelftestResult(pass, fail, skip);
    return;
  }
  reportSelftestCheck("probe responds", st.ok(), st.ok() ? "" : errName(st.code),
                      &pass, &fail, &skip);
  const bool probeNoTrack = gDev.totalSuccess() == succBefore &&
                            gDev.totalFailures() == failBefore &&
                            gDev.consecutiveFailures() == consBefore;
  reportSelftestCheck("probe no-health-side-effects", probeNoTrack, "",
                      &pass, &fail, &skip);

  uint8_t wiper = 0;
  st = gDev.readWiper(wiper);
  reportSelftestCheck("read wiper", st.ok(), st.ok() ? "" : errName(st.code),
                      &pass, &fail, &skip);
  const bool wiperReadOk = st.ok();

  uint8_t tcon = 0;
  st = gDev.readTcon(tcon);
  reportSelftestCheck("read TCON", st.ok(), st.ok() ? "" : errName(st.code),
                      &pass, &fail, &skip);
  const bool tconReadOk = st.ok();

  const uint8_t maxCode = activeMaxWiperCode();
  reportSelftestCheck("wiper within configured resolution", wiperReadOk && wiper <= maxCode,
                      "", &pass, &fail, &skip);
  const MCP45HVX1::TerminalStatus terminalStatus =
      MCP45HVX1::MCP45HVX1::decodeTcon(tcon);
  reportSelftestCheck("decodeTcon mode known",
                      tconReadOk && strcmp(terminalModeName(terminalStatus.mode), "?") != 0,
                      "", &pass, &fail, &skip);
  reportSelftestCheck("variant config valid",
                      gCfg.resolution == MCP45HVX1::Resolution::Bits7 ||
                          gCfg.resolution == MCP45HVX1::Resolution::Bits8,
                      "", &pass, &fail, &skip);
  reportSelftestCheck("address policy valid",
                      isPrimaryAddress(gCfg.i2cAddress) ||
                          (gCfg.allowAlternateAddressRange &&
                           isAlternateAddress(gCfg.i2cAddress)),
                      "", &pass, &fail, &skip);

  uint8_t last = 0;
  st = gDev.readLastAddress(last);
  reportSelftestCheck("readLastAddress", st.ok(), st.ok() ? "" : errName(st.code),
                      &pass, &fail, &skip);

  MCP45HVX1::TerminalMode mode = MCP45HVX1::TerminalMode::Potentiometer;
  st = gDev.getTerminalMode(mode);
  reportSelftestCheck("getTerminalMode", st.ok(), st.ok() ? "" : errName(st.code),
                      &pass, &fail, &skip);

  bool shutdown = false;
  st = gDev.getSoftwareShutdown(shutdown);
  reportSelftestCheck("getSoftwareShutdown", st.ok(), st.ok() ? "" : errName(st.code),
                      &pass, &fail, &skip);

  st = gDev.recover();
  reportSelftestCheck("recover", st.ok(), st.ok() ? "" : errName(st.code),
                      &pass, &fail, &skip);
  reportSelftestCheck("isOnline", gDev.isOnline(), "", &pass, &fail, &skip);

  printSelftestResult(pass, fail, skip);
}

void runOutputSelftest() {
  uint32_t pass = 0;
  uint32_t fail = 0;
  uint32_t skip = 0;
  printHeader("MCP45HVX1 selftest (output-changing)");
  printWarning("selftest output writes Wiper/TCON, verifies readback, then restores baseline.");
  printWarning("Confirm that the connected analog/high-voltage circuit is safe.");

  MCP45HVX1::RegisterSnapshot baseline{};
  MCP45HVX1::Status st = gDev.readSnapshot(baseline);
  if (!st.ok()) {
    reportSelftestCheck("capture baseline", false, errName(st.code), &pass, &fail, &skip);
    printStatus("baseline", st);
    printSelftestResult(pass, fail, skip);
    return;
  }
  reportSelftestCheck("capture baseline", true, "", &pass, &fail, &skip);

  const uint8_t maxCode = activeMaxWiperCode();
  const uint8_t testCodes[3] = {0U, static_cast<uint8_t>(maxCode / 2U), maxCode};
  for (uint8_t i = 0; i < 3U; ++i) {
    st = gDev.writeWiper(testCodes[i]);
    reportSelftestCheck("write test wiper", st.ok(), st.ok() ? "" : errName(st.code),
                        &pass, &fail, &skip);
    if (!st.ok()) {
      break;
    }
    uint8_t readback = 0;
    st = gDev.readWiper(readback);
    reportSelftestCheck("verify test wiper", st.ok() && readback == testCodes[i],
                        st.ok() ? "" : errName(st.code), &pass, &fail, &skip);
    if (!st.ok()) {
      break;
    }
  }

  st = gDev.writeTcon(MCP45HVX1::cmd::TCON_DEFAULT);
  reportSelftestCheck("write TCON default", st.ok(), st.ok() ? "" : errName(st.code),
                      &pass, &fail, &skip);
  if (st.ok()) {
    uint8_t tcon = 0;
    st = gDev.readTcon(tcon);
    reportSelftestCheck("verify TCON default",
                        st.ok() && tcon == MCP45HVX1::cmd::TCON_DEFAULT,
                        st.ok() ? "" : errName(st.code), &pass, &fail, &skip);
  }

  const MCP45HVX1::Status restore = restoreSnapshot(baseline);
  reportSelftestCheck("restore baseline", restore.ok(), restore.ok() ? "" : errName(restore.code),
                      &pass, &fail, &skip);
  if (!restore.ok()) {
    printWarning("restore failure is high severity; verify analog output before continuing.");
  }

  printSelftestResult(pass, fail, skip);
}

void handleGc(const char* args) {
  char local[LINE_LEN];
  snprintf(local, sizeof(local), "%s", args == nullptr ? "" : args);
  char* sub = trim(local);
  lowerAscii(sub);
  char* valueText = splitWhitespace(sub);
  if (strcmp(sub, "arm") == 0) {
    if (!isBlankArg(valueText)) {
      puts("Usage: gc arm");
      gGcArmed = false;
      return;
    }
    gGcArmed = true;
    printDanger("General Call armed for one broadcast command.");
    printWarning("It affects every enabled device on the bus and cache will become uncertain.");
    printWarning("DS80000649B: affected silicon has General Call decode anomalies.");
    printWarning("Output-changing General Call commands require isolated-bus evidence.");
    return;
  }
  if (strcmp(sub, "disarm") == 0) {
    gGcArmed = false;
    if (isBlankArg(valueText)) {
      puts("General Call disarmed.");
    } else {
      puts("Usage: gc disarm");
    }
    return;
  }
  if (!gGcArmed) {
    puts("General Call is broadcast; run 'gc arm' first.");
    return;
  }
  gGcArmed = false;
  MCP45HVX1::Status st = MCP45HVX1::Status::Ok();
  if (strcmp(sub, "inc") == 0) {
    if (!isBlankArg(valueText)) {
      puts("Usage: gc inc");
      return;
    }
    printDanger("General Call broadcast may affect every enabled device on the bus.");
    st = gDev.generalCallIncrementWiper();
    printStatus("gc inc", st);
  } else if (strcmp(sub, "dec") == 0) {
    if (!isBlankArg(valueText)) {
      puts("Usage: gc dec");
      return;
    }
    printDanger("General Call broadcast may affect every enabled device on the bus.");
    st = gDev.generalCallDecrementWiper();
    printStatus("gc dec", st);
  } else if (strcmp(sub, "wiper") == 0) {
    uint8_t v = 0;
    printDanger("General Call broadcast may affect every enabled device on the bus.");
    st = parseWiperCodeArg(valueText, &v)
             ? gDev.generalCallWriteWiper(v)
             : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "bad code");
    printStatus("gc wiper", st);
  } else if (strcmp(sub, "tcon") == 0) {
    uint8_t v = 0;
    printDanger("General Call broadcast may affect every enabled device on the bus.");
    st = parseTconArg(valueText, &v)
             ? gDev.generalCallWriteTcon(v)
             : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "bad value");
    printStatus("gc tcon", st);
  } else {
    puts("Usage: gc arm|disarm|wiper <code>|tcon <value>|inc|dec");
    return;
  }
  if (st.ok()) {
    gOutputStateUncertain = true;
  }
}

void handleCommand(char* line) {
  char* cmd = trim(line);
  lowerAscii(cmd);
  char* args = splitWhitespace(cmd);
  if (args == nullptr) {
    args = cmd + strlen(cmd);
  }

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    handleHelp(args);
  } else if (strcmp(cmd, "version") == 0 || strcmp(cmd, "ver") == 0) {
    printf("MCP45HVX1 %s %s\n", MCP45HVX1::VERSION, MCP45HVX1::VERSION_FULL);
  } else if (strcmp(cmd, "scan") == 0) {
    scanBus();
  } else if (strcmp(cmd, "color") == 0) {
    if (*args == '\0') {
      printf("color=%d\n", styleIsEnabled() ? 1 : 0);
    } else {
      bool enabled = false;
      if (parseBoolArg(args, &enabled)) {
        styleSetEnabled(enabled);
        printf("color=%d\n", styleIsEnabled() ? 1 : 0);
      } else {
        puts("Usage: color [on|off]");
      }
    }
  } else if (strcmp(cmd, "begin") == 0) {
    char local[LINE_LEN];
    snprintf(local, sizeof(local), "%s", args);
    char* first = trim(local);
    char* second = splitWhitespace(first);
    uint8_t address = 0;
    bool alternate = false;
    MCP45HVX1::Resolution resolution = gCfg.resolution;
    if (*first != '\0') {
      if (parseResolutionText(first, &resolution)) {
        gCfg.resolution = resolution;
      } else if (parseAnySupportedAddressArg(first, &address, &alternate)) {
        gCfg.i2cAddress = address;
        gCfg.allowAlternateAddressRange = alternate;
        if (alternate) {
          printWarning("using disputed alternate address range 0x5c..0x5f.");
        }
      } else {
        puts("Usage: begin [addr] [7|8]");
        return;
      }
    }
    if (second != nullptr && *second != '\0') {
      if (!parseResolutionText(second, &resolution)) {
        puts("Usage: begin [addr] [7|8]");
        return;
      }
      gCfg.resolution = resolution;
    }
    beginDriver();
  } else if (strcmp(cmd, "addr") == 0) {
    uint8_t address = 0;
    if (*args == '\0') {
      printInfo();
    } else if (parsePrimaryAddressArg(args, &address)) {
      gCfg.i2cAddress = address;
      gCfg.allowAlternateAddressRange = false;
      beginDriver();
    } else {
      puts("Usage: addr <0x3c..0x3f>; use addr_alt for 0x5c..0x5f");
    }
  } else if (strcmp(cmd, "addr_alt") == 0) {
    uint8_t address = 0;
    if (parseAlternateAddressArg(args, &address)) {
      printWarning("using disputed alternate address range 0x5c..0x5f.");
      gCfg.i2cAddress = address;
      gCfg.allowAlternateAddressRange = true;
      beginDriver();
    } else {
      puts("Usage: addr_alt <0x5c..0x5f>");
    }
  } else if (strcmp(cmd, "res") == 0 || strcmp(cmd, "variant") == 0) {
    MCP45HVX1::Resolution resolution = gCfg.resolution;
    if (*args == '\0') {
      printInfo();
    } else if (parseResolutionText(args, &resolution)) {
      gCfg.resolution = resolution;
      beginDriver();
    } else {
      puts("Usage: variant [hv31|hv51] or res <7|8>");
    }
  } else if (strcmp(cmd, "probe") == 0) {
    if (!requireNoArgs(args, "Usage: probe")) {
      return;
    }
    printStatus("probe", gDev.probe());
  } else if (strcmp(cmd, "recover") == 0) {
    if (!requireNoArgs(args, "Usage: recover")) {
      return;
    }
    const MCP45HVX1::Status st = gDev.recover();
    printStatus("recover", st);
    if (st.ok()) {
      gOutputStateUncertain = false;
    }
  } else if (strcmp(cmd, "iface_reset") == 0) {
    if (!requireNoArgs(args, "Usage: iface_reset")) {
      return;
    }
    printStatus("iface_reset", gDev.resetI2cState());
  } else if (strcmp(cmd, "defaults") == 0) {
    if (!requireNoArgs(args, "Usage: defaults")) {
      return;
    }
    printWarning("defaults writes Wiper and TCON volatile registers.");
    const MCP45HVX1::Status st = gDev.restorePowerOnDefaults();
    printStatus("defaults", st);
    if (st.ok()) {
      readRegisters(false);
    }
  } else if (strcmp(cmd, "cfg") == 0 || strcmp(cmd, "settings") == 0) {
    if (!requireNoArgs(args, "Usage: cfg | settings")) {
      return;
    }
    printConfigSnapshot();
  } else if (strcmp(cmd, "detail") == 0) {
    if (!requireNoArgs(args, "Usage: detail")) {
      return;
    }
    printConfigSnapshot();
    printDriverHealth();
  } else if (strcmp(cmd, "drv") == 0 || strcmp(cmd, "health") == 0) {
    if (!requireNoArgs(args, "Usage: drv | health")) {
      return;
    }
    printDriverHealth();
  } else if (strcmp(cmd, "state") == 0) {
    if (!requireNoArgs(args, "Usage: state")) {
      return;
    }
    printStateLine();
  } else if (strcmp(cmd, "read") == 0 || strcmp(cmd, "rregs") == 0) {
    if (!requireNoArgs(args, "Usage: read | rregs")) {
      return;
    }
    readRegisters(false);
  } else if (strcmp(cmd, "dump") == 0) {
    if (!requireNoArgs(args, "Usage: dump")) {
      return;
    }
    readRegisters(true);
  } else if (strcmp(cmd, "readwiper") == 0) {
    if (!requireNoArgs(args, "Usage: readwiper")) {
      return;
    }
    readWiperCommand("readwiper");
  } else if (strcmp(cmd, "readtcon") == 0) {
    if (!requireNoArgs(args, "Usage: readtcon")) {
      return;
    }
    readTconCommand("readtcon");
  } else if (strcmp(cmd, "last") == 0) {
    if (!requireNoArgs(args, "Usage: last")) {
      return;
    }
    uint8_t v = 0;
    MCP45HVX1::Status st = gDev.readLastAddress(v);
    printStatus("last", st);
    if (st.ok()) {
      printf("last=0x%02X\n", v);
    }
  } else if (strcmp(cmd, "rreg") == 0 || strcmp(cmd, "reg") == 0) {
    uint8_t reg = 0;
    uint8_t value = 0;
    MCP45HVX1::Status st = parseRegisterArg(args, &reg)
                                ? gDev.readRegister(reg, value)
                                : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "bad reg");
    printStatus("rreg", st);
    if (st.ok()) {
      printf("reg=0x%02X value=0x%02X\n", static_cast<unsigned>(reg), value);
    }
  } else if (strcmp(cmd, "wiper") == 0) {
    char local[LINE_LEN];
    snprintf(local, sizeof(local), "%s", args);
    char* sub = trim(local);
    char* valueText = splitWhitespace(sub);
    if (strcmp(sub, "percent") == 0) {
      float percent = 0.0f;
      if (parseFloatRangeArg(valueText, 0.0f, 100.0f, &percent)) {
        printWarning("wiper percent changes the analog output state.");
        const MCP45HVX1::Status st = gDev.writeWiperFraction(percent / 100.0f);
        printStatus("wiper percent", st);
        if (st.ok()) {
          readWiperCommand("readwiper");
        }
      } else {
        puts("Usage: wiper percent <0..100>");
      }
    } else if (strcmp(sub, "fraction") == 0) {
      float fraction = 0.0f;
      if (parseFloatRangeArg(valueText, 0.0f, 1.0f, &fraction)) {
        printWarning("wiper fraction changes the analog output state.");
        const MCP45HVX1::Status st = gDev.writeWiperFraction(fraction);
        printStatus("wiper fraction", st);
        if (st.ok()) {
          readWiperCommand("readwiper");
        }
      } else {
        puts("Usage: wiper fraction <0.0..1.0>");
      }
    } else if (*args != '\0') {
      uint8_t v = 0;
      if (!parseWiperCodeArg(args, &v)) {
        puts("Usage: wiper [0..max]");
        return;
      }
      printWarning("wiper changes the analog output state.");
      const MCP45HVX1::Status st = gDev.writeWiper(v);
      printStatus("wiper", st);
      if (st.ok()) {
        readWiperCommand("readwiper");
      }
    } else {
      readWiperCommand("wiper");
    }
  } else if (strcmp(cmd, "zero") == 0 || strcmp(cmd, "mid") == 0 || strcmp(cmd, "max") == 0) {
    const uint8_t max = MCP45HVX1::MCP45HVX1::maxWiperCode(gCfg.resolution);
    const uint8_t v = (strcmp(cmd, "zero") == 0) ? 0U : (strcmp(cmd, "mid") == 0 ? max / 2U : max);
    printWarning("wiper preset changes the analog output state.");
    const MCP45HVX1::Status st = gDev.writeWiper(v);
    printStatus(cmd, st);
    if (st.ok()) {
      readWiperCommand("readwiper");
    }
  } else if (strcmp(cmd, "inc") == 0 || strcmp(cmd, "dec") == 0) {
    uint8_t n = 1;
    if (!parseStepArg(args, &n)) {
      puts("Usage: inc [1..255] | dec [1..255]");
      return;
    }
    printWarning("wiper step changes the analog output state.");
    const MCP45HVX1::Status st = strcmp(cmd, "inc") == 0 ? gDev.incrementWiper(n)
                                                         : gDev.decrementWiper(n);
    printStatus(cmd, st);
    if (st.ok()) {
      readWiperCommand("readwiper");
    }
  } else if (strcmp(cmd, "tcon") == 0) {
    uint8_t v = 0;
    if (strcmp(args, "default") == 0) {
      printWarning("tcon default changes terminal state.");
      const MCP45HVX1::Status st = gDev.writeTcon(MCP45HVX1::cmd::TCON_DEFAULT);
      printStatus("tcon default", st);
      if (st.ok()) {
        readTconCommand("readtcon");
      }
    } else if (*args != '\0' && parseTconArg(args, &v)) {
      printWarning("tcon changes terminal state.");
      const MCP45HVX1::Status st = gDev.writeTcon(v);
      printStatus("tcon", st);
      if (st.ok()) {
        readTconCommand("readtcon");
      }
    } else if (*args != '\0') {
      puts("Usage: tcon [0x00..0xff|default]");
    } else {
      readTconCommand("tcon");
    }
  } else if (strcmp(cmd, "raw") == 0) {
    if (*args == '\0') {
      readRegisters(true);
    } else {
      char local[LINE_LEN];
      snprintf(local, sizeof(local), "%s", args);
      char* sub = trim(local);
      char* regText = splitWhitespace(sub);
      char* valueText = splitWhitespace(regText);
      uint8_t reg = 0;
      uint8_t value = 0;
      if (strcmp(sub, "write") == 0) {
        printDanger("raw write may affect live hardware.");
        MCP45HVX1::Status st =
            (valueText != nullptr && parseRegisterArg(regText, &reg) &&
             parseRawRegisterValueArg(reg, valueText, &value))
                ? gDev.writeRegister(reg, value)
                : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM,
                                           "usage: raw write <reg> <value>");
        printStatus("raw write", st);
        if (st.ok()) {
          readRegisters(false);
        }
      } else {
        puts("Usage: raw [write <reg> <value>]");
      }
    }
  } else if (strcmp(cmd, "wreg") == 0 || strcmp(cmd, "wregs") == 0) {
    char local[LINE_LEN];
    snprintf(local, sizeof(local), "%s", args);
    char* regText = trim(local);
    char* valueText = splitWhitespace(regText);
    uint8_t reg = 0;
    uint8_t value = 0;
    printDanger("raw register write may affect live hardware.");
    MCP45HVX1::Status st = (valueText != nullptr && parseRegisterArg(regText, &reg) &&
                            parseRawRegisterValueArg(reg, valueText, &value))
                               ? gDev.writeRegister(reg, value)
                               : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM,
                                                          "usage: wreg <reg> <value>");
    printStatus(cmd, st);
    if (st.ok()) {
      readRegisters(false);
    }
  } else if (strcmp(cmd, "frac") == 0 || strcmp(cmd, "pos") == 0) {
    float fraction = 0.0f;
    if (*args != '\0' && parseFloatRangeArg(args, 0.0f, 1.0f, &fraction)) {
      printWarning("fraction write changes the analog output state.");
      const MCP45HVX1::Status st = gDev.writeWiperFraction(fraction);
      printStatus(cmd, st);
      if (st.ok()) {
        readWiperCommand("readwiper");
      }
    } else if (*args != '\0') {
      puts("Usage: frac [0.0..1.0]");
    } else {
      MCP45HVX1::Status st = gDev.readWiperFraction(fraction);
      printStatus(cmd, st);
      if (st.ok()) {
        printf("fraction=%.4f\n", fraction);
      }
    }
  } else if (strcmp(cmd, "rab") == 0) {
    MCP45HVX1::ResistanceOption option{};
    if (*args == '\0') {
      printf("rab=%s nominal=%lu\n", resistanceName(gCfg.resistance),
             static_cast<unsigned long>(
                 MCP45HVX1::MCP45HVX1::nominalResistanceOhms(gCfg.resistance)));
    } else if (parseResistance(args, &option)) {
      gCfg.resistance = option;
      beginDriver();
    } else {
      puts("Usage: rab [5k|10k|50k|100k]");
    }
  } else if (strcmp(cmd, "info") == 0) {
    printInfo();
  } else if (strcmp(cmd, "errata") == 0) {
    printErrata();
  } else if (strcmp(cmd, "term") == 0 || strcmp(cmd, "terminal") == 0) {
    char local[LINE_LEN];
    snprintf(local, sizeof(local), "%s", args);
    char* valueText = splitWhitespace(local);
    MCP45HVX1::Terminal terminal{};
    bool enabled = false;
    if (valueText != nullptr) {
      if (parseTerminal(local, &terminal) && parseBoolArg(valueText, &enabled)) {
        printWarning("terminal command changes TCON terminal connectivity.");
        MCP45HVX1::Status st = gDev.setTerminalEnabled(terminal, enabled);
        printStatus(cmd, st);
        if (st.ok()) {
          readTconCommand("readtcon");
        }
      } else {
        puts("Usage: term a|w|b [on|off]");
      }
    } else if (parseTerminal(local, &terminal)) {
      bool enabledFlag = false;
      MCP45HVX1::Status st = gDev.getTerminalEnabled(terminal, enabledFlag);
      printStatus("term", st);
      if (st.ok()) {
        printf("enabled=%d\n", enabledFlag ? 1 : 0);
      }
    } else {
      MCP45HVX1::TerminalStatus status{};
      MCP45HVX1::Status st = gDev.readTerminalStatus(status);
      printStatus("term", st);
      if (st.ok()) {
        printf("mode=%s shutdown=%d A=%d W=%d B=%d\n",
               terminalModeName(status.mode), status.softwareShutdown ? 1 : 0,
               status.terminalA ? 1 : 0, status.terminalW ? 1 : 0,
               status.terminalB ? 1 : 0);
      }
    }
  } else if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "software-shutdown") == 0) {
    bool enabled = false;
    if (*args != '\0' && parseBoolArg(args, &enabled)) {
      printWarning("software shutdown changes TCON terminal connectivity, not the SHDN pin.");
      MCP45HVX1::Status st = gDev.setSoftwareShutdown(enabled);
      printStatus(cmd, st);
      if (st.ok()) {
        readTconCommand("readtcon");
      }
    } else if (*args != '\0') {
      puts("Usage: shutdown [on|off]");
    } else {
      bool isShutdown = false;
      MCP45HVX1::Status st = gDev.getSoftwareShutdown(isShutdown);
      printStatus(cmd, st);
      if (st.ok()) {
        printf("shutdown=%d\n", isShutdown ? 1 : 0);
      }
    }
  } else if (strcmp(cmd, "mode") == 0) {
    MCP45HVX1::TerminalMode mode{};
    if (*args != '\0' && parseTerminalMode(args, &mode)) {
      printWarning("mode command changes TCON terminal connectivity.");
      MCP45HVX1::Status st = gDev.setTerminalMode(mode);
      printStatus("mode", st);
      if (st.ok()) {
        readTconCommand("readtcon");
      }
    } else if (*args == '\0') {
      MCP45HVX1::Status st = gDev.getTerminalMode(mode);
      printStatus("mode", st);
      if (st.ok()) {
        printf("mode=%s\n", terminalModeName(mode));
      }
    } else {
      puts("Usage: mode [pot|bw|aw|float|shutdown]");
    }
  } else if (strcmp(cmd, "gc") == 0) {
    handleGc(args);
  } else if (strcmp(cmd, "selftest") == 0) {
    if (strcmp(args, "output") == 0) {
      runOutputSelftest();
    } else if (*args != '\0' && strcmp(args, "safe") != 0) {
      puts("Usage: selftest [safe|output]");
      return;
    } else {
      runSelftestSafe();
    }
  } else if (strcmp(cmd, "stress") == 0 || strcmp(cmd, "stress_mix") == 0) {
    uint32_t count = DEFAULT_STRESS_COUNT;
    if (!parseStressCountArg(args, &count)) {
      puts("Usage: stress [1..255] | stress_mix [1..255]");
      return;
    }
    runStress(count, strcmp(cmd, "stress_mix") == 0);
  } else if (strcmp(cmd, "verbose") == 0) {
    bool enabled = false;
    if (*args == '\0') {
      gVerbose = !gVerbose;
      printf("verbose=%d\n", gVerbose ? 1 : 0);
    } else if (parseBoolArg(args, &enabled)) {
      gVerbose = enabled;
      printf("verbose=%d\n", gVerbose ? 1 : 0);
    } else {
      puts("Usage: verbose [0|1|on|off]");
    }
  } else {
    puts("Unknown command. Try 'help'.");
  }
}

}  // namespace

extern "C" void app_main(void) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
  puts("");
  printHeader("MCP45HVX1 native ESP-IDF CLI");
  if (!initBus()) {
    printDanger("I2C init failed");
  }
  beginDriver();
  printHelp();
  char line[LINE_LEN] = {};
  while (true) {
    printf("> ");
    if (fgets(line, sizeof(line), stdin) != nullptr) {
      handleCommand(line);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
