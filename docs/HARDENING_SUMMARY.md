# MCP45HVX1 Hardening Summary

This is the maintained summary for the software-hardening branch. It replaces
the previous prompt-by-prompt audit reports as the human-readable closure
record.

It is not a release approval and does not claim hardware validation, analog
accuracy, high-voltage safety, General Call safety, field readiness, or
production readiness.

## Fixed In Software

| Area | Current status |
|---|---|
| Ambiguous state-changing writes | Wiper/TCON/raw/General Call failures that may have reached hardware mark affected cache unknown and set `hardwareStateUncertain()`. |
| Startup side effects | `begin()` is read-only by default. Optional startup writes run only after baseline readback and preserve recoverability on ambiguous failure. |
| Status and health | Address NACK maps to `DEVICE_NOT_FOUND`; transport errors preserve detail; OFFLINE blocks normal I2C until `recover()`. |
| Reset and recover | `resetI2cState()` is interface-only. `recover()` requires Wiper and TCON readback before returning READY. |
| Copy/move policy | Driver objects are non-copyable and non-movable. |
| Device model | HV31/HV51 resolution, POR/BOR defaults, TCON reserved bits, SHDN/WLAT physical limits, and DS80000649B gates are documented. |
| General Call | Core helpers are disabled by default through `Config::allowGeneralCall`; diagnostic CLIs add one-shot arming and warnings. |
| Poll jobs | Side-effecting and multi-instruction work has explicit bounded job APIs and instruction accounting. |
| ESP-IDF example | Native ESP-IDF CLI has bounded parsing, color toggle, real `selftest output`, state/health parity, and contract guards. |
| Tests and guards | Native fake-bus tests, CLI/IDF contract scripts, core timing guard, version check, generated-artifact guard, package pack, and Arduino S2/S3 builds are part of the release checklist. |

## Deferred Evidence

These are not software blockers for merge review, but they block release and
readiness claims:

- local or CI pure ESP-IDF build logs for the release commit
- hardware/HIL transcript on real target hardware
- address strap matrix evidence
- package marking/date-code and errata review
- POR/BOR rail cycling and `TBORD` margin
- analog P0A/P0W/P0B movement under safe load
- terminal-current and high-voltage validation if claimed
- SHDN and WLAT physical behavior if wired
- General Call isolated-bus evidence or explicit shared-bus risk acceptance

## Maintained Evidence Locations

- API and software behavior: [`MCP45HVX1_API_CONTRACT.md`](MCP45HVX1_API_CONTRACT.md)
- Device facts and errata: [`DEVICE_REFERENCE.md`](DEVICE_REFERENCE.md)
- ESP-IDF status: [`IDF_PORT.md`](IDF_PORT.md)
- Hardware/HIL template: [`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md)
- Release gates: [`MCP45HVX1_RELEASE_CHECKLIST.md`](MCP45HVX1_RELEASE_CHECKLIST.md)

Release notes and exact check results belong in `CHANGELOG.md`, CI logs, HIL
evidence bundles, and the final release record rather than in one-off prompt
reports.
