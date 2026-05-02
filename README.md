# MCP45HVX1 Driver Library

Production-grade MCP45HVX1 high-voltage I2C digital potentiometer driver for
ESP32 (Arduino/PlatformIO).

## Features

- **Injected I2C transport** - no `Wire` dependency in library code
- **Health monitoring** - READY/DEGRADED/OFFLINE state tracking
- **Volatile register API** - Wiper 0 and TCON0 read/write helpers
- **Wiper commands** - direct write plus documented INC/DEC commands
- **Terminal control** - potentiometer, rheostat, floating wiper, and software shutdown presets
- **General Call helpers** - broadcast wiper/TCON write and wiper INC/DEC frames
- **Deterministic behavior** - no heap allocation in the core driver
- **Settings snapshot** - runtime config, cache, and health counters
- **Native tests** - fake-bus protocol tests for register and command behavior

## Installation

### PlatformIO

```ini
lib_deps =
  https://github.com/janhavelka/MCP45HVX1.git
```

### Manual

Copy `include/MCP45HVX1/` and `src/` to your project.

## Quick Start

```cpp
#include <Wire.h>
#include "MCP45HVX1/MCP45HVX1.h"

MCP45HVX1::Status myI2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                             uint32_t timeoutMs, void* user);
MCP45HVX1::Status myI2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                                 uint8_t* rxData, size_t rxLen,
                                 uint32_t timeoutMs, void* user);

MCP45HVX1::MCP45HVX1 pot;

void setup() {
  Wire.begin(8, 9, 400000);

  MCP45HVX1::Config cfg;
  cfg.i2cWrite = myI2cWrite;
  cfg.i2cWriteRead = myI2cWriteRead;
  cfg.i2cUser = &Wire;
  cfg.i2cAddress = 0x3C;
  cfg.resolution = MCP45HVX1::Resolution::Bits8; // MCP45HV51

  MCP45HVX1::Status st = pot.begin(cfg);
  if (!st.ok()) {
    return;
  }

  pot.writeWiper(0x80);
  pot.setTerminalMode(MCP45HVX1::TerminalMode::Potentiometer);
}

void loop() {
  pot.tick(millis());
}
```

The ready-made Arduino transport adapter used by the example CLI is in
`examples/common/I2cTransport.h`.

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - validate config, read Wiper/TCON, optionally apply configured initial values
- `void tick(uint32_t nowMs)` - no-op reserved hook
- `void end()` - reset driver object state without changing device terminals

### Diagnostics

- `Status probe()` - raw Wiper read without health-counter updates
- `Status recover()` - tracked Wiper/TCON reads to refresh cache and health
- `SettingsSnapshot getSettings()` - config, cache, and health snapshot

### Wiper

- `Status readWiper(uint8_t& code)`
- `Status writeWiper(uint8_t code)`
- `Status incrementWiper(uint8_t steps = 1)`
- `Status decrementWiper(uint8_t steps = 1)`
- `uint8_t codeFromFraction(float fraction, Resolution resolution)`
- `float fractionFromCode(uint8_t code, Resolution resolution)`

### Terminal Control

- `Status readTcon(uint8_t& value)`
- `Status writeTcon(uint8_t value)` - forces reserved bits `[7:4]` high
- `Status setTerminalEnabled(Terminal terminal, bool enabled)`
- `Status getTerminalEnabled(Terminal terminal, bool& enabled)`
- `Status setSoftwareShutdown(bool enabled)`
- `Status getSoftwareShutdown(bool& enabled)`
- `Status setTerminalMode(TerminalMode mode)`

### Direct Register Access

- `Status readRegister(uint8_t reg, uint8_t& value)` - valid registers: `0x00`, `0x04`
- `Status writeRegister(uint8_t reg, uint8_t value)`
- `Status readLastAddress(uint8_t& value)` - uses the documented last-address read

### General Call

- `Status generalCallWriteWiper(uint8_t code)`
- `Status generalCallWriteTcon(uint8_t value)`
- `Status generalCallIncrementWiper()`
- `Status generalCallDecrementWiper()`

The datasheet references a `GCEN` bit but the extracted register location is not
documented. This library sends only the documented General Call frames.

## Configuration

| Field | Default | Description |
|---|---:|---|
| `i2cAddress` | `0x3C` | A1=A0=0 address |
| `resolution` | `Bits8` | `Bits8` for MCP45HV51, `Bits7` for MCP45HV31 |
| `i2cTimeoutMs` | `50` | Transport timeout |
| `writeInitialWiper` | `false` | Opt-in Wiper write during `begin()` |
| `initialWiperCode` | `0x7F` | Wiper value used when opt-in write is enabled |
| `writeInitialTcon` | `false` | Opt-in TCON write during `begin()` |
| `initialTcon` | `0xFF` | TCON value used when opt-in write is enabled |
| `requirePowerOnDefaults` | `false` | Require POR/BOR Wiper and TCON defaults during `begin()` |
| `requireReadMsbZero` | `true` | Enforce documented read MSB byte `0x00` |
| `offlineThreshold` | `5` | Consecutive tracked failures before OFFLINE |

## Device Notes

- The implemented registers are `0x00` Wiper 0 and `0x04` TCON0. All other
  addresses are treated as reserved.
- Reads return two bytes on the bus. The driver checks that the first byte is
  `0x00` and returns the second byte as the register value.
- `WLAT` affects the physical wiper update, not I2C command acceptance.
- `SHDN` overrides terminal connectivity but does not corrupt registers.
- `begin()` does not write the analog state by default. Enable the initial-write
  flags only when that startup behavior is intentional.

## Address Note

DS20005304B Rev B Table 6-2 and the official PDF text give fixed address bits
`01111`, so the default address range is `0x3C-0x3F`. The extracted markdown in
`docs/08_variant_differences_and_open_questions.md` records an apparent
`0x5C-0x5F` conflict from command figures. The driver defaults to `0x3C` and
accepts both ranges so hardware-verification builds can test either candidate.
See `ASSUMPTIONS.md`.

## Examples

### 01_basic_bringup_cli

Interactive serial CLI with commands for bus scan, probe/recover, settings,
Wiper/TCON reads and writes, terminal modes, and a small INC/DEC stress pass.

Typical commands:

```text
scan
cfg
read
wiper 0x80
mode bw
tcon 0xFF
drv
```

## Running Tests

```bash
pio run -e esp32s3dev
pio run -e esp32s2dev
pio test -e native
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
python scripts/generate_version.py check
```

## Documentation

- [Assumptions](ASSUMPTIONS.md)
- [Implementation Manual](MCP45HVX1_digital_potentiometer_implementation_manual.md)
- [Register Map](docs/05_register_map.md)
- [Driver Register Reference](docs/register_reference.md)
- [Protocol Commands](docs/04_protocol_commands_and_transactions.md)
- [Initialization Notes](docs/07_initialization_reset_and_operational_notes.md)
- [Release Notes v1.0.0](docs/releases/v1.0.0.md)

## License

MIT License - see [LICENSE](LICENSE) for details.
