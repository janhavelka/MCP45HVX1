# Prompt 04 — Datasheet Device Model, POR/BOR, SHDN/WLAT, Fraction Contract, and Errata Gate

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this prompt, commit, sync/push, and stop.

## Exploration findings fixed by this prompt

- H4. Errata workaround is not a release gate.
- M6. POR/BOR timing documentation is inconsistent.
- M7. Public API contracts too thin.
- L5. `writeWiperFraction()` documentation disagrees with behavior.
- Supports H3 by improving validation requirements.

## Goal

Bring datasheet-driven behavior, documentation, and release gates into alignment.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
```

Stop if dirty or pull fails.

Inspect:

```bash
grep -R "POR\\|BOR\\|TBORD\\|tPOR\\|startup\\|SHDN\\|WLAT\\|errata\\|GCEN\\|General Call\\|fraction\\|clamp\\|0x3F\\|0x7F" -n include src test README.md docs || true
```

## Spawn subagents

1. `datasheet-fidelity-agent`
   - Verify MCP45HV31/HV51, Wiper/TCON, POR/BOR, SHDN/WLAT.

2. `errata-release-agent`
   - Convert errata into a release/validation gate.

3. `api-doc-consistency-agent`
   - Align public docs and implementation.

4. `safety-doc-agent`
   - Ensure analog/high-voltage safety language is clear.

5. `test-agent`
   - Add small tests for doc/behavior mismatches if needed.

## Required fixes

### 1. POR/BOR timing docs

The audit found conflicting docs:
- one says no explicit startup delay is quantified;
- another records `TBORD` 10 us typical / 20 us max after reset exit;
- `begin()` reads immediately.

Fix docs:
- State verified datasheet timing facts exactly.
- State the app must ensure analog/digital rails and reset are stable before `begin()`.
- State the core does not own rail sequencing.
- Do not add arbitrary delays unless backed by contract and tests.
- If a `nowMs` or delay hook exists, document whether it is used during begin/recover.

### 2. SHDN/WLAT

Docs and public header must state:
- SHDN is a hardware pin, active-low, external to core unless an adapter controls it.
- WLAT can prevent volatile wiper register writes from reaching the physical wiper output until released.
- Wiper register readback does not prove physical analog movement when WLAT/SHDN is active or external hardware overrides output.
- Any API named shutdown must distinguish TCON/software shutdown from external SHDN pin.

### 3. Errata release gate

Update hardware validation and release checklist:
- require silicon/errata review before production release,
- require isolated bus or documented risk acceptance if errata applies,
- General Call output-changing commands require isolated bus confirmation,
- shared-bus deployments cannot claim General Call safety without evidence.

### 4. `writeWiperFraction()` contract

Audit says header says clamp, implementation/tests reject.

Choose one contract. For high-voltage output-changing control, prefer rejection.

If keeping rejection:
- update Doxygen/header docs,
- README examples,
- CLI help if needed,
- tests remain rejection-based.

If changing to clamp:
- justify why clamping is safer,
- update tests and CLI messaging.

### 5. Device model consistency

Verify and document:
- HV31 = 7-bit/128 taps/POR 0x3F;
- HV51 = 8-bit/256 taps/POR 0x7F;
- Wiper0 `0x00`;
- TCON0 `0x04`;
- TCON reserved bits forced high/read masked;
- standard address range default;
- alternate range status.

## Tests

Add/adjust tests for:
- fraction out-of-range behavior matching docs;
- POR defaults for both variants;
- TCON default/reserved bits;
- errata info helper if present;
- shutdown naming if API changed.

## Report

Create:

```text
docs/MCP45HVX1_DEVICE_MODEL_ERRATA_REPORT.md
```

Include:
- datasheet facts verified,
- docs fixed,
- errata gate added,
- behavior/doc mismatches fixed,
- tests run,
- remaining hardware validation.

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
git commit -m "docs: align MCP45HVX1 device model and errata gates"
git push
```

Stop and report.