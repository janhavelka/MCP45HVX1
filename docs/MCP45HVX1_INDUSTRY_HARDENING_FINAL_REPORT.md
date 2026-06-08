# MCP45HVX1 Industry Hardening Final Report

Branch: `hardening/mcp45hvx1-industry-readiness`

Base commit for final cleanup: `5965ac6`

Status: merge-preparation complete pending final cleanup commit and push.

This report closes the original audit findings for the software hardening
branch. It is not a release approval and does not claim hardware validation,
analog accuracy, high-voltage safety, General Call safety, field readiness, or
production readiness.

## Closure Table

| Finding | Status | Evidence |
|---|---|---|
| H1 failed state-changing writes / stale cache | Fixed | Core uncertainty accessors and cache invalidation in `include/MCP45HVX1/MCP45HVX1.h` / `src/MCP45HVX1.cpp`; report `docs/MCP45HVX1_CORE_UNCERTAIN_STATE_REPORT.md`; native ambiguous-write tests in `test/test_basic.cpp`. |
| H2 begin optional writes / startup side effects | Fixed | Two-phase `begin()` with read-only default and recoverable optional-write failures; report `docs/MCP45HVX1_BEGIN_STARTUP_RECOVERABILITY_REPORT.md`; startup recoverability tests in `test/test_basic.cpp`. |
| H3 hardware validation missing | Deferred to hardware/HIL | HIL runner and templates exist in `tools/run_hil_mcp45hvx1.py` and `docs/MCP45HVX1_HARDWARE_VALIDATION.md`; no hardware logs are bundled. |
| H4 errata release gate | Fixed for software/docs; deferred to hardware evidence | `SiliconErrataInfo`, CLI errata output, release checklist, and hardware validation docs gate DS80000649B review and General Call evidence; actual silicon marking review remains pending. |
| H5 ESP-IDF behavioral parity | Fixed for software; deferred to local/CI evidence | Native ESP-IDF CLI implements real command behavior, bounded parsing, color, state/health/cfg parity, General Call warnings, and guards; see `docs/MCP45HVX1_ESP_IDF_PARITY_REPORT.md`. Local `idf.py` remains unavailable and remote CI logs were not inspected. |
| M1 `resetI2cState()` READY proof | Fixed | `resetI2cState()` is interface-only and does not prove READY; report `docs/MCP45HVX1_STATUS_HEALTH_LIFECYCLE_REPORT.md`; native tests cover reset-without-device-proof behavior. |
| M2 uncertainty observability | Fixed | Arduino and ESP-IDF `state`, `drv`/`health`, and `cfg` expose uncertainty and cache-known flags; HIL report documents capture expectations. |
| M3 begin/probe error fidelity | Fixed | Address NACK maps to `DEVICE_NOT_FOUND` with detail preserved; timeout/bus/data-NACK/generic errors preserve public status; covered by native tests. |
| M4 copy/move semantics | Fixed | `MCP45HVX1` copy/move constructors and assignments are deleted; static assertions cover the policy. |
| M5 hardware checklist too narrow | Fixed as documentation; deferred to hardware/HIL | `docs/MCP45HVX1_HARDWARE_VALIDATION.md` covers address straps, POR/BOR, fault injection, output-changing tests, analog measurements, SHDN/WLAT, General Call, and errata. |
| M6 POR/BOR docs | Fixed | README, device model report, initialization notes, and validation docs use HV31/HV51 defaults and `TBORD` timing without inventing a host-side `tPOR` core delay. |
| M7 API contracts | Fixed | `docs/MCP45HVX1_API_CONTRACT.md` is the authoritative software API contract. |
| M8 version/changelog/tag policy | Fixed for branch; release deferred | CHANGELOG and release checklist keep `1.0.0` as an engineering/pre-production package version until evidence and explicit tag approval exist. No tag was created. |
| L1 address matrix tests | Fixed | Native address-matrix tests cover standard range, alternate range opt-in, and adjacent invalid addresses. |
| L2 stale reports | Fixed for maintained docs | `docs/README.md` separates authoritative docs from historical/superseded reports. |
| L3 package/export | Fixed | `library.json` and `idf_component.yml` exclude CI/build/test/tool/generated/PDF/extracted artifacts from normal packages; generated-artifact guard checks tracked and untracked outputs. |
| L4 stale SECURITY/CONTRIBUTING | Fixed | SECURITY and CONTRIBUTING now describe current core storage, transport ownership, checks, and evidence requirements. |
| L5 fraction doc mismatch | Fixed | Header/README/tests document `writeWiperFraction()` as rejection-based for NaN/out-of-range output-changing writes. |

## Software Validation Position

Local software checks have repeatedly passed on this branch in the prompt
reports. The final cleanup report records the latest exact check results.

The repository CI configuration includes native tests, guard scripts, package
pack, Arduino ESP32-S2/S3 PlatformIO builds, and pure ESP-IDF `esp32s2` /
`esp32s3` build jobs. The workflow currently runs on `main`, tags, and pull
requests to `main`; direct hardening-branch pushes are not remote CI evidence.

## Evidence Gaps

- Local pure ESP-IDF build evidence is absent where `idf.py` is not on PATH.
- Remote CI green status for the final cleanup commit must be checked from a PR
  or release/tag workflow run before it is cited.
- Hardware/HIL logs are absent unless a future run attaches `hil_logs/...`.
- Address straps, silicon marking/date code, DS80000649B applicability,
  POR/BOR rail cycling, SHDN/WLAT physical behavior, analog output movement,
  safe-load/current limits, high-voltage behavior, and General Call isolation
  remain hardware evidence tasks.

## Merge Recommendation

The hardening branch is suitable for merge review as a software-hardening and
documentation cleanup branch after final checks pass and the cleanup commit is
pushed. It is not suitable for release approval or production/field claims until
the release checklist evidence is complete.
