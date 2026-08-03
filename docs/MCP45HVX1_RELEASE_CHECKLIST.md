# MCP45HVX1 Release Checklist

This checklist gates release claims for this repository. Passing software checks
alone is not enough to claim production readiness, high-voltage validation,
analog accuracy, or General Call safety.

Current state: v1.0.0 pre-production software package pending full hardware
validation. A release tag may be created for the software package after local
checks pass, but do not describe the release as production-ready, high-voltage
validated, analog-accurate, or General Call safe without the physical evidence
gates below.

## Repository State

| Gate | Evidence |
|---|---|
| Correct release branch | `git branch --show-current` recorded |
| Clean worktree before release | `git status --short` empty |
| Remote synchronized | `git pull --ff-only` completed |
| No generated artifacts | `python tools/check_generated_artifacts.py` passed after packaging |

## Version, Changelog, And Tag Policy

| Gate | Evidence |
|---|---|
| Version files agree | `library.json`, `idf_component.yml`, and generated `include/MCP45HVX1/Version.h` reviewed |
| Version check passed | `python scripts/generate_version.py check` |
| Changelog finalized | `CHANGELOG.md` has the intended release section and no stale overclaims |
| Tag status recorded | No tag exists yet, or intended tag such as `v1.0.0` is recorded |
| Tag created only after approval | Tag command/log recorded if a release is approved |

If version `1.0.0` is retained before full physical/output-changing evidence
exists, describe it as an engineering or pre-production package version. Do not
call it production-ready or industry-grade.

## Software Validation

On Windows, replace `pio` in individual commands with `.\scripts\pio.cmd`; the
aggregate `python tools/validate.py` command selects that wrapper automatically.
Other platforms use `pio` from `PATH`.

| Gate | Command/evidence |
|---|---|
| Python tool syntax | `python -m py_compile scripts/generate_version.py tools/run_hil_mcp45hvx1.py tools/test_run_hil_mcp45hvx1_parser.py tools/check_generated_artifacts.py tools/check_cli_contract.py tools/check_idf_example_contract.py tools/check_core_timing_guard.py` |
| Core timing guard | `python tools/check_core_timing_guard.py` |
| Arduino/CLI contract | `python tools/check_cli_contract.py` |
| ESP-IDF example contract | `python tools/check_idf_example_contract.py` |
| Generated artifact guard | `python tools/check_generated_artifacts.py` |
| HIL parser/evidence guard | `python tools/test_run_hil_mcp45hvx1_parser.py` |
| Version check | `python scripts/generate_version.py check` |
| Native tests | `pio test -e native` |
| Arduino ESP32-S3 build | `pio run -e esp32s3dev` |
| Arduino ESP32-S2 build | `pio run -e esp32s2dev` |
| Pure ESP-IDF esp32s3 build | `idf.py -C examples/espidf_basic set-target esp32s3 build`, or CI log link |
| Pure ESP-IDF esp32s2 build | `idf.py -C examples/espidf_basic set-target esp32s2 build`, or CI log link |
| Remote CI run reviewed | `gh run list --limit 10` or equivalent workflow/log URL |

Remote CI can be cited only when the workflow actually ran for the release
commit. Local checks and CI checks are separate evidence. The current workflow
triggers on `main`, `v*` tags, and pull requests to `main`; direct hardening
branch pushes are not remote CI evidence by themselves.

## v1.0.0 Release Evidence Snapshot

| Field | Evidence |
|---|---|
| Package version | `library.json` and `idf_component.yml`: `1.0.0` |
| Intended tag | `v1.0.0` |
| Release type | Pre-production software package; not production-readiness approval |
| Local validation | `python tools/validate.py` passed before this release-prep pass |
| Package validation | `pio pkg pack` passed during release prep; generated archive removed |
| Package artifact and size | `MCP45HVX1-1.0.0.tar.gz`, `95527` bytes when packed locally |
| ESP-IDF local build | Not run locally; `idf.py` was not found on PATH |
| Remote CI | [Tag `v1.0.0` GitHub Actions run](https://github.com/janhavelka/MCP45HVX1/actions/runs/28450131279) passed all jobs |
| Safe-only HIL report | [`docs/reports/hil-validation-COM8-20260629.md`](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-validation-COM8-20260629.md) |
| Safe-only HIL verdict | `PASS_SAFE_ONLY`, `183221 / 183221 / 0` soak commands, worst latency `0.188 s` |
| Panic repro report | [`docs/reports/hil-panic-repro-COM8-20260629.md`](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-panic-repro-COM8-20260629.md) |
| Panic repro verdict | `PASS_SAFE_ONLY`, `23056 / 23056 / 0` soak commands |
| Firmware under HIL | ESP32-S2 `esp32s2dev`, firmware reported `1.0.0 (4de67ab, clean)` |
| HIL runner caveat | The 8-hour runner recorded repo `dirty=True` because the serial-drain evidence patch was uncommitted during the run; that patch is committed in `b2ae78f` |
| Remaining non-claims | Output-changing behavior, analog movement, terminal current, high-voltage behavior, SHDN/WLAT physical behavior, rail cycling, fault injection, address strap matrix, and General Call safety |

## Packaging

| Gate | Evidence |
|---|---|
| Package pack | `pio pkg pack` passed |
| Generated tarball removed | package archive removed before commit |
| Generated archive ignore policy | `.gitignore` and guard cover `*.tar.gz`, `*.tgz`, and `*.zip` |
| Export policy reviewed | `library.json` export section reviewed |
| ESP-IDF file policy reviewed | `idf_component.yml` files/exclude section reviewed |
| Package size accepted | packed archive size recorded |
| Large reference docs decision | PDFs and extracted datasheet markdown are intentionally excluded from normal packages |

Normal packages should include headers, source, examples, metadata, and current
core docs. Large reference PDFs and extracted audit markdown stay in the
repository unless a release explicitly decides to ship them.

## Documentation Gates

| Gate | Evidence |
|---|---|
| API contract current | `docs/MCP45HVX1_API_CONTRACT.md` reviewed |
| Device reference current | `docs/DEVICE_REFERENCE.md` reviewed |
| Hardware validation doc current | `docs/MCP45HVX1_HARDWARE_VALIDATION.md` reviewed |
| HIL runner behavior current | `docs/MCP45HVX1_HARDWARE_VALIDATION.md` and `tools/run_hil_mcp45hvx1.py` reviewed |
| Release checklist current | this file reviewed |
| Maintained docs structure reviewed | `docs/README.md` links only current docs and reference PDFs |
| README honesty | no unsupported production/industry/hardware-validation claims |
| SECURITY/CONTRIBUTING current | no stale persistent-storage or missing-format-tool references |

## Silicon And Errata

| Gate | Evidence |
|---|---|
| Package marking/date code | recorded in hardware validation log |
| Datasheet and errata revisions | recorded in hardware validation log |
| `DS80000649B` review | completed against actual silicon |
| Affected/unknown silicon decision | isolated bus or shared-bus risk acceptance recorded |
| General Call release decision | isolated-bus evidence or explicit risk acceptance |
| CLI General Call warning parity | Arduino and ESP-IDF `gc arm` warnings reviewed |

Treat Rev A1 through `1518NNN`, unreadable markings, or unknown silicon as
affected until proven otherwise.

## Hardware/HIL Evidence

| Gate | Evidence |
|---|---|
| HIL runner bundle | `hil_logs/mcp45hvx1_<timestamp>/` attached with `summary.json`, `report.md`, `raw_serial.txt`, `commands.txt`, and `operator_notes.md` |
| HIL command line recorded | exact `tools/run_hil_mcp45hvx1.py` invocation in `summary.json` |
| HIL verdict recorded | `summary.json` verdict reviewed |
| Safe read-only HIL | safe default runner sequence passed with `PASS_SAFE_ONLY` or stronger verdict |
| Address strap tests | A1/A0 matrix evidence for `0x3C..0x3F` or documented alternate behavior |
| POR/BOR rail cycling | rail cycle logs, POR defaults, and `TBORD` margin |
| I2C fault injection | address NACK, data NACK, timeout, bus error, unplug/replug, reset behavior |
| Output-changing HIL | `PASS_WITH_OUTPUT_CHANGE` or reviewed `OPERATOR_REVIEW_REQUIRED`, baseline, measurement, restore logs |
| SHDN/WLAT checks | physical override and readback-vs-output observations in `operator_notes.md` if pins are wired |
| Low-voltage analog evidence | P0A/P0W/P0B voltage/current measurements on safe load |
| High-voltage evidence | required only if any high-voltage validation claim is made |
| General Call isolated-bus evidence | ACK/readback/analog measurement on isolated bus |

Safe-only HIL cannot support output-changing, analog-accuracy, high-voltage, or
General Call safety claims.

Use `docs/MCP45HVX1_HARDWARE_VALIDATION.md` for templates and detailed
measurement requirements.

## Final Signoff

| Field | Value |
|---|---|
| Release commit | Pending |
| Version | Pending |
| Tag | Pending |
| CI run | Pending |
| Package artifact and size | Pending |
| Hardware evidence bundle | Pending |
| HIL verdict | Pending |
| HIL evidence directory/report link | Pending |
| Hardware HIL actually run | yes / no |
| Remaining risk acceptances | Pending |
| Reviewer | Pending |
| Date | Pending |
