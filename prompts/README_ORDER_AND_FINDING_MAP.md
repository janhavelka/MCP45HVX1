# MCP45HVX1 Tailored Industry-Readiness Fix Prompts

These prompts are tailored directly from `docs/MCP45HVX1_PRODUCTION_READINESS_EXPLORATION_REPORT.md`.

Send them to the coding agent **one by one, in order**. Each prompt instructs the agent to:
- complete only that chunk,
- spawn focused subagents,
- implement the requested changes,
- run checks,
- commit,
- sync/push,
- stop and report.

## Order

1. `00_branch_agents_and_baseline.md`
2. `01_core_uncertain_hardware_state.md`
3. `02_begin_startup_side_effects_and_recoverability.md`
4. `03_status_health_probe_recover_copy_contracts.md`
5. `04_datasheet_device_model_docs_errata_release_gate.md`
6. `05_espidf_cli_behavioral_parity_and_build_gate.md`
7. `06_tests_fault_injection_and_guard_scripts.md`
8. `07_docs_release_metadata_packaging_and_stale_reports.md`
9. `08_hil_tooling_and_hardware_evidence.md`
10. `09_final_integration_merge_gate.md`

## Finding coverage map

| Exploration finding | Covered by prompt(s) |
| --- | --- |
| H1 Failed state-changing writes leave stale known Wiper/TCON cache | 01, 06, 09 |
| H2 Optional initial writes during `begin()` can hide hardware side effects | 02, 06, 09 |
| H3 Missing hardware validation evidence for high-voltage production use | 08, 09 |
| H4 Errata workaround not a release gate | 04, 07, 08, 09 |
| H5 ESP-IDF token parity but not behavioral parity | 05, 06, 09 |
| M1 `resetI2cState()` can mark READY without device proof | 03, 06 |
| M2 Uncertain-state observability inconsistent across CLIs | 01, 05, 08 |
| M3 `begin()`/`probe()` collapse transport detail | 03, 06 |
| M4 Copy/move semantics implicit | 03 |
| M5 Hardware validation checklist too narrow | 08 |
| M6 POR/BOR timing docs inconsistent | 04, 07 |
| M7 Public API contracts too thin | 03, 04, 07 |
| M8 Version/changelog/tag policy not release-clean | 07, 09 |
| L1 Address tests do not cover every candidate address | 06 |
| L2 Stale reports conflict with current state | 07 |
| L3 Package size/export scope uncontrolled | 07 |
| L4 SECURITY/CONTRIBUTING stale generic text | 07 |
| L5 `writeWiperFraction()` doc disagrees with behavior | 04, 06 |

## Core principle

Fix software truthfulness before improving evidence and release presentation:

1. Make core state reporting honest under ambiguous write failures.
2. Make startup side effects recoverable/visible.
3. Make status, health, lifecycle, and public API contracts explicit.
4. Align docs with datasheet and errata.
5. Fix ESP-IDF behavioral parity.
6. Strengthen tests/guards.
7. Remove overclaims and stale docs.
8. Add HIL tooling/evidence capture.
9. Final integration and merge/release verdict.