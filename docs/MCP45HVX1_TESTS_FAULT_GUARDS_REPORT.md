# MCP45HVX1 Tests Fault Guards Report

Branch: `hardening/mcp45hvx1-industry-readiness`

Prompt: `06_tests_fault_injection_and_guard_scripts`

## Tests Added

- Added `test_begin_address_matrix`, a table-driven address contract covering:
  documented `0x3C..0x3F`, adjacent invalid `0x3B`/`0x40`, alternate
  `0x5C..0x5F` rejected by default, alternate accepted only with opt-in, and
  opt-in adjacent invalid `0x5B`/`0x60`.
- Added `test_variant_wiper_code_matrix`, covering HV31 accepted `0x00`,
  `0x3F`, `0x7F`, HV31 rejected `0x80`, and HV51 accepted `0x00`, `0x7F`,
  `0x80`, `0xFF`.
- Added `test_fraction_endpoint_matrix_by_variant`, covering 0%, 50%, and 100%
  conversion/write/readback for HV31 and HV51 plus out-of-range/NaN rejection
  without I2C writes.
- Strengthened ambiguous-write tests to use exact requested statuses:
  Wiper mutation then timeout and TCON mutation then data NACK.
- Added direct public raw-register mutation-failure coverage through
  `writeRegister()` for Wiper and TCON, asserting affected cache invalidation and
  hardware uncertainty.
- Added recover status-detail matrix for timeout, bus error, data NACK, and
  address NACK.
- Added partial-recover evidence coverage: `recover()` verifying Wiper but
  failing TCON keeps uncertainty until TCON is read back.
- Added lifecycle coverage for `end()` resetting state/cache without additional
  bus I/O and expanded pre-begin public API rejection checks.

## Guard Scripts Strengthened

- `tools/check_cli_contract.py`
  - Extracts Arduino help commands from `printHelpItem()` and fails if help
    advertises a command without `handleCommand()` dispatch.
  - Requires warning/danger tokens for output-changing Arduino command surfaces.
  - Expands placeholder markers to include `coming soon`, `TBD`, `NYI`, and
    `dummy`.
  - Scans command-facing docs for advertised placeholder language.
  - Detects raw ANSI escapes as `\033` or `\x1B`, with the native ESP-IDF CLI
    explicitly allowed to own its local ANSI implementation.
- `tools/check_idf_example_contract.py`
  - Extracts ESP-IDF commands from `printHelp()` and fails if advertised command
    text has no dispatch branch.
  - Requires warning/danger tokens for output-changing ESP-IDF command surfaces.
  - Broadens unsafe `uint8_t` cast detection in command handlers.
  - Keeps native-IDF boundary, placeholder, color, bounded-parser, General Call,
    and `selftest output` restore-flow guards.
- `tools/check_generated_artifacts.py`
  - Now checks both tracked files and untracked non-ignored files, so package
    tarballs and other generated artifacts are caught before commit.

## CI Changes

- Added explicit CI steps in `native-tests` for:
  - `python tools/check_idf_example_contract.py`
  - `python tools/check_generated_artifacts.py`
- Existing CI already covers native tests, core timing guard, version check,
  Arduino ESP32-S3/S2 PlatformIO builds, package validation, and pure ESP-IDF
  `esp32s3`/`esp32s2` build jobs.
- Remote CI was not observed in this prompt; this report records local results
  only.

## Local Results

- `python tools/check_core_timing_guard.py` - passed.
- `python tools/check_cli_contract.py` - passed.
- `python tools/check_idf_example_contract.py` - passed.
- `python tools/check_generated_artifacts.py` - passed.
- `python scripts/generate_version.py check` - passed; `Version.h` was up to
  date.
- `python -m platformio test -e native` - passed, 60/60 tests.
- `python -m platformio run -e esp32s3dev` - passed.
- `python -m platformio run -e esp32s2dev` - passed.
- `python -m platformio pkg pack` - passed and produced
  `MCP45HVX1-1.0.0.tar.gz`; the generated tarball was removed.
- `python tools/check_generated_artifacts.py` - passed again after removing the
  package tarball.

PlatformIO printed its existing obsolete-core warning during the PlatformIO
commands, but the required build, test, and package commands completed
successfully.

## Coverage Still Missing

- No hardware/HIL transcript was produced; analog movement, WLAT/SHDN behavior,
  and General Call shared-bus safety remain evidence-gated.
- Static warning guards are token/regex based. They prevent common regressions
  but do not replace golden CLI transcript tests.
- Pure ESP-IDF builds are covered by CI configuration, but local `idf.py`
  remains unavailable in this environment unless ESP-IDF is installed on PATH.
