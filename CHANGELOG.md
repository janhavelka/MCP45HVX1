# Changelog

## Unreleased

### Fixed

- `restorePowerOnDefaults()` now writes Wiper 0 before TCON0. The default TCON
  reconnects every terminal, so the previous order briefly connected the analog
  terminals while Wiper 0 still held the old code.
- General Call helpers now check `Config::allowGeneralCall` before the OFFLINE
  latch. A disabled helper previously reported `BUSY` on an offline driver,
  implying a retry that `recover()` could never make succeed; it now reports
  `UNSUPPORTED` as the API contract states.
- `startIncrementWiperJob(0)` / `startDecrementWiperJob(0)` no longer leave the
  previous job's snapshot visible. A zero-step no-op now publishes a completed
  snapshot for the requested job type instead of a stale readback result.
- Arduino example: the `dirty=` field in `state` and the health summary
  repeated the `uncertain=` value instead of reporting the CLI's own
  output-changed flag, so the two columns were always identical.
- Arduino example transport: a read that returns no bytes is now reported as
  `I2C_NACK_ADDR`. On arduino-esp32 `endTransmission(false)` only marks the
  transfer as repeated-start and the error surfaces inside `requestFrom()`,
  which made `Err::DEVICE_NOT_FOUND` unreachable from `begin()` and `probe()`.
- Arduino example transport: a short `Wire.write()` no longer returns with the
  transmission still open. `TwoWire::beginTransmission()` acquires the Wire
  mutex with `portMAX_DELAY` and only `endTransmission(true)` releases it, so
  the previous early return leaked that mutex and would deadlock the next I2C
  transaction. Sending the truncated frame is safe on this device: a Stop
  before the data ACK aborts the write and stores nothing (DS20005304B §7.4.1).
- Arduino example transport: `Config::i2cTimeoutMs` is clamped to the
  `uint16_t` that `TwoWire::setTimeOut()` accepts instead of silently wrapping.
- Arduino example transport: `initWire()` reports the real `Wire.begin()`
  result rather than always returning `true`, and releases SDA after the
  bus-recovery clock burst.
- ESP-IDF example: `verbose` with no argument now shows the current value
  instead of toggling it, matching its help text and the Arduino CLI.
- `scripts/generate_version.py` no longer crashes with an unhandled
  `RuntimeError` when `idf_component.yml` has an unquoted `version:`. The
  regex now accepts either quoting form, so `generate_version.py check`
  reports "Out of date" instead of raising a traceback in CI. The rewrite
  mechanism originally blamed for producing that form does not reach this
  repository's manifest — see finding 17 in `docs/CODE_AUDIT.md` — and if a
  manifest is ever reformatted, canonical form is restored with
  `git checkout -- idf_component.yml`, not by running the generator.
- CI uploads build logs with `actions/upload-artifact@v7` (Node 24). The
  previous `@v4` pin was the last Node 20 official action in the workflow, and
  `tools/check_idf_example_contract.py` pinned that exact major, which blocked
  the update; the check is now version-agnostic.
- The documentation CI job was failing. `docs/CODE_AUDIT.md` was not in the
  Doxyfile's `INPUT` list, so a cross-reference to it from
  `docs/DEVICE_REFERENCE.md` could not resolve, and
  `WARN_AS_ERROR = FAIL_ON_WARNINGS` turns an unresolved `\ref` into a hard
  error. `doxygen Doxyfile` now emits zero diagnostics.
- `scripts/generate_version.py`'s tolerant `version:` regex ended in `\s*$`,
  which under `re.MULTILINE` consumes the trailing newline when `version:` is
  the last line of `idf_component.yml` — exactly where a `yaml.dump` rewrite
  places it. Changed to `[^\S\n]*$`.

### Added

- `static_assert` in the Arduino example transport that the Wire buffer can
  carry a full `cmd::MAX_COMMAND_CHUNK` INC/DEC burst, and derivation of the
  transport's buffer limit from the platform's own `I2C_BUFFER_LENGTH`.
- Native tests for the wiper-before-TCON restore order, zero-step job
  snapshots, General Call gating while OFFLINE, all five `TerminalMode` presets
  and all three terminals, a full-chunk transport write, and unacknowledged
  reads. 78 tests total.
- `Err` status-code table in the API contract; `Err::IN_PROGRESS` was
  previously documented nowhere.

### Changed

- `docs/DEVICE_REFERENCE.md` rewritten against DS20005304B with section and
  table citations, replacing statements attributed to a deleted intermediate
  text extraction. It now records that `0x5C-0x5F` belongs to the
  standard-voltage MCP45XX/46XX family (Table 6-2 Note 1) and is not an
  alternate range for this part, that DS80000649B Issue 1 makes shared-bus use
  hazardous for *all* traffic rather than only General Call, and the Table 5-3
  minimum safe wiper codes at 36 V.
- `ASSUMPTIONS.md` merged into `docs/DEVICE_REFERENCE.md` and removed; the same
  facts had three homes.
- README reduced from about 30 KB to about 12 KB by removing content duplicated
  in the API contract and device reference, and by moving the HIL runner
  procedure into `docs/MCP45HVX1_HARDWARE_VALIDATION.md`.
- `docs/MCP45HVX1_RELEASE_CHECKLIST.md` generalized from a frozen v1.1.0
  runbook to a version-neutral one, and corrected to describe what
  `tools/validate.py` actually runs.
- `AGENTS.md` trimmed to repository-specific rules, dropping session-workflow
  narration and references to prompt files that no longer exist. It, along with
  `CODEOWNERS`, `Doxyfile`, `platformio.ini`, `scripts/`, and `.vscode/`, is now
  excluded from the ESP-IDF component export.
- The HIL runner no longer emits "parent validation notes" / "final response"
  into generated operator reports, and `scripts/pio.cmd` no longer prints
  agent-directed instructions to a human.
- The audit document moved to `docs/CODE_AUDIT.md` and every finding in it was
  independently re-verified against the working tree, one investigation per
  finding, by running the code rather than re-reading the first pass. Findings
  6, 16, and 17 had their central claim refuted, findings 9 and 11 had
  load-bearing numbers or a formula corrected, and one "verified correct" bullet
  cited a regression test that does not exist. Each finding now carries an
  Open/Fixed status so an implementer can tell proposals from work already done.
- Finding 17 (an ESP32 build rewriting `idf_component.yml`) is closed as not
  reproducible: the pioarduino component manager checks the Arduino framework
  directory first and rewrites the framework's own manifest, never reaching
  `project_src_dir`. The proposed `src_dir` restructuring is not needed.
- `docs/CODE_AUDIT.md` is excluded from both package exports. It is a
  maintainer work-item document, so it is treated like `AGENTS.md`,
  `CODEOWNERS`, and `Doxyfile` rather than shipped into consumers' trees by the
  `docs/*.md` include rule.
- `README.md`, `docs/MCP45HVX1_API_CONTRACT.md`, and
  `docs/MCP45HVX1_HARDWARE_VALIDATION.md` now agree with
  `docs/DEVICE_REFERENCE.md` that `0x5C-0x5F` is not an MCP45HVX1 address
  range. The hardware-validation address matrix no longer carries an
  "Alternate candidate" column inviting evidence against it.
- `docs/DEVICE_REFERENCE.md` corrects its own addressing arithmetic:
  `0x5C-0x5F` are 8-bit *control bytes* of the standard-voltage family, whose
  7-bit addresses are `0x2E-0x2F`. It also restores the second sentence of
  DS20005304B Table 6-2 Note 1, which states that the disjoint fixed bits exist
  so both families can share one bus.

### Removed

- The three committed HIL session reports under `docs/reports/`. They recorded
  one developer's COM8/COM9 ESP32-S2 bench sessions and referenced a
  `raw_serial.txt` bundle that was never committed; the durable results are now
  a summary table in `docs/MCP45HVX1_HARDWARE_VALIDATION.md`.
- Unused `cmd::MIN_REGISTER` and `cmd::MAX_REGISTER`. `MAX_REGISTER` was also
  wrong: the command byte's address field is four bits, so the addressable
  range is `0x00-0x0F`, not `0x00-0xFF`.
- Dead code in `resetI2cState()` and the redundant validation in
  `setTerminalMode()`; the terminal read-modify-write logic that was duplicated
  between the synchronous and poll-job paths is now one helper.
- The empty, untracked `tmp/` scratch directory and the stale `docs/doxygen/`
  exclude in `Doxyfile`.

## 1.1.0 - 2026-08-05

### Added

- Added table-driven `help <command>` / `? <command>` lookup with canonical
  aliases, colored safety classifications, syntax, examples, Arduino/native
  CLI parity guards, and safe HIL coverage.
- Added a targeted ESP32-S2 COM9 `PASS_SAFE_ONLY` report with 31 executed
  checks, zero failures, and final READY MCP45HV51 state at `0x3C`.
- Added Doxygen validation with warnings treated as errors to GitHub Actions.

### Changed

- Upgraded Arduino example builds from pioarduino `platform-espressif32`
  `54.03.20` (Arduino-ESP32 3.2.0 / ESP-IDF 5.4.1) to `55.03.311`
  (Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5).
- Pinned PlatformIO Core 6.1.19 in CI and centralized its cache key version.
- Updated official checkout, cache, and Python setup actions to their Node 24
  majors so GitHub-hosted CI does not depend on the deprecated Node 20 runtime.
- Removed the forced PSRAM macro and original-ESP32 PSRAM cache workaround from
  the declared no-PSRAM ESP32-S3 target.
- Excluded repository-local `.pio` content explicitly from all source filters.
- Added Arduino-ESP32 and ESP-IDF versions to the Arduino CLI `version` output
  for build and HIL provenance.
- Consolidated the documentation around the maintained README, device
  reference, API contract, hardware-validation procedure, release checklist,
  ESP-IDF notes, datasheets, and compact HIL evidence summaries.
- Made repository HIL report copies explicit through `--report-file`, omitted
  repetitive benchmark/soak rows from human reports, and renamed the hard-coded
  `8-hour soak` feature label to the duration-neutral `Safe-only soak`.
- Made the README quick start read-only after initialization and documented the
  legacy `tick()` hook as unnecessary for new code.
- Reworked example logging macros to remain standard C++17 when a log message
  has no format arguments.
- Added a portable Windows PlatformIO wrapper and aligned the aggregate
  validator and maintained contributor/release instructions with it.

### Removed

- Removed completed prompt files, one-off hardening/release summaries, and the
  duplicate implementation manual after merging their durable information into
  the maintained documentation and changelog.
- Removed unused `CommandHandler` and `HealthDiag` helpers, the include-only
  `TransportAdapter` and `BusDiag` wrappers, duplicate status/rate formatters,
  and an unused second I2C recovery implementation from the examples.
- Removed two private transport/register helpers that had no callers.
- Cleared generated build/cache output, duplicate local HIL staging bundles,
  and roughly 21 MB of repetitive command-by-command Markdown rows while
  preserving compact HIL provenance and result summaries.

### Validation

- Passed all 74 native tests, the aggregate repository validator, package
  creation, and clean ESP32-S2/ESP32-S3 Arduino builds on pioarduino
  `55.03.311`.
- Passed warning-free Doxygen generation and the HIL parser/self-tests.
- The targeted COM9 run completed `PASS_SAFE_ONLY`; output-changing and
  physical hardware gates remained disabled.

### Known Limits

- This remains a pre-production software package. It does not claim analog
  accuracy, high-voltage safety, production readiness, or General Call safety.
- Output-changing, SHDN/WLAT, address-matrix, rail-cycle, physical fault,
  analog, and high-voltage evidence gates remain open.

## 1.0.0 - 2026-06-30

### Added

- Hardware-uncertainty tracking for ambiguous failed Wiper/TCON/raw/General
  Call state-changing writes, including cache invalidation, public snapshot
  fields, and readback-only clearing.
- Poll-chunked job APIs for one-instruction Wiper writes, bounded snapshot
  readback, visible terminal read-modify-write sequencing, chunked Wiper step
  commands, and recovery readback.
- HIL evidence tooling in `tools/run_hil_mcp45hvx1.py` with safe read-only
  defaults, explicit output-changing opt-ins, General Call isolated-bus gating,
  SHDN/WLAT operator prompts, restore checks, and attachable evidence bundles.
- GitHub Actions coverage for ESP32-S3/ESP32-S2 PlatformIO builds, native
  tests, guard scripts, package validation, version checks, and pure ESP-IDF
  `esp32s2`/`esp32s3` matrix builds.
- Maintained documentation set: docs index, API contract, hardware validation
  template, release checklist, ESP-IDF port notes, hardening summary, and
  v1.0.0 release notes.

### Changed

- Made optional `begin()` startup Wiper/TCON writes explicitly opt-in,
  readback-first, visible after failure, and recoverable through normal
  readback/recovery paths.
- Gated core General Call helpers behind explicit `Config::allowGeneralCall`
  opt-in while keeping diagnostic CLI General Call commands behind one-shot
  operator arming and warnings.
- Aligned the device model and documentation with datasheet-driven
  MCP45HV31/MCP45HV51 resolution/POR defaults, Wiper/TCON registers, TCON
  reserved bits, SHDN/WLAT physical semantics, POR/BOR timing, and DS80000649B
  errata gates.
- Updated package/export rules so normal packages include headers, source,
  examples, metadata, and selected current docs while excluding generated
  archives, local build output, tests, tools, prompts, CI metadata, large PDFs,
  HIL reports, and extracted reference markdown.
- Simplified `docs/` around maintained API, device reference, ESP-IDF,
  hardware validation, release checklist, hardening summary, and release notes.

### Fixed

- Clarified status, health, lifecycle, reset/probe/recover, callback lifetime,
  non-ISR, threading, and deleted copy/move contracts.
- Completed native ESP-IDF CLI behavioral parity: bounded command parsing,
  functional `color on|off`, real `selftest output`, read-only `stress`,
  state-restoring `stress_mix`, state/health/cfg parity, null-handle guards,
  raw-write warnings, and General Call warning/argument parity.
- Strengthened native tests, fault-injection coverage, generated-artifact
  checks, Arduino/ESP-IDF CLI contract guards, stale-placeholder guards, and
  ESP-IDF native-boundary checks.

### Validation

- Added bundled safe-only ESP32-S2 HIL evidence: one 1-hour panic-repro run and
  one 8-hour safe-only soak.
- The 8-hour run finished `PASS_SAFE_ONLY` with `183221 / 183221 / 0` soak
  commands, worst latency `0.188 s`, and final driver state READY with zero
  failures.
- Release tag CI for `v1.0.0` passed after the tag was pushed, including
  PlatformIO ESP32-S2/ESP32-S3 builds, native tests, package validation, and
  pure ESP-IDF `esp32s2`/`esp32s3` builds.

### Known Limits

- This is a pre-production software package. It does not claim production
  readiness, hardware validation, high-voltage safety, analog accuracy, General
  Call safety, or industry readiness.
- Output-changing, General Call, SHDN, WLAT, rail-cycle, fault-injection,
  analog, and high-voltage checks were not run and remain separate evidence
  gates.
- Production-readiness claims remain blocked until the hardware validation
  checklist evidence is complete.
