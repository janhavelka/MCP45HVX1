# Prompt 00 — Branch, Agents, Baseline, and Chunk Discipline

You are working in the MCP45HVX1 repository.

The file `docs/MCP45HVX1_PRODUCTION_READINESS_EXPLORATION_REPORT.md` is the authoritative audit baseline. This implementation will be fed to you in multiple chronological prompts. Complete only the current prompt, commit, sync/push, and stop.

## Scope of this prompt

Do not implement production fixes yet. Establish the hardening branch, update `AGENTS.md`, verify the audit still matches the code, and create a baseline plan/report.

## Required first steps

```bash
pwd
git rev-parse --show-toplevel
git branch --show-current
git status --short
git remote -v
git log --oneline --decorate -n 12
```

If the worktree is dirty, stop and report the dirty files. Do not overwrite user work.

Create or switch to the implementation branch:

```bash
git checkout -b hardening/mcp45hvx1-industry-readiness
```

If the branch exists, switch to it only if it is clearly the intended active branch. Do not delete or reset anything.

## Read and verify the exploration report

Read the full report:

```bash
python - <<'PY'
from pathlib import Path
p = Path("docs/MCP45HVX1_PRODUCTION_READINESS_EXPLORATION_REPORT.md")
print(p.read_text(encoding="utf-8"))
PY
```

Then inspect the files referenced by the report:

```bash
find include src examples test tools docs .github -maxdepth 5 -type f 2>/dev/null | sort
```

Use PowerShell equivalents if on Windows.

## Spawn subagents

Spawn these subagents and collect short factual reports:

1. `audit-consistency-agent`
   - Confirm H1–H5, M1–M8, L1–L5 still apply.
   - Note any findings already fixed since the audit.

2. `core-contracts-agent`
   - Confirm likely files for H1, H2, M1, M3, M4, M7.

3. `device-datasheet-agent`
   - Confirm datasheet-driven issues: HV31/HV51, Wiper/TCON, POR/BOR, SHDN/WLAT, errata.

4. `espidf-cli-agent`
   - Confirm H5 and authoritative ESP-IDF example path.

5. `tests-release-agent`
   - Confirm current tests, guards, CI, release docs, package metadata.

## Update/create AGENTS.md

Add MCP45HVX1-specific hardening rules:

```markdown
# MCP45HVX1 hardening rules

- Work in small chronological chunks. After each chunk, run available checks, commit, sync/push, and stop.
- Core code in `include/` and `src/` must remain framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, Serial, framework logging, framework delays, global bus ownership, pin ownership, or task ownership.
- MCP45HVX1 core must use injected/non-owning I2C transport. Application/examples own bus, locking, timeout policy, bus reset, pins, and hardware lifecycle.
- Preserve I2C error distinctions where possible: invalid config, invalid argument, not initialized, device not found, address NACK, data NACK, timeout, bus error, unsupported command/register, and uncertain hardware state.
- MCP45HVX1 is a high-voltage volatile digital potentiometer. Wiper/TCON/output-changing operations can affect real analog/high-voltage circuits.
- Default CLI/HIL/selftest must be safe/read-only or state-restoring. Output-changing tests must be explicit opt-in and must restore baseline or mark uncertainty.
- MCP45HV31 and MCP45HV51 resolution differences must be respected: HV31 is 7-bit/128 taps/POR 0x3F; HV51 is 8-bit/256 taps/POR 0x7F.
- Standard I2C address range is 0x3C..0x3F. Alternate 0x5C..0x5F, if supported, must be explicit opt-in and not claimed verified without evidence.
- SHDN and WLAT are hardware pins. Core must not silently assume control over them. Register readback can differ from physical output if WLAT/SHDN/external circuitry overrides behavior.
- Software cannot enforce analog rail/current safety. Documentation and HIL must require external measurement and safe-load validation.
- Do not claim hardware validation, ESP-IDF build success, analog accuracy, General Call safety, or industry-grade readiness without logged evidence.
```

## Create baseline report

Create:

```text
docs/MCP45HVX1_INDUSTRY_HARDENING_BASELINE.md
```

Include:
- branch,
- starting commit,
- audit report used,
- confirmed findings,
- changed/rejected findings if any,
- prompt sequence,
- baseline checks,
- explicit statement: no production code fixes in this prompt.

## Baseline checks

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
```

Record exact results. Remove generated package tarball if it should not be committed.

## Commit and sync

```bash
git diff --check
git status --short
git add AGENTS.md docs/MCP45HVX1_INDUSTRY_HARDENING_BASELINE.md
git commit -m "docs: baseline MCP45HVX1 industry hardening"
git push -u origin hardening/mcp45hvx1-industry-readiness
```

If no changes are needed, report that. If push fails, report the exact failure.

## Final response

Return:
- branch,
- commit hash,
- pushed yes/no,
- baseline report path,
- findings confirmed,
- checks run,
- ready for Prompt 01 yes/no.