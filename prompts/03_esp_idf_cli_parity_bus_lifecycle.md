# Prompt 03 — MCP45HVX1 ESP-IDF CLI Parity and Native Bus Lifecycle

You are continuing the MCP45HVX1 industry-readiness branch. Complete only this chunk, commit and sync, then stop.

## Goal

Make the ESP-IDF example honest, native, and useful. Remove or fix placeholder parity. Ensure I2C handle lifecycle, timeout/error mapping, CLI parsing, and selftest behavior are production-like for a diagnostic example.

## Starting procedure

```bash
git branch --show-current
git status --short
git pull --ff-only
```

If dirty or pull fails, stop and report.

Spawn subagents:
- `mcp-idf-example-agent`
- `mcp-cli-contract-agent`
- `mcp-core-contracts-agent`
- `mcp-tests-agent`
- `mcp-integration-review-agent`

## Required audit

Inspect:

```bash
find examples -maxdepth 5 -type f | sort
sed -n '1,360p' examples/espidf_basic/main/main.cpp 2>/dev/null || true
sed -n '360,760p' examples/espidf_basic/main/main.cpp 2>/dev/null || true
sed -n '1,360p' examples/esp_idf/basic/main/main.cpp 2>/dev/null || true
sed -n '1,260p' tools/check_idf_example_contract.py 2>/dev/null || true
sed -n '1,260p' tools/check_cli_contract.py 2>/dev/null || true
```

Determine:
1. Which ESP-IDF example path is authoritative: `examples/espidf_basic` or `examples/esp_idf/basic`.
2. Whether any duplicate/stale example path exists.
3. Which commands are real and which are placeholders/stubs.
4. Whether the CLI help overclaims feature parity.
5. Whether the IDF example uses fixed buffers and native IDF APIs.
6. Whether it adds/removes I2C device handles per transaction or keeps persistent handles.
7. Whether timeouts and IDF errors map into driver `Status` precisely.
8. Whether output-changing commands are clearly marked.

## Implementation requirements

### 1. Resolve example path naming

If both `examples/espidf_basic` and `examples/esp_idf/basic` exist:
- choose the repository’s established convention or align with the rest of this library family;
- avoid duplicate stale examples;
- update README/docs accordingly;
- update guard scripts to check the actual authoritative path.

Do not delete a path if CI/docs depend on it without updating all references.

### 2. Remove placeholder parity

For every CLI command listed in help:
- implement it fully, or
- remove it from help/contract, or
- label it clearly as unsupported in this example.

No placeholder command may pass the contract as if it were implemented.

Minimum useful native IDF commands:
- `help`
- `version`
- `scan`
- `addr`
- `probe`
- `recover`
- `health` / `drv`
- `cfg` / `state`
- `readwiper`
- `wiper <code|percent/fraction>`
- `readtcon`
- `tcon`
- `term`
- `shutdown` / `software-shutdown` with explicit semantics
- `mode` / `resolution` if variant is configurable
- `selftest`
- `stress`
- `reg read`
- `reg write` diagnostic only, guarded/warned
- `safe` / `destructive` mode indicator if output-changing commands exist.

### 3. Native ESP-IDF bus lifecycle

Prefer:
- persistent bus/device handle for the active address;
- clean address change path that removes/recreates the device handle once;
- mutex or explicit single-task policy;
- no per-transaction add/remove churn;
- timeout propagated into IDF transaction calls;
- precise `esp_err_t` mapping where possible.

Document if the example is diagnostic, not a production shared-bus manager.

### 4. CLI parsing and output safety

- Use fixed-size C buffers; avoid `std::string` in IDF example unless the repo explicitly allows it and documents why.
- Check argument counts and parse errors.
- Bound all numeric inputs.
- Print active address, resolution, RAB value, and whether alternate address range is enabled.
- Output-changing commands must clearly say they may affect the analog circuit.
- Raw register writes must warn that they can desync cache/state unless the driver intentionally syncs.

### 5. Selftest split

Implement or verify two selftest levels:

1. `selftest` or `selftest safe`
   - read-only or state-restoring operations only;
   - must not leave wiper/TCON changed;
   - must not require unsafe analog output movement.

2. `selftest destructive` or `hil output`
   - opt-in only;
   - may write wiper/TCON;
   - must restore baseline;
   - must print exactly what will change before changing it.

### 6. Guard scripts

Update `tools/check_idf_example_contract.py` and `tools/check_cli_contract.py`:
- require real command handlers, not only help text;
- reject known placeholder markers: `TODO`, `placeholder`, `not implemented`, `stub`, `unsupported` unless intentionally documented in an unsupported-command table;
- enforce no Arduino includes in IDF example.

## Validation

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

Adjust the `idf.py -C` path to the actual authoritative ESP-IDF example.

If IDF tools are unavailable, record exact failure.

## Report

Create/update:

```text
docs/MCP45HVX1_ESP_IDF_EXAMPLE_PARITY_REPORT.md
```

Include:
- authoritative example path,
- commands implemented,
- commands intentionally unsupported,
- bus lifecycle model,
- parsing/safety changes,
- checks run,
- remaining hardware validation.

## Commit and sync

```bash
git diff --check
git status --short
git add examples tools README.md docs CMakeLists.txt idf_component.yml 2>/dev/null || true
git commit -m "fix: improve MCP45HVX1 ESP-IDF CLI parity"
git push
```

Stop after reporting.
