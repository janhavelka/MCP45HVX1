# MCP45HVX1 v1.1.0 Release Checklist

This checklist prepares the v1.1.0 software package and tag. It does not grant
production-readiness, analog-accuracy, high-voltage-safety, field-deployment,
or General Call safety approval. Those claims remain gated by the physical
evidence in [`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md).

## Release Candidate

| Field | Intended value |
|---|---|
| Version | `1.1.0` |
| Annotated tag | `v1.1.0` |
| Release title | `MCP45HVX1 v1.1.0` |
| Branch | `main` |
| Release type | Pre-production software package |
| Changelog | [`CHANGELOG.md`](../CHANGELOG.md), section `1.1.0` |
| Safe-only HIL | COM8 8-hour and 1-hour summaries; targeted COM9 report |
| Explicit non-claims | Output-changing, analog, high-voltage, SHDN/WLAT, rail-cycle, fault-injection, address-matrix, and General Call safety |

Version metadata must agree in `library.json`, `idf_component.yml`,
`include/MCP45HVX1/Version.h`, and `Doxyfile`. The tag must point to the exact
clean commit whose `main` CI passed.

## 1. Synchronize And Inspect

Run from the repository root:

```bash
git switch main
git fetch --prune origin
git pull --ff-only
git status --short --branch
git log -1 --oneline --decorate
git tag --list v1.1.0
```

Expected before tagging:

- branch is `main`
- local and `origin/main` are synchronized
- the worktree is clean
- `v1.1.0` does not already exist

Stop on a dirty, divergent, conflicted, or unexpected branch state. Never
overwrite work to force synchronization.

## 2. Validate Locally

Aggregate validation:

```bash
python tools/validate.py
```

On Windows, the validator uses the repository wrapper. For individual
PlatformIO commands use:

```powershell
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd run -e esp32s3dev
.\scripts\pio.cmd run -e esp32s2dev
.\scripts\pio.cmd pkg pack
```

On Linux/macOS use:

```bash
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
pio pkg pack
```

The aggregate validator covers:

- Python tool syntax and HIL parser tests
- generated version/header consistency
- core timing, generated-artifact, Arduino CLI, and ESP-IDF boundary guards
- 74 native tests
- Arduino ESP32-S2 and ESP32-S3 builds

Validate Doxygen separately; warnings are fatal and output stays under ignored
`.pio/doxygen/`:

```bash
doxygen Doxyfile
```

If `idf.py` is installed, native ESP-IDF builds may also be recorded:

```bash
idf.py -C examples/espidf_basic set-target esp32s2 build
idf.py -C examples/espidf_basic set-target esp32s3 build
```

Otherwise use the ESP-IDF matrix jobs from GitHub Actions as evidence. Do not
claim a local ESP-IDF build without a local log.

## 3. Inspect The Package

After `pio pkg pack`, inspect the generated archive and then remove it before
committing or tagging:

```powershell
Get-Item .\MCP45HVX1-1.1.0.tar.gz | Select-Object Name,Length,LastWriteTime
Get-FileHash .\MCP45HVX1-1.1.0.tar.gz -Algorithm SHA256
Remove-Item -LiteralPath .\MCP45HVX1-1.1.0.tar.gz
python tools/check_generated_artifacts.py
git status --short
```

Normal packages include headers, source, examples, metadata, and maintained
core Markdown. Tests, tools, CI metadata, PDFs, HIL reports, caches, and local
build output remain excluded by package policy.

## 4. Review Main-Branch CI

The workflow must pass for the release commit before tagging:

```bash
gh auth status
gh run list --workflow CI --branch main --limit 10
gh run watch <MAIN_RUN_ID> --exit-status
gh run view <MAIN_RUN_ID> --log-failed
```

`--log-failed` prints nothing when no job failed. The workflow covers Arduino
ESP32-S2/ESP32-S3 builds, native tests, package validation, documentation,
guards, and pure ESP-IDF 6.0.1 builds for both targets.

## 5. Create And Push The Tag

Only after the release commit is clean, synchronized, and green:

```bash
git switch main
git pull --ff-only
git status --short
python scripts/generate_version.py check
git tag -a v1.1.0 -m "MCP45HVX1 v1.1.0"
git show --no-patch --decorate v1.1.0
git push origin v1.1.0
```

The tag push triggers CI. Review that tag run before publishing the GitHub
Release:

```bash
gh run list --workflow CI --limit 10
gh run watch <TAG_RUN_ID> --exit-status
gh run view <TAG_RUN_ID> --log-failed
```

Do not move or recreate a published release tag. If the tag run fails, fix the
source on `main`, prepare a new version/tag, and preserve the failed evidence.

## 6. Publish The GitHub Release

Open:

```text
https://github.com/janhavelka/MCP45HVX1/releases/new
```

Then:

1. Choose existing tag `v1.1.0`.
2. Set title to `MCP45HVX1 v1.1.0`.
3. Use the `1.1.0` changelog section as the release notes; generated comparison
   notes may be added, but do not replace the safety limits.
4. Keep the explicit pre-production/non-claim paragraph.
5. Mark it as the latest release after the tag CI passes.
6. Publish and verify the release page points to the intended commit.

Optional CLI equivalent:

```bash
gh release create v1.1.0 --verify-tag --title "MCP45HVX1 v1.1.0" --generate-notes --latest
```

## Hardware And Documentation Gates

Before making any stronger hardware claim, separately record:

- package marking/date code and errata applicability
- A1:A0 address matrix and any alternate-range decision
- POR/BOR rail cycling and `TBORD` margin
- address NACK, data NACK, timeout, bus error, and unplug/replug behavior
- safe-load output-changing Wiper/TCON tests with verified restoration
- low-voltage analog measurements and terminal current
- SHDN/WLAT physical override behavior
- isolated-bus General Call evidence
- high-voltage measurements, if a high-voltage claim is intended

Safe-only HIL and software tests cannot satisfy these gates.

## Final Record

Record these values in the release notes or release issue:

| Field | Value |
|---|---|
| Release commit | clean `main` tip selected for `v1.1.0` |
| Main CI URL | record after the release commit passes |
| Tag CI URL | record after `v1.1.0` passes |
| Package name / size / SHA-256 | record from the final clean tree |
| HIL verdict | `PASS_SAFE_ONLY` |
| Hardware HIL performed for v1.1.0 | targeted COM9 safe-only run |
| Remaining risk acceptance | all physical/output-changing gates remain open |
| Reviewer / date | record at publication |
