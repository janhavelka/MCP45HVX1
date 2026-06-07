# MCP45HVX1 Hardware Validation Checklist

This file records hardware checks that must be completed before removing the
address-conflict caveat from the driver documentation.

## I2C Address Conflict

The internal docs record a DS20005304B conflict:

- Table/text path: fixed bits `01111`, 7-bit address range `0x3C-0x3F`
- Command-figure path: fixed bits `10111`, 7-bit address range `0x5C-0x5F`

The driver defaults to `0x3C-0x3F`. The `0x5C-0x5F` range is accepted only when
`Config::allowAlternateAddressRange` is enabled.

## Silicon / Errata Release Gate

These items block any production-ready claim until recorded:

| Gate | Required evidence |
|---|---|
| Silicon and errata review | Package marking/date code reviewed against DS80000649B Appendix B and any newer errata; Rev A1 through `1518NNN`, unreadable markings, or unknown silicon are treated as affected |
| Shared-bus deployment | Affected or unknown silicon is isolated on its own I2C bus, or explicit shared-bus risk acceptance is recorded for DS80000649B traffic-acceptance hazards |
| General Call output-changing commands | Isolated-bus HIL evidence that no other devices are affected and MCP45HVX1 behavior is verified by readback and measurement before production use |
| Analog/high-voltage safety | Safe-load measurements of P0A/P0W/P0B voltage and current for Wiper/TCON/startup-write operations |

Shared-bus deployments must not claim General Call safety without logged
evidence and signoff. Software readback alone is insufficient when WLAT, SHDN,
or external circuits can override physical output.

## Required Bench Record

For each A1:A0 pin combination, record the responding 7-bit address using the
`scan` CLI command:

| A1 | A0 | Expected if table is correct | Expected if figures are correct | Observed |
|---:|---:|---:|---:|---|
| 0 | 0 | `0x3C` | `0x5C` | Pending bench test |
| 0 | 1 | `0x3D` | `0x5D` | Pending bench test |
| 1 | 0 | `0x3E` | `0x5E` | Pending bench test |
| 1 | 1 | `0x3F` | `0x5F` | Pending bench test |

## General Call Isolated-Bus Evidence

Because `GCEN` is referenced but not located in the extracted register map,
record whether each General Call helper ACKs, whether `read` confirms the
expected local state afterward, and whether analog output was measured on an
isolated bus:

| Command | ACK/NACK | Local state verified after read | Isolated-bus analog measurement |
|---|---|---|---|
| `gc arm`, `gc wiper <code>` | Pending bench test | Pending bench test | Pending bench test |
| `gc arm`, `gc tcon <value>` | Pending bench test | Pending bench test | Pending bench test |
| `gc arm`, `gc inc` | Pending bench test | Pending bench test | Pending bench test |
| `gc arm`, `gc dec` | Pending bench test | Pending bench test | Pending bench test |

## Startup Write Recoverability

`begin()` must be read-only unless `Config::writeInitialWiper` or
`Config::writeInitialTcon` is explicitly enabled. Validate this with a safe load
or disconnected analog path before using startup writes on real circuits.

| Check | Expected result | Observed |
|---|---|---|
| Default `begin()` with both startup writes disabled | Only Wiper/TCON read frames; no output-changing write frames | Pending bench test |
| POR/BOR defaults required | MCP45HV31 reads Wiper `0x3F`, MCP45HV51 reads Wiper `0x7F`, and TCON reads `0xFF` before startup writes | Pending bench test |
| `writeInitialWiper=true` on safe load | Wiper changes to requested code and readback confirms value | Pending bench test |
| `writeInitialTcon=true` on safe load | Terminal mode changes to requested TCON and readback confirms value | Pending bench test |
| Induced failure during optional startup write | CLI/API reports uncertainty, original error is preserved, readback or `recover()` can inspect volatile state | Pending bench test |
| WLAT held high during Wiper write | Wiper register changes; physical P0W movement is held until WLAT release | Pending bench test |
| SHDN held low during TCON/Wiper readback | Registers read back; physical terminals remain in hardware shutdown state | Pending bench test |
| Output-changing test restore | Validation log records baseline, external measurement, restore result, and final uncertainty state | Pending bench test |

Software cannot validate terminal current, analog rail safety, SHDN/WLAT
overrides, or external circuit response. Record external measurements before
enabling output-changing startup writes in production firmware.

## Status, Probe, and Recover Checks

Use safe loads and induced transport faults to confirm that software status does
not overstate hardware proof.

| Check | Expected result | Observed |
|---|---|---|
| Address NACK during `begin()` or `probe()` | API reports `DEVICE_NOT_FOUND` and preserves numeric detail | Pending bench test |
| Timeout, bus error, or data NACK during `begin()` or `probe()` | API preserves the original public status code | Pending bench test |
| Successful `probe()` while DEGRADED | Probe reports presence but does not change health counters or state | Pending bench test |
| Successful `resetI2cState()` while DEGRADED/OFFLINE | Bus reset callback runs, but READY is not reported until a tracked device read or `recover()` succeeds | Pending bench test |
| `recover()` after OFFLINE or uncertainty | Wiper and TCON are read back; READY and uncertainty clearing occur only after required readback succeeds | Pending bench test |
