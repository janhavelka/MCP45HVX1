# MCP45HVX1 Driver Library

Framework-neutral, production-oriented MCP45HVX1 high-voltage I2C digital
potentiometer driver for ESP32 (Arduino/PlatformIO and ESP-IDF). The repository
emphasizes deterministic software behavior, explicit hardware evidence gates,
and maintained documentation.

Current release status: v1.0.0 pre-production software package. Local software
validation and an ESP32-S2 safe-only HIL run have passed, but this is not a
production-readiness, analog-accuracy, high-voltage-safety, or General Call
safety claim.

## Features

- **Injected I2C transport** - no `Wire` dependency in library code
- **Health monitoring** - READY/DEGRADED/OFFLINE state tracking
- **Volatile register API** - Wiper 0 and TCON0 read/write helpers
- **Wiper commands** - direct write plus documented INC/DEC commands
- **Terminal control** - potentiometer, rheostat, floating wiper, and TCON software shutdown presets
- **General Call helpers** - broadcast wiper/TCON write and wiper INC/DEC frames, with explicit `GCEN` caveats
- **Bus-interface reset hook** - optional board callback for the documented software-reset/bus-release sequence
- **Variant helpers** - 7-bit/8-bit code limits, terminal-current limits, nominal resistance, and position conversion helpers
- **Comprehensive bring-up CLI** - colored diagnostics, safe read-only stress, explicit output-changing commands, and General Call gating
- **Deterministic behavior** - no heap allocation in the core driver
- **Settings snapshot** - runtime config, cache, uncertainty, and health counters
- **Native tests** - fake-bus protocol tests for register and command behavior

## Installation

### PlatformIO

```ini
lib_deps =
  https://github.com/janhavelka/MCP45HVX1.git
```

### Manual

Copy `include/MCP45HVX1/` and `src/` to your project. The Quick Start also
uses the optional example Wire adapter from `examples/common/I2cTransport.h`;
copy that file too, or provide equivalent `Config` transport callbacks.

### ESP-IDF Component

This repository also builds as a pure ESP-IDF component. Add the repo as an
extra component or dependency, then include `MCP45HVX1/MCP45HVX1.h` and provide
`Config::i2cWrite` / `Config::i2cWriteRead` callbacks from your project-owned
I2C master bus.

The ESP-IDF bring-up CLI is implemented as a native IDF example with the same
command contract as the Arduino CLI:

```bash
cd examples/espidf_basic
idf.py set-target esp32s3
idf.py build
```

The ESP-IDF example uses `app_main`, `driver/i2c_master.h`, `esp_timer`,
`vTaskDelay`, and fixed C command buffers. It does not include Arduino CLI
sources or compatibility facades.

Software validation status: command parity is checked by repo-local contract
scripts. The pure ESP-IDF build is configured in CI; local `idf.py` may be
absent on developer machines, so local IDF build success must not be claimed
unless an actual `idf.py` log is recorded. The latest bundled HIL evidence is a
safe-only ESP32-S2 Arduino/PlatformIO CLI run; pure ESP-IDF hardware smoke,
output-changing, analog, high-voltage, and General Call validation remain
separate evidence gates.

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
  Wire.setTimeOut(50);

  MCP45HVX1::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  cfg.nowMs = nowMs;
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
`examples/common/I2cTransport.h`. Applications that need meaningful health
timestamps should inject `Config::nowMs`; otherwise timestamps remain `0`.

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - validate config, read Wiper/TCON first, then apply explicit output-changing startup writes if enabled
- `void tick(uint32_t nowMs)` - no-op reserved hook
- `void end()` - reset driver object state without changing device terminals

### Diagnostics

- `Status probe()` - raw Wiper read without health-counter or Wiper/TCON cache updates
- `Status recover()` - tracked Wiper/TCON reads to refresh cache, clear verified uncertainty, and return READY only after both reads succeed
- `Status resetI2cState()` - run optional board-provided I2C bus/software-reset callback without proving device READY
- `Status restorePowerOnDefaults()` / `resetToDefaults()` - write documented volatile defaults
- `Status readSnapshot(RegisterSnapshot& snapshot)` - read Wiper then TCON and publish the caller snapshot only after both reads succeed
- `SettingsSnapshot getSettings()` - config, cache, and health snapshot
- `Status getSettings(SettingsSnapshot& out)` - output-parameter snapshot form matching sibling drivers
- `DriverState state()` / `driverState()` - current health state
- `bool isInitialized()` / `isOnline()` - lifecycle and health convenience checks
- `const Config& getConfig()` - active copied configuration
- `bool hardwareStateUncertain()` - true after an ambiguous state-changing write failure until readback verifies affected state
- `Status hardwareStateUncertainError()` - last ambiguous failure that set hardware uncertainty
- `DeviceInfo getDeviceInfo()` - active address, resolution, nominal RAB, step size, terminal-current limit, defaults
- `SiliconErrataInfo siliconErrataInfo()` - static DS80000649B errata summary for diagnostics and release gates
- `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, `totalSuccess()` - tracked health counters and timestamps

### Poll-Chunked Jobs

The synchronous raw APIs remain available, but side-effecting or multi-step
work can also be driven as explicit jobs:

- `Status startSetWiperJob(uint8_t code)`
- `Status startReadSnapshotJob()`
- `Status startSetTerminalJob(Terminal terminal, bool enabled)`
- `Status startIncrementWiperJob(uint8_t steps = 1)`
- `Status startDecrementWiperJob(uint8_t steps = 1)`
- `Status startRecoverJob()`
- `Status pollJob(uint32_t nowMs, uint8_t maxInstructions)`
- `JobSnapshot getJobSnapshot()`
- `Status getJobSnapshot(JobSnapshot& out)`
- `bool jobActive()`

A register read or command-write chunk is counted as one instruction.
`startReadSnapshotJob()` reads Wiper then TCON as separate instructions.
`startSetTerminalJob()` exposes the TCON read-modify-write as separate read and
write phases, and completes after the read if the requested bit already matches.
Wiper step jobs split requests into bounded command chunks and execute at most
one chunk per poll. `startSetWiperJob()` is always one instruction.

While a job is active, other bus-touching public APIs return `Err::BUSY` so an
application-owned I2C task can keep ordering explicit. Ambiguous output-changing
job failures use the same hardware-uncertainty and cache-invalidating semantics
as the synchronous APIs. `startRecoverJob()` is the only job that may be started
while OFFLINE; if it starts offline, the offline latch remains asserted until
both Wiper and TCON readback instructions succeed.

### Wiper

- `Status readWiper(uint8_t& code)`
- `Status writeWiper(uint8_t code)`
- `Status incrementWiper(uint8_t steps = 1)`
- `Status decrementWiper(uint8_t steps = 1)`
- `uint8_t codeFromFraction(float fraction, Resolution resolution)`
- `float fractionFromCode(uint8_t code, Resolution resolution)`
- `Status readWiperFraction(float& fraction)`
- `Status writeWiperFraction(float fraction)` - rejects values outside `0.0..1.0`
- `maxWiperCode()`, `defaultWiperCode()`, `nominalResistanceOhms()` - variant and ordering helpers
- `stepResistanceOhms()`, `resistanceBToWOhms()`, `resistanceAToWOhms()` - ideal helper math only
- `maxTerminalCurrentMilliAmps()` - datasheet terminal-current limit by RAB option

`codeFromFraction()` clamps helper input to the configured resolution and
`fractionFromCode()` clamps raw codes before converting to a normalized value.
`writeWiperFraction()` is output-changing and rejects values outside the
inclusive `0.0..1.0` range with `Err::INVALID_PARAM`; it sends no I2C write for
out-of-range values.

### Terminal Control

- `Status readTcon(uint8_t& value)`
- `Status writeTcon(uint8_t value)` - forces reserved bits `[7:4]` high
- `Status setTerminalEnabled(Terminal terminal, bool enabled)`
- `Status getTerminalEnabled(Terminal terminal, bool& enabled)`
- `Status setSoftwareShutdown(bool enabled)`
- `Status getSoftwareShutdown(bool& enabled)`
- `Status setTerminalMode(TerminalMode mode)`
- `Status getTerminalMode(TerminalMode& mode)`
- `Status readTerminalStatus(TerminalStatus& status)`
- `TerminalStatus decodeTcon(uint8_t value)`
- `uint8_t sanitizeTcon(uint8_t value)` - force reserved TCON bits `[7:4]` high before writes

`TerminalMode::Custom` is returned for valid TCON bit combinations that do not
match a named preset. It is a decoded state, not a valid argument to
`setTerminalMode()`.

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
documented. This library sends only the documented General Call frames, and the
core helpers are disabled unless `Config::allowGeneralCall` is explicitly true.
Because General Call ACKs are broadcast and not device-specific, successful
General Call helpers mark the affected local cache entry unknown; call
`readSnapshot()` to verify local state afterward.

Production firmware must treat General Call as a release-gated feature. Review
`DS80000649B` and any newer errata against the actual package marking/date code
before release. Output-changing General Call commands require isolated-bus
evidence, or a documented risk acceptance for affected or unknown silicon.
Shared-bus deployments must not claim General Call safety without logged
evidence and signoff.

The Arduino and ESP-IDF diagnostic CLIs opt into the core General Call helpers
so their `gc arm` command can remain functional. That opt-in is not a
production bus-manager policy; it is paired with one-shot arming, warnings, and
evidence requirements in the examples.

## Uncertain Hardware State

MCP45HVX1 Wiper and TCON writes can change real analog and high-voltage
circuits. If a state-changing transaction reaches the I2C bus and then returns
an ambiguous transport failure, the driver preserves the original `Status` and
marks the affected cache unknown. `SettingsSnapshot::hardwareStateUncertain`
and `hardwareStateUncertain()` remain true until successful readback verifies
every affected volatile register.

Validation failures before I2C access, unsupported commands, offline `BUSY`,
device-not-found/register-mismatch results, and address NACKs do not set this
flag. Generic I2C failures, data NACKs, timeouts, and bus errors can mean bytes
were accepted before the host observed failure, so they set uncertainty for the
affected Wiper or TCON state. `resetI2cState()` does not clear uncertainty; use
`readWiper()`, `readTcon()`, `readSnapshot()`, or `recover()` to perform real
readback/resync.

`codeFromFraction()` is a pure conversion helper and clamps out-of-range input
to the nearest endpoint. `writeWiperFraction()` is output-changing and rejects
out-of-range or NaN input with `Err::INVALID_PARAM` instead of clamping, so
caller mistakes do not silently drive high-voltage analog output to an endpoint.

`begin()` uses the same uncertainty model for optional startup writes. Baseline
Wiper/TCON reads must succeed before any configured startup write is attempted.
If an optional startup write then fails, `begin()` returns the original error
but preserves the runtime config and initialized transport state so
`readWiper()`, `readTcon()`, `readSnapshot()`, or `recover()` can inspect the
possibly changed volatile hardware state.

For `begin()` and `probe()` presence checks, a definite address NACK is reported
as `Err::DEVICE_NOT_FOUND` with the callback detail value preserved. Timeouts,
bus errors, data NACKs, generic I2C errors, and register-format mismatches are
returned with their original public status code.

## Configuration

| Field | Default | Description |
|---|---:|---|
| `i2cAddress` | `0x3C` | A1=A0=0 address |
| `resolution` | `Bits8` | `Bits8` for MCP45HV51, `Bits7` for MCP45HV31 |
| `resistance` | `R10K` | Nominal RAB option for helper math |
| `allowAlternateAddressRange` | `false` | Opt-in disputed `0x5C-0x5F` address range |
| `allowGeneralCall` | `false` | Explicit opt-in for unsafe broadcast General Call helpers |
| `i2cTimeoutMs` | `50` | Transport timeout |
| `busReset` | `nullptr` | Optional board callback for I2C bus/software reset |
| `controlUser` | `nullptr` | Context pointer for `busReset` |
| `writeInitialWiper` | `false` | Explicit output-changing Wiper write during `begin()` |
| `initialWiperCode` | `0x7F` | Wiper value used when startup Wiper write is enabled |
| `writeInitialTcon` | `false` | Explicit output-changing TCON write during `begin()` |
| `initialTcon` | `0xFF` | TCON value used when startup TCON write is enabled |
| `requirePowerOnDefaults` | `false` | Require Wiper/TCON readback to equal POR/BOR defaults during `begin()` |
| `requireReadMsbZero` | `true` | Enforce documented read MSB byte `0x00` |
| `offlineThreshold` | `5` | Consecutive tracked failures before OFFLINE |

## Runtime Model

The core driver is synchronous and transport-agnostic. It does not allocate heap
memory or own an Arduino `Wire` instance; callers inject I2C callbacks through
`Config`. Callback pointers and `i2cUser`/`controlUser`/`timeUser` contexts are
non-owning and must outlive every driver call that can use them. Calls are not
internally locked, so share one driver instance from a single task/thread at a
time or serialize access in the application.

The public API is not ISR-oriented. I2C transports, callbacks, and optional bus
reset hooks are expected to run from normal task context where blocking I2C
transactions and timeout handling are acceptable.

`MCP45HVX1` objects are not copyable or movable. A driver instance owns runtime
health counters, volatile register-cache knowledge, and non-owning callback
contexts; create one instance per physical device/bus binding.

Health tracking is latched at `DriverState::OFFLINE`: after the configured
consecutive tracked-failure threshold is reached, normal public I2C operations
return `Err::BUSY` with `Driver is offline; call recover()` and do not call the
I2C transport. `probe()` may still perform a raw presence check without changing
health counters, Wiper/TCON cache state, or READY/DEGRADED/OFFLINE state; the
raw read may update address-pointer knowledge. `resetI2cState()` may run the
configured bus-reset callback and records reset failures, but a
successful callback is not device proof: it does not clear uncertainty, refresh
cache, or mark a DEGRADED/OFFLINE driver READY. Call `recover()` or perform a
tracked device read to prove the device responds.

`recover()` does not run the bus-reset callback. It reads Wiper first and TCON
second using tracked transactions. Both reads must succeed before it returns OK
and READY; readback clears hardware uncertainty only for the volatile registers
that were verified. If recovery starts from OFFLINE and only partially succeeds,
the OFFLINE latch is restored.

Hardware uncertainty is separate from `DriverState`. A device can be READY while
the analog state is still uncertain after an ambiguous failed Wiper/TCON or
General Call write. Inspect `cachedWiperKnown`, `cachedTconKnown`,
`hardwareStateUncertain`, and `hardwareStateUncertainError` before trusting the
physical output after a write failure.

## Device Notes

- The implemented registers are `0x00` Wiper 0 and `0x04` TCON0. All other
  addresses are treated as reserved.
- MCP45HV31 is the 7-bit/128-tap variant with full-scale code `0x7F` and
  POR/BOR Wiper default `0x3F`. MCP45HV51 is the 8-bit/256-tap variant with
  full-scale code `0xFF` and POR/BOR Wiper default `0x7F`.
- Reads return two bytes on the bus. The driver checks that the first byte is
  `0x00` and returns the second byte as the register value.
- TCON0 is register `0x04`; reserved bits `[7:4]` are forced high on writes and
  expected high on readback. TCON POR/BOR default is `0xFF`.
- `WLAT` is a hardware pin. When high, it can hold physical wiper movement even
  though Wiper register writes are accepted and read back.
- `SHDN` is an external active-low hardware pin. It overrides terminal
  connectivity but does not corrupt Wiper/TCON registers. Core APIs named
  `shutdown` control TCON software shutdown via R0HW, not the SHDN pin.
- Register readback proves volatile Wiper/TCON contents only. It does not prove
  physical analog movement when WLAT is high, SHDN is asserted, or external
  circuitry overrides the output.
- `begin()` does not write the analog state by default. Enable the initial-write
  flags only when that startup behavior is intentional.
- Optional startup writes are two-phase: read Wiper/TCON first, then write TCON
  before Wiper if enabled. A failed optional write can leave the driver
  initialized but degraded/uncertain so the application can read back or recover.
- DS20005304B specifies `TBORD`, delay after device exits reset state with
  `VL > VBOR`, as 10 us typical and 20 us maximum. No separate `tPOR` ready
  delay is identified in the local datasheet extract. The core does not own rail
  sequencing, reset supervision, SHDN/WLAT pins, or startup delays; applications
  must ensure digital and analog rails and pins are stable before `begin()`.
  `Config::nowMs` is used for health timestamps, not for delay during
  `begin()` or `recover()`.

## Address Note

DS20005304B Rev B Table 6-2 and the official PDF text give fixed address bits
`01111`, so the default address range is `0x3C-0x3F`. The maintained device
reference records an apparent `0x5C-0x5F` conflict from command figures. The
driver defaults to `0x3C` and accepts `0x5C-0x5F` only when
`Config::allowAlternateAddressRange` is explicitly enabled for
hardware-verification builds. See `ASSUMPTIONS.md` and
`docs/DEVICE_REFERENCE.md`.

Hardware validation must verify the populated A1/A0 address, silicon marking
and errata applicability, WLAT and SHDN board strap behavior, General Call
isolation or documented risk acceptance, and analog terminal limits for the
selected RAB option. Opt-in startup writes and other output-changing tests must
be measured on a safe load and restored before production use. The helper math
is idealized and does not include tolerance, wiper resistance, leakage, INL/DNL,
or board-level loading.

## Examples

The bundled examples are diagnostic bring-up tools. They are not production bus
managers: production firmware must own bus locking, reset policy, timeout
policy, rails, SHDN/WLAT pins, safe-load decisions, and any operator gating.

### 01_basic_bringup_cli

Interactive serial CLI with commands for bus scan, begin/reconfigure,
probe/recover, settings, Wiper/TCON reads and writes, direct register access,
terminal modes, General Call frames, volatile defaults, I2C interface reset, and
safe/default stress passes.

The CLI starts in safe mode. `selftest` and `stress [N]` are read-only or
state-restoring and do not intentionally leave Wiper/TCON changed. Commands that
can affect a live analog/high-voltage circuit print warnings before issuing the
write. Use explicit commands such as `selftest output`, `stress_mix [N]`,
`raw write <reg> <value>`, or `gc arm` plus `gc ...` for output-changing or
bus-wide operations.

The CLI follows the same line-oriented output style as the sibling I2C
bring-up examples. Normal commands print compact sections and two-space
indented rows:

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

Use `state` for the parseable one-line state summary, including
`uncertain=yes/no`; `drv` or `health` for detailed driver health and the last
uncertainty error; and `detail`, `cfg`, or `settings` for configuration and
explicit Wiper/TCON cache-known flags. Raw hardware-oriented output is kept
behind explicit commands:
`raw`/`dump` adds the last-address pointer, `reg`/`rreg` read direct registers,
and `raw write`/`wreg` write volatile registers. `color off` disables ANSI escape
codes for logs, and `verbose on` enables per-operation details during
long-running diagnostics.

The default stress command is read-only and reports progress at fixed intervals
with totals, timing, health-counter deltas, first/last failure detail when
applicable, and a final health line:

```text
[I] Starting read-only stress: 8 iterations, 24 operations
  Progress: 12/24 (50%, ok=12, fail=0)
=== Stress Summary ===
  Test: stress
  Target: 8 iterations
  Mode: read-only probe/readWiper/readTCON
  Output changes: no
  Attempts: 24/24
  Success: 24
  Errors: 0
  Success rate: 100.00%
  Restore: not needed
  Result: PASS
```

ANSI color is enabled by default and applied only to status, warning, pass/fail,
and counter tokens through the shared `Log.h`/`CliStyle.h` helpers. Disable it
at runtime with `color off` or at compile time with
`-DMCP45HVX1_CLI_ENABLE_COLOR=0`; plain serial logs remain readable either way.
To capture diagnostics for a failure, run `scan`, `probe`, `read`, `state`,
`drv`, `detail`, the failing command, and then `verbose on` plus `stress [N]`.

Typical commands:

```text
scan
color off
cfg
state
drv
read
wiper 0x80
wiper percent 50
frac 0.5
mode bw
terminal a off
tcon 0xFF
gc arm
gc inc
dump
drv
```

### espidf_basic

Native ESP-IDF build of the bring-up CLI command contract. It uses
`app_main`, `driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, and fixed C
buffers. The example explicitly supports the MCP45HVX1 last-address read format
(`txLen == 0`) and General Call writes to address `0x00` using ESP-IDF defined
I2C operations with manual address bytes. `tools/check_idf_example_contract.py`
rejects Arduino compatibility facades, placeholder command behavior, unsafe
parse-to-byte casts, no-op color support, and selftest-output mismatches.

The IDF CLI uses bounded command parsers before all byte-sized writes, has
optional ANSI color via `color on|off`, reports uncertainty/cache-known fields
in `state`, `drv`/`health`, and `cfg`, and implements `selftest output` as an
explicit output-changing, state-restoring test. `gc arm` prints bus-wide,
DS80000649B, and isolated-bus-evidence warnings before enabling one broadcast
General Call command.

## HIL Evidence Capture

`tools/run_hil_mcp45hvx1.py` captures attachable hardware-in-loop evidence from
the Arduino or ESP-IDF CLI over serial. The default sequence is safe/read-only:
`version`, `color off`, `help`, `scan`, `addr`, optional address selection,
`probe`, `cfg`, `settings`, `state`, `drv`, `health`, `readwiper`,
`readtcon`, `dump`, `selftest safe`, `stress 100`, final `state`, and final
`drv`.

```bash
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C --include-output-change --operator-prompts
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C --include-general-call --confirm-isolated-bus --operator-prompts
```

Output-changing groups require explicit flags and `--operator-prompts`; the
operator must confirm a safe load and measurement setup before local Wiper/TCON
changes run. `--include-output-change` captures Wiper/TCON baseline, writes
bounded values, reads back, restores the baseline, and reports
`FAIL_RESTORE_UNCERTAIN` if restore cannot be verified.
`--include-shdn` and `--include-wlat` require `--operator-prompts` because
physical pin behavior must be observed separately from register readback.
`--include-general-call` requires `--confirm-isolated-bus` and
`--operator-prompts` so the operator confirms the isolated bus and safe-load
setup before any broadcast command is sent. The runner records the
errata/isolation warning in the evidence.

Each run writes `hil_logs/mcp45hvx1_<timestamp>/` with `raw_serial.txt`,
`commands.txt`, `summary.json`, `report.md`, and `operator_notes.md`.
Hardware validation is not claimed unless the HIL runner was actually run and
the resulting evidence bundle is attached to the release or validation record.
The HIL runner is repository tooling and is excluded from normal PlatformIO
package exports; use the full repository when capturing HIL evidence. The
ignored `hil_logs/` directory is a local staging area: archive a required bundle
or commit a curated transcript report before clearing it.

Current bundled safe-only evidence:

- [8-hour ESP32-S2 safe-only HIL report](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-validation-COM8-20260629.md):
  `PASS_SAFE_ONLY`, `183221 / 183221 / 0` soak commands, worst latency
  `0.188 s`, no output-changing groups requested.
- [1-hour panic-repro safe-only HIL report](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-panic-repro-COM8-20260629.md):
  `PASS_SAFE_ONLY`, `23056 / 23056 / 0` soak commands.

These reports support safe/read-only CLI behavior on the recorded fixture only.
They do not support analog movement, terminal-current, high-voltage, SHDN/WLAT,
rail-cycle, fault-injection, or General Call safety claims.
The report markdown files are repository/release-source evidence and are not
included in the normal PlatformIO package archive.

## Running Tests

The repository `platformio.ini` pins ESP32 example builds to pioarduino
`platform-espressif32` 55.03.311 (Arduino-ESP32 3.3.11 with bundled ESP-IDF
5.5.5), which requires PlatformIO Core 6.1.19 or newer. `Wire` is supplied by
the Arduino ESP32 framework; applications that consume this library through
`lib_deps` do not need to add a separate `Wire` dependency. The native ESP-IDF
example and its CI build remain independently pinned to ESP-IDF 6.0.1.

On Windows, use `.\scripts\pio.cmd` for individual PlatformIO commands. It
selects the current user's VS Code-managed PlatformIO Core and fails clearly if
that installation is unavailable. `python tools/validate.py` selects this
wrapper automatically. On other platforms, the examples below use `pio` from
`PATH`.

```bash
python tools/validate.py
python -m py_compile scripts/generate_version.py tools/run_hil_mcp45hvx1.py tools/test_run_hil_mcp45hvx1_parser.py tools/check_generated_artifacts.py tools/check_cli_contract.py tools/check_idf_example_contract.py tools/check_core_timing_guard.py
pio run -e esp32s3dev
pio run -e esp32s2dev
pio test -e native
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/check_core_timing_guard.py
python tools/check_generated_artifacts.py
python tools/test_run_hil_mcp45hvx1_parser.py
python scripts/generate_version.py check

# Build the ESP-IDF full CLI example (requires ESP-IDF on PATH)
cd examples/espidf_basic
idf.py set-target esp32s3
idf.py build
```

## Packaging

`library.json` defines the package export policy. Normal PlatformIO packages
include headers, source, examples, metadata, and current core docs. Datasheets,
tests, tools, CI metadata, local build output, and transcript reports are
intentionally excluded from normal packages to keep the install artifact
focused and reproducible. The full reference corpus, curated transcript
reports, and release tooling remain in the repository for audit and validation.

## Documentation

- [Docs Index](docs/README.md)
- [Assumptions](ASSUMPTIONS.md)
- [Changelog](CHANGELOG.md)
- <a href="docs/DEVICE_REFERENCE.md">Device Reference</a>
- <a href="docs/MCP45HVX1_API_CONTRACT.md">API Contract</a>
- <a href="docs/MCP45HVX1_HARDWARE_VALIDATION.md">Hardware Validation</a>
- <a href="docs/MCP45HVX1_RELEASE_CHECKLIST.md">Release Checklist</a>
- <a href="docs/IDF_PORT.md">ESP-IDF Port</a>
- [Datasheets and application notes](docs/reference-pdfs/)
- [Curated HIL transcript reports](docs/reports/)
- `Doxyfile` indexes the public headers, maintained docs, the Arduino CLI, and
  the native IDF entry point.

## License

MIT License - see [LICENSE](LICENSE) for details.
