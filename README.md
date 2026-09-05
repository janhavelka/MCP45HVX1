# MCP45HVX1 Driver Library

[![CI](https://github.com/janhavelka/MCP45HVX1/actions/workflows/ci.yml/badge.svg)](https://github.com/janhavelka/MCP45HVX1/actions/workflows/ci.yml)

Framework-neutral driver for the Microchip MCP45HV31 / MCP45HV51 high-voltage
I2C digital potentiometer, usable both as a standalone bring-up tool for the
part and as a component inside a larger firmware.

The library core (`include/`, `src/`) has no framework dependency: it takes I2C
and timing through `Config` callbacks and never touches `Wire`, ESP-IDF, GPIO,
tasks, or delays. The bundled examples are ESP32 diagnostic CLIs and are not
part of the library API.

> **Scope.** Software checks prove register-level and status behavior only.
> Analog movement, terminal current, high-voltage operation, SHDN/WLAT physical
> override, and General Call safety on a shared bus all require hardware
> evidence — see [Hardware Validation](docs/MCP45HVX1_HARDWARE_VALIDATION.md).

## Features

- **Injected I2C transport** — no `Wire` or ESP-IDF dependency in library code
- **Volatile register API** — Wiper 0 and TCON0 read/write, plus raw register
  and last-address reads
- **Wiper commands** — direct write and documented INC/DEC step commands,
  chunked into bounded transactions
- **Terminal control** — potentiometer, rheostat, floating-wiper, and TCON
  software-shutdown presets
- **Health monitoring** — READY/DEGRADED/OFFLINE tracking with an explicit
  `recover()` path out of OFFLINE
- **Uncertainty tracking** — an ambiguous write failure marks the affected
  register unknown until a readback proves otherwise
- **Poll-chunked jobs** — bounded I2C work per call for cooperative schedulers
- **General Call helpers** — broadcast wiper/TCON write and INC/DEC frames,
  disabled unless explicitly opted in
- **Variant helpers** — 7-bit/8-bit code limits, nominal resistance, ideal step
  and terminal-current limits
- **No heap allocation** in the core driver
- **Native tests** — fake-bus protocol tests for register and command behavior

## Installation

### PlatformIO

```ini
lib_deps =
  https://github.com/janhavelka/MCP45HVX1.git
```

### ESP-IDF Component

The repository also builds as a pure ESP-IDF component. Add it as an extra
component or dependency, include `MCP45HVX1/MCP45HVX1.h`, and supply
`Config::i2cWrite` / `Config::i2cWriteRead` from your project-owned I2C master
bus. See [ESP-IDF Port](docs/IDF_PORT.md).

### Manual

Copy `include/MCP45HVX1/` and `src/` into your project and add `include/` to
the include path. That is the whole library.

The Quick Start below also uses `examples/common/I2cTransport.h`, an
example-only Arduino adapter. It includes its own headers relative to the
repository root, so either add the repository root to your include path, copy
the file and fix its includes, or write the two transport callbacks yourself —
they are about thirty lines each.

## Quick Start

```cpp
#include <Wire.h>
#include "MCP45HVX1/MCP45HVX1.h"
#include "examples/common/I2cTransport.h"

MCP45HVX1::MCP45HVX1 pot;

uint32_t nowMs(void*) {
  return millis();
}

void setup() {
  Wire.begin(8, 9);
  Wire.setClock(400000);

  MCP45HVX1::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  cfg.nowMs = nowMs;                              // otherwise timestamps stay 0
  cfg.i2cAddress = 0x3C;
  cfg.resolution = MCP45HVX1::Resolution::Bits8;  // MCP45HV51

  if (!pot.begin(cfg).ok()) {
    return;
  }

  MCP45HVX1::RegisterSnapshot snapshot;
  if (!pot.readSnapshot(snapshot).ok()) {
    return;
  }
}

void loop() {}
```

`begin()` is read-only by default: it validates the configuration, reads
Wiper/TCON to confirm the device is present, and caches them. It changes the
analog output only if `writeInitialWiper` or `writeInitialTcon` is set.

## API Overview

Every fallible call returns `Status` (`code`, `detail`, and a static `msg`).
`Status::ok()` is the only success test needed.

| Area | Entry points |
|---|---|
| Lifecycle | `begin()`, `end()`, `isInitialized()` |
| Wiper | `readWiper()`, `writeWiper()`, `incrementWiper()`, `decrementWiper()`, `readWiperFraction()`, `writeWiperFraction()` |
| Terminals | `readTcon()`, `writeTcon()`, `setTerminalEnabled()`, `setTerminalMode()`, `setSoftwareShutdown()`, `readTerminalStatus()` |
| Registers | `readRegister()`, `writeRegister()`, `readLastAddress()`, `readSnapshot()` |
| Diagnostics | `probe()`, `recover()`, `resetI2cState()`, `restorePowerOnDefaults()` |
| Health | `state()`, `isOnline()`, `lastError()`, `consecutiveFailures()`, `hardwareStateUncertain()` |
| Snapshots | `getSettings()`, `getDeviceInfo()`, `getJobSnapshot()`, `siliconErrataInfo()` |
| Poll jobs | `startSetWiperJob()`, `startReadSnapshotJob()`, `startSetTerminalJob()`, `startIncrementWiperJob()`, `startDecrementWiperJob()`, `startRecoverJob()`, `pollJob()` |
| General Call | `generalCallWriteWiper()`, `generalCallWriteTcon()`, `generalCallIncrementWiper()`, `generalCallDecrementWiper()` |
| Static helpers | `maxWiperCode()`, `defaultWiperCode()`, `codeFromFraction()`, `fractionFromCode()`, `nominalResistanceOhms()`, `stepResistanceOhms()`, `resistanceBToWOhms()`, `resistanceAToWOhms()`, `maxTerminalCurrentMilliAmps()`, `sanitizeTcon()`, `decodeTcon()` |

The behavioral contract for these — threading, uncertainty, offline latching,
job semantics — is specified in
[API Contract](docs/MCP45HVX1_API_CONTRACT.md). Per-symbol documentation lives
in the headers and in the generated Doxygen output.

### Two things worth knowing up front

**Uncertain hardware state.** If a Wiper or TCON write reaches the bus and then
fails ambiguously (timeout, bus error, data NACK), the driver cannot know
whether the device applied it. It returns the original error, marks the
affected cache unknown, and sets `hardwareStateUncertain()`. Only a successful
readback clears it — `resetI2cState()` does not. Address NACK and validation
failures that happen before any bus access do not set it.

**Offline latch.** After `Config::offlineThreshold` consecutive tracked
failures the driver latches OFFLINE and refuses bus-touching calls until
`recover()` (or `startRecoverJob()`) reads Wiper and TCON successfully.
`probe()` still works while OFFLINE and does not disturb health counters.

## Configuration

| Field | Default | Description |
|---|---:|---|
| `i2cWrite`, `i2cWriteRead` | `nullptr` | Required transport callbacks |
| `i2cUser` | `nullptr` | Context passed to the transport callbacks |
| `busReset` / `controlUser` | `nullptr` | Optional board callback for the documented I2C software-reset sequence |
| `nowMs` / `timeUser` | `nullptr` | Optional monotonic clock for health timestamps |
| `i2cAddress` | `0x3C` | 7-bit address; `0x3C..0x3F` from the A1/A0 straps |
| `i2cTimeoutMs` | `50` | Timeout handed to the transport callbacks |
| `resolution` | `Bits8` | `Bits8` for MCP45HV51, `Bits7` for MCP45HV31 |
| `resistance` | `R10K` | Nominal RAB option, used only for helper math |
| `allowGeneralCall` | `false` | Opt-in for broadcast General Call helpers |
| `writeInitialWiper` / `initialWiperCode` | `false` / `0x7F` | Optional output-changing startup wiper write |
| `writeInitialTcon` / `initialTcon` | `false` / `0xFF` | Optional output-changing startup TCON write |
| `requirePowerOnDefaults` | `false` | Require `begin()` readback to equal POR/BOR defaults |
| `requireReadMsbZero` | `true` | Enforce the documented `0x00` first read byte |
| `offlineThreshold` | `5` | Consecutive tracked failures before OFFLINE |

Callback pointers and user contexts are non-owning and must outlive the driver.
The core does no locking and is not ISR-safe; serialize shared bus access
externally.

## Device Notes

Register map, command framing, addressing, TCON bit semantics, POR/BOR
behavior, resistance equations, SHDN/WLAT interaction, and the DS80000649B
errata gate are documented in [Device Reference](docs/DEVICE_REFERENCE.md).
Two points change how application code should be written:

- **Register readback proves register contents, not analog output.** A high
  `WLAT`, an asserted `SHDN`, or external circuitry can hold or override the
  terminals while writes are accepted and read back normally.
- **Rev A1 silicon is not safe on a shared bus.** DS80000649B Issue 1 states
  the part executes serial data that any *other* client acknowledged. The
  documented workaround is a dedicated bus. This applies to all traffic, not
  only to General Call.

## Examples

The bundled examples are ESP32 diagnostic bring-up tools, not production bus
managers. Production firmware owns bus locking, reset policy, rails, SHDN/WLAT
pins, and safe-load decisions.

- **`examples/01_basic_bringup_cli`** — Arduino/PlatformIO serial CLI.
- **`examples/espidf_basic`** — the same command contract as a native ESP-IDF
  application. See [ESP-IDF Port](docs/IDF_PORT.md).

Both start in a safe mode where every command is read-only or state-restoring.
Commands that can move a live analog output print a warning first and must be
requested explicitly: `selftest output`, `stress_mix [N]`, `raw write`, or
`gc arm` followed by a `gc` command.

Run `help` for the sectioned command list, or `help <command>` (`? <command>`)
for a single entry with its aliases, safety classification, syntax, and
examples. Alias lookup resolves to the canonical command, so `help health`,
`help rreg`, and `? terminal` show the `drv`, `reg`, and `term` entries.
Help lookup never performs the selected operation.

```text
=== Register Snapshot ===
  Wiper: 0x7F (127)
  Position: 0.4980
  R_BW: 4980.4 ohm
  R_AW: 5019.6 ohm
  TCON: 0xFF
  Mode: pot
  Shutdown: no
  Terminals: A=yes W=yes B=yes
```

Use `state` for a parseable one-line summary (including `uncertain=` and
`dirty=`), `drv`/`health` for detailed health and the last uncertainty error,
and `cfg`/`settings` for configuration and cache-known flags. `color off`
disables ANSI output for log capture.

`tools/run_hil_mcp45hvx1.py` drives either CLI over serial to capture
attachable hardware-in-the-loop evidence; see
[Hardware Validation](docs/MCP45HVX1_HARDWARE_VALIDATION.md).

## Building And Testing

ESP32 example builds are pinned to pioarduino `platform-espressif32` 55.03.311
(Arduino-ESP32 3.3.11, ESP-IDF 5.5.5), which needs PlatformIO Core 6.1.19 or
newer. The native ESP-IDF example is pinned separately to ESP-IDF 6.0.1.

```bash
python tools/validate.py     # compiles the core, builds both ESP32 envs,
                             # runs the native tests and every contract guard
```

On Windows, `tools/validate.py` and individual PlatformIO commands go through
`.\scripts\pio.cmd`. Individual steps:

```bash
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
doxygen Doxyfile

cd examples/espidf_basic && idf.py set-target esp32s3 && idf.py build
```

## Packaging

`library.json` and `idf_component.yml` define the export policy. Packages ship
headers, source, examples, and the maintained Markdown docs. Datasheets, tests,
repository tooling, CI metadata, and local build output are excluded to keep
the install artifact focused.

## Documentation

- <a href="docs/README.md">Docs index</a>
- [Device Reference](docs/DEVICE_REFERENCE.md) — device facts and driver assumptions
- [API Contract](docs/MCP45HVX1_API_CONTRACT.md) — behavioral contract
- [Hardware Validation](docs/MCP45HVX1_HARDWARE_VALIDATION.md) — HIL and evidence gates
- [Release Checklist](docs/MCP45HVX1_RELEASE_CHECKLIST.md)
- [ESP-IDF Port](docs/IDF_PORT.md)
- [Datasheets and application notes](docs/reference-pdfs/)
- [Changelog](CHANGELOG.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)

## License

MIT License — see [LICENSE](LICENSE).
