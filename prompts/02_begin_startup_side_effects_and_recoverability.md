# Prompt 02 — Fix H2: Begin/Startup Side Effects and Recoverability

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this prompt, commit, sync/push, and stop.

## Exploration findings fixed by this prompt

Primary:
- H2. Optional initial writes during `begin()` can affect analog hardware and then return an uninitialized driver with no read/recover path.

Supporting:
- M7. Public API contracts too thin.

## Goal

Make startup behavior safe and explicit. If `begin()` performs optional output-changing writes, a failure must not hide possible analog side effects.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
```

Stop if dirty or pull fails.

Inspect:

```bash
grep -R "begin\\|initialWiper\\|initialTcon\\|resetAfterFailedBegin\\|resetAfter\\|recover" -n include src test README.md docs || true
```

## Spawn subagents

1. `begin-flow-agent`
   - Map config validation, probe/readback, optional initial writes, failed-begin cleanup.

2. `startup-safety-agent`
   - Identify output-changing behavior and safest production contract.

3. `api-compat-agent`
   - Minimize breakage while fixing the unsafe contract.

4. `fault-test-agent`
   - Add tests for partial startup writes.

5. `docs-agent`
   - Update startup and public API docs.

## Required design decision

Implement one of these explicit contracts. Prefer A or C unless the existing API strongly requires B.

### Option A — Read-only begin by default

- `begin()` is read-only by default.
- Initial Wiper/TCON writes require explicit config fields named so their output-changing nature is obvious.
- If disabled, no output-changing frames are sent.

### Option B — Recoverable failed begin

- If optional initial writes are attempted and fail, the driver preserves enough config/transport state to allow `recover()`, `readWiper()`, `readTcon()`, or an explicit diagnostic readback method.
- Affected state is marked uncertain.

### Option C — Two-phase begin

- First validate config and perform safe baseline reads/probe.
- Only after baseline success, optional output-changing writes may run.
- If optional writes fail, uncertainty is set and recoverability is preserved.

Do not retain this unsafe pattern:
- write TCON/Wiper,
- receive transport failure,
- wipe runtime state,
- return uninitialized with no way to read back affected state.

## Implementation requirements

- Optional initial writes must be documented as output-changing.
- Any failed optional startup write after attempted I2C access must set uncertainty using Prompt 01 model.
- `begin()` must preserve low-level error detail where possible.
- `begin()` must not call an internal cleanup that discards the only way to inspect possible hardware side effects.
- If there is a compatibility reason to keep old behavior, add a safer production config path and mark old behavior as deprecated/diagnostic.

## Tests required

1. `begin()` with no optional writes:
   - sends only read/probe frames,
   - succeeds without output-changing writes.

2. `begin()` with optional initial Wiper enabled:
   - sends Wiper write only when explicitly configured.

3. Initial TCON success then Wiper failure after simulated mutation:
   - returned status is original error,
   - uncertainty true,
   - affected cache unknown,
   - read/recover path available.

4. Initial Wiper success then TCON failure if order can occur:
   - same expected behavior.

5. Failure before any mutation:
   - no false uncertainty if no I2C state change possible.

6. Recover after failed startup:
   - can re-read volatile state or reports why not.

7. CLI output after startup failure:
   - does not claim clean READY if hardware state is uncertain.

## Documentation

Update:
- README startup section,
- `Config` Doxygen,
- `begin()` Doxygen,
- API contract doc,
- hardware validation doc.

Create:
```text
docs/MCP45HVX1_BEGIN_STARTUP_RECOVERABILITY_REPORT.md
```

Include:
- selected contract option,
- public API changes,
- safety implications,
- tests added,
- remaining limitations.

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
git commit -m "fix: make MCP45HVX1 startup writes recoverable"
git push
```

Stop and report.