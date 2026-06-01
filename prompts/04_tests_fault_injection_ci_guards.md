# Prompt 04 — MCP45HVX1 Native Tests, Fault Injection, and Guard Scripts

You are continuing the MCP45HVX1 industry-readiness branch. Complete only this chunk, commit and sync, then stop.

## Goal

Build confidence with fake-transport tests and guard scripts. Focus on software-verifiable behavior: address validation, wiper/TCON commands, I2C error mapping, invalid/reserved command behavior, recovery/health, and no framework leakage.

## Starting procedure

```bash
git branch --show-current
git status --short
git pull --ff-only
```

If dirty or pull fails, stop and report.

Spawn subagents:
- `mcp-tests-agent`
- `mcp-fault-injection-agent`
- `mcp-core-contracts-agent`
- `mcp-idf-example-agent`
- `mcp-integration-review-agent`

## Required audit

Inspect existing tests and tools:

```bash
find test tests tools scripts .github -maxdepth 4 -type f 2>/dev/null | sort
sed -n '1,320p' test/test_basic.cpp 2>/dev/null || true
sed -n '1,320p' tools/check_core_timing_guard.py 2>/dev/null || true
sed -n '1,320p' tools/check_cli_contract.py 2>/dev/null || true
sed -n '1,320p' tools/check_idf_example_contract.py 2>/dev/null || true
sed -n '1,260p' .github/workflows/* 2>/dev/null || true
```

## Required native tests

Add or extend fake I2C transport tests for:

### 1. Address policy

- Standard addresses `0x3C..0x3F` accepted.
- Addresses outside range rejected.
- Alternate `0x5C..0x5F`, if supported, rejected by default and accepted only with explicit opt-in.
- Address switching/reconfigure preserves expected health state and active config.

### 2. Wiper/resolution behavior

- MCP45HV31 7-bit range: `0x00`, mid `0x3F`, max `0x7F`, invalid `0x80`.
- MCP45HV51 8-bit range: `0x00`, mid `0x7F`, `0x80`, max `0xFF`.
- Percent/fraction helpers map correctly and do not exceed max code.
- Readback reconstruction handles returned 16-bit read format correctly.

### 3. TCON/terminal control

- POR/default `TCON=0xFF`.
- Terminal A/W/B connect/disconnect helpers map to correct bits.
- Hardware shutdown control bit maps correctly.
- Reserved bits are preserved/forced as expected.
- Raw TCON writes are validated or documented as diagnostic.

### 4. Volatile reset/recover semantics

- Begin/recover reads or applies expected volatile defaults.
- POR/BOR default docs match code behavior.
- Cached desired state reapplication, if implemented, is explicit and tested.
- Recover after I2C failure clears or preserves health according to contract.

### 5. Fault injection

- Address NACK.
- Data NACK.
- Timeout.
- Generic bus error.
- Invalid/reserved memory address access.
- Partial write or lost ACK after command byte/data byte where fake transport can simulate it.
- Driver does not claim hardware state changed if a write failed before the ACK that commits data.
- Driver marks uncertainty/dirty state if it cannot know whether the volatile wiper/TCON changed.

### 6. Probe and health

- `probe()` success/failure behavior and health side effects exactly match documentation.
- `recover()` and `resetI2cState()` are tracked consistently.
- Offline/degraded behavior, if present, is tested.

### 7. Framework leakage and guards

Guard scripts must reject:
- Arduino/ESP-IDF headers in core,
- `Wire`, `Serial`, `String`, `delay`, `millis`, `micros` in core,
- placeholder IDF CLI handlers,
- accidental generated artifacts,
- mismatch between CLI help and handlers.

## CI/build improvements

If `.github/workflows/ci.yml` exists:
- ensure native tests run;
- ensure Arduino ESP32-S2 and ESP32-S3 builds run if this repository supports them;
- ensure guard scripts run;
- add ESP-IDF container build for the authoritative IDF example if the repo pattern supports it.

If CI does not exist, add a minimal workflow consistent with the other hardened libraries.

Do not claim CI passed unless it actually ran. Local workflow edits are not remote CI validation.

## Validation commands

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
```

Attempt pure IDF if available:

```bash
idf.py -C <idf-example-path> set-target esp32s3 build
idf.py -C <idf-example-path> set-target esp32s2 build
```

## Report

Create/update:

```text
docs/MCP45HVX1_TESTS_AND_CI_REPORT.md
```

Include:
- tests added,
- fake transport capabilities,
- coverage still missing,
- CI changes,
- exact local results,
- exact commands not run and why.

## Commit and sync

```bash
git diff --check
git status --short
git add test tests tools scripts .github README.md docs platformio.ini CMakeLists.txt idf_component.yml 2>/dev/null || true
git commit -m "test: expand MCP45HVX1 fault coverage"
git push
```

Stop after reporting.
