# MCP45HVX1 Production-Readiness Exploration Report

Date: 2026-06-01
Repository: C:/Users/Honza/Documents/Projects/MCP45HVX1
Branch: audit/mcp45hvx1-production-readiness-exploration
Starting commit: 6985d66
Audit mode: exploration only / no implementation

## Executive Summary

The repository is no longer an early prototype. The core driver is framework-neutral, uses injected transport callbacks, has a meaningful `Status` model, supports MCP45HV31/MCP45HV51 variant limits, has native tests, has an Arduino CLI with unusually strong diagnostics and color control, and now contains a native ESP-IDF component/example boundary.

It is not yet industry-grade. The main blockers are not style issues; they are field-failure and evidence issues:

- state-changing write failures can leave stale "known" cached Wiper/TCON state;
- optional initial writes during `begin()` can affect analog hardware and then return an uninitialized driver with no read/recover path;
- hardware validation is still pending for address, WLAT, SHDN, POR/BOR, terminal limits, and errata/isolated-bus behavior;
- the ESP-IDF CLI has command-token parity but not full behavioral parity;
- CI does not run a real `idf.py build`;
- package/release docs still overclaim production readiness before fault/HIL evidence exists.

The project is ready for implementation hardening chunks. It is not ready to release as production/industry-grade.

## Readiness Classification

Classification: **Engineering-grade with major gaps**.

Why:

- Core architecture and native test coverage are strong enough for engineering use.
- CLI diagnostics are much better than minimal bring-up tooling.
- However, there is no hardware validation evidence, no HIL runner, no ESP-IDF CI build, and no complete handling of ambiguous hardware state after failed writes.
- The library metadata and README still use "Production-grade" wording while the same README says hardware smoke tests are pending (`README.md:3`, `README.md:56`).

## Scope Reviewed

Reviewed:

- Public headers: `include/MCP45HVX1/CommandTable.h`, `Config.h`, `MCP45HVX1.h`, `Status.h`, `Version.h`.
- Core implementation: `src/MCP45HVX1.cpp`.
- Arduino CLI and helpers: `examples/01_basic_bringup_cli/main.cpp`, `examples/common/*`.
- ESP-IDF component/example: `CMakeLists.txt`, `idf_component.yml`, `examples/espidf_basic/*`.
- Native tests and stubs: `test/test_basic.cpp`, `test/stubs/*`.
- Tooling and guards: `tools/*.py`, `scripts/generate_version.py`.
- CI and package metadata: `.github/workflows/ci.yml`, `platformio.ini`, `library.json`, `idf_component.yml`.
- Documentation: `README.md`, `ASSUMPTIONS.md`, `docs/*.md`, `docs/pdf-extracted-md/*.md`, prompt files.
- Local datasheet/errata PDFs and extracted markdown.
- Sibling repositories under `C:/Users/Honza/Documents/Projects` as read-only references.

## Workspace Reference Libraries Inspected

| Library | Path | Why inspected | Useful patterns |
| --- | --- | --- | --- |
| ADS1115 | `../ADS1115` | Mature sensor CLI baseline | Status rows, selftest, stress, `drv`/state style |
| SSD1315 | `../SSD1315` | Deep CLI surface and display diagnostics | Grouped command help, extensive diagnostics without overloading normal output |
| SHT3x-main | `../SHT3x-main` | Sensor health/stress style | Periodic/safe test split, status handling |
| BME280 | `../BME280` | Best Arduino CLI style match | Grouped help, color helpers, selftest/stress summaries |
| RV3032-C7 | `../RV3032-C7` | Domain-specific CLI and validity checks | Status/validity diagnostics and restore-conscious commands |
| PCA9555 | `../PCA9555` | Output-changing I/O expander model | Save/restore style for output/config tests |
| INA228 | `../INA228` | Best dual Arduino/ESP-IDF reference | Native IDF CLI boundary and contract checks |
| INA3221 | `../INA3221` | Dual-framework reference | Native IDF parser shape and measurement CLI parity |
| MB85RC | `../MB85RC` | Best destructive-operation safety reference | Backup/write/verify/restore and restore-status reporting |

No automated HIL runner was found in the inspected sibling libraries. Their evidence is mostly native tests, PlatformIO builds, contract scripts, and documented manual/hardware procedures.

## MCP45HVX1 Datasheet / Documentation Sources

Local sources found:

- `docs/MCP45HVX1-Data-Sheet-DS20005304.pdf`
- `docs/MCP45HVX1-Data-Sheet-DS20005304-Duplicate.pdf`
- `docs/pdf-extracted-md/MCP45HVX1-Data-Sheet-DS20005304.md`
- `docs/pdf-extracted-md/MCP45HVX1_Errata_DS80000649B.md`
- `docs/01_chip_overview.md`
- `docs/02_pinout_and_signals.md`
- `docs/03_electrical_and_timing.md`
- `docs/04_protocol_commands_and_transactions.md`
- `docs/05_register_map.md`
- `docs/06_modes_interrupts_status_and_faults.md`
- `docs/07_initialization_reset_and_operational_notes.md`
- `docs/08_variant_differences_and_open_questions.md`
- `docs/register_reference.md`
- `ASSUMPTIONS.md`

Verified facts from local docs/code:

- MCP45HV31 is 7-bit / 128 taps; MCP45HV51 is 8-bit / 256 taps (`docs/01_chip_overview.md:23`, `docs/01_chip_overview.md:24`).
- Wiper POR/BOR defaults are 7-bit `0x3F`, 8-bit `0x7F`; code constants match (`include/MCP45HVX1/CommandTable.h:85`).
- Implemented volatile registers are Wiper0 `0x00` and TCON0 `0x04` (`docs/05_register_map.md:10`, `docs/05_register_map.md:24`).
- TCON reserved bits are forced high by the driver (`src/MCP45HVX1.cpp:362`, `src/MCP45HVX1.cpp:532`).
- Standard I2C address range is `0x3C-0x3F`; alternate `0x5C-0x5F` is treated as disputed and opt-in (`ASSUMPTIONS.md:3`, `include/MCP45HVX1/CommandTable.h:16`, `src/MCP45HVX1.cpp:68`).
- SHDN and WLAT are hardware pins; software readback cannot prove physical output movement if those pins override behavior (`README.md:214`, `README.md:215`).
- Software cannot enforce analog rail, terminal-current, silicon-revision, GCEN, or actual bus-isolation safety.

## Current Repository Inventory

Core:

- `include/MCP45HVX1/CommandTable.h` - command bytes, registers, defaults, address ranges.
- `include/MCP45HVX1/Config.h` - non-owning transport/timing/reset callbacks.
- `include/MCP45HVX1/MCP45HVX1.h` - public driver API, state snapshots, device info.
- `include/MCP45HVX1/Status.h` - status/error model.
- `src/MCP45HVX1.cpp` - implementation.

Examples:

- Arduino CLI: `examples/01_basic_bringup_cli/main.cpp`.
- Arduino helper layer: `examples/common/*`.
- Native ESP-IDF CLI: `examples/espidf_basic/main/main.cpp`.

ESP-IDF component files:

- `CMakeLists.txt`
- `idf_component.yml`
- `examples/espidf_basic/CMakeLists.txt`
- `examples/espidf_basic/main/CMakeLists.txt`

Tests/tools:

- Native Unity tests: `test/test_basic.cpp`.
- Test stubs: `test/stubs/Arduino.h`, `test/stubs/Wire.h`.
- Guards: `tools/check_cli_contract.py`, `tools/check_core_timing_guard.py`, `tools/check_idf_example_contract.py`, `tools/check_generated_artifacts.py`.
- Validation wrapper: `tools/validate.py`.
- Version generator: `scripts/generate_version.py`.

Docs:

- README, assumptions, register reference, extracted datasheet docs, hardware validation checklist, IDF port docs, CLI reports, prompt plan.
- Duplicate datasheet PDF is tracked and called out in `docs/00_document_inventory.md:15`.

CI/package metadata:

- `.github/workflows/ci.yml`
- `platformio.ini`
- `library.json`
- `idf_component.yml`

Existing stale notes:

- `docs/MCP45HVX1_CLI_PARITY_AND_COLOR_REPORT.md` still says no ESP-IDF example existed and lists IDF as a remaining gap (`docs/MCP45HVX1_CLI_PARITY_AND_COLOR_REPORT.md:41`, `docs/MCP45HVX1_CLI_PARITY_AND_COLOR_REPORT.md:119`).
- `docs/cli_output_style_alignment_report.md` says there is no runtime color toggle, while current CLI and README document `color off` (`docs/cli_output_style_alignment_report.md:160`, `README.md:293`).
- `SECURITY.md` mentions NVS side effects, which does not match this volatile-only library (`SECURITY.md:28`).
- `CONTRIBUTING.md` references `.clang-format`, but no `.clang-format` is tracked (`CONTRIBUTING.md:17`).

## Scorecard

| Area | Rating | Notes |
| --- | --- | --- |
| Core framework neutrality | Strong | No Arduino/Wire/ESP-IDF/FreeRTOS dependencies found under `include/` or `src/`; guard passes. |
| I2C ownership/injection | Strong | Core uses injected callbacks from `Config`; examples own framework-specific buses. |
| MCP45HV31/HV51 variant handling | Good | Limits/defaults enforced and tested, but HIL does not cover both variants. |
| Address policy | Good | Standard range default, alternate range opt-in; tests do not cover every candidate address. |
| Wiper/TCON semantics | Good | Register model, reserved bits, and conversion helpers implemented; ambiguous failed writes are not safe enough. |
| SHDN/WLAT documentation | Medium | Mentioned in README/docs, but HIL checklist is too thin. |
| High-voltage safety | Medium | CLI warnings exist; release docs still overclaim and no bench evidence exists. |
| Status/error model | Good | Error vocabulary is broad; `begin()`/`probe()` collapse transport distinctions. |
| Health/recovery model | Medium | OFFLINE/recover is tested; bus reset can mark READY without a device read. |
| Partial/uncertain hardware state handling | Weak | Main production blocker: failed writes can leave stale known cache and failed initial writes hide possible side effects. |
| Thread/ISR contract | Medium | README states caller serialization and not ISR-oriented; public header contract is thin. |
| CLI depth and color | Good | Arduino CLI is strong; IDF color/status/state parity is incomplete. |
| Arduino example readiness | Good | Builds locally; rich CLI; still no hardware logs. |
| ESP-IDF example readiness | Medium | Native boundary exists; not built locally/CI; parity gaps remain. |
| Tests/fault injection | Good | 39 native tests pass; gaps remain for ambiguous writes, HIL, IDF CLI behavior. |
| CI/build coverage | Medium | PlatformIO builds/tests run; no `idf.py build`; no HIL. |
| HIL/hardware validation | Weak | Checklist exists but evidence is pending and incomplete. |
| Release metadata | Medium | Metadata exists; wording overclaims and package includes large PDFs. |

## What Is Already Strong

- Core transport is non-owning and injected through `Config` callbacks (`include/MCP45HVX1/Config.h:20`, `include/MCP45HVX1/Config.h:36`, `src/MCP45HVX1.cpp:619`, `src/MCP45HVX1.cpp:630`).
- Core timing is injected via `Config::nowMs`; fallback is neutral `0` (`include/MCP45HVX1/Config.h:84`, `src/MCP45HVX1.cpp:1041`).
- Address range validation is conservative: standard `0x3C-0x3F` by default, alternate `0x5C-0x5F` only with `allowAlternateAddressRange` (`src/MCP45HVX1.cpp:68`).
- Variant limits are explicit and tested (`include/MCP45HVX1/CommandTable.h:85`, `test/test_basic.cpp:565`).
- Public fallible operations consistently return `Status`.
- Native tests cover config rejection, register frames, wiper boundaries, TCON reserved bits, terminal helpers, General Call cache invalidation on success, health/offline/recover behavior, conversion helpers, and example transport mapping (`test/test_basic.cpp:1046` through `test/test_basic.cpp:1084`).
- Arduino CLI has grouped help, runtime and compile-time color disable, active config output, parseable state, detailed health, read-only `stress`, state-restoring `stress_mix`, warnings for output-changing commands, and one-shot General Call arming (`examples/01_basic_bringup_cli/main.cpp:809`, `examples/01_basic_bringup_cli/main.cpp:461`, `examples/01_basic_bringup_cli/main.cpp:1305`, `examples/01_basic_bringup_cli/main.cpp:1814`).
- ESP-IDF example is native, not an Arduino compatibility facade (`examples/espidf_basic/main/main.cpp:12`, `examples/espidf_basic/main/main.cpp:1047`).
- Repo-local guards catch CLI command presence, IDF native-boundary tokens, core framework/timing leakage, generated artifacts, and version drift.
- `CHANGELOG.md` exists and lists both `Unreleased` and `1.0.0`, but no Git tag is present for the advertised version.

## High-Severity Findings

### H1. Failed State-Changing Writes Can Leave Stale Known Cache

Severity: High

Evidence:

- `writeRegister()` only syncs cache on success and returns failures without marking the targeted cache unknown (`src/MCP45HVX1.cpp:532`).
- Multi-step INC/DEC can fail after some bus progress and leave cached wiper state trusted (`src/MCP45HVX1.cpp:806`, `src/MCP45HVX1.cpp:811`).
- General Call helpers mark caches unknown only on `Status::Ok()` (`src/MCP45HVX1.cpp:563`, `src/MCP45HVX1.cpp:576`).
- Transport callback docs say `Ok` means success, but do not guarantee failed writes are device-atomic (`include/MCP45HVX1/Config.h:19`, `include/MCP45HVX1/Config.h:35`).

Impact:

A timeout or data NACK after the device accepted a command byte/data byte can leave the physical Wiper/TCON changed while `SettingsSnapshot.cachedWiperKnown` or `cachedTconKnown` remains true and stale. That is unsafe for unattended high-voltage analog control.

Recommended remediation:

Invalidate the affected cache on any non-parameter/non-config failure from a state-changing write path. Add an explicit hardware-state-uncertain signal in the core snapshot, not only in the Arduino CLI.

Suggested tests:

- Fake transport that mutates Wiper then returns `I2C_TIMEOUT`; assert cached wiper becomes unknown.
- Fake transport that applies one INC command then fails; assert cached wiper becomes unknown.
- Fake General Call failure after applying state; assert affected cache unknown.

Suggested implementation prompt chunk:

Core contracts and uncertain hardware state handling.

### H2. Optional Initial Writes During Failed `begin()` Can Hide Hardware Side Effects

Severity: High

Evidence:

- `begin()` can write initial TCON and then initial Wiper (`src/MCP45HVX1.cpp:122`).
- On failure, `resetAfterFailedBegin` clears config, health, and caches and returns an uninitialized driver (`src/MCP45HVX1.cpp:46`).
- Existing tests lock in this cleared-runtime behavior (`test/test_basic.cpp:454`).

Impact:

An optional startup write can change analog output and then leave the driver uninitialized with no public read/recover path through that instance. This hides a possible live hardware side effect.

Recommended remediation:

Either avoid optional writes during `begin()` for production profiles, or preserve enough initialized/config state to allow explicit recovery/readback after partial startup writes. At minimum, document and expose uncertainty when optional initial writes fail.

Suggested tests:

- Initial TCON write succeeds, initial Wiper write fails after bus mutation; assert recover/read path or uncertain state is observable.
- Initial Wiper write failure invalidates targeted cache or leaves a diagnostic marker.

Suggested implementation prompt chunk:

Device model, startup side effects, and recoverability contracts.

### H3. Hardware Validation Evidence Is Missing For High-Voltage Production Use

Severity: High

Evidence:

- README says hardware smoke tests are pending (`README.md:56`).
- Hardware validation checklist currently focuses on address and General Call rows and leaves them pending (`docs/hardware_validation.md:16`, `docs/hardware_validation.md:28`).
- README itself says validation should cover A1/A0, WLAT, SHDN, General Call enablement, and analog terminal limits (`README.md:228`).

Impact:

The library controls a high-voltage volatile digital potentiometer. Without bench logs for real address selection, Wiper/TCON effects, SHDN/WLAT behavior, POR/BOR, terminal current, and errata isolation, production claims are not defensible.

Recommended remediation:

Create a HIL/manual validation procedure with explicit safe voltage limits, operator gates for output-changing tests, captured serial logs, measured analog outputs, and pass/fail evidence.

Suggested tests:

- Low-voltage analog Wiper sweep before any high-voltage validation.
- TCON terminal connect/disconnect measurement.
- WLAT high/low effect check if wired.
- SHDN asserted/deasserted effect check if wired.
- POR/BOR reset defaults after real rail cycling.

Suggested implementation prompt chunk:

HIL validation tooling and hardware evidence capture.

### H4. Errata Workaround Is Not A Release Gate

Severity: High

Evidence:

- Local errata says affected devices can accept serial data meant for another I2C client (`docs/pdf-extracted-md/MCP45HVX1_Errata_DS80000649B.md:29`).
- Errata workaround is a unique I2C bus with no other devices (`docs/pdf-extracted-md/MCP45HVX1_Errata_DS80000649B.md:49`).
- Driver exposes `SiliconErrataInfo` (`include/MCP45HVX1/MCP45HVX1.h:69`) and tests it (`test/test_basic.cpp:418`).
- User-facing validation docs do not add an isolated-bus/silicon-marking release gate.

Impact:

On affected silicon, shared-bus deployments may behave incorrectly even when the driver and CLI pass software tests.

Recommended remediation:

Make errata review and bus-isolation decision mandatory in the hardware validation checklist and release checklist. Require marking/revision evidence or isolated-bus acceptance.

Suggested tests:

- HIL checklist item for silicon marking.
- HIL checklist item for isolated bus or documented risk acceptance.

Suggested implementation prompt chunk:

Docs/release metadata and HIL checklist hardening.

### H5. ESP-IDF Example Has Token Parity But Not Behavioral Parity

Severity: High

Evidence:

- `selftest output` is advertised in IDF help as output-changing (`examples/espidf_basic/main/main.cpp:427`) but handler only warns and runs read-only `runSelftest()` (`examples/espidf_basic/main/main.cpp:1017`).
- IDF `parseU32()` has no max bound or negative guard (`examples/espidf_basic/main/main.cpp:227`), and state-changing commands cast values to `uint8_t`.
- IDF `color` toggles `gColor` but no output uses ANSI color based on it (`examples/espidf_basic/main/main.cpp:43`, `examples/espidf_basic/main/main.cpp:724`).
- CI does not run `idf.py build` (`.github/workflows/ci.yml:15`, `.github/workflows/ci.yml:49`, `.github/workflows/ci.yml:82`).

Impact:

`library.json` and `idf_component.yml` advertise ESP-IDF support, and the IDF example is native, but current evidence is contract-token parity and documentation, not a locally/CI-run IDF build with behavior-equivalent diagnostics.

Recommended remediation:

Implement or remove IDF `selftest output`, bound all numeric parsing before casts, add real optional ANSI color or document IDF color as unsupported, expand state output, and add CI `idf.py build`.

Suggested tests:

- Contract guard that rejects placeholder IDF branches.
- IDF parser tests or static checks for bounded casts.
- CI job building `examples/espidf_basic` for esp32s3 and esp32s2.

Suggested implementation prompt chunk:

ESP-IDF CLI parity and native bus lifecycle.

## Medium-Severity Findings

### M1. `resetI2cState()` Can Mark Health READY Without Device Proof

Severity: Medium

Evidence:

- If not already OFFLINE, reset success is tracked as a health success (`src/MCP45HVX1.cpp:255`).
- Any tracked success sets `DriverState::READY` (`src/MCP45HVX1.cpp:984`).
- The bus reset callback is a bus/interface operation, not a device register read (`include/MCP45HVX1/Config.h:40`).

Impact:

After a local bus reset, a missing or unhealthy device can appear READY until the next real transaction.

Recommended remediation:

Either keep reset success from clearing DEGRADED, or follow reset with an explicit tracked read/probe before returning READY.

Suggested tests:

- Force DEGRADED, make bus reset callback return OK while reads still fail; assert state does not become READY without a read.

Suggested implementation prompt chunk:

Core health/recovery semantics.

### M2. Uncertain-State Observability Is Inconsistent Across CLIs

Severity: Medium

Evidence:

- Arduino CLI has `gOutputStateUncertain` and includes it in some state/health output (`examples/01_basic_bringup_cli/main.cpp:32`, `examples/01_basic_bringup_cli/main.cpp:661`).
- ESP-IDF CLI has no equivalent uncertainty flag.
- Arduino `health_view::printSummary()` dirty calculation ignores the CLI uncertainty flag (`examples/common/HealthView.h:110`, `examples/01_basic_bringup_cli/main.cpp:1810`).

Impact:

Diagnostics can under-report uncertainty after restore failures or ambiguous output-changing operations.

Recommended remediation:

Move uncertain-state visibility into the core snapshot or make both CLIs consistently include it in `state`, `drv`, stress summaries, and HIL output.

Suggested tests:

- CLI contract/golden output checks for uncertainty fields after simulated restore failure.

Suggested implementation prompt chunk:

CLI and core uncertainty reporting.

### M3. `begin()` / `probe()` Collapse Transport Error Detail

Severity: Medium

Evidence:

- `Err` distinguishes NACK address, NACK data, timeout, and bus errors (`include/MCP45HVX1/Status.h:23`).
- Example transport preserves Wire error classes (`examples/common/I2cTransport.h:43`, `examples/common/I2cTransport.h:68`).
- `begin()` and `probe()` remap most read failures to `DEVICE_NOT_FOUND` (`src/MCP45HVX1.cpp:89`, `src/MCP45HVX1.cpp:211`).

Impact:

Production callers lose direct timeout vs NACK vs bus-fault classification during bring-up unless they inspect detail fields and transport-specific behavior.

Recommended remediation:

Preserve original transport error code where possible, or add cause fields/helpers that retain low-level status.

Suggested tests:

- Begin/probe with each injected I2C failure type; assert expected code/cause.

Suggested implementation prompt chunk:

Status and error fidelity.

### M4. Copy/Move Semantics Are Implicit

Severity: Medium

Evidence:

- `MCP45HVX1` declares no copy/move/destructor policy (`include/MCP45HVX1/MCP45HVX1.h:101`).
- A copied live instance duplicates raw callback contexts, caches, health counters, and initialized state (`include/MCP45HVX1/MCP45HVX1.h:458`, `include/MCP45HVX1/Config.h:79`).

Impact:

Accidental copies can produce two driver objects that believe they own the same logical state and callback context.

Recommended remediation:

Delete copy/move or explicitly document non-owning copied semantics. For production, deleting copy/move is safer.

Suggested tests:

- Compile-time static assertions for non-copyable/non-movable behavior if changed.

Suggested implementation prompt chunk:

Core lifecycle/copy contract.

### M5. Hardware Validation Checklist Is Too Narrow

Severity: Medium

Evidence:

- README names A1/A0, WLAT/SHDN, General Call, and analog terminal limits as hardware validation items (`README.md:228`).
- `docs/hardware_validation.md` currently covers address conflict and General Call only (`docs/hardware_validation.md:6`, `docs/hardware_validation.md:28`).

Impact:

The checklist is not sufficient for release signoff.

Recommended remediation:

Expand it into a concrete matrix with safe/default/output-changing classification and required evidence.

Suggested implementation prompt chunk:

Docs/release metadata and HIL procedure.

### M6. POR/BOR Timing Documentation Is Inconsistent

Severity: Medium

Evidence:

- `docs/07_initialization_reset_and_operational_notes.md:102` says no explicit startup delay is quantified.
- `docs/08_variant_differences_and_open_questions.md:170` repeats that tPOR is missing.
- `docs/03_electrical_and_timing.md:82` records `TBORD` as 10 us typical, 20 us max after reset exit.
- `begin()` reads Wiper/TCON immediately after the caller invokes it (`src/MCP45HVX1.cpp:89`).

Impact:

Users may call `begin()` before rails/reset are stable, and docs do not give a consistent rule.

Recommended remediation:

Clarify the startup/reset timing contract and require the application to wait for stable rails/reset before `begin()`.

Suggested implementation prompt chunk:

Device model and docs correction.

### M7. Public API Contracts Are Too Thin For Release

Severity: Medium

Evidence:

- The public header exposes a rich stateful API but does not fully document failed-write cache semantics, startup side effects from optional initial writes, copy/move behavior, thread/ISR expectations, or WLAT/SHDN readback limitations (`include/MCP45HVX1/MCP45HVX1.h:101`, `include/MCP45HVX1/MCP45HVX1.h:458`).
- README covers several operational caveats, but the enforceable API contract is split across README prose, docs, tests, and implementation behavior.

Impact:

Production integrators need stable contracts at the public API boundary, not only examples and audit notes. Ambiguous contracts make later fixes look like behavioral regressions even when they are safety fixes.

Recommended remediation:

Add focused public-header and API reference notes for initialization, recoverability, cache validity, uncertainty after write failures, copy/move policy, callback lifetime, and caller-side serialization.

Suggested implementation prompt chunk:

Docs/release metadata and API contract hardening.

### M8. Version, Changelog, And Tag Policy Are Not Release-Clean

Severity: Medium

Evidence:

- Package metadata advertises `1.0.0` (`library.json:3`, `idf_component.yml:1`).
- `CHANGELOG.md` has both `Unreleased` and `1.0.0` sections (`CHANGELOG.md:3`, `CHANGELOG.md:37`).
- `git tag --list` returned no tags during this audit.
- README and package descriptions still say "Production-grade" while hardware validation is pending (`README.md:3`, `README.md:56`, `library.json:4`, `idf_component.yml:2`).

Impact:

Users can install a package that looks like a production 1.0 release even though validation evidence, IDF CI, and release tagging are not complete.

Recommended remediation:

Define a release checklist covering version bump, changelog finalization, tag creation, package export scope, CI evidence, IDF build evidence, and hardware/HIL evidence. Until then, qualify the release as engineering/pre-production.

Suggested implementation prompt chunk:

Docs/release metadata and release gate.

## Low-Severity Findings

### L1. Address Tests Do Not Cover Every Candidate Address

Severity: Low

Evidence:

- Tests cover `0x3F` and `0x5C` but not every address in `0x3C-0x3F` and `0x5C-0x5F` (`test/test_basic.cpp:333`).

Recommended remediation:

Add table-driven tests for all standard and alternate addresses plus adjacent invalid addresses.

### L2. Stale Reports Conflict With Current State

Severity: Low

Evidence:

- CLI parity report says no IDF example existed and lists IDF as remaining gap (`docs/MCP45HVX1_CLI_PARITY_AND_COLOR_REPORT.md:41`, `docs/MCP45HVX1_CLI_PARITY_AND_COLOR_REPORT.md:119`).
- Style report says runtime color toggle is missing (`docs/cli_output_style_alignment_report.md:160`).

Recommended remediation:

Supersede or update stale reports after implementation chunks are complete.

### L3. Package Size / Export Scope Is Uncontrolled

Severity: Low

Evidence:

- `docs/` includes many PDFs and is about 22.8 MB per inventory agent.
- `library.json` has no export/include-exclude rules beyond build include/src directories (`library.json:47`).

Recommended remediation:

Add package export policy or release packaging checklist to avoid shipping large reference PDFs unless intentional.

### L4. Security And Contributing Docs Contain Stale Generic Text

Severity: Low

Evidence:

- `SECURITY.md` mentions NVS side effects, which do not match this volatile-only driver (`SECURITY.md:28`).
- `CONTRIBUTING.md` references `.clang-format`, but no `.clang-format` is tracked (`CONTRIBUTING.md:17`).

Recommended remediation:

Align docs with actual project contracts.

### L5. `writeWiperFraction()` Documentation Disagrees With Behavior

Severity: Low

Evidence:

- The public header says out-of-range fractions are clamped (`include/MCP45HVX1/MCP45HVX1.h:246`).
- The implementation rejects out-of-range fractions with `INVALID_PARAM` (`src/MCP45HVX1.cpp:332`).
- Tests assert rejection for `1.1f` (`test/test_basic.cpp:976`).

Recommended remediation:

Choose one contract and align header docs, implementation, and tests. Rejection is safer for output-changing analog control; if retained, update the header wording.

## CLI Scope, Depth, and Color Findings

Current Arduino command list:

- Common: `help`, `?`, `version`, `ver`, `scan`, `color`, `verbose`.
- Device selection: `begin`, `addr`, `addr_alt`, `variant`, `res`, `rab`.
- Read-only diagnostics: `probe`, `recover`, `iface_reset`, `detail`, `cfg`, `settings`, `drv`, `health`, `state`, `read`, `rregs`, `readwiper`, `readtcon`, `last`, `reg`, `rreg`, `dump`, `raw`, `info`, `errata`.
- Safe tests: `selftest`, `selftest safe`, `stress`.
- Output-changing commands: `defaults`, `wiper`, `wiper percent`, `wiper fraction`, `frac`, `pos`, `zero`, `mid`, `max`, `inc`, `dec`, `tcon`, `term`, `terminal`, `shutdown`, `software-shutdown`, `mode`.
- Dangerous/operator-gated: `selftest output`, `stress_mix`, `raw write`, `wreg`, `wregs`, `gc arm`, `gc disarm`, `gc wiper`, `gc tcon`, `gc inc`, `gc dec`.

Arduino CLI status:

- Strong. It has grouped help (`examples/01_basic_bringup_cli/main.cpp:809`), active config output (`examples/01_basic_bringup_cli/main.cpp:451`), symbolic status output (`examples/01_basic_bringup_cli/main.cpp:499`), runtime color (`examples/01_basic_bringup_cli/main.cpp:1271`), and compile-time color disable (`examples/common/Log.h:24`).

ESP-IDF CLI status:

- Medium. Command names are present, but `selftest output` is placeholder behavior, numeric parsing can wrap, symbolic status names are absent, `state` is thinner, and `color` is only a flag.

Recommended final CLI command surface:

```text
[Common]
  help / ?
  version / ver
  scan
  color [on|off]
  verbose [on|off]

[Device Selection]
  begin [addr] [7|8]
  addr [0x3c..0x3f]
  addr_alt 0x5c..0x5f
  variant [hv31|hv51]
  res [7|8]
  rab [5000|10000|50000|100000]

[Read-only Diagnostics]
  probe
  recover
  iface_reset
  cfg / settings
  detail
  state
  drv / health
  read / rregs
  readwiper
  readtcon
  reg / rreg <addr>
  dump / raw
  last
  info
  errata

[Safe Tests]
  selftest
  selftest safe
  stress [N]

[Output-changing Commands]
  defaults
  wiper <code>
  wiper percent <0..100>
  wiper fraction <0.0..1.0>
  frac / pos <0.0..1.0>
  zero / mid / max
  inc [N] / dec [N]
  tcon [value|default]
  terminal <a|w|b> <on|off>
  shutdown [on|off]
  software-shutdown [on|off]
  mode [pot|bw|aw|float|shutdown]

[Dangerous / Operator-Gated]
  selftest output
  stress_mix [N]
  raw write <register> <value>
  wreg / wregs <register> <value>
  gc arm | gc disarm
  gc wiper <code>
  gc tcon <value>
  gc inc | gc dec
```

Recommended color scheme:

```text
green   PASS / OK / online / ready
yellow  WARN / caution / degraded / output-changing
red     FAIL / error / offline / invalid / dangerous
cyan    section headers / device identity
blue    information
gray    notes / disabled / skipped / not run
magenta destructive or operator-confirmation warnings
```

Color must remain optional for logs and HIL parsing. Arduino already supports runtime and compile-time disable; IDF should either implement matching ANSI output or stop presenting `color` as equivalent.

Safe/destructive classification:

- Safe/read-only by default: `help`, `version`, `scan`, no-arg config/info/state/health/read commands, `probe`, `recover`, `selftest`, `selftest safe`, `stress`.
- State-restoring but still output-changing internally: `selftest output`, `stress_mix`.
- Output-changing local writes: Wiper/TCON/terminal/shutdown/mode/defaults/raw register writes.
- Bus-wide dangerous: all `gc` write/step commands.

## ESP-IDF Port Findings

Component path:

- Root component: `CMakeLists.txt`, `idf_component.yml`.
- Example: `examples/espidf_basic`.

Native boundary:

- Native IDF headers are used: `driver/i2c_master.h`, `esp_timer.h`, FreeRTOS headers (`examples/espidf_basic/main/main.cpp:12`).
- Entry point is `extern "C" void app_main(void)` (`examples/espidf_basic/main/main.cpp:1047`).
- No Arduino compatibility facade is present per `tools/check_idf_example_contract.py`.

Bus ownership and lifecycle:

- `NativeBus` owns bus/device/manual handles (`examples/espidf_basic/main/main.cpp:30`).
- Address changes remove/re-add device handles (`examples/espidf_basic/main/main.cpp:77`).
- Reset removes devices, deletes bus, bit-bangs recovery, and reinitializes (`examples/espidf_basic/main/main.cpp:173`).

Timeout/error mapping:

- `ESP_ERR_TIMEOUT` maps to `I2C_TIMEOUT`; invalid args to `INVALID_PARAM`; NACK-ish responses to `I2C_NACK_ADDR`; other errors to `I2C_BUS` (`examples/espidf_basic/main/main.cpp:53`).

Gaps:

- `selftest output` is advertised but not implemented behaviorally.
- Numeric parsing has no command-specific bounds before `uint8_t` casts.
- `color` does not color output.
- `scanBus()` lacks a null bus-handle guard if init fails.
- CI does not run `idf.py build`, and local `idf.py` is not on PATH.

## Tests and Fault-Injection Findings

Tests present:

- Native Unity tests: 39 tests pass locally.
- Fake transport supports register reads/writes, General Call, read MSB mismatch, transport errors, reset callback behavior, and nowMs injection (`test/test_basic.cpp:21`, `test/test_basic.cpp:88`, `test/test_basic.cpp:137`).
- Tests cover:
  - status basics;
  - command constants;
  - config defaults and invalid config;
  - documented and alternate address acceptance;
  - begin/read/cache behavior;
  - variant/resistance helpers;
  - POR defaults;
  - optional initial writes;
  - Wiper/TCON writes;
  - 7-bit out-of-range rejection;
  - INC/DEC clamp/cache;
  - terminal helpers;
  - direct register rejection;
  - last-address reads;
  - reset/defaults;
  - General Call helpers;
  - probe/recover/offline behavior;
  - read MSB mismatch;
  - conversion helpers;
  - example transport mapping (`test/test_basic.cpp:1046` through `test/test_basic.cpp:1084`).

Gaps:

- No tests for ambiguous partial write side effects and cache invalidation.
- No tests for optional initial write failure that may have affected hardware but leaves driver uninitialized.
- No tests for bus reset marking READY without a real device read.
- No IDF CLI parser tests for overflow/wrap.
- No contract test that public header documentation matches fraction conversion behavior.
- No public-header-only compile guard to catch accidental dependency leakage through headers.
- No HIL runner or serial-output parser.
- No automated hardware validation logs.
- No CI `idf.py build`.

Recommended additions:

- Add fake transport modes that mutate state then return timeout/NACK.
- Add table-driven address tests for every standard/alternate address.
- Add tests for `begin()` optional-write failure uncertainty.
- Add IDF static/contract guard for bounded numeric parsing before casts.
- Add HIL output parser that strips ANSI and checks section/status fields.

## Hardware Validation Needed

| Test | Safe by default? | Requires output change? | Requires analog measurement? | Evidence needed |
| --- | --- | --- | --- | --- |
| Standard address scan `0x3C-0x3F` | Yes | No | No | Serial scan log, populated A1/A0 strap record |
| Alternate address `0x5C-0x5F` opt-in | Yes | No | No | Explicit board note explaining why alternate range was tested |
| Read Wiper after POR/BOR | Yes | No | Optional | Serial `readwiper` log after real rail cycle |
| Read TCON after POR/BOR | Yes | No | Optional | Serial `readtcon` log after real rail cycle |
| Write/read Wiper with safe load | No | Yes | Yes | Low-voltage measured Wiper voltage/resistance and serial readback |
| TCON connect/disconnect | No | Yes | Yes | Continuity/voltage evidence for A/W/B states |
| SHDN pin behavior if wired | No | Yes via hardware pin | Yes | Pin state, serial readback, analog terminal measurement |
| WLAT pin behavior if wired | No | Yes via hardware pin | Yes | Register readback vs physical output evidence |
| Brownout/POR defaults | No | Rail cycling | Yes | Power-cycle log, Wiper/TCON default evidence |
| I2C NACK/timeout/unplug | Yes if analog safe | No intended output change | No | Serial failure logs and recovery behavior |
| General Call disabled/enabled behavior | No | Yes, bus-wide | Optional/Yes | Isolated bus proof, GCEN/silicon note, serial and analog evidence |
| Low-voltage analog validation before high-voltage use | No | Yes | Yes | Low-voltage sweep logs and measured outputs |
| High-voltage validation | No | Yes | Yes | Current/voltage-limited bench setup, terminal current/power evidence |
| Errata isolated-bus validation | Yes by inspection | No | No | Silicon marking, bus topology, acceptance record |

## Recommended Implementation Roadmap

### P0 - Must fix before production claim

1. Fix core uncertain-state handling for failed state-changing writes.
2. Fix or explicitly constrain optional initial-write failure behavior during `begin()`.
3. Add hardware validation matrix/procedure and errata isolated-bus release gate.
4. Add ESP-IDF CI build or remove/qualify ESP-IDF readiness claims.
5. Fix IDF CLI numeric bounds and `selftest output` placeholder behavior.

### P1 - Should fix before release/merge

1. Clarify reset/health semantics so bus reset does not imply device READY without proof.
2. Decide copy/move policy for the stateful driver.
3. Preserve or expose lower-level transport error detail in `begin()`/`probe()`.
4. Expand tests for all address ranges and ambiguous write failure cases.
5. Update stale reports and generic docs (`SECURITY.md`, `CONTRIBUTING.md`, stale CLI reports).
6. Add public API contract notes for cache/uncertainty/startup/copy/thread semantics.
7. Finalize version/changelog/tag policy before publishing `1.0.0` as a production release.

### P2 - Nice hardening / polish

1. Add IDF real color support or document plain IDF output.
2. Add package export policy to reduce release artifact size.
3. Add HIL output parser and serial log normalization.
4. Add richer IDF `state`/`drv` parity with Arduino cache/dirty/RAB output.
5. Add public-header-only compile guard and documentation contract checks for conversion helpers.

## Proposed Chunked Prompt Plan

1. Core uncertain-state and health contracts.
   - Expected changes: invalidate caches on ambiguous write failures, expose hardware-state uncertainty, adjust reset health semantics.
   - Acceptance: targeted native tests for partial write failure and reset/recover behavior pass.

2. Begin/startup side-effect contract.
   - Expected changes: define optional initial-write failure behavior and recoverability.
   - Acceptance: tests prove side effects are observable or explicitly impossible.

3. Device model and datasheet docs correction.
   - Expected changes: POR/BOR timing consistency, SHDN/WLAT contract, errata guidance.
   - Acceptance: docs no longer conflict and checklist maps to datasheet facts.

4. ESP-IDF CLI parity and native build gate.
   - Expected changes: bounded parsing, real or removed `selftest output`, status names, color decision, null bus guard, CI `idf.py build`.
   - Acceptance: IDF contract strengthened and `idf.py` builds in CI for esp32s3/esp32s2.

5. Tests/fault injection/guards.
   - Expected changes: partial-write fake transport, address matrices, IDF parser/placeholder guards, HIL output contract.
   - Acceptance: `tools/validate.py` includes new guards and native tests.

6. Docs/release metadata.
   - Expected changes: remove overclaims, fix stale reports, add release checklist, package export policy.
   - Acceptance: release docs state exact validation status and no production claim without evidence.

7. HIL tooling.
   - Expected changes: serial runner or manual log parser with safe and output-changing modes.
   - Acceptance: low-voltage safe-load logs can be captured and attached.

8. Final integration and merge gate.
   - Expected changes: run all checks, update final readiness report, tag only after evidence.
   - Acceptance: clean tree, CI green, HIL evidence present or release explicitly marked non-production.

## Commands Run

| Command | Result | Notes |
| --- | --- | --- |
| `pwd` | Passed | `C:\Users\Honza\Documents\Projects\MCP45HVX1` |
| `git rev-parse --show-toplevel` | Passed | `C:/Users/Honza/Documents/Projects/MCP45HVX1` |
| `git branch --show-current` | Passed | Started on `main`, then audit branch |
| `git status --short` | Passed | Clean before branch creation |
| `git remote -v` | Passed | `origin` GitHub remote |
| `git log --oneline --decorate -n 20` | Passed | Starting commit `6985d66` |
| `git tag --list` | Passed | No tags returned |
| `git checkout -b audit/mcp45hvx1-production-readiness-exploration` | Passed | Created audit branch |
| Bash `find ...` inventory commands | Failed | `bash` not on PATH in this Windows shell |
| `git ls-files | Sort-Object` | Passed | Used as tracked-file inventory equivalent |
| PowerShell sibling discovery | Passed | Found ADS1115, BME280, INA228, INA3221, LDC1614, LSM6DS3TR, MB85RC, OPT4001, PCA9555, RV3032-C7, SHT3x-main, SSD1315 |
| `python --version` | Passed | Python 3.12.10 |
| `python -m platformio --version` | Passed | PlatformIO Core 6.1.18 |
| `python tools/check_core_timing_guard.py` | Passed | Core timing guard PASSED |
| `python tools/check_cli_contract.py` | Passed | IDF example contract PASSED; CLI contract PASSED |
| `python tools/check_idf_example_contract.py` | Passed | IDF example contract PASSED |
| `python tools/check_generated_artifacts.py` | Passed | Generated artifact check PASSED |
| `python scripts/generate_version.py check` | Passed | Version header up to date |
| `python -m platformio test -e native` | Passed | 39/39 tests passed |
| `python -m platformio run -e esp32s3dev` | Passed | Build succeeded; PlatformIO obsolete-core warning emitted |
| `python -m platformio run -e esp32s2dev` | Passed | Build succeeded; PlatformIO obsolete-core warning emitted |
| `python -m platformio pkg pack` | Passed | Tarball created then removed |
| `idf.py --version` | Failed | `idf.py` not recognized on PATH |
| `git diff --check` | Passed | No whitespace errors |

## Commands Not Run

| Command | Reason |
| --- | --- |
| `idf.py -C examples/espidf_basic set-target esp32s3 build` | `idf.py` is not on PATH locally |
| `idf.py -C examples/espidf_basic set-target esp32s2 build` | `idf.py` is not on PATH locally |
| Hardware/HIL commands | No target hardware or HIL runner available in this audit |

## Final Verdict

Ready to merge now: **No**, not as an industry-readiness claim. This audit branch is docs-only and can be reviewed separately.

Ready to release now: **No**.

Ready for implementation hardening: **Yes**.

Blocks industry-grade claim:

- ambiguous failed writes can leave stale known hardware state;
- optional initial-write failure can hide possible analog side effects;
- no hardware validation evidence;
- errata/isolated-bus behavior is not a release gate;
- ESP-IDF example is not built in CI and has behavioral parity gaps;
- no HIL runner/log evidence;
- release docs and metadata overclaim production readiness.

Suggested first implementation chunk:

Core uncertain-state handling and health contracts. Fix stale cache/uncertain state first because every CLI, HIL, and documentation layer depends on truthful driver state.
