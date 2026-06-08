# MCP45HVX1 Hardening Prompt Archive

This directory is an archive of the chronological hardening prompts used for
the branch `hardening/mcp45hvx1-industry-readiness`.

The prompt sequence is documented in
[`README_ORDER_AND_FINDING_MAP.md`](README_ORDER_AND_FINDING_MAP.md). Prompts
00 through 08 have been completed on this branch; Prompt 09 was superseded by
the final docs/release cleanup and final hardening report.

## Current Status

- Prompt 01 completed core uncertainty tracking after ambiguous output-changing
  failures.
- Prompt 02 completed recoverable optional startup writes.
- Prompt 03 completed status fidelity, reset/probe/recover, and copy/move
  contracts.
- Prompt 04 completed datasheet device model, POR/BOR, SHDN/WLAT, fraction
  contract, and errata gates.
- Prompt 05 completed ESP-IDF behavioral parity: real `selftest output`,
  bounded parsing, functional color, state/health/cfg parity, null-handle
  guards, General Call warnings, and stronger contract guards. It did not
  produce local pure-IDF build logs or hardware/HIL logs.
- Prompt 06 completed fault-injection tests and guard strengthening.
- Prompt 07 completed docs, release metadata, package export, and stale-report
  cleanup.
- Prompt 08 completed HIL tooling and validation templates. It did not run
  hardware HIL.

## Future Agent Guidance

Do not rerun completed chunks unless a regression is found. Treat the prompt
files as historical input, not current instructions. Current evidence and
remaining blockers live in:

- `docs/MCP45HVX1_INDUSTRY_HARDENING_FINAL_REPORT.md`
- `docs/MCP45HVX1_FINAL_DOCS_RELEASE_CLEANUP_REPORT.md`
- `docs/MCP45HVX1_RELEASE_CHECKLIST.md`
- `docs/MCP45HVX1_HARDWARE_VALIDATION.md`
- `docs/MCP45HVX1_ESP_IDF_PARITY_REPORT.md`

Hardware validation, pure ESP-IDF local build evidence, and remote CI status
must be checked with actual logs before any release or production-readiness
claim.
