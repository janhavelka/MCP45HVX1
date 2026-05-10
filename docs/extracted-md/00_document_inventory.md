# MCP45HVX1 Document Inventory

Compact driver notes live in `docs/extracted-md/00_*.md` through `08_*.md`. Raw PDF text extracts live in `docs/pdf-extracted-md/`; use them for search and page traceability, then verify dense tables and figures against the PDFs under `docs/`.

## Source Documents

| Source PDF | Pages | Role | Notes |
|---|---:|---|---|
| `MCP45HVX1-Data-Sheet-DS20005304.pdf` | 102 | Primary datasheet | Normative source for device identity, electrical limits, I2C transactions, registers, reset behavior, packages, and ordering codes. Source: DS20005304B. |
| `MCP45HVX1-Data-Sheet-DS20005304-Duplicate.pdf` | 102 | Duplicate datasheet | Duplicate copy of the same DS20005304B datasheet. |
| `MCP45HVX1_Errata_DS80000649B.pdf` | 4 | Errata | Rev. A1 silicon / datasheet errata. Review before changing behavior for early silicon. |
| `MCP45HVX1_Typical_Performance_Curves_DS20005307A.pdf` | 24 | Characterization | Typical curves; graph values need PDF review, not raw text extraction alone. |
| Microchip application notes and design guide PDFs | varies | Supplemental design guidance | Useful for circuit-design context; not a replacement for the MCP45HVX1 datasheet. |

## Compact Note Set

| File | Driver-implementation focus |
|---|---|
| `01_chip_overview.md` | Device identity, variants, supply domains, part numbering, packages. |
| `02_pinout_and_signals.md` | Pin functions, address pins, WLAT, SHDN, terminal pins, package differences. |
| `03_electrical_and_timing.md` | Electrical limits, operating conditions, I2C timing, analog terminal constraints. |
| `04_protocol_commands_and_transactions.md` | I2C addressing, command bytes, read/write/increment/decrement, General Call, HS mode. |
| `05_register_map.md` | Wiper and TCON registers, bit fields, reset values, access semantics. |
| `06_modes_interrupts_status_and_faults.md` | Shutdown, WLAT behavior, POR/BOR, NACK/error behavior, General Call side effects. |
| `07_initialization_reset_and_operational_notes.md` | Bring-up sequence, power-domain ordering, software reset, implementation notes. |
| `08_variant_differences_and_open_questions.md` | 7-bit vs 8-bit, packages, errata, unresolved documentation ambiguities. |

## Source Priority

Use DS20005304B as the normative source unless an errata item applies. Supplemental Microchip PDFs are engineering context only. The title of DS20005304B mentions non-volatile memory, but the register map documents volatile RAM registers only; see `08_variant_differences_and_open_questions.md`.
