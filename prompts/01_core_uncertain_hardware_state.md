# Prompt 01 — Fix H1: Core Uncertain Hardware State After Failed State-Changing Writes

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this prompt, commit, sync/push, and stop.

## Exploration findings fixed by this prompt

Primary:
- H1. Failed state-changing writes can leave stale known Wiper/TCON cache.
- M2. Uncertain-state observability is inconsistent across CLIs.

Supporting:
- M7. Public API contracts too thin.

## Goal

A failed or ambiguous Wiper/TCON/General Call state-changing write must not leave the driver claiming it knows the hardware state when the physical device may have changed.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
```

Stop if dirty or pull fails.

Inspect:

```bash
grep -R "cachedWiper\\|cachedTcon\\|writeWiper\\|writeTcon\\|increment\\|decrement\\|General\\|hardware.*uncertain\\|SettingsSnapshot" -n include src examples test docs README.md || true
```

## Spawn subagents

1. `state-changing-paths-agent`
   - Map every path that can alter Wiper/TCON/terminal state.
   - Include raw writes and General Call helpers.

2. `uncertainty-api-agent`
   - Propose the smallest public model for uncertainty.

3. `fault-transport-agent`
   - Extend fake transport to simulate “device mutated but transport returned failure.”

4. `cli-state-agent`
   - Ensure Arduino and ESP-IDF status/health/state can display uncertainty consistently.

5. `integration-review-agent`
   - Prevent unrelated refactors.

## Required implementation

### 1. Add core-level uncertainty model

Add explicit state in the core, not only in CLI globals.

Suggested names:
- `hardwareStateUncertain`
- `hardwareStateUncertainError`
- `wiperCacheKnown`
- `tconCacheKnown`

Add to `SettingsSnapshot` or equivalent:
- whether hardware state is uncertain,
- last status/error that caused uncertainty,
- whether Wiper cache is known,
- whether TCON cache is known.

Add accessors if consistent with the existing style:
- `hardwareStateUncertain()`
- `hardwareStateUncertainError()`

Do not add a public “clear uncertainty” unless it is tied to real readback/resync.

### 2. Invalidate cache on ambiguous failed writes

For every state-changing write path:
- if validation fails before I2C access, return `INVALID_PARAM` / `INVALID_CONFIG` without setting uncertainty;
- if a transport failure occurs after a transaction was attempted and the device might have accepted bytes, mark the affected state unknown and set hardware-state uncertainty;
- preserve original error status.

Affected paths must include:
- Wiper writes,
- Wiper percent/fraction helpers,
- increment/decrement,
- TCON writes,
- terminal connect/disconnect helpers,
- software shutdown/TCON mode helpers,
- reset/default helpers if they write Wiper/TCON,
- raw register writes,
- General Call write/step helpers.

### 3. Clear uncertainty only by evidence

Clear uncertainty only when:
- Wiper and/or TCON have been successfully read back, and
- every affected unknown state has been verified, or
- a successful recover/resync explicitly reads the needed volatile state.

Do not clear uncertainty merely because a bus reset callback returned OK.

### 4. CLI propagation

Update Arduino and ESP-IDF CLIs, if present:
- `state` prints `uncertain=yes/no`;
- `drv`/`health` prints uncertainty and last uncertainty error;
- `cfg`/`settings` prints cache-known flags;
- restore failures in output-changing commands set uncertainty;
- stress summaries do not hide uncertainty.

## Tests required

Add tests for:

1. `writeWiper()` failure before mutation:
   - no false uncertainty if no hardware mutation is possible.

2. `writeWiper()` failure after simulated hardware mutation:
   - cached Wiper unknown,
   - uncertainty true,
   - original status preserved.

3. `writeTcon()` failure after simulated hardware mutation:
   - cached TCON unknown,
   - uncertainty true.

4. `increment()` / `decrement()` partial failure:
   - cached Wiper unknown,
   - uncertainty true.

5. General Call ambiguous failure:
   - affected caches unknown or all volatile caches unknown,
   - uncertainty true.

6. Successful readback:
   - restores known cache,
   - clears uncertainty only when all affected state is verified.

7. Snapshot/accessors:
   - expose uncertainty and last error.

8. CLI/contract guard:
   - state/health/cfg output includes uncertainty fields if guard is feasible.

## Documentation

Update:
- public header Doxygen,
- README,
- API contract doc if present,
- create `docs/MCP45HVX1_CORE_UNCERTAIN_STATE_REPORT.md`.

Document:
- what uncertainty means,
- which failures cause it,
- how it is cleared,
- why it matters for high-voltage analog output,
- how CLI/HIL must display it.

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
git add include src examples test tests tools README.md docs 2>/dev/null || true
git commit -m "fix: track uncertain MCP45HVX1 hardware state"
git push
```

## Final response

Return:
- commit hash,
- changed files,
- exact uncertainty model,
- tests added,
- checks run,
- remaining uncertainty-related risks,
- ready for Prompt 02 yes/no.