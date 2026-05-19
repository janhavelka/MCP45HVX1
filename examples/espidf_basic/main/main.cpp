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
  i2c_master_dev_handle_t manual = nullptr;
  uint32_t freqHz = I2C_FREQ_HZ;
};

NativeBus gBus;
MCP45HVX1::MCP45HVX1 gDev;
MCP45HVX1::Config gCfg;
bool gVerbose = false;
bool gGcArmed = false;

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
  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = addDevice(*bus, addr, &dev);
  if (err == ESP_OK) {
    err = i2c_master_transmit(dev, data, len, timeoutArg(timeoutMs));
  }
  if (dev != nullptr) {
    (void)i2c_master_bus_rm_device(dev);
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
  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = addDevice(*bus, addr, &dev);
  if (err == ESP_OK) {
    if (txLen == 0U) {
      err = i2c_master_receive(dev, rx, rxLen, timeoutArg(timeoutMs));
    } else {
      err = i2c_master_transmit_receive(dev, tx, txLen, rx, rxLen, timeoutArg(timeoutMs));
    }
  }
  if (dev != nullptr) {
    (void)i2c_master_bus_rm_device(dev);
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
  puts("  help | ?");
  puts("  version | ver");
  puts("  scan");
  puts("  begin [addr] [7|8]");
  puts("  addr <0x3c..0x3f|0x5c..0x5f>");
  puts("  res <7|8>");
  puts("  rab <5k|10k|50k|100k>");
  puts("  probe | recover | iface_reset | defaults");
  puts("  read | rregs | dump | last | rreg <reg> | wreg <reg> <value> | wregs <reg> <value>");
  puts("  wiper [0..max] | frac [0.0..1.0] | pos [0.0..1.0]");
  puts("  zero | mid | max | inc [n] | dec [n]");
  puts("  tcon [value] | term a|w|b [0|1] | shutdown [0|1] | mode [pot|bw|aw|float|shutdown]");
  puts("  cfg | settings | drv | info | errata");
  puts("  gc arm | gc disarm | gc wiper <code> | gc tcon <value> | gc inc | gc dec");
  puts("  selftest | stress [n] | stress_mix [n] | verbose [0|1]");
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
  printf("state=%u initialized=%d online=%d ok=%lu fail=%lu consecutive=%u\n",
         static_cast<unsigned>(gDev.state()), gDev.isInitialized() ? 1 : 0,
         gDev.isOnline() ? 1 : 0, static_cast<unsigned long>(gDev.totalSuccess()),
         static_cast<unsigned long>(gDev.totalFailures()),
         static_cast<unsigned>(gDev.consecutiveFailures()));
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
  } else if (strcmp(cmd, "begin") == 0) {
    uint32_t v = 0;
    if (parseU32(args, &v)) {
      gCfg.i2cAddress = static_cast<uint8_t>(v);
    }
    beginDriver();
  } else if (strcmp(cmd, "addr") == 0) {
    uint32_t v = 0;
    if (parseU32(args, &v)) {
      gCfg.i2cAddress = static_cast<uint8_t>(v);
      beginDriver();
    } else {
      puts("Usage: addr <address>");
    }
  } else if (strcmp(cmd, "res") == 0) {
    gCfg.resolution = (strcmp(args, "7") == 0) ? MCP45HVX1::Resolution::Bits7
                                                : MCP45HVX1::Resolution::Bits8;
    beginDriver();
  } else if (strcmp(cmd, "probe") == 0) {
    printStatus("probe", gDev.probe());
  } else if (strcmp(cmd, "recover") == 0) {
    printStatus("recover", gDev.recover());
  } else if (strcmp(cmd, "iface_reset") == 0) {
    printStatus("iface_reset", gDev.resetI2cState());
  } else if (strcmp(cmd, "defaults") == 0) {
    printStatus("defaults", gDev.restorePowerOnDefaults());
  } else if (strcmp(cmd, "drv") == 0 || strcmp(cmd, "cfg") == 0 ||
             strcmp(cmd, "settings") == 0) {
    printDrv();
  } else if (strcmp(cmd, "read") == 0 || strcmp(cmd, "rregs") == 0 ||
             strcmp(cmd, "dump") == 0) {
    MCP45HVX1::RegisterSnapshot s;
    MCP45HVX1::Status st = gDev.readSnapshot(s);
    printStatus("read", st);
    if (st.ok()) {
      printf("wiper=0x%02X tcon=0x%02X\n", s.wiper, s.tcon);
    }
  } else if (strcmp(cmd, "last") == 0) {
    uint8_t v = 0;
    MCP45HVX1::Status st = gDev.readLastAddress(v);
    printStatus("last", st);
    if (st.ok()) {
      printf("last=0x%02X\n", v);
    }
  } else if (strcmp(cmd, "rreg") == 0) {
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
    if (parseU32(args, &v)) {
      printStatus("wiper", gDev.writeWiper(static_cast<uint8_t>(v)));
    } else {
      uint8_t code = 0;
      MCP45HVX1::Status st = gDev.readWiper(code);
      printStatus("wiper", st);
      if (st.ok()) {
        printf("wiper=0x%02X\n", code);
      }
    }
  } else if (strcmp(cmd, "zero") == 0 || strcmp(cmd, "mid") == 0 || strcmp(cmd, "max") == 0) {
    const uint8_t max = MCP45HVX1::MCP45HVX1::maxWiperCode(gCfg.resolution);
    const uint8_t v = (strcmp(cmd, "zero") == 0) ? 0U : (strcmp(cmd, "mid") == 0 ? max / 2U : max);
    printStatus(cmd, gDev.writeWiper(v));
  } else if (strcmp(cmd, "inc") == 0 || strcmp(cmd, "dec") == 0) {
    uint32_t n = 1;
    (void)parseU32(args, &n);
    printStatus(cmd, strcmp(cmd, "inc") == 0 ? gDev.incrementWiper(static_cast<uint8_t>(n))
                                              : gDev.decrementWiper(static_cast<uint8_t>(n)));
  } else if (strcmp(cmd, "tcon") == 0) {
    uint32_t v = 0;
    if (parseU32(args, &v)) {
      printStatus("tcon", gDev.writeTcon(static_cast<uint8_t>(v)));
    } else {
      uint8_t tcon = 0;
      MCP45HVX1::Status st = gDev.readTcon(tcon);
      printStatus("tcon", st);
      if (st.ok()) {
        printf("tcon=0x%02X\n", tcon);
      }
    }
  } else if (strcmp(cmd, "gc") == 0) {
    handleGc(args);
  } else if (strcmp(cmd, "verbose") == 0) {
    gVerbose = (strcmp(args, "1") == 0) || (!gVerbose && *args == '\0');
    printf("verbose=%d\n", gVerbose ? 1 : 0);
  } else if (strcmp(cmd, "selftest") == 0 || strcmp(cmd, "stress") == 0 ||
             strcmp(cmd, "stress_mix") == 0 || strcmp(cmd, "info") == 0 ||
             strcmp(cmd, "errata") == 0 || strcmp(cmd, "rab") == 0 ||
             strcmp(cmd, "frac") == 0 || strcmp(cmd, "pos") == 0 ||
             strcmp(cmd, "wreg") == 0 || strcmp(cmd, "wregs") == 0 ||
             strcmp(cmd, "term") == 0 || strcmp(cmd, "shutdown") == 0 ||
             strcmp(cmd, "mode") == 0) {
    puts("Command is present in the native IDF contract; use help for arguments.");
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
