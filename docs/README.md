# MCP45HVX1 Documentation Index

This index separates current authoritative documentation from historical audit
reports and raw reference extracts. The repository is a pre-production,
industry-readiness hardening branch; hardware validation and release approval
remain pending until logged evidence exists.

## Authoritative Docs

| Document | Purpose |
|---|---|
| [`MCP45HVX1_INDUSTRY_HARDENING_FINAL_REPORT.md`](MCP45HVX1_INDUSTRY_HARDENING_FINAL_REPORT.md) | Final closure table for the original hardening findings and merge/release recommendation. |
| [`MCP45HVX1_API_CONTRACT.md`](MCP45HVX1_API_CONTRACT.md) | Public API, transport, lifecycle, uncertainty, General Call, and safety contract. |
| [`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md) | Required hardware/HIL, analog, WLAT, SHDN, POR/BOR, errata, and General Call evidence template. |
| [`MCP45HVX1_RELEASE_CHECKLIST.md`](MCP45HVX1_RELEASE_CHECKLIST.md) | Release gates for software checks, CI, package, HIL, hardware validation, changelog, and tag policy. |
| [`MCP45HVX1_ESP_IDF_PARITY_REPORT.md`](MCP45HVX1_ESP_IDF_PARITY_REPORT.md) | Current ESP-IDF CLI parity, bounded parsing, color, General Call, and build-evidence status. |
| [`MCP45HVX1_DEVICE_MODEL_ERRATA_REPORT.md`](MCP45HVX1_DEVICE_MODEL_ERRATA_REPORT.md) | Datasheet model, HV31/HV51 differences, SHDN/WLAT semantics, POR/BOR, and errata release gates. |
| [`MCP45HVX1_BEGIN_STARTUP_RECOVERABILITY_REPORT.md`](MCP45HVX1_BEGIN_STARTUP_RECOVERABILITY_REPORT.md) | `begin()` startup side-effect and recoverability contract. |
| [`MCP45HVX1_CORE_UNCERTAIN_STATE_REPORT.md`](MCP45HVX1_CORE_UNCERTAIN_STATE_REPORT.md) | Core hardware-uncertainty model after ambiguous state-changing writes. |
| [`MCP45HVX1_FINAL_DOCS_RELEASE_CLEANUP_REPORT.md`](MCP45HVX1_FINAL_DOCS_RELEASE_CLEANUP_REPORT.md) | Final docs/release cleanup audit, checks, package, CI/IDF, and HIL evidence status. |

## Additional Current Reports

| Document | Purpose |
|---|---|
| [`MCP45HVX1_STATUS_HEALTH_LIFECYCLE_REPORT.md`](MCP45HVX1_STATUS_HEALTH_LIFECYCLE_REPORT.md) | Status fidelity, health, reset/probe/recover semantics, and copy/move policy. |
| [`MCP45HVX1_TESTS_FAULT_GUARDS_REPORT.md`](MCP45HVX1_TESTS_FAULT_GUARDS_REPORT.md) | Native tests, fault coverage, guard scripts, and local software-validation status. |
| [`MCP45HVX1_I2C_LIBRARY_HARDENING_REPORT.md`](MCP45HVX1_I2C_LIBRARY_HARDENING_REPORT.md) | General Call opt-in guard, raw-operation classification, dirty/cache uncertainty, and TunnelMonitor fit. |
| [`MCP45HVX1_POLL_CHUNKING_REPORT.md`](MCP45HVX1_POLL_CHUNKING_REPORT.md) | Poll-chunked job API, instruction accounting, failure stop behavior, and native budget tests. |
| [`MCP45HVX1_DOCS_RELEASE_METADATA_REPORT.md`](MCP45HVX1_DOCS_RELEASE_METADATA_REPORT.md) | Documentation, release wording, metadata, and package export cleanup history. |
| [`MCP45HVX1_HIL_TOOLING_REPORT.md`](MCP45HVX1_HIL_TOOLING_REPORT.md) | HIL runner behavior and evidence bundle format. Hardware HIL was not run in that prompt. |
| [`IDF_PORT.md`](IDF_PORT.md) and [`IDF_PORT_IMPLEMENTATION.md`](IDF_PORT_IMPLEMENTATION.md) | ESP-IDF component and native example notes. |
| [`register_reference.md`](register_reference.md) and [`05_register_map.md`](05_register_map.md) | Maintained register references for Wiper0, TCON0, and command framing. |

## Historical Or Superseded Reports

These files are kept for audit history. Prefer the authoritative docs above for
current behavior and release decisions.

| Document | Status |
|---|---|
| [`cli_output_style_alignment_report.md`](cli_output_style_alignment_report.md) | Historical CLI style pass; runtime color limitations in it are superseded. |
| [`MCP45HVX1_CLI_PARITY_AND_COLOR_REPORT.md`](MCP45HVX1_CLI_PARITY_AND_COLOR_REPORT.md) | Historical CLI parity report; current ESP-IDF behavior is in the ESP-IDF parity report. |
| [`hardware_validation.md`](hardware_validation.md) | Legacy lowercase redirect to `MCP45HVX1_HARDWARE_VALIDATION.md`. |
| Prompt-specific reports from Prompts 01-08 | Audit trail for completed chunks; do not rerun those chunks unless regression is found. |

## Raw Reference Material

The PDF files under `docs/`, `docs/extracted-md/`, and
`docs/pdf-extracted-md/` are reference and extraction material. They may contain
old open questions or vendor boilerplate. They are not authoritative release
claims and are excluded from normal PlatformIO package exports.
