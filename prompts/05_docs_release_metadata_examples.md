# Prompt 05 — MCP45HVX1 Documentation, Release Metadata, and Example Honesty

You are continuing the MCP45HVX1 industry-readiness branch. Complete only this chunk, commit and sync, then stop.

## Goal

Make documentation and release metadata industry-grade and honest. This chunk should not add new driver features except small docs/metadata fixes required for correctness.

## Starting procedure

```bash
git branch --show-current
git status --short
git pull --ff-only
```

If dirty or pull fails, stop and report.

Spawn subagents:
- `mcp-docs-release-agent`
- `mcp-datasheet-agent`
- `mcp-idf-example-agent`
- `mcp-tests-agent`
- `mcp-integration-review-agent`

## Required documentation audit

Inspect:

```bash
find . -maxdepth 4 -type f \( -name 'README*' -o -name 'CHANGELOG*' -o -name 'LICENSE*' -o -name 'library.json' -o -name 'platformio.ini' -o -name 'CMakeLists.txt' -o -name 'idf_component.yml' -o -path './docs/*' \) | sort
git tag --list
```

## Required README sections

Update README with concise, accurate sections for:

1. What the library is:
   - MCP45HVX1 high-voltage volatile digital potentiometer family.
   - MCP45HV31 7-bit, MCP45HV51 8-bit.
   - I2C client device.

2. Address policy:
   - standard range `0x3C..0x3F`;
   - A1/A0 hardware pins;
   - alternate range, if supported, is opt-in and not default;
   - how to scan/select address.

3. Volatile behavior:
   - Wiper and TCON only;
   - no nonvolatile persistence;
   - POR/BOR defaults;
   - reapply desired state after reset/brownout.

4. High-voltage safety:
   - software cannot enforce analog voltage/current limits;
   - terminal pins must stay within V-/V+;
   - VL/DGND/V+/V- constraints;
   - output-changing tests can affect connected circuits;
   - require bench validation before connecting to production load.

5. Wiper/resistance model:
   - nominal only;
   - RAB tolerance, wiper resistance, end resistance, voltage/current/temp effects;
   - no precision resistance claim without calibration.

6. TCON/SHDN/WLAT:
   - distinguish software terminal control from hardware shutdown pin;
   - WLAT readback caveat;
   - SHDN active-low behavior and serial interface still active.

7. Thread/ISR and shared bus:
   - not thread-safe;
   - external serialization required;
   - not ISR-safe;
   - transport callbacks must not reenter the driver.

8. API latency:
   - table of public APIs and I2C transactions;
   - worst-case as `N * i2cTimeoutMs`;
   - no clock stretching by the MCP45HVX1, but host timeout still matters.

9. Examples:
   - Arduino example scope;
   - ESP-IDF example scope;
   - diagnostic vs production classification;
   - safe vs destructive commands.

10. Hardware validation matrix:
   - safe read-only checks;
   - opt-in output-changing checks;
   - analog measurement checks;
   - address range checks;
   - fault/unplug/brownout checks.

## Required docs

Create or update:

```text
docs/MCP45HVX1_HARDWARE_VALIDATION.md
docs/MCP45HVX1_API_CONTRACT.md
docs/MCP45HVX1_RELEASE_CHECKLIST.md
```

### Hardware validation doc must include

- equipment needed:
  - MCP45HVX1 board/module;
  - controllable safe analog rail or low-voltage equivalent;
  - DMM/ADC for wiper measurement;
  - logic analyzer optional for I2C/WLAT/SHDN;
  - known load resistor/topology.
- safe limits:
  - do not exceed device or board analog rails;
  - prefer low-voltage validation before high-voltage validation.
- command sequence:
  - version/help/scan/probe/config/readback;
  - read-only selftest;
  - opt-in wiper ramp;
  - TCON terminal disconnect/reconnect;
  - SHDN if hardware pin accessible;
  - WLAT if hardware pin accessible;
  - brownout/reset/recover.
- pass/fail criteria.
- report template.

### API contract doc must include

- fallible return model;
- address policy;
- resolution policy;
- wiper code policy;
- TCON policy;
- reset/recover behavior;
- cache/dirty-state behavior if implemented;
- thread/ISR limitations;
- exact meaning of any diagnostic raw register access.

### Release checklist must include

- local commands,
- CI commands,
- hardware validation needed,
- version/tag policy,
- no generated artifacts,
- no unverified "industry-grade" claim.

## Release metadata

Audit and fix if needed:

- `library.json` frameworks/platforms;
- semantic version;
- `scripts/generate_version.py` behavior;
- generated `Version.h` policy;
- root `CMakeLists.txt`;
- `idf_component.yml`;
- `.github/workflows/ci.yml`;
- root `LICENSE`.

If there are no tags and this repo is not yet released, document that release tag is pending. Do not create a tag unless explicitly requested by the user.

## Validation

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
```

## Report

Create/update:

```text
docs/MCP45HVX1_DOCS_RELEASE_REPORT.md
```

Include:
- docs changed,
- metadata changed,
- release blockers,
- hardware validation still needed,
- exact checks run.

## Commit and sync

```bash
git diff --check
git status --short
git add README.md CHANGELOG.md LICENSE library.json platformio.ini CMakeLists.txt idf_component.yml .github docs include 2>/dev/null || true
git commit -m "docs: clarify MCP45HVX1 release contracts"
git push
```

Stop after reporting.
