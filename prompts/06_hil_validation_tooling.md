# Prompt 06 — MCP45HVX1 HIL Validation Tooling and Evidence Capture

You are continuing the MCP45HVX1 industry-readiness branch. Complete only this chunk, commit and sync, then stop.

## Goal

Add or improve hardware-in-the-loop validation tooling so the user can produce evidence. The tooling must be safe by default and must clearly separate read-only checks from output-changing tests.

## Starting procedure

```bash
git branch --show-current
git status --short
git pull --ff-only
```

If dirty or pull fails, stop and report.

Spawn subagents:
- `mcp-hil-agent`
- `mcp-cli-agent`
- `mcp-datasheet-agent`
- `mcp-docs-safety-agent`
- `mcp-integration-review-agent`

## Required safety rule

Default HIL must be **safe/read-only**.

Output-changing operations must be opt-in with explicit flags, for example:

```bash
--include-output-change
--include-destructive
--include-wiper-ramp
--include-tcon-toggle
--include-shdn
--include-wlat
--include-brownout-manual
```

Do not make tests change wiper/TCON by default. A digital potentiometer may be connected to a real analog/high-voltage circuit.

## Tooling requirements

Add or improve a Python HIL runner, for example:

```text
tools/run_hil_mcp45hvx1.py
```

It should support:

```bash
python tools/run_hil_mcp45hvx1.py --port COMx --baud 115200 --address 0x3C
python tools/run_hil_mcp45hvx1.py --port COMx --baud 115200 --address 0x3C --include-output-change
python tools/run_hil_mcp45hvx1.py --port COMx --baud 115200 --address 0x3C --include-wiper-ramp --operator-prompts
```

### Safe default checks

The default runner should capture:

- firmware/library version;
- reset reason if CLI provides it;
- help;
- scan;
- active address;
- probe;
- configuration/state/health;
- read wiper;
- read TCON;
- read raw registers if available;
- safe selftest;
- short read-only stress if possible.

### Opt-in output-changing checks

Only with flags:
- set wiper to known values and read back:
  - min,
  - midpoint,
  - max,
  - restored baseline;
- set by percent/fraction if commands exist;
- TCON disconnect/reconnect terminal A/W/B;
- software shutdown/restore;
- address switching if multiple addresses are detected;
- general call or bus reset only with explicit destructive/bus-wide flag and clear warning;
- brownout/unplug/replug procedure as operator-guided only.

### Operator measurement prompts

For analog validation, the runner should pause and ask operator to record:

- V+ and V- rail voltages;
- VL/DGND voltage;
- RAB nominal value;
- measured resistance/voltage at selected wiper codes;
- whether load is safe;
- whether SHDN/WLAT pins are available;
- notes.

The runner should not infer analog correctness from I2C readback alone.

### Output artifacts

Create a timestamped evidence folder such as:

```text
hil_logs/mcp45hvx1_<timestamp>/
```

Include:
- raw serial transcript;
- parsed summary JSON;
- markdown report;
- command list;
- operator notes;
- final verdict:
  - PASS,
  - FAIL,
  - OPERATOR_REVIEW_REQUIRED,
  - SKIPPED_UNSAFE.

Ensure generated logs are ignored by git unless the project intentionally tracks validation reports.

## CLI improvements if needed

If the existing CLI lacks commands needed for safe HIL, add minimal commands:
- `version`
- `scan`
- `addr`
- `probe`
- `health`
- `cfg`
- `wiper?` / `readwiper`
- `tcon?` / `readtcon`
- `selftest`
- `selftest safe`
- `selftest output` or equivalent opt-in.

Keep output parseable and human-readable.

## Documentation

Update:

```text
docs/MCP45HVX1_HARDWARE_VALIDATION.md
```

with:
- exact HIL commands;
- safety warnings;
- what evidence is generated;
- how to interpret PASS/FAIL/OPERATOR_REVIEW_REQUIRED;
- what remains manual.

## Validation

Run software checks:

```bash
python -m py_compile tools/run_hil_mcp45hvx1.py
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

If hardware is connected and explicitly safe, run the safe default HIL only. Do not run output-changing HIL unless the user confirms the load is safe.

## Report

Create/update:

```text
docs/MCP45HVX1_HIL_TOOLING_REPORT.md
```

Include:
- runner commands,
- safe/default groups,
- opt-in groups,
- generated artifact formats,
- exact checks run,
- hardware commands run if any,
- what still needs physical validation.

## Commit and sync

```bash
git diff --check
git status --short
git add tools examples README.md docs .gitignore 2>/dev/null || true
git commit -m "tools: add MCP45HVX1 HIL validation runner"
git push
```

Stop after reporting.
