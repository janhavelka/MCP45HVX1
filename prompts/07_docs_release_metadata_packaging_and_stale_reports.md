# Prompt 07 — Docs, Release Metadata, Packaging, and Stale Report Cleanup

You are continuing `hardening/mcp45hvx1-industry-readiness`.

Complete only this prompt, commit, sync/push, and stop.

## Exploration findings fixed by this prompt

- M5. Hardware validation checklist too narrow.
- M8. Version/changelog/tag policy not release-clean.
- L2. Stale reports conflict with current state.
- L3. Package size/export scope uncontrolled.
- L4. SECURITY/CONTRIBUTING stale generic text.
- Remaining overclaim issues from Executive Summary.

## Goal

Make docs and metadata honest. Remove or qualify “production-grade” claims until evidence exists. Fix stale docs and define release gates.

## Start

```bash
git branch --show-current
git status --short
git pull --ff-only
```

Stop if dirty or pull fails.

Inspect:

```bash
grep -R "production-grade\\|Production-grade\\|industry-grade\\|field-ready\\|validated\\|pending\\|NVS\\|clang-format\\|ESP-IDF\\|color toggle\\|no ESP-IDF" -n README.md CHANGELOG.md SECURITY.md CONTRIBUTING.md docs library.json idf_component.yml platformio.ini .github 2>/dev/null || true
git tag --list
python - <<'PY'
from pathlib import Path
total=0
for p in Path('docs').rglob('*'):
    if p.is_file():
        total += p.stat().st_size
print("docs bytes:", total)
PY
```

## Spawn subagents

1. `overclaim-agent`
   - Find and fix unsupported production/industry/validation claims.

2. `stale-report-agent`
   - Supersede/update stale docs listed in the audit.

3. `release-metadata-agent`
   - Fix version/changelog/tag/release wording.

4. `packaging-agent`
   - Define export policy for large PDFs/docs.

5. `hardware-validation-doc-agent`
   - Expand validation checklist.

## Required changes

### 1. Wording honesty

Replace unqualified “production-grade” claims with:
- “production-oriented”,
- “industry-readiness hardening in progress”,
- “pre-production candidate pending hardware validation”,
as appropriate.

Do not claim:
- hardware smoke passed without logs,
- pure IDF build passed without `idf.py` or CI evidence,
- high-voltage validation without measurements,
- industry-grade release before HIL/fault evidence.

### 2. Stale reports

The exploration report specifically identified stale/conflicting docs:
- CLI parity report says no ESP-IDF example existed.
- CLI style report says no runtime color toggle.
- SECURITY.md mentions NVS side effects.
- CONTRIBUTING.md references `.clang-format` when missing.

Fix them:
- update directly if still maintained,
- or add a clear “Superseded by …” note at top,
- or create a doc index explaining current authoritative docs.

### 3. Hardware validation docs

Create/update:

```text
docs/MCP45HVX1_HARDWARE_VALIDATION.md
```

Must include:
- safe read-only checks,
- output-changing checks,
- analog measurement checks,
- SHDN/WLAT checks if wired,
- POR/BOR rail cycling,
- address strap tests,
- I2C NACK/timeout/unplug,
- General Call isolated-bus requirement,
- errata review,
- low-voltage validation before high-voltage validation,
- evidence template.

### 4. Release checklist

Create/update:

```text
docs/MCP45HVX1_RELEASE_CHECKLIST.md
```

Must include gates:
- clean tree,
- version check,
- native tests,
- guards,
- Arduino ESP32-S2/S3 builds,
- pure ESP-IDF build evidence,
- package check,
- HIL safe evidence,
- output-changing HIL evidence,
- analog low-voltage evidence,
- high-voltage evidence if claimed,
- errata/isolated-bus decision,
- changelog finalized,
- tag created,
- no generated artifacts,
- package size/export decision.

### 5. API contract doc

Create/update:

```text
docs/MCP45HVX1_API_CONTRACT.md
```

Include:
- transport ownership,
- callback lifetime,
- thread/ISR,
- address/variant/RAB policy,
- Wiper/TCON volatile state,
- uncertainty model,
- begin/startup writes,
- recover behavior,
- SHDN/WLAT,
- raw register/General Call warnings,
- high-voltage safety.

### 6. Packaging/export

Decide and document:
- Are large PDFs included in packages intentionally?
- Should `library.json` include export exclude/include?
- Are extracted docs needed in package?
- Should package include only headers/src/examples/core docs?

Implement package metadata changes if clearly safe.

### 7. Changelog/tag/version

Update changelog/release docs:
- If `1.0.0` remains, call it pre-production/engineering unless evidence exists.
- If no tag exists, release checklist says tag pending.
- Do not create tag unless user explicitly asked.

## Report

Create:

```text
docs/MCP45HVX1_DOCS_RELEASE_METADATA_REPORT.md
```

Include all docs/metadata changed and remaining release blockers.

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
git add README.md CHANGELOG.md SECURITY.md CONTRIBUTING.md library.json idf_component.yml platformio.ini CMakeLists.txt .github docs tools 2>/dev/null || true
git commit -m "docs: align MCP45HVX1 release readiness claims"
git push
```

Stop and report.