# Prompt 07 — MCP45HVX1 Final Integration Review, Comprehensive Report, and Merge Gate

You are continuing the MCP45HVX1 industry-readiness branch. Complete only this final chunk, commit and sync, then stop.

## Goal

Perform an adversarial final review of all hardening changes, run all available checks, produce a comprehensive final report, and state whether the repository is ready to merge or what remains.

## Starting procedure

```bash
git branch --show-current
git status --short
git pull --ff-only
git log --oneline -n 12
```

If dirty unexpectedly, inspect and report.

Spawn subagents:
- `mcp-final-integration-reviewer`
- `mcp-datasheet-reviewer`
- `mcp-tests-ci-reviewer`
- `mcp-docs-release-reviewer`
- `mcp-hardware-validation-reviewer`

## Final review checklist

### 1. Architecture

Verify:
- no Arduino/ESP-IDF/FreeRTOS leakage in core;
- core uses injected transport;
- no hidden bus ownership in core;
- no global mutable driver state unless justified;
- copy/move deleted;
- no steady-state heap allocation in core;
- thread/ISR contract documented.

### 2. Device correctness

Verify:
- standard address `0x3C..0x3F`;
- alternate range policy is opt-in and documented if present;
- 7-bit/8-bit variants handled correctly;
- volatile-only behavior documented;
- TCON/SHDN/WLAT semantics correct;
- high-voltage safety docs are prominent;
- resistance calculations are nominal and caveated.

### 3. Examples and CLI

Verify:
- Arduino example still builds if toolchain available;
- ESP-IDF example is native and honest;
- no placeholder command is advertised as implemented;
- safe selftest does not leave wiper/TCON changed;
- output-changing operations are opt-in and warned.

### 4. Tests and guards

Run:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
python -m py_compile tools/run_hil_mcp45hvx1.py
```

Attempt pure IDF if available:

```bash
idf.py -C <idf-example-path> set-target esp32s3 build
idf.py -C <idf-example-path> set-target esp32s2 build
```

Run:

```bash
git diff --check
git status --short
```

Do not invent pass results. Record exact failures.

### 5. Hardware validation status

Separate:
- software checks completed;
- safe hardware HIL completed;
- output-changing HIL completed;
- high-voltage/analog validation completed;
- not run.

Never claim industry-grade field readiness if hardware/fault validation is pending.

## Required final report

Create:

```text
docs/MCP45HVX1_HARDENING_FINAL_REPORT.md
```

Use this structure:

```markdown
# MCP45HVX1 Industry-Readiness Hardening Final Report

Date:
Branch:
Starting commit:
Final commit:

## Executive Summary

## What Was Implemented

## Public API Changes

## Core Contract Changes

## Device-Specific Correctness Changes

## ESP-IDF Example Changes

## Tests Added

## Documentation Changes

## HIL / Hardware Validation Tooling

## Checks Run Locally

| Command | Result | Notes |
| --- | --- | --- |

## Checks Not Run

## Hardware Validation Performed

## Hardware Validation Still Needed

## Remaining Risks

## Release / Merge Recommendation

Choose one:
- Ready to merge, not yet field-release.
- Ready to release as pre-production candidate.
- Not ready to merge.
- Blocked by hardware validation.
- Blocked by software defects.

## Follow-Up Work

List P0/P1/P2.
```

## Acceptance criteria before merge recommendation

A “ready to merge” recommendation requires:
- clean worktree except intended final report before commit;
- all available local software checks pass or failures are explained as missing tools;
- no known source-code blocker remains;
- docs do not overclaim hardware validation;
- generated artifacts not committed;
- final report exists;
- branch pushed.

A “ready to release as industry-grade” recommendation additionally requires:
- pure ESP-IDF build evidence;
- Arduino S2/S3 build evidence;
- native tests;
- real hardware safe and output-changing validation where applicable;
- address policy verified on actual hardware;
- analog/high-voltage validation or explicit low-voltage-only scope.

## Commit and sync

```bash
git diff --check
git status --short
git add docs README.md CHANGELOG.md .github tools test tests include src examples library.json platformio.ini CMakeLists.txt idf_component.yml 2>/dev/null || true
git commit -m "docs: finalize MCP45HVX1 industry hardening report"
git push
git status --short
```

Stop and report:
- branch,
- final commit,
- checks run,
- pass/fail,
- whether ready to merge,
- whether ready to release,
- remaining hardware validation.
