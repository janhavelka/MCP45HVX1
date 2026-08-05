# MCP45HVX1 Documentation

This directory contains maintained documentation, vendor reference PDFs, and
compact hardware evidence summaries for the MCP45HVX1 driver. Completed task
prompts and one-off hardening summaries are intentionally not retained after
their durable content has been merged into these documents and the changelog.

Current status: v1.1.0 pre-production software package. Local software
validation and ESP32-S2 safe-only HIL evidence are available, while
output-changing behavior, high-voltage behavior, analog accuracy, SHDN/WLAT
physical behavior, fault injection, rail cycling, General Call safety, and
production release approval remain evidence-gated.

## Maintained Docs

| Document | Purpose |
|---|---|
| [`DEVICE_REFERENCE.md`](DEVICE_REFERENCE.md) | Device facts used by the driver: variants, pins, address policy, register map, command framing, reset behavior, errata, and open hardware-validation items. |
| [`MCP45HVX1_API_CONTRACT.md`](MCP45HVX1_API_CONTRACT.md) | Public software contract: transport ownership, lifecycle, health, uncertainty, poll jobs, raw access, and General Call policy. |
| [`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md) | Hardware/HIL evidence procedure for address straps, POR/BOR, analog output, SHDN/WLAT, safe loads, and General Call isolation. |
| [`MCP45HVX1_RELEASE_CHECKLIST.md`](MCP45HVX1_RELEASE_CHECKLIST.md) | Release gates for repository state, versioning, checks, packaging, CI, hardware evidence, and signoff. |
| [`IDF_PORT.md`](IDF_PORT.md) | ESP-IDF component/example boundary, command parity, local/CI build evidence rules, and native-IDF guard coverage. |

Repository-wide user and release information lives in
[`../README.md`](../README.md), [`../CHANGELOG.md`](../CHANGELOG.md),
[`../ASSUMPTIONS.md`](../ASSUMPTIONS.md), and
[`../CONTRIBUTING.md`](../CONTRIBUTING.md).

## Reference PDFs

Vendor PDFs live under [`reference-pdfs/`](reference-pdfs/). They are kept for
traceability and manual review, but they are excluded from normal PlatformIO and
ESP-IDF package exports.

Key references:

- `MCP45HVX1-Data-Sheet-DS20005304.pdf`
- `MCP45HVX1_Errata_DS80000649B.pdf`
- `MCP45HVX1_Typical_Performance_Curves_DS20005307A.pdf`
- supplemental Microchip digital-potentiometer application notes

Raw PDF text extracts are not maintained in this repository. Regenerate them
from the PDFs when needed for a focused review.

## HIL Evidence Summaries

The retained reports preserve run metadata, aggregate results, timing, and
scope limits for three recorded safe-only ESP32-S2 sessions:

- <a href="reports/hil-validation-COM8-20260629.md">8-hour validation run</a>.
- <a href="reports/hil-panic-repro-COM8-20260629.md">1-hour panic-reproduction run</a>.
- <a href="reports/hil-validation-COM9-20260805.md">targeted COM9 validation run</a>.

The historical command-by-command Markdown rows were removed because they
duplicated machine-readable/raw bundles and made the repository unnecessarily
large. Generated local `hil_logs/` bundles remain ignored staging artifacts;
archive required bundles externally. Use `--report-file` only when an explicit
repository report copy is wanted.

## Evidence Policy

- Software checks prove driver behavior only within the tested transports and
  fake-bus scenarios.
- HIL logs are evidence only when produced by an actual hardware run and
  retained in a release or validation record.
- The bundled 8-hour COM8 ESP32-S2 report finished `PASS_SAFE_ONLY` with zero
  failures. All retained reports are excluded from install packages.
- Register readback proves volatile register contents, not analog output
  movement when WLAT, SHDN, or external circuitry overrides the terminals.
- General Call use needs isolated-bus evidence or explicit risk acceptance for
  the actual silicon and board.
