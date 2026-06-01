# MCP45HVX1 CLI Parity And Color Report

Branch: `main`

Base commit inspected: `9a969fd` (`Enhance health diagnostics and output formatting for MCP45HVX1`)

Change commit: recorded in the final coder response after this report is committed.

## Scope

This was a focused CLI parity pass. Code changes were kept inside this
repository. Sibling repositories under `C:\Users\Honza\Documents\Projects` were
read only for CLI patterns.

## Sibling Libraries Inspected

| Library | CLI commands | Color | Health | Selftest | Stress | HIL runner | Notes to borrow |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ADS1115 | ADC read, voltage, mux/gain/rate/mode, comparator, registers | yes | `drv` / state | safe, baseline-aware | conversion stress | no | compact health and selftest rows |
| SSD1315 | display controls, drawing, buffer/page tools, raw commands | yes | `drv`, `health`, counters | draw/flush checks | display stress | no | broad grouped help and status tokens |
| SHT3x | measurement, periodic, heater, status, serial, alerts | yes | `drv`, state | restore cached mode | read stress | no | safe mode restoration |
| BME280 | measurement, config, calib, status, chip ID, regs | yes | `drv`, state | strong baseline/config checks | read and mixed stress | no | closest modern Arduino CLI pattern |
| RV3032-C7 | time, alarms, timer, clkout, offset, status, RAM/EEPROM | yes | `drv` | read-only plausibility checks | command stress | no | dense domain diagnostics |
| PCA9555 | IO/config/polarity, masks, raw regs, patterns | yes | `drv` | save/restore output registers | IO stress | no | explicit restore failure handling |
| INA228 | Arduino plus native ESP-IDF measurement/config/alert CLI | yes | `drv` | identity/MEMSTAT/read checks | read and mixed stress | no | best ESP-IDF parity contract |
| MB85RC | Arduino plus ESP-IDF memory CLI, write/fill/verify/bench | Arduino yes | `drv` | save/restore memory regions | write/read stress | no | best destructive-operation safety pattern |
| MCP45HVX1 before | wiper/TCON, raw regs, General Call, health, selftest, stress | yes, fixed on | `drv`, `detail` | safe-ish read checks | `stress` changed output | no | already had useful command coverage |
| MCP45HVX1 after | grouped safe/read-only/output-changing/dangerous commands | yes, runtime/compile disable | `state`, `drv`, `health`, `cfg` | `selftest safe`, explicit `selftest output` | read-only `stress`, output `stress_mix` | no | matches safety and evidence style without sensor-only commands |

## Gaps Before Changes

- Color was always compiled into output; there was no `color off` command or
  compile-time no-color guard.
- `stress` changed Wiper/TCON despite being presented as a normal diagnostic.
- Help mixed safe diagnostics, output-changing local writes, and bus-wide
  General Call commands.
- Variant, RAB, active address, and cache state were not consistently visible in
  command output.
- `drv` was compact only; there was no separate parseable `state` line and rich
  health view.
- Core timing used an Arduino `millis()` fallback even though `Config::nowMs`
  existed.
- There is no ESP-IDF example in this repository, so Arduino/IDF parity is
  documented as a remaining gap rather than claimed.

## Implemented CLI Improvements

- Added runtime color control with `color`, `color on`, and `color off`.
- Added compile-time no-color support with `MCP45HVX1_CLI_ENABLE_COLOR=0`.
- Added startup/version banner with firmware name, library version, framework,
  target, reset reason, color state, and safe-mode statement.
- Reworked help into Common, Device Selection, Read-only Diagnostics, Safe Tests,
  Output-changing Commands, and Dangerous / Operator-Gated sections.
- Added `variant [hv31|hv51]`, `addr_alt`, `state`, `health`, `readwiper`,
  `readtcon`, `reg`, `raw write`, `terminal`, and `software-shutdown` aliases.
- Extended `rab` to accept numeric values: `5000`, `10000`, `50000`, `100000`.
- Added `wiper percent <0..100>` and `wiper fraction <0.0..1.0>`.
- Made `stress [N]` read-only: probe, read wiper, read TCON.
- Kept output-changing stress behind `stress_mix [N]` with warnings and restore
  reporting.
- Added `selftest safe` and explicit `selftest output`.
- Added warnings before output-changing local writes and magenta danger warnings
  for General Call/raw paths.
- Removed Arduino timing fallback from core; examples inject `Config::nowMs`.

## Safety Classification

Safe/read-only:
`help`, `version`, `scan`, `color`, `verbose`, `addr` with no args, `variant`
with no args, `rab` with no args, `probe`, `cfg`, `settings`, `detail`, `drv`,
`health`, `state`, `read`, `readwiper`, `readtcon`, `reg`/`rreg`, `dump`,
`last`, no-arg `wiper`, `frac`, `tcon`, `term`, `shutdown`, `mode`, `info`,
`errata`, `selftest safe`, `stress`.

State-restoring:
`recover`, `iface_reset`, `selftest output`, `stress_mix`.

Output-changing:
`wiper <code>`, `wiper percent`, `wiper fraction`, `frac <value>`, `zero`,
`mid`, `max`, `inc`, `dec`, `tcon <value|default>`, `term`/`terminal` set,
`shutdown`, `software-shutdown`, `mode`, `defaults`.

Dangerous / operator-gated:
`raw write`, `wreg`, `wregs`, `gc arm` plus `gc wiper`, `gc tcon`, `gc inc`,
`gc dec`.

Safety note: register readback does not prove physical output movement when WLAT
or SHDN external pins override the analog state, and software cannot enforce
analog rail voltage/current safety.

## Tests And Checks Run

Run during implementation:

```text
python tools/validate.py
```

That executed and passed:

```text
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py   # skipped: no ESP-IDF example present
python tools/check_core_timing_guard.py
python tools/check_generated_artifacts.py
python scripts/generate_version.py check
g++ core compile without Arduino stubs
g++ Arduino example compile with color enabled
g++ Arduino example compile with color disabled
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
```

`pio test -e native` passed 39/39 tests. ESP32-S3 and ESP32-S2 PlatformIO builds
passed. PlatformIO printed an obsolete-core warning, but builds and tests
completed successfully.

## Remaining Gaps

- No native ESP-IDF MCP45HVX1 example exists yet. The new IDF contract script
  enforces rules if an IDF example is added, but currently reports a skip.
- No automated HIL/serial evidence runner was found in the sibling repositories
  or this repo.
- CLI output is statically guarded and build-tested, but there are no golden
  serial-output unit tests because the current native Serial stub discards text.
- `raw write`/`wreg` remain explicit command-gated rather than interactive
  confirmation-gated so automated HIL remains possible.
