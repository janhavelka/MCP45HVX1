# Prompt 01 — MCP45HVX1 Core Contracts, Health Model, and Address Policy

You are continuing the MCP45HVX1 industry-readiness branch. Complete only this chunk, commit and sync, then stop.

## Goal

Fix high-confidence core contract issues: health tracking consistency, address validation/policy, fallible API behavior, copy/move prevention, thread/ISR documentation, and explicit recovery semantics.

## Starting procedure

```bash
git branch --show-current
git status --short
git pull --ff-only
```

If dirty or pull fails, stop and report.

Read:

```bash
sed -n '1,260p' AGENTS.md
sed -n '1,260p' docs/MCP45HVX1_INDUSTRY_HARDENING_PLAN.md
find include src test tools docs -maxdepth 4 -type f | sort
```

Spawn subagents:
- `mcp-core-contracts-agent`
- `mcp-tests-agent`
- `mcp-datasheet-agent`
- `mcp-integration-review-agent`

## Required investigation

Verify these audit findings directly:

1. Does any public method still call `_updateHealth()` directly outside the intended tracked I2C/recovery wrapper path?
2. Does `resetI2cState()` update health consistently?
3. Does `probe()` affect or not affect health? Document and test the intended behavior.
4. Are address rules correct?
   - Standard MCP45HVX1 range: `0x3C..0x3F`.
   - `0x5C..0x5F`, if present, must be opt-in and clearly labeled unverified/legacy/alternate unless actual hardware proves it.
5. Does address selection reject invalid ranges and preserve useful errors?
6. Are copy/move constructors and assignments deleted?
7. Are thread-safety and ISR-safety stated in public Doxygen/README?
8. Are bus reset/recover semantics explicit?

## Implementation requirements

### 1. Health update discipline

If direct `_updateHealth()` calls still exist outside the intended tracked/recovery path:

- Refactor them into a dedicated private helper such as `_busResetTracked()` or `_recoverTracked()`.
- Preserve the old public behavior unless it was clearly wrong.
- Do not make diagnostic `probe()` mutate health unless the existing public contract says it should.
- Add tests proving:
  - successful tracked operations update success counters;
  - failed tracked operations update failure counters;
  - diagnostic probe does not mutate health if that is the contract;
  - reset/recover updates health consistently and never bypasses the health model accidentally.

### 2. Address policy

Implement/verify a strict address policy:

- Default valid address range: `0x3C`, `0x3D`, `0x3E`, `0x3F`.
- Optional alternate range `0x5C..0x5F` only if the code already supports it or compatibility requires it.
- Add a config field or clearly named option if needed, for example `allowAlternateAddressRange`.
- If alternate range exists:
  - default must be false unless current public behavior requires otherwise;
  - README must say standard MCP45HVX1 datasheet range is `0x3C..0x3F`;
  - alternate range requires explicit opt-in and hardware verification.
- Add tests for all valid and invalid addresses.

### 3. Copy/move and concurrency

- Delete copy and move operations for the main driver class unless already done.
- Add Doxygen and README notes:
  - not thread-safe internally,
  - external serialization required on shared bus,
  - public APIs are not ISR-safe,
  - transport callbacks must not call back into the same instance.

### 4. Status/error precision

- Preserve I2C NACK/timeout/bus errors where available.
- Do not collapse all probe failures into generic `DEVICE_NOT_FOUND`.
- Return `INVALID_ARGUMENT` or equivalent for invalid addresses, invalid wiper code, invalid TCON mask, invalid register address, invalid resolution, etc.
- Add tests for these mappings.

## Validation

Run all available checks:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

Record exact results.

## Report update

Append/update `docs/MCP45HVX1_INDUSTRY_HARDENING_PLAN.md` or create:

```text
docs/MCP45HVX1_CORE_CONTRACTS_REPORT.md
```

Include:
- what was fixed,
- what was already correct,
- public API compatibility impact,
- tests added,
- commands run,
- remaining gaps for later chunks.

## Commit and sync

```bash
git diff --check
git status --short
git add include src test README.md docs AGENTS.md tools 2>/dev/null || true
git commit -m "fix: tighten MCP45HVX1 core contracts"
git push
```

If there are no code changes because everything was already correct, commit only the report if useful. Stop after reporting.
