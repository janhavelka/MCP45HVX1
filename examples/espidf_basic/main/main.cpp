/**
 * @file main.cpp
 * @brief Native ESP-IDF bring-up CLI for MCP45HVX1.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
bool gVerbose = false;
bool gGcArmed = false;
bool gColor = true;

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

void printStatus(const char* op, MCP45HVX1::Status st) {
  printf("%s: %s (code=%u detail=%ld)\n", op, st.ok() ? "OK" : "FAIL",
         static_cast<unsigned>(st.code), static_cast<long>(st.detail));
  if (!st.ok() && st.msg != nullptr) {
    printf("  %s\n", st.msg);
  }
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

bool parseU32(const char* text, uint32_t* out) {
  if (text == nullptr || *text == '\0' || out == nullptr) {
    return false;
  }
  char* end = nullptr;
  const unsigned long v = strtoul(text, &end, 0);
  if (end == text) {
    return false;
  }
  while (*end != '\0' && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  *out = static_cast<uint32_t>(v);
  return *end == '\0';
}

bool parseFloatArg(const char* text, float* out) {
  if (text == nullptr || *text == '\0' || out == nullptr) {
    return false;
  }
  char* end = nullptr;
  const float v = strtof(text, &end);
  if (end == text) {
    return false;
  }
  while (*end != '\0' && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  *out = v;
  return *end == '\0';
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

bool isPrimaryAddress(uint8_t address) {
  return address >= MCP45HVX1::cmd::MIN_ADDRESS && address <= MCP45HVX1::cmd::MAX_ADDRESS;
}

bool isAlternateAddress(uint8_t address) {
  return address >= MCP45HVX1::cmd::ALT_MIN_ADDRESS &&
         address <= MCP45HVX1::cmd::ALT_MAX_ADDRESS;
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

const char* variantName(MCP45HVX1::Resolution resolution) {
  return resolution == MCP45HVX1::Resolution::Bits7 ? "MCP45HV31" : "MCP45HV51";
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
  if (strcmp(text, "5k") == 0 || strcmp(text, "5000") == 0) {
    *out = MCP45HVX1::ResistanceOption::R5K;
    return true;
  }
  if (strcmp(text, "10k") == 0 || strcmp(text, "10000") == 0) {
    *out = MCP45HVX1::ResistanceOption::R10K;
    return true;
  }
  if (strcmp(text, "50k") == 0 || strcmp(text, "50000") == 0) {
    *out = MCP45HVX1::ResistanceOption::R50K;
    return true;
  }
  if (strcmp(text, "100k") == 0 || strcmp(text, "100000") == 0) {
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

void beginDriver() {
  gCfg.i2cWrite = i2cWrite;
  gCfg.i2cWriteRead = i2cWriteRead;
  gCfg.i2cUser = &gBus;
  gCfg.busReset = resetBus;
  gCfg.nowMs = nowMs;
  gCfg.i2cTimeoutMs = I2C_TIMEOUT_MS;
  printStatus("begin", gDev.begin(gCfg));
}

void printHelp() {
  puts("Native ESP-IDF MCP45HVX1 CLI");
  puts("Common:");
  puts("  help | ? | version | ver | color [on|off] | verbose [0|1]");
  puts("Device selection:");
  puts("  scan | begin [addr] [7|8] | addr <0x3c..0x3f> | addr_alt <0x5c..0x5f>");
  puts("  variant [hv31|hv51] | res <7|8> | rab <5k|10k|50k|100k>");
  puts("Read-only diagnostics:");
  puts("  probe | recover | iface_reset | read | rregs | readwiper | readtcon");
  puts("  dump | raw | last | reg <reg> | rreg <reg>");
  puts("  cfg | settings | detail | drv | health | state | info | errata");
  puts("Output-changing commands:");
  puts("  defaults | wiper [0..max] | wiper percent <0..100> | wiper fraction <0.0..1.0>");
  puts("  frac [0.0..1.0] | pos [0.0..1.0] | zero | mid | max | inc [n] | dec [n]");
  puts("  tcon [value|default] | term a|w|b [on|off] | terminal a|w|b [on|off]");
  puts("  shutdown [on|off] | software-shutdown [on|off] | mode [pot|bw|aw|float|shutdown]");
  puts("Dangerous / operator-gated:");
  puts("  raw write <reg> <value> | wreg <reg> <value> | wregs <reg> <value>");
  puts("  gc arm | gc disarm | gc wiper <code> | gc tcon <value> | gc inc | gc dec");
  puts("  selftest | selftest safe | selftest output | stress [n] | stress_mix [n]");
}

void scanBus() {
  puts("I2C scan:");
  for (uint8_t addr = 0x08U; addr <= 0x77U; ++addr) {
    if (i2c_master_probe(gBus.bus, addr, timeoutArg(I2C_TIMEOUT_MS)) == ESP_OK) {
      printf("  0x%02X\n", addr);
    }
  }
}

void printDrv() {
  printf("state=%s initialized=%d online=%d ok=%lu fail=%lu consecutive=%u\n",
         stateName(gDev.state()), gDev.isInitialized() ? 1 : 0,
         gDev.isOnline() ? 1 : 0, static_cast<unsigned long>(gDev.totalSuccess()),
         static_cast<unsigned long>(gDev.totalFailures()),
         static_cast<unsigned>(gDev.consecutiveFailures()));
}

void printStateLine() {
  const MCP45HVX1::DeviceInfo info = gDev.getDeviceInfo();
  const MCP45HVX1::Status last = gDev.lastError();
  printf("state=%s initialized=%d online=%d addr=0x%02X variant=%s ok=%lu fail=%lu consecutive=%u last_code=%u\n",
         stateName(gDev.state()), gDev.isInitialized() ? 1 : 0, gDev.isOnline() ? 1 : 0,
         info.i2cAddress, variantName(info.resolution),
         static_cast<unsigned long>(gDev.totalSuccess()),
         static_cast<unsigned long>(gDev.totalFailures()),
         static_cast<unsigned>(gDev.consecutiveFailures()),
         static_cast<unsigned>(last.code));
}

void printDriverHealth() {
  MCP45HVX1::SettingsSnapshot s{};
  (void)gDev.getSettings(s);
  printf("health state=%s initialized=%d online=%d\n",
         stateName(s.state), s.initialized ? 1 : 0, gDev.isOnline() ? 1 : 0);
  printf("  ok=%lu fail=%lu consecutive=%u threshold=%u\n",
         static_cast<unsigned long>(s.totalSuccess),
         static_cast<unsigned long>(s.totalFailures),
         static_cast<unsigned>(s.consecutiveFailures),
         static_cast<unsigned>(s.config.offlineThreshold));
  printf("  last_ok_ms=%lu last_error_ms=%lu last_code=%u last_detail=%ld\n",
         static_cast<unsigned long>(s.lastOkMs),
         static_cast<unsigned long>(s.lastErrorMs),
         static_cast<unsigned>(s.lastError.code),
         static_cast<long>(s.lastError.detail));
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
  printf("timeout_ms=%lu alternate_allowed=%d require_por=%d require_msb_zero=%d color=%d verbose=%d\n",
         static_cast<unsigned long>(s.config.i2cTimeoutMs),
         s.config.allowAlternateAddressRange ? 1 : 0,
         s.config.requirePowerOnDefaults ? 1 : 0,
         s.config.requireReadMsbZero ? 1 : 0,
         gColor ? 1 : 0,
         gVerbose ? 1 : 0);
  printf("initial_wiper_write=%d initial_wiper=0x%02X initial_tcon_write=%d initial_tcon=0x%02X\n",
         s.config.writeInitialWiper ? 1 : 0,
         s.config.initialWiperCode,
         s.config.writeInitialTcon ? 1 : 0,
         s.config.initialTcon);
}

void printErrata() {
  const MCP45HVX1::SiliconErrataInfo errata = MCP45HVX1::MCP45HVX1::siliconErrataInfo();
  printf("%s rev %s: %s\n", errata.documentNumber, errata.documentRevision,
         errata.documentTitle);
  printf("shared_bus_hazard=%d general_call_decode_hazard=%d isolated_bus_workaround=%d\n",
         errata.sharedBusI2cHazard ? 1 : 0,
         errata.generalCallAddressDecodeHazard ? 1 : 0,
         errata.uniqueBusWorkaroundForAffectedSilicon ? 1 : 0);
  puts(errata.markingSummary);
}

void readRegisters(bool includeLastAddress) {
  MCP45HVX1::RegisterSnapshot s;
  MCP45HVX1::Status st = gDev.readSnapshot(s);
  printStatus("read", st);
  if (st.ok()) {
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
    puts("WARNING: stress_mix changes Wiper/TCON repeatedly, then restores the starting snapshot.");
    MCP45HVX1::RegisterSnapshot original;
    MCP45HVX1::Status st = gDev.readSnapshot(original);
    if (!st.ok()) {
      printStatus("stress_mix baseline", st);
      return;
    }
    const uint8_t maxCode = MCP45HVX1::MCP45HVX1::maxWiperCode(gCfg.resolution);
    for (uint32_t i = 0; i < count; ++i) {
      st = gDev.writeWiper((i % 2U) == 0U ? 0U : maxCode);
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
    const MCP45HVX1::Status restoreTcon = gDev.writeTcon(original.tcon);
    const MCP45HVX1::Status restoreWiper = gDev.writeWiper(original.wiper);
    printf("stress_mix: ok=%lu fail=%lu restore_tcon=%s restore_wiper=%s\n",
           static_cast<unsigned long>(ok), static_cast<unsigned long>(fail),
           restoreTcon.ok() ? "OK" : "FAIL", restoreWiper.ok() ? "OK" : "FAIL");
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
}

void runSelftest() {
  uint32_t pass = 0;
  uint32_t fail = 0;
  auto check = [&](const char* name, MCP45HVX1::Status st) {
    printf("[%s] %s\n", st.ok() ? "OK" : "FAIL", name);
    if (st.ok()) {
      ++pass;
    } else {
      ++fail;
      if (gVerbose) {
        printStatus(name, st);
      }
    }
  };
  check("probe", gDev.probe());
  MCP45HVX1::RegisterSnapshot snapshot{};
  check("readSnapshot", gDev.readSnapshot(snapshot));
  uint8_t last = 0;
  check("readLastAddress", gDev.readLastAddress(last));
  printf("selftest: pass=%lu fail=%lu\n", static_cast<unsigned long>(pass),
         static_cast<unsigned long>(fail));
}

void handleGc(const char* args) {
  char local[LINE_LEN];
  snprintf(local, sizeof(local), "%s", args == nullptr ? "" : args);
  char* sub = trim(local);
  if (strcmp(sub, "arm") == 0) {
    gGcArmed = true;
    puts("General Call armed for one command.");
    return;
  }
  if (strcmp(sub, "disarm") == 0) {
    gGcArmed = false;
    puts("General Call disarmed.");
    return;
  }
  if (!gGcArmed) {
    puts("General Call rejected; run 'gc arm' first.");
    return;
  }
  gGcArmed = false;
  if (strcmp(sub, "inc") == 0) {
    printStatus("gc inc", gDev.generalCallIncrementWiper());
  } else if (strcmp(sub, "dec") == 0) {
    printStatus("gc dec", gDev.generalCallDecrementWiper());
  } else if (strncmp(sub, "wiper ", 6) == 0) {
    uint32_t v = 0;
    printStatus("gc wiper", parseU32(sub + 6, &v)
                                ? gDev.generalCallWriteWiper(static_cast<uint8_t>(v))
                                : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "bad code"));
  } else if (strncmp(sub, "tcon ", 5) == 0) {
    uint32_t v = 0;
    printStatus("gc tcon", parseU32(sub + 5, &v)
                               ? gDev.generalCallWriteTcon(static_cast<uint8_t>(v))
                               : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "bad value"));
  } else {
    puts("Usage: gc arm|disarm|wiper <code>|tcon <value>|inc|dec");
  }
}

void handleCommand(char* line) {
  char* cmd = trim(line);
  char* args = strchr(cmd, ' ');
  if (args != nullptr) {
    *args++ = '\0';
    args = trim(args);
  } else {
    args = cmd + strlen(cmd);
  }

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
  } else if (strcmp(cmd, "version") == 0 || strcmp(cmd, "ver") == 0) {
    printf("MCP45HVX1 %s %s\n", MCP45HVX1::VERSION, MCP45HVX1::VERSION_FULL);
  } else if (strcmp(cmd, "scan") == 0) {
    scanBus();
  } else if (strcmp(cmd, "color") == 0) {
    if (*args == '\0') {
      printf("color=%d\n", gColor ? 1 : 0);
    } else if (parseBoolArg(args, &gColor)) {
      printf("color=%d\n", gColor ? 1 : 0);
    } else {
      puts("Usage: color [on|off]");
    }
  } else if (strcmp(cmd, "begin") == 0) {
    char local[LINE_LEN];
    snprintf(local, sizeof(local), "%s", args);
    char* first = trim(local);
    char* second = strchr(first, ' ');
    if (second != nullptr) {
      *second++ = '\0';
      second = trim(second);
    }
    uint32_t v = 0;
    MCP45HVX1::Resolution resolution = gCfg.resolution;
    if (*first != '\0') {
      if (parseResolutionText(first, &resolution)) {
        gCfg.resolution = resolution;
      } else if (parseU32(first, &v)) {
        const uint8_t address = static_cast<uint8_t>(v);
        gCfg.i2cAddress = address;
        gCfg.allowAlternateAddressRange = isAlternateAddress(address);
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
    uint32_t v = 0;
    if (*args == '\0') {
      printInfo();
    } else if (parseU32(args, &v) && isPrimaryAddress(static_cast<uint8_t>(v))) {
      gCfg.i2cAddress = static_cast<uint8_t>(v);
      gCfg.allowAlternateAddressRange = false;
      beginDriver();
    } else {
      puts("Usage: addr <0x3c..0x3f>; use addr_alt for 0x5c..0x5f");
    }
  } else if (strcmp(cmd, "addr_alt") == 0) {
    uint32_t v = 0;
    if (parseU32(args, &v) && isAlternateAddress(static_cast<uint8_t>(v))) {
      puts("WARNING: using disputed alternate address range 0x5c..0x5f.");
      gCfg.i2cAddress = static_cast<uint8_t>(v);
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
    printStatus("probe", gDev.probe());
  } else if (strcmp(cmd, "recover") == 0) {
    printStatus("recover", gDev.recover());
  } else if (strcmp(cmd, "iface_reset") == 0) {
    printStatus("iface_reset", gDev.resetI2cState());
  } else if (strcmp(cmd, "defaults") == 0) {
    puts("WARNING: defaults writes Wiper and TCON volatile registers.");
    printStatus("defaults", gDev.restorePowerOnDefaults());
  } else if (strcmp(cmd, "cfg") == 0 || strcmp(cmd, "settings") == 0) {
    printConfigSnapshot();
  } else if (strcmp(cmd, "detail") == 0) {
    printConfigSnapshot();
    printDriverHealth();
  } else if (strcmp(cmd, "drv") == 0 || strcmp(cmd, "health") == 0) {
    printDriverHealth();
  } else if (strcmp(cmd, "state") == 0) {
    printStateLine();
  } else if (strcmp(cmd, "read") == 0 || strcmp(cmd, "rregs") == 0) {
    readRegisters(false);
  } else if (strcmp(cmd, "dump") == 0) {
    readRegisters(true);
  } else if (strcmp(cmd, "readwiper") == 0) {
    readWiperCommand("readwiper");
  } else if (strcmp(cmd, "readtcon") == 0) {
    readTconCommand("readtcon");
  } else if (strcmp(cmd, "last") == 0) {
    uint8_t v = 0;
    MCP45HVX1::Status st = gDev.readLastAddress(v);
    printStatus("last", st);
    if (st.ok()) {
      printf("last=0x%02X\n", v);
    }
  } else if (strcmp(cmd, "rreg") == 0 || strcmp(cmd, "reg") == 0) {
    uint32_t reg = 0;
    uint8_t value = 0;
    MCP45HVX1::Status st = parseU32(args, &reg)
                                ? gDev.readRegister(static_cast<uint8_t>(reg), value)
                                : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM, "bad reg");
    printStatus("rreg", st);
    if (st.ok()) {
      printf("reg=0x%02X value=0x%02X\n", static_cast<unsigned>(reg), value);
    }
  } else if (strcmp(cmd, "wiper") == 0) {
    uint32_t v = 0;
    if (strncmp(args, "percent ", 8) == 0) {
      float percent = 0.0f;
      if (parseFloatArg(args + 8, &percent) && percent >= 0.0f && percent <= 100.0f) {
        puts("WARNING: wiper percent changes the analog output state.");
        printStatus("wiper percent", gDev.writeWiperFraction(percent / 100.0f));
        readWiperCommand("readwiper");
      } else {
        puts("Usage: wiper percent <0..100>");
      }
    } else if (strncmp(args, "fraction ", 9) == 0) {
      float fraction = 0.0f;
      if (parseFloatArg(args + 9, &fraction) && fraction >= 0.0f && fraction <= 1.0f) {
        puts("WARNING: wiper fraction changes the analog output state.");
        printStatus("wiper fraction", gDev.writeWiperFraction(fraction));
        readWiperCommand("readwiper");
      } else {
        puts("Usage: wiper fraction <0.0..1.0>");
      }
    } else if (parseU32(args, &v)) {
      puts("WARNING: wiper changes the analog output state.");
      printStatus("wiper", gDev.writeWiper(static_cast<uint8_t>(v)));
      readWiperCommand("readwiper");
    } else {
      readWiperCommand("wiper");
    }
  } else if (strcmp(cmd, "zero") == 0 || strcmp(cmd, "mid") == 0 || strcmp(cmd, "max") == 0) {
    const uint8_t max = MCP45HVX1::MCP45HVX1::maxWiperCode(gCfg.resolution);
    const uint8_t v = (strcmp(cmd, "zero") == 0) ? 0U : (strcmp(cmd, "mid") == 0 ? max / 2U : max);
    puts("WARNING: wiper preset changes the analog output state.");
    printStatus(cmd, gDev.writeWiper(v));
  } else if (strcmp(cmd, "inc") == 0 || strcmp(cmd, "dec") == 0) {
    uint32_t n = 1;
    (void)parseU32(args, &n);
    puts("WARNING: wiper step changes the analog output state.");
    printStatus(cmd, strcmp(cmd, "inc") == 0 ? gDev.incrementWiper(static_cast<uint8_t>(n))
                                              : gDev.decrementWiper(static_cast<uint8_t>(n)));
  } else if (strcmp(cmd, "tcon") == 0) {
    uint32_t v = 0;
    if (strcmp(args, "default") == 0) {
      puts("WARNING: tcon default changes terminal state.");
      printStatus("tcon default", gDev.writeTcon(MCP45HVX1::cmd::TCON_DEFAULT));
      readTconCommand("readtcon");
    } else if (parseU32(args, &v)) {
      puts("WARNING: tcon changes terminal state.");
      printStatus("tcon", gDev.writeTcon(static_cast<uint8_t>(v)));
      readTconCommand("readtcon");
    } else {
      readTconCommand("tcon");
    }
  } else if (strcmp(cmd, "raw") == 0) {
    if (*args == '\0') {
      readRegisters(true);
    } else if (strncmp(args, "write ", 6) == 0) {
      char local[LINE_LEN];
      snprintf(local, sizeof(local), "%s", args + 6);
      char* valueText = strchr(local, ' ');
      uint32_t reg = 0;
      uint32_t value = 0;
      if (valueText != nullptr) {
        *valueText++ = '\0';
        valueText = trim(valueText);
      }
      puts("DANGER: raw write may affect live hardware.");
      printStatus("raw write",
                  (valueText != nullptr && parseU32(local, &reg) && parseU32(valueText, &value))
                      ? gDev.writeRegister(static_cast<uint8_t>(reg),
                                           static_cast<uint8_t>(value))
                      : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM,
                                                 "usage: raw write <reg> <value>"));
    } else {
      puts("Usage: raw [write <reg> <value>]");
    }
  } else if (strcmp(cmd, "wreg") == 0 || strcmp(cmd, "wregs") == 0) {
    char local[LINE_LEN];
    snprintf(local, sizeof(local), "%s", args);
    char* valueText = strchr(local, ' ');
    uint32_t reg = 0;
    uint32_t value = 0;
    if (valueText != nullptr) {
      *valueText++ = '\0';
      valueText = trim(valueText);
    }
    puts("DANGER: raw register write may affect live hardware.");
    printStatus(cmd, (valueText != nullptr && parseU32(local, &reg) && parseU32(valueText, &value))
                         ? gDev.writeRegister(static_cast<uint8_t>(reg),
                                              static_cast<uint8_t>(value))
                         : MCP45HVX1::Status::Error(MCP45HVX1::Err::INVALID_PARAM,
                                                    "usage: wreg <reg> <value>"));
  } else if (strcmp(cmd, "frac") == 0 || strcmp(cmd, "pos") == 0) {
    float fraction = 0.0f;
    if (parseFloatArg(args, &fraction)) {
      puts("WARNING: fraction write changes the analog output state.");
      printStatus(cmd, gDev.writeWiperFraction(fraction));
      readWiperCommand("readwiper");
    } else {
      MCP45HVX1::Status st = gDev.readWiperFraction(fraction);
      printStatus(cmd, st);
      if (st.ok()) {
        printf("fraction=%.4f\n", fraction);
      }
    }
  } else if (strcmp(cmd, "rab") == 0) {
    MCP45HVX1::ResistanceOption option{};
    if (parseResistance(args, &option)) {
      gCfg.resistance = option;
      beginDriver();
    } else {
      printf("rab=%s nominal=%lu\n", resistanceName(gCfg.resistance),
             static_cast<unsigned long>(
                 MCP45HVX1::MCP45HVX1::nominalResistanceOhms(gCfg.resistance)));
    }
  } else if (strcmp(cmd, "info") == 0) {
    printInfo();
  } else if (strcmp(cmd, "errata") == 0) {
    printErrata();
  } else if (strcmp(cmd, "term") == 0 || strcmp(cmd, "terminal") == 0) {
    char local[LINE_LEN];
    snprintf(local, sizeof(local), "%s", args);
    char* valueText = strchr(local, ' ');
    if (valueText != nullptr) {
      *valueText++ = '\0';
      valueText = trim(valueText);
    }
    MCP45HVX1::Terminal terminal{};
    bool enabled = false;
    uint32_t enabledInt = 0;
    if (valueText != nullptr && parseTerminal(local, &terminal) &&
        (parseBoolArg(valueText, &enabled) ||
         (parseU32(valueText, &enabledInt) && ((enabled = enabledInt != 0U), true)))) {
      puts("WARNING: terminal command changes TCON terminal connectivity.");
      printStatus(cmd, gDev.setTerminalEnabled(terminal, enabled));
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
    uint32_t enabledInt = 0;
    if (parseBoolArg(args, &enabled) ||
        (parseU32(args, &enabledInt) && ((enabled = enabledInt != 0U), true))) {
      puts("WARNING: shutdown command changes TCON terminal connectivity.");
      printStatus(cmd, gDev.setSoftwareShutdown(enabled));
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
    if (parseTerminalMode(args, &mode)) {
      puts("WARNING: mode command changes TCON terminal connectivity.");
      printStatus("mode", gDev.setTerminalMode(mode));
    } else {
      MCP45HVX1::Status st = gDev.getTerminalMode(mode);
      printStatus("mode", st);
      if (st.ok()) {
        printf("mode=%s\n", terminalModeName(mode));
      }
    }
  } else if (strcmp(cmd, "gc") == 0) {
    handleGc(args);
  } else if (strcmp(cmd, "selftest") == 0) {
    if (strcmp(args, "output") == 0) {
      puts("WARNING: selftest output is operator-requested; native checks remain state-restoring.");
    } else if (*args != '\0' && strcmp(args, "safe") != 0) {
      puts("Usage: selftest [safe|output]");
      return;
    }
    runSelftest();
  } else if (strcmp(cmd, "stress") == 0 || strcmp(cmd, "stress_mix") == 0) {
    uint32_t count = 10;
    (void)parseU32(args, &count);
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
  puts("\nMCP45HVX1 native ESP-IDF CLI");
  if (!initBus()) {
    puts("I2C init failed");
  }
  beginDriver();
  printHelp();
  char line[LINE_LEN] = {};
  while (true) {
    printf("> ");
    if (fgets(line, sizeof(line), stdin) != nullptr) {
      handleCommand(line);
    }
    gDev.tick(nowMs(nullptr));
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
