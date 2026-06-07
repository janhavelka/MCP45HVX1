# MCP45HVX1 ESP-IDF Parity Report

Branch: `hardening/mcp45hvx1-industry-readiness`

Prompt: `05_espidf_cli_behavioral_parity_and_build_gate`

## Command Changes

- The native ESP-IDF CLI now normalizes command input to lowercase and splits
  tokens on any whitespace, matching the Arduino CLI expectation for uppercase
  commands and tab-separated input.
- `selftest output` is a real operator-gated output-changing command. It reads a
  Wiper/TCON baseline, writes and verifies Wiper/TCON test values, restores the
  baseline, and marks output uncertainty on restore/readback failure.
- `stress` remains read-only and defaults to 8 iterations. `stress_mix` remains
  output-changing, restores the starting snapshot, reports uncertainty, and now
  ramps Wiper values across the active variant range instead of toggling only
  endpoints.
- Read-only diagnostics such as `probe`, `recover`, `cfg`, `settings`, `detail`,
  `drv`, `health`, `state`, `read`, `dump`, `readwiper`, `readtcon`, and `last`
  reject unexpected extra arguments instead of silently ignoring them.

## Parser Changes

- Output-changing and byte-sized command arguments now use command-specific
  bounded parsers before any `uint8_t` write, address, register, or step count is
  issued.
- The ESP-IDF parser rejects negative values, leading plus signs, trailing
  garbage, overflows, addresses outside the documented ranges, Wiper values above
  the active HV31/HV51 max, invalid register IDs, invalid TCON/raw values, and
  stress/step counts outside `1..255`.
- `raw write`, `wreg`, General Call, Wiper percent/fraction, terminal, mode,
  and address commands use shared bounded helpers and whitespace tokenization.

## Color Decision

- `color on|off` is functional in the ESP-IDF CLI.
- ANSI color output is locally gated through `styleCode()`,
  `styleSetEnabled()`, and `styleIsEnabled()`.
- PASS/WARN/FAIL/header/danger output uses the same style path so the command is
  not a no-op.

## State And Health Parity

- `state`, `drv`/`health`, and `cfg` show active address, variant, resolution,
  RAB nominal, cache-known flags, aggregate/core uncertainty, last status, and
  last uncertainty error.
- Scan and transport paths guard null bus/device handles after failed init or
  reset.
- `recover` clears the example-local output uncertainty only after successful
  core recovery/readback.

## Build Gate

- CI now includes a pure ESP-IDF build matrix for `esp32s3` and `esp32s2` using
  `espressif/esp-idf-ci-action@v1` pinned to ESP-IDF `v6.0.1`.
- The ESP-IDF example CMake no longer exposes `examples/common`; it includes
  only the example directory and repo root plus native IDF components.

## Contract Guard

- `tools/check_idf_example_contract.py` now rejects Arduino/compatibility
  dependencies across the whole native IDF example tree.
- It requires exact command dispatch, native IDF tokens, bounded parser helpers,
  functional color, real `selftest output` behavior, General Call native IDF
  implementation tokens, and no stale docs claiming the IDF example is absent.
- `tools/check_cli_contract.py` now records the current native General Call IDF
  tokens.

## Checks Run

- `python tools/check_core_timing_guard.py` - passed.
- `python tools/check_cli_contract.py` - passed.
- `python tools/check_idf_example_contract.py` - passed.
- `python tools/check_generated_artifacts.py` - passed.
- `python scripts/generate_version.py check` - passed.
- `python -m platformio test -e native` - first attempt hit a transient local
  `.pio/build/native` artifact-directory creation error; immediate rerun passed
  55/55 tests.
- `python -m platformio run -e esp32s3dev` - passed.
- `python -m platformio run -e esp32s2dev` - passed.
- `python -m platformio pkg pack` - passed; generated
  `MCP45HVX1-1.0.0.tar.gz`, which was removed before commit.
- `idf.py --version` - failed locally because `idf.py` is not on PATH.
- `idf.py -C examples/espidf_basic set-target esp32s3 build` - failed locally
  because `idf.py` is not on PATH.
- `idf.py -C examples/espidf_basic set-target esp32s2 build` - failed locally
  because `idf.py` is not on PATH.

## Remaining IDF Limitations

- Local validation still depends on whether `idf.py` is installed in the
  developer environment. CI is the authoritative pure-IDF build gate when local
  ESP-IDF tooling is unavailable.
- No hardware console/HIL transcript was produced in this prompt, so runtime
  hardware parity and General Call safety remain evidence-gated.
