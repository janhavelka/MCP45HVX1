# Prompt 03 — Status Fidelity, Health/Recover Semantics, Copy/Move, and Public API Contracts

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this prompt, commit, sync/push, and stop.

## Exploration findings fixed by this prompt

- M1. `resetI2cState()` can mark READY without device proof.
- M3. `begin()` / `probe()` collapse transport error detail.
- M4. Copy/move semantics are implicit.
- M7. Public API contracts are too thin.
- Part of M2 if uncertainty output still needs consistency.

## Goal

Make status and lifecycle contracts production-safe and explicit.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
```

Stop if dirty or pull fails.

Inspect:

```bash
grep -R "probe\\|recover\\|resetI2cState\\|updateHealth\\|DriverState\\|Status\\|DEVICE_NOT_FOUND\\|I2C_TIMEOUT\\|I2C_BUS\\|I2C_NACK\\|class MCP45HVX1" -n include src test README.md docs || true
```

## Spawn subagents

1. `status-fidelity-agent`
   - Audit all places where transport errors are remapped.

2. `health-state-agent`
   - Audit READY/DEGRADED/OFFLINE transitions, reset, recover, probe.

3. `lifecycle-agent`
   - Decide and implement copy/move policy.

4. `api-doc-agent`
   - Update public header and README contract.

5. `tests-agent`
   - Add focused tests.

## Required implementation

### 1. `resetI2cState()` must not prove device READY

If a reset callback succeeds but no device register is read:
- do not mark the driver READY solely from bus-reset success;
- keep DEGRADED if it was DEGRADED;
- if OFFLINE, require `recover()`/probe/read to return READY;
- or follow reset with a real device transaction before READY.

Add tests for:
- degraded + bus reset OK + next device read fails => not READY;
- degraded + bus reset OK + device probe/read succeeds => READY.

### 2. Preserve transport error detail

Audit `begin()`, `probe()`, `recover()`, raw read/write helpers.

Rules:
- Definite address NACK can map to `DEVICE_NOT_FOUND` if that is public contract.
- Timeouts, bus errors, data NACKs, invalid args, and generic I2C errors should be preserved where possible.
- If `DEVICE_NOT_FOUND` is returned, retain original detail/cause if the `Status` type supports it.
- Documentation must say what `probe()` does and does not prove.

Add tests:
- begin/probe timeout returns timeout or exposes timeout cause;
- begin/probe bus error preserved;
- address NACK maps as documented;
- data NACK not collapsed incorrectly.

### 3. Probe health side effects

Decide and document:
- `probe()` is diagnostic/no-health-side-effect, or
- `probe()` is tracked and updates health.

The exploration report suggests the architecture likely expects diagnostic probe to avoid health mutation. Verify and enforce the intended behavior.

Add tests:
- probe success/failure health counters/state behavior.

### 4. Recover contract

Document and test:
- what `recover()` does,
- whether it resets bus,
- whether it reads Wiper/TCON,
- whether it clears uncertainty,
- when it returns READY,
- what happens if recovery partially succeeds.

### 5. Copy/move

For a stateful driver with callback contexts and caches, prefer:

```cpp
MCP45HVX1(const MCP45HVX1&) = delete;
MCP45HVX1& operator=(const MCP45HVX1&) = delete;
MCP45HVX1(MCP45HVX1&&) = delete;
MCP45HVX1& operator=(MCP45HVX1&&) = delete;
```

If you intentionally keep copy/move, document why and test it. Production-safe default is to delete.

Add static_assert tests if feasible:
- not copy constructible,
- not copy assignable,
- not move constructible,
- not move assignable.

### 6. Public API docs

Public header and README must state:
- callback lifetime requirements,
- external serialization required,
- public APIs not ISR-safe,
- failed-write uncertainty,
- `begin()` side effects,
- `probe()` side effects,
- `recover()` semantics,
- copy/move policy.

## Report

Create:

```text
docs/MCP45HVX1_STATUS_HEALTH_LIFECYCLE_REPORT.md
```

Include:
- status mapping changes,
- health/recover contract,
- copy/move decision,
- API docs updated,
- tests run.

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
git commit -m "fix: clarify MCP45HVX1 status and lifecycle contracts"
git push
```

Stop and report.