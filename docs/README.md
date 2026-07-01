# MCP45HVX1 Documentation

This directory contains maintained documentation for the MCP45HVX1 driver plus
vendor reference PDFs. Audit reports and prompt-by-prompt implementation notes
were merged into the current docs or removed from the maintained tree.

Current status: v1.0.0 pre-production software package. Local software
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
| [`RELEASE_NOTES_v1.0.0.md`](RELEASE_NOTES_v1.0.0.md) | GitHub release notes for the v1.0.0 pre-production software package. |
| [`IDF_PORT.md`](IDF_PORT.md) | ESP-IDF component/example boundary, command parity, local/CI build evidence rules, and native-IDF guard coverage. |
| [`HARDENING_SUMMARY.md`](HARDENING_SUMMARY.md) | Concise closure summary for the software-hardening work and remaining evidence gaps. |

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

## Evidence Policy

- Software checks prove driver behavior only within the tested transports and
  fake-bus scenarios.
- HIL logs are evidence only when produced by an actual hardware run and
  attached to a release or validation record.
- Current bundled safe-only HIL reports live under
  [`docs/reports/`](https://github.com/janhavelka/MCP45HVX1/tree/v1.0.0/docs/reports)
  in the repository/source archive: the 8-hour COM8 ESP32-S2 run finished
  `PASS_SAFE_ONLY` with zero failures. These large report files are excluded
  from the normal PlatformIO package archive.
- Register readback proves volatile register contents, not analog output
  movement when WLAT, SHDN, or external circuitry overrides the terminals.
- General Call use needs isolated-bus evidence or explicit risk acceptance for
  the actual silicon and board.
