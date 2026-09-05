# MCP45HVX1 Documentation

Maintained documentation and vendor reference PDFs for the MCP45HVX1 driver.

Current status: pre-production software package. Local software validation and
ESP32-S2 safe-only HIL evidence exist. Output-changing behavior, high-voltage
behavior, analog accuracy, SHDN/WLAT physical behavior, fault injection, rail
cycling, General Call safety, and production release approval remain
evidence-gated.

## Maintained Docs

| Document | Purpose |
|---|---|
| [`DEVICE_REFERENCE.md`](DEVICE_REFERENCE.md) | Device facts the driver relies on: variants, pins, addressing, register map, command framing, reset behavior, and errata. |
| [`MCP45HVX1_API_CONTRACT.md`](MCP45HVX1_API_CONTRACT.md) | Public software contract: transport ownership, lifecycle, health, uncertainty, poll jobs, raw access, and General Call policy. |
| [`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md) | Hardware/HIL evidence procedure for address straps, POR/BOR, analog output, SHDN/WLAT, safe loads, and General Call isolation. |
| [`MCP45HVX1_RELEASE_CHECKLIST.md`](MCP45HVX1_RELEASE_CHECKLIST.md) | Release gates for repository state, versioning, checks, packaging, CI, hardware evidence, and signoff. |
| [`IDF_PORT.md`](IDF_PORT.md) | ESP-IDF component/example boundary, command parity, and build evidence rules. |
| [`CODE_AUDIT.md`](CODE_AUDIT.md) | Findings from the datasheet/code audit, each independently re-verified against the tree, with a proposal and an Open/Closed status. Maintainer document; excluded from package exports. |

Repository-wide user and release information lives in
[`../README.md`](../README.md), [`../CHANGELOG.md`](../CHANGELOG.md), and
[`../CONTRIBUTING.md`](../CONTRIBUTING.md).

## Reference PDFs

Vendor PDFs live under [`reference-pdfs/`](reference-pdfs/) for traceability and
manual review. They are excluded from PlatformIO and ESP-IDF package exports.

- `MCP45HVX1-Data-Sheet-DS20005304.pdf` (Rev B) — the normative source for
  addressing, command framing, register map, and resistance equations.
- `MCP45HVX1_Errata_DS80000649B.pdf` — Rev A1 silicon errata; see the errata
  section of [`DEVICE_REFERENCE.md`](DEVICE_REFERENCE.md).
- `MCP45HVX1_Typical_Performance_Curves_DS20005307A.pdf` — graph-only
  characterization data.
- Supplemental Microchip digital-potentiometer application notes.

## Evidence Policy

- Software checks prove driver behavior only within the tested transports and
  fake-bus scenarios.
- HIL results are evidence only when produced by an actual hardware run and
  retained in a release or validation record. Generated `hil_logs/` bundles are
  local, ignored artifacts; attach them to the release record rather than
  committing them.
- Register readback proves volatile register contents, not analog output
  movement, when WLAT, SHDN, or external circuitry overrides the terminals.
- General Call use needs isolated-bus evidence or explicit risk acceptance for
  the actual silicon and board. DS80000649B Issue 1 also makes *any* shared-bus
  traffic a hazard on Rev A1 silicon, not just General Call.
