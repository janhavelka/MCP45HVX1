# Prompt 00 — MCP45HVX1 Baseline, Branch, AGENTS.md, and Work Plan

You are working in the MCP45HVX1 repository.

This will be a **chunked production/industry-readiness hardening workflow**. The user will send prompts one by one. Do **not** race ahead into later chunks. Complete only this chunk, commit it, sync/push it, and stop with a factual report.

## Goal of this chunk

Establish a clean baseline, start the hardening branch, verify the existing audit report against the actual code, and update `AGENTS.md` so all later chunks follow the same production rules.

## Required first steps

1. Check repository identity and status:

```bash
pwd
git rev-parse --show-toplevel
git branch --show-current
git status --short
git remote -v
git log --oneline -n 8
```

2. If the worktree is dirty, stop and report the dirty files. Do not overwrite user work.

3. Create or switch to the production-readiness branch:

```bash
git checkout -b hardening/mcp45hvx1-industry-readiness
```

If the branch already exists, switch to it only if it is clearly the intended active branch. Never delete an existing branch.

4. Locate and read the existing audit/report docs. At minimum inspect:

```bash
find . -maxdepth 4 -type f | sort
ls -la docs || true
sed -n '1,260p' README.md || true
sed -n '1,260p' AGENTS.md || true
sed -n '1,260p' docs/*MCP* 2>/dev/null || true
sed -n '1,260p' docs/*INDUSTRY* 2>/dev/null || true
sed -n '1,260p' docs/*IDF* 2>/dev/null || true
```

5. Inspect the core and example structure:

```bash
find include src examples test tools .github -maxdepth 4 -type f 2>/dev/null | sort
```

6. Run baseline checks if available, without editing first:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
idf.py --version
```

Do not invent results. If a command is unavailable, record the exact error.

## Subagents to spawn

Spawn focused subagents and require factual file/line evidence:

1. `mcp-core-contracts-agent`
   - framework neutrality,
   - injected I2C transport,
   - health tracking consistency,
   - copy/move semantics,
   - thread/ISR contract,
   - allocation/lifetime behavior.

2. `mcp-datasheet-agent`
   - MCP45HV31 vs MCP45HV51,
   - 7-bit vs 8-bit wiper range,
   - I2C address policy,
   - volatile Wiper/TCON map,
   - POR/BOR defaults,
   - SHDN/TCON/WLAT semantics,
   - high-voltage safety limits.

3. `mcp-idf-example-agent`
   - native ESP-IDF example,
   - command parity,
   - fixed-buffer CLI,
   - persistent I2C handle,
   - no Arduino dependency,
   - timeout/error mapping.

4. `mcp-tests-agent`
   - fake I2C tests,
   - fault injection,
   - invalid/reserved command tests,
   - address validation,
   - public API edge cases.

5. `mcp-docs-release-agent`
   - README,
   - docs,
   - hardware validation checklist,
   - CI/build coverage,
   - release metadata/tags,
   - overclaim detection.

## Required `AGENTS.md` update

Update or create `AGENTS.md` with MCP45HVX1-specific hardening rules. Include at least:

```markdown
# MCP45HVX1 hardening rules

- The core in `include/` and `src/` must remain framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, logging framework, heap-heavy framework types, pin ownership, or task ownership.
- The MCP45HVX1 core must not own I2C. The application or adapter owns the bus, locking, timeout policy, bus reset, and physical pins.
- All fallible public production APIs should return `Status` or an equivalent diagnostic result. Avoid new silent `bool` failures.
- Do not hide or collapse I2C errors. Preserve address NACK, data NACK, timeout, bus error, invalid command, invalid argument, and not-initialized states where possible.
- Instances are not internally thread-safe unless explicitly changed and tested. Public APIs are not ISR-safe.
- Copy/move should be deleted unless a deliberate safe semantic is documented and tested.
- MCP45HVX1 is volatile. Do not document or imply nonvolatile wiper persistence.
- MCP45HV31 is 7-bit and MCP45HV51 is 8-bit. All wiper, fraction, resistance, and CLI commands must respect the configured resolution.
- The standard MCP45HVX1 I2C address range is `0x3C..0x3F` from fixed bits `01111` plus A1:A0. Any alternate range must be opt-in, clearly documented, and not treated as verified without hardware evidence.
- Changing the wiper/TCON/SHDN state can affect external analog/high-voltage circuits. Selftest and HIL must separate safe read-only tests from opt-in output-changing tests.
- Do not claim hardware validation, analog accuracy, ESP-IDF build success, or high-voltage behavior unless it was actually tested and logged.
```

Also add the subagent plan and the chunking rule: after each prompt, commit and sync, then stop.

## Baseline audit file

Create or update:

```text
docs/MCP45HVX1_INDUSTRY_HARDENING_PLAN.md
```

Include:
- current branch,
- starting commit,
- existing audit findings confirmed/rejected,
- items already fixed by prior work, if any,
- remaining missing pieces,
- planned prompt chunks,
- baseline commands run and results.

Important: if earlier reports say some items were already fixed, verify directly in code before trusting them.

## Commit and sync

After this chunk only:

```bash
git diff --check
git status --short
git add AGENTS.md docs/MCP45HVX1_INDUSTRY_HARDENING_PLAN.md
git commit -m "docs: plan MCP45HVX1 industry hardening"
git push -u origin hardening/mcp45hvx1-industry-readiness
```

If push is not possible, report why. Do not proceed to Prompt 01.
