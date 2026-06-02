# Prompt 06 — Complete Software Test/Fault Coverage and Guard Scripts

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this prompt, commit, sync/push, and stop.

## Exploration findings fixed by this prompt

- L1. Address tests do not cover every candidate address.
- Remaining H1/H2/M1/M3/H5 test gaps.
- Part of M7 public contract evidence.
- Guard weaknesses around placeholders, docs mismatch, generated artifacts.

## Goal

Make software-verifiable production contracts hard to regress.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
```

Stop if dirty or pull fails.

Inspect:

```bash
find test tests tools scripts .github -maxdepth 4 -type f 2>/dev/null | sort
sed -n '1,420p' test/test_basic.cpp
sed -n '420,880p' test/test_basic.cpp
sed -n '880,1320p' test/test_basic.cpp
sed -n '1,320p' tools/check_cli_contract.py
sed -n '1,320p' tools/check_idf_example_contract.py
sed -n '1,320p' tools/check_core_timing_guard.py
```

## Spawn subagents

1. `native-test-agent`
   - Add table-driven tests and edge cases.

2. `fault-injection-agent`
   - Add fake transport mutation-then-fail modes.

3. `guard-script-agent`
   - Strengthen contract scripts.

4. `ci-agent`
   - Verify workflow covers all intended software checks.

5. `integration-review-agent`
   - Prevent nonessential churn.

## Required tests

### 1. Address matrix

Add full table-driven tests:
- standard accepted by default: `0x3C`, `0x3D`, `0x3E`, `0x3F`;
- adjacent invalid: `0x3B`, `0x40`;
- alternate rejected by default: `0x5C`, `0x5D`, `0x5E`, `0x5F`;
- alternate accepted only with opt-in;
- adjacent invalid with opt-in: `0x5B`, `0x60`.

### 2. Variant/wiper matrix

Ensure tests cover:
- HV31: `0x00`, `0x3F`, `0x7F`, reject `0x80`;
- HV51: `0x00`, `0x7F`, `0x80`, `0xFF`;
- percent/fraction conversion at 0%, 50%, 100%;
- out-of-range percent/fraction rejection;
- header/docs behavior consistency if a doc checker exists.

### 3. Ambiguous writes

If not already complete:
- Wiper mutated then timeout;
- TCON mutated then data NACK;
- INC/DEC partial failure;
- General Call possible mutation then failure;
- raw write possible mutation then failure;
- uncertainty/cache expectations.

### 4. Begin/startup failures

If not already complete:
- no optional output-changing writes by default;
- optional initial write failure after mutation;
- recover/readback after failed begin.

### 5. Health/status/probe/recover

- probe health side effects match docs;
- reset callback OK alone does not prove READY;
- recover only clears uncertainty after evidence;
- transport error mapping for begin/probe/recover.

### 6. Public lifecycle

- copy/move policy static assertions if implemented;
- public header can be included alone without Arduino/IDF stubs;
- no framework leakage in core.

### 7. CLI/IDF static guards

Update guard scripts to catch:
- command in help without handler;
- placeholder handler advertised as implemented;
- output-changing command without warning;
- `color` command mismatch;
- Arduino include in IDF;
- unsafe numeric cast path in IDF example;
- stale generated artifacts.

## CI

Ensure CI includes:
- native tests,
- guard scripts,
- version check,
- generated artifact check,
- Arduino S2/S3 builds,
- package validation,
- pure IDF build job if feasible.

Do not claim remote CI passed unless it actually ran. Local checks are separate.

## Report

Create:

```text
docs/MCP45HVX1_TESTS_FAULT_GUARDS_REPORT.md
```

Include:
- tests added,
- guard scripts added/strengthened,
- coverage still missing,
- local results,
- CI changes.

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
```

Remove package artifact if generated.

## Commit and sync

```bash
git diff --check
git status --short
git add test tests tools scripts .github include src examples README.md docs platformio.ini CMakeLists.txt idf_component.yml 2>/dev/null || true
git commit -m "test: expand MCP45HVX1 fault and guard coverage"
git push
```

Stop and report.