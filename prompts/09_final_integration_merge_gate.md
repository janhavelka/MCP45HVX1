# Prompt 09 — Final Integration Review, Comprehensive Report, and Merge/Release Gate

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this final prompt, commit, sync/push, and stop.

## Goal

Adversarially review all changes against the exploration report, run all available checks, produce one comprehensive final report, and decide whether the branch is ready to merge and/or release.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
git log --oneline --decorate -n 30
```

If dirty unexpectedly, inspect and report.

## Spawn final-review subagents

1. `final-core-reviewer`
   - H1/H2/M1/M3/M4/M7 resolved?

2. `final-device-reviewer`
   - Datasheet, SHDN/WLAT, POR/BOR, errata, high-voltage docs resolved?

3. `final-cli-idf-reviewer`
   - Arduino/IDF command honesty, parsing, color, uncertainty output, safe/output split.

4. `final-tests-ci-reviewer`
   - Native tests, guards, CI, IDF build evidence.

5. `final-docs-release-reviewer`
   - Overclaims removed, release checklist, package policy, stale docs fixed.

6. `final-hil-reviewer`
   - HIL tooling/evidence status and remaining hardware validation.

Each subagent must report PASS/FAIL/PARTIAL with evidence.

## Required final checks

Run:

```bash
git diff --check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/check_generated_artifacts.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
python -m py_compile tools/run_hil_mcp45hvx1.py
idf.py --version
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

Record exact results. Remove generated package artifact if not meant to be committed.

If IDF tool unavailable, do not claim local IDF pass.

## Hardware/HIL evidence status

Separate:
- HIL runner exists,
- safe HIL actually run,
- output-changing HIL actually run,
- low-voltage analog validation actually run,
- high-voltage validation actually run,
- errata/isolated-bus decision actually recorded,
- not run.

No hardware claim without logs.

## Required final report

Create:

```text
docs/MCP45HVX1_INDUSTRY_HARDENING_FINAL_REPORT.md
```

Structure:

```markdown
# MCP45HVX1 Industry-Readiness Hardening Final Report

Date:
Branch:
Starting commit:
Final commit:

## Executive Summary

## Original Audit Findings Closure Matrix

| Finding | Status | Evidence |
| --- | --- | --- |

## What Was Implemented

## Public API Changes

## Core Contract Changes

## Device-Specific Correctness Changes

## CLI Changes

## ESP-IDF Changes

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
- Ready to release as industry-grade.
- Not ready to merge.
- Blocked by hardware validation.
- Blocked by software defects.

## Follow-Up Work

### P0
### P1
### P2
```

## Decision rules

Ready to merge requires:
- no known P0 software blockers,
- all available local software checks pass or missing tools are explained,
- docs do not overclaim,
- final report exists,
- branch pushed.

Ready to release as industry-grade additionally requires:
- pure ESP-IDF build evidence,
- hardware safe HIL logs,
- output-changing HIL logs on safe load,
- low-voltage analog validation,
- high-voltage validation if claimed,
- errata/isolated-bus evidence,
- final changelog/version/tag/package evidence.

## Commit and sync

```bash
git diff --check
git status --short
git add docs README.md CHANGELOG.md SECURITY.md CONTRIBUTING.md library.json idf_component.yml platformio.ini CMakeLists.txt .github tools test tests include src examples .gitignore 2>/dev/null || true
git commit -m "docs: finalize MCP45HVX1 industry hardening report"
git push
git status --short
```

## Final response

Return:
- branch,
- final commit,
- pushed yes/no,
- final report path,
- audit finding closure summary,
- software checks summary,
- hardware validation summary,
- ready to merge yes/no,
- ready to release yes/no,
- remaining P0/P1/P2.