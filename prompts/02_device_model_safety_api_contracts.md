# Prompt 02 — MCP45HVX1 Device Model, Safety, and Public API Semantics

You are continuing the MCP45HVX1 industry-readiness branch. Complete only this chunk, commit and sync, then stop.

## Goal

Make the library's device model explicitly correct for MCP45HV31/MCP45HV51 and safe for high-voltage analog usage. This chunk focuses on datasheet fidelity, public API semantics, safety docs, and tests.

## Starting procedure

```bash
git branch --show-current
git status --short
git pull --ff-only
```

If dirty or pull fails, stop and report.

Spawn subagents:
- `mcp-datasheet-agent`
- `mcp-api-contract-agent`
- `mcp-tests-agent`
- `mcp-docs-safety-agent`
- `mcp-integration-review-agent`

## Datasheet facts to verify in code and docs

Verify directly against local datasheet and current source. Do not invent behavior.

Key MCP45HVX1 facts:

1. Device family:
   - `MCP45HV31`: 7-bit, 128 taps, wiper range `0x00..0x7F`, POR midpoint `0x3F`.
   - `MCP45HV51`: 8-bit, 256 taps, wiper range `0x00..0xFF`, POR midpoint `0x7F`.

2. Memory map is volatile only:
   - address `0x00`: volatile wiper 0;
   - address `0x04`: volatile TCON register;
   - reserved addresses must not be written by production APIs.

3. TCON POR/BOR default is all terminals connected (`0xFF`) with reserved high bits read as forced `1`.

4. Hardware shutdown:
   - SHDN pin active low forces hardware shutdown;
   - it does not corrupt the wiper register;
   - serial interface remains available while SHDN is active;
   - TCON control resumes after SHDN is inactive.

5. Wiper latch:
   - WLAT high inhibits transfer from wiper register to physical wiper output;
   - WLAT low allows transfer;
   - if WLAT pin is not controlled by the driver, software must document that readback of the wiper register does not prove the analog terminal moved.

6. High-voltage safety:
   - analog terminal voltages must stay between `V-` and `V+`;
   - maximum `V+ - V-` is 36 V;
   - digital VL/DGND relationship must respect datasheet limits;
   - analog performance is specified only in defined operating ranges;
   - the driver cannot enforce board-level analog voltage/current safety.

7. Wiper/resistance math:
   - expose “nominal estimate” clearly, not exact real resistance;
   - include RAB tolerance, wiper resistance, full-scale/zero-scale resistance, voltage, current, and temperature caveats;
   - do not overclaim analog accuracy from digital code alone.

## Implementation requirements

### 1. Resolution-aware wiper contracts

Audit and fix all public APIs and CLI commands that accept wiper code, percentage, fraction, position, resistance, or raw register values.

Requirements:
- 7-bit devices reject or clamp codes above `0x7F` according to documented behavior.
- 8-bit devices accept `0x00..0xFF`.
- Fraction/percent conversion uses correct max code.
- Midpoint defaults are correct for each variant.
- `readWiper()` returns a resolution-aware value.
- `writeWiper()` validates by configured variant.
- Add tests for boundary values:
  - 7-bit: `0x00`, `0x3F`, `0x7F`, invalid `0x80`;
  - 8-bit: `0x00`, `0x7F`, `0x80`, `0xFF`, invalid `0x100` if API type can express it.

### 2. TCON and terminal control

Audit and fix:
- reserved TCON bits;
- terminal A/W/B connect/disconnect semantics;
- hardware shutdown bit (`R0HW`) semantics;
- readback masking;
- all-terminals-connected default.

Add tests for:
- writing each terminal bit;
- preserving reserved bits;
- invalid TCON values if API exposes raw writes;
- POR/default snapshot.

### 3. SHDN/WLAT documentation and optional API

The core must not own GPIO pins. However:
- README/Doxygen must explain SHDN and WLAT external-pin effects.
- Any API that says “shutdown” must specify whether it is TCON/software shutdown or hardware SHDN pin behavior.
- If existing naming is ambiguous, add clear names or compatibility wrappers.
- Do not add GPIO ownership to the core.

### 4. Volatile-only behavior

Remove/avoid any wording implying nonvolatile persistence. Add docs:
- wiper and TCON are volatile;
- POR/BOR returns default mid-scale wiper and `TCON=0xFF`;
- the application must reapply desired state after reset/brownout.

Add `recover()` or `begin()` documentation that states whether cached desired wiper/TCON is reapplied or not.

### 5. High-voltage safety warning

Add prominent README and Doxygen safety section:
- This software cannot protect hardware from overvoltage/overcurrent.
- Validate VL/DGND/V+/V- sequencing and terminal currents externally.
- Do not run output-changing selftests on a connected production circuit unless it is safe.
- Board-level validation must include analog rails, terminal currents, and load topology.

## Tests

Add native/fake tests for:
- 7-bit/8-bit range handling;
- fraction/percent conversions;
- TCON mask/readback;
- volatile defaults;
- SHDN/TCON semantic helpers;
- invalid register/address/command handling;
- no accidental writes during safe read-only operations.

## Validation commands

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

## Report

Create/update:

```text
docs/MCP45HVX1_DEVICE_MODEL_AND_SAFETY_REPORT.md
```

Include:
- verified datasheet facts,
- implemented device-model fixes,
- safety documentation changes,
- tests added,
- known limits that software cannot enforce,
- remaining HIL items.

## Commit and sync

```bash
git diff --check
git status --short
git add include src test README.md docs tools 2>/dev/null || true
git commit -m "fix: document and test MCP45HVX1 device semantics"
git push
```

Stop after reporting.
