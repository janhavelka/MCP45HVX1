# Prompt 05 — Fix H5: ESP-IDF CLI Behavioral Parity, Bounded Parsing, and Build Gate

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this prompt, commit, sync/push, and stop.

## Exploration findings fixed by this prompt

Primary:
- H5. ESP-IDF example has command-token parity but not behavioral parity.

Supporting:
- M2. Uncertain-state observability inconsistent across CLIs.
- H3/H4 release evidence requirements.

## Goal

The ESP-IDF example must be honest, native, bounded, and not advertise commands that do not really behave as stated.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
```

Stop if dirty or pull fails.

Inspect:

```bash
find examples/espidf_basic -maxdepth 5 -type f | sort
sed -n '1,260p' examples/espidf_basic/main/main.cpp
sed -n '260,620p' examples/espidf_basic/main/main.cpp
sed -n '620,1100p' examples/espidf_basic/main/main.cpp
sed -n '1,260p' tools/check_idf_example_contract.py
sed -n '1,240p' tools/check_cli_contract.py
sed -n '1,220p' .github/workflows/ci.yml
```

## Spawn subagents

1. `idf-command-parity-agent`
   - Compare Arduino vs IDF command behavior.

2. `idf-parser-bounds-agent`
   - Find all numeric parsing/casts and add command-specific bounds.

3. `idf-color-status-agent`
   - Implement real color or remove/qualify color command.

4. `idf-build-ci-agent`
   - Add pure `idf.py build` workflow if feasible.

5. `contract-guard-agent`
   - Ensure future placeholder commands fail scripts.

## Required implementation

### 1. Remove placeholder behavior

The audit found:
- `selftest output` is advertised as output-changing,
- but handler only warns and runs read-only selftest.

Choose one:
- implement real opt-in output-changing selftest with baseline capture, write/readback, restore, and uncertainty on restore failure;
- or remove it from help and explicitly document it as not implemented.

No advertised command may be a placeholder.

### 2. Bounded parsing before casts

Fix `parseU32()` or add command-specific parsers:
- reject negative strings,
- reject trailing garbage,
- reject overflow,
- reject values above target type max,
- reject values above command-specific max before `uint8_t` cast,
- validate address ranges,
- validate Wiper by active variant,
- validate TCON and register values,
- validate RAB choices,
- validate counts for stress/increment/decrement.

### 3. IDF color honesty

Either:
- implement ANSI color helpers and use them in PASS/WARN/FAIL/headers, with `color on/off`, or
- remove/qualify `color` as a no-op/plain-output setting.

Preferred: implement simple optional ANSI output matching Arduino semantics.

### 4. IDF state/health parity

IDF `state`, `drv`/`health`, and `cfg` should include:
- active address,
- variant,
- resolution,
- RAB nominal,
- Wiper/TCON cache-known flags,
- uncertainty if Prompt 01 added it,
- last status/error name/detail/message.

### 5. Null handle guards

Ensure no scan/probe/read/write dereferences invalid bus/device handles after init/reset failure.

### 6. CI build gate

Add/verify CI for pure ESP-IDF builds:
- esp32s3,
- esp32s2 if supported.

If local `idf.py` unavailable, record local failure but still add workflow if project style supports it. Do not claim it passed.

### 7. Contract guard

Update `tools/check_idf_example_contract.py`:
- reject placeholder/stub/TODO for advertised commands,
- verify no Arduino includes,
- verify bounded parsing helper usage for output-changing commands,
- verify color command either functional or not advertised,
- verify `selftest output` behavior matches help.

## Report

Create:

```text
docs/MCP45HVX1_ESP_IDF_PARITY_REPORT.md
```

Include:
- command changes,
- parser changes,
- color decision,
- build gate,
- checks run,
- remaining IDF limitations.

## Validation

Run:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/check_generated_artifacts.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
idf.py --version
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

Remove package artifact if generated.

## Commit and sync

```bash
git diff --check
git status --short
git add examples tools .github README.md docs CMakeLists.txt idf_component.yml platformio.ini 2>/dev/null || true
git commit -m "fix: align MCP45HVX1 ESP-IDF CLI behavior"
git push
```

Stop and report.