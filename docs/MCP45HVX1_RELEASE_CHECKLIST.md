# MCP45HVX1 Release Checklist

This checklist gates release claims for this repository. Passing software checks
alone is not enough to claim production readiness, high-voltage validation,
analog accuracy, or General Call safety.

Current state: pre-production candidate pending hardware validation. No release
tag is created by this checklist; create a tag only after the gates below are
complete and an operator explicitly approves the release.

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

If version `1.0.0` is retained before hardware evidence exists, describe it as
an engineering or pre-production package version. Do not call it
production-ready or industry-grade.

## Software Validation

| Gate | Command/evidence |
|---|---|
| HIL runner syntax | `python -m py_compile tools/run_hil_mcp45hvx1.py` |
| Core timing guard | `python tools/check_core_timing_guard.py` |
| Arduino/CLI contract | `python tools/check_cli_contract.py` |
| ESP-IDF example contract | `python tools/check_idf_example_contract.py` |
| Generated artifact guard | `python tools/check_generated_artifacts.py` |
| Version check | `python scripts/generate_version.py check` |
| Native tests | `python -m platformio test -e native` |
| Arduino ESP32-S3 build | `python -m platformio run -e esp32s3dev` |
| Arduino ESP32-S2 build | `python -m platformio run -e esp32s2dev` |
| Pure ESP-IDF esp32s3 build | `idf.py -C examples/espidf_basic set-target esp32s3 build`, or CI log link |
| Pure ESP-IDF esp32s2 build | `idf.py -C examples/espidf_basic set-target esp32s2 build`, or CI log link |

Remote CI can be cited only when the workflow actually ran for the release
commit. Local checks and CI checks are separate evidence.

## Packaging

| Gate | Evidence |
|---|---|
| Package pack | `python -m platformio pkg pack` passed |
| Generated tarball removed | package archive removed before commit |
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
| Hardware validation doc current | `docs/MCP45HVX1_HARDWARE_VALIDATION.md` reviewed |
| HIL tooling report current | `docs/MCP45HVX1_HIL_TOOLING_REPORT.md` reviewed |
| Release checklist current | this file reviewed |
| Stale reports handled | historical reports have superseded/current-status notes |
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
