# Prompt 08 — HIL Tooling and Hardware Evidence Capture

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this prompt, commit, sync/push, and stop.

## Exploration findings fixed by this prompt

- H3. Hardware validation evidence missing.
- H4. Errata workaround not release-gated.
- M5. Hardware checklist too narrow.
- Supports final release evidence.

## Goal

Add safe-by-default HIL tooling that can produce attachable evidence. Do not claim hardware validation unless the tool is actually run and logs are captured.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
```

Stop if dirty or pull fails.

Inspect:

```bash
find tools examples docs -maxdepth 5 -type f | sort
grep -R "HIL\\|hardware validation\\|selftest\\|stress\\|serial\\|COM\\|General Call\\|SHDN\\|WLAT" -n tools examples docs README.md || true
```

## Spawn subagents

1. `hil-safety-agent`
   - Define safe and output-changing command groups.

2. `serial-runner-agent`
   - Implement Python HIL runner.

3. `evidence-format-agent`
   - Define raw transcript, JSON summary, Markdown report.

4. `cli-compat-agent`
   - Ensure CLI output is parseable and color can be stripped/disabled.

5. `docs-agent`
   - Update validation docs.

## Required HIL runner

Create:

```text
tools/run_hil_mcp45hvx1.py
```

Default must be safe/read-only.

Supported options:
- `--port`
- `--baud`
- `--address`
- `--timeout`
- `--output-dir`
- `--no-color`
- `--include-output-change`
- `--include-wiper-ramp`
- `--include-tcon-toggle`
- `--include-shdn`
- `--include-wlat`
- `--include-general-call`
- `--confirm-isolated-bus`
- `--operator-prompts`

Example commands:

```bash
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C --include-output-change --operator-prompts
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C --include-general-call --confirm-isolated-bus
```

## Safe default sequence

Run and capture:
- `version`
- `color off` if supported
- `help`
- `scan`
- `addr`
- set address if requested
- `probe`
- `cfg`
- `state`
- `drv` or `health`
- `readwiper`
- `readtcon`
- `dump` or read-only register dump if safe
- `selftest` / `selftest safe`
- `stress 100` only if read-only
- final `state`
- final `drv`

## Output-changing opt-in

Only with `--include-output-change`:
- capture baseline Wiper/TCON,
- set bounded Wiper values,
- read back,
- prompt operator for measured voltage/resistance,
- restore baseline,
- verify restore,
- if restore fails, verdict is `FAIL_RESTORE_UNCERTAIN`.

## SHDN/WLAT

Only with flags and operator prompt:
- record whether pins are wired,
- ask operator to toggle or use CLI if available,
- distinguish register readback from physical output.

## General Call

Only if:
- `--include-general-call`,
- `--confirm-isolated-bus`,
- operator confirms bus isolation or accepts risk.

Print errata warning into logs and report.

## Evidence outputs

Create:

```text
hil_logs/mcp45hvx1_<timestamp>/
```

Files:
- `raw_serial.txt`
- `commands.txt`
- `summary.json`
- `report.md`
- `operator_notes.md`

Add `hil_logs/` to `.gitignore` unless project intentionally tracks evidence.

Verdict values:
- `PASS_SAFE_ONLY`
- `PASS_WITH_OUTPUT_CHANGE`
- `FAIL`
- `FAIL_RESTORE_UNCERTAIN`
- `OPERATOR_REVIEW_REQUIRED`
- `SKIPPED_UNSAFE`

## Docs

Update:
- `docs/MCP45HVX1_HARDWARE_VALIDATION.md`
- `docs/MCP45HVX1_RELEASE_CHECKLIST.md`
- README HIL section

## Report

Create:

```text
docs/MCP45HVX1_HIL_TOOLING_REPORT.md
```

Include:
- tool features,
- safe/default group,
- opt-in groups,
- evidence artifacts,
- exact software checks run,
- whether hardware HIL was actually run.

## Validation

Run:

```bash
python -m py_compile tools/run_hil_mcp45hvx1.py
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/check_generated_artifacts.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

If hardware is connected and safe, run safe default HIL only. Do not run output-changing HIL unless the user confirms a safe load.

## Commit and sync

```bash
git diff --check
git status --short
git add tools README.md docs .gitignore examples 2>/dev/null || true
git commit -m "tools: add MCP45HVX1 HIL evidence runner"
git push
```

Stop and report.