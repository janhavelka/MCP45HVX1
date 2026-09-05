# MCP45HVX1 Release Checklist

Runbook for cutting a software release of this library. It does not grant
production-readiness, analog-accuracy, high-voltage-safety, field-deployment,
or General Call safety approval; those remain gated by the physical evidence in
[`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md).

Throughout, `X.Y.Z` is the version being released and `vX.Y.Z` its annotated
tag. Release type is a pre-production software package unless the hardware
gates at the end of this document have been satisfied and recorded.

## 1. Set The Version

`scripts/generate_version.py` is the single source of truth. It propagates the
version from `library.json` into `include/MCP45HVX1/Version.h`,
`idf_component.yml`, and `Doxyfile`.

```bash
python scripts/generate_version.py set X.Y.Z    # or: bump patch|minor|major
python scripts/generate_version.py check
```

`check` must report all three generated files up to date. The Markdown docs are
hand-maintained and are not covered by that check — grep for the previous
version before tagging.

## 2. Synchronize And Inspect

```bash
git switch main
git fetch --prune origin
git pull --ff-only
git status --short --branch
git log -1 --oneline --decorate
git tag --list vX.Y.Z
```

Expected: on `main`, synchronized with `origin/main`, clean worktree, and
`vX.Y.Z` does not already exist. Stop on a dirty, divergent, or conflicted
state; never force a synchronization over someone's work.

## 3. Validate Locally

```bash
python tools/validate.py
```

The aggregate validator runs, in order:

- Python syntax and the HIL evidence parser tests
- generated version/header consistency
- the core timing, generated-artifact, Arduino CLI, and ESP-IDF boundary guards
- three standalone `g++` compiles: the core alone, the Arduino CLI, and the
  Arduino CLI with color disabled (`MCP45HVX1_CLI_ENABLE_COLOR=0`)
- the native test suite
- Arduino ESP32-S2 and ESP32-S3 builds

Individual PlatformIO commands, if needed (`.\scripts\pio.cmd` on Windows):

```bash
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
pio pkg pack
```

Doxygen is validated separately; warnings are fatal and output stays under the
ignored `.pio/doxygen/`:

```bash
doxygen Doxyfile
```

If `idf.py` is installed, record native ESP-IDF builds too; otherwise cite the
ESP-IDF matrix jobs from CI. Do not claim a local ESP-IDF build without a local
log.

```bash
idf.py -C examples/espidf_basic set-target esp32s2 build
idf.py -C examples/espidf_basic set-target esp32s3 build
```

## 4. Inspect The Package

```powershell
Get-Item .\MCP45HVX1-X.Y.Z.tar.gz | Select-Object Name,Length,LastWriteTime
Get-FileHash .\MCP45HVX1-X.Y.Z.tar.gz -Algorithm SHA256
Remove-Item -LiteralPath .\MCP45HVX1-X.Y.Z.tar.gz
python tools/check_generated_artifacts.py
git status --short
```

Packages include headers, source, examples, metadata, and the maintained
Markdown docs. Tests, tools, scripts, CI metadata, PDFs, caches, and local
build output stay excluded by the policy in `library.json` and
`idf_component.yml`. Remove the archive before committing or tagging.

## 5. Review Main-Branch CI

```bash
gh run list --workflow CI --branch main --limit 10
gh run watch <MAIN_RUN_ID> --exit-status
gh run view <MAIN_RUN_ID> --log-failed
```

`--log-failed` prints nothing when no job failed. The workflow covers Arduino
ESP32-S2/S3 builds, native tests, package validation, documentation, the
contract guards, and pure ESP-IDF builds for both targets.

## 6. Update The Changelog

Move the `Unreleased` entries into a new `X.Y.Z` section with the release date,
grouped by change type. State explicitly what the release does *not* claim.

## 7. Tag And Publish

```bash
git tag -a vX.Y.Z -m "MCP45HVX1 vX.Y.Z"
git show --no-patch --decorate vX.Y.Z
git push origin vX.Y.Z
```

The tag push triggers CI. Review that run before publishing the release:

```bash
gh run watch <TAG_RUN_ID> --exit-status
```

Then create the GitHub release against the existing tag, titled
`MCP45HVX1 vX.Y.Z`, using the changelog section as the release notes and
keeping the explicit non-claim paragraph. Mark it latest only after the tag CI
passes.

```bash
gh release create vX.Y.Z --verify-tag --title "MCP45HVX1 vX.Y.Z" --notes-file <notes> --latest
```

Do not move or recreate a published tag. If the tag run fails, fix `main`,
prepare a new version, and preserve the failed evidence.

## Hardware And Documentation Gates

Before making any stronger hardware claim, separately record:

- package marking and date code, and errata applicability (DS80000649B)
- the A1:A0 address matrix for the populated board
- POR/BOR rail cycling and `TBORD` margin
- address NACK, data NACK, timeout, bus error, and unplug/replug behavior
- safe-load output-changing Wiper/TCON tests with verified restoration
- low-voltage analog measurements and terminal current
- SHDN/WLAT physical override behavior
- isolated-bus General Call evidence
- high-voltage measurements, if a high-voltage claim is intended

Safe-only HIL and software tests cannot satisfy these gates.

## Final Record

Record in the release notes or release issue:

| Field | Value |
|---|---|
| Release commit | clean `main` tip selected for `vX.Y.Z` |
| Main CI URL | record after the release commit passes |
| Tag CI URL | record after `vX.Y.Z` passes |
| Package name / size / SHA-256 | record from the final clean tree |
| HIL verdict and fixture | record the run, board, and scope |
| Remaining risk acceptance | list every gate still open |
| Reviewer / date | record at publication |
