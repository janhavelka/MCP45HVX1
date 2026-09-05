# Code Audit Verification And Resolution

Reviewed on 2026-09-05 against synchronized `main` at `1faad98`
(`1faad9898e5fd50c62fdcd335b1894731b980124`). The working tree was clean and
`git pull --ff-only` confirmed it was current. This report supersedes the
work-item statuses in [`CODE_AUDIT.md`](CODE_AUDIT.md), which is retained as
the historical input. In particular, fixes that audit called "uncommitted"
were already committed in the starting tree.

All 17 findings were checked against the implementation. Several are valid,
but the proposed startup ordering, transport mappings, minimum-code helper,
and comment-stripping fix needed correction. No hardware was flashed or
driven during this review. Software and source evidence must not be read as
analog or production qualification.

## Finding Dispositions

| # | Verification | Resolution |
|---|---|---|
| 1 | Valid console setup/input defect. Default nonblocking stdio can lose fragments, retain errors, and flood prompts. "Cannot ever read a command" overstates a timing-dependent failure. | Configure the selected UART/USB console, use bounded nonblocking input accumulation, clear transient stream errors, and reject/drain overlong or invalid lines. Physical console HIL remains required. |
| 2 | Valid: only `0x3C..0x3F` belongs to this device. | Removed alternate address constants, configuration/diagnostic members, private helper, CLI command, help entries, HIL assumptions, and checker requirements. Exhaustive address tests reject every other byte. |
| 3 | Valid: an offline latch and an active job require different caller actions. | Appended `Err::OFFLINE` as value 15, preserving values 0–14. Updated both examples, docs, and code assertions. Offline rejection remains non-ambiguous and sends no traffic. |
| 4 | Valid: no job initially looked like a completed successful job. | `pollJob()` returns `INVALID_PARAM` for `JobType::None`; completed and zero-step job results remain available. Tested across lifecycle resets. |
| 5 | Valid warm-start reconnect hazard; proposed partial-TCON classification incomplete. | Order using both current and requested TCON: shutdown/disconnect-only/unchanged topology first; leaving shutdown or adding a terminal connection after the wiper. No extra transactions or intermediate topology. |
| 6 | Design limitation, not a current transport failure. Existing Arduino size assertion and pre-I/O rejection were already present. | Retained the 64-byte work/buffer bound and documented the callback requirement. Both supplied adapters support it. Added boundary, saturation, and rejection tests; no speculative configuration knob. |
| 7 | Claimed ESP-IDF error value is wrong for pinned 6.0.1; proposed mapping would falsely certify no state change. | Preserve native detail and map ambiguous responses to `I2C_ERROR`. Also corrected the audit's allegedly fixed Arduino zero-byte/address-NACK classification. Adapter limitations are explicit below. |
| 8 | Valid: Arduino restore trusted ACKs without comparing registers. Physical-output rationale needs qualification. | Read Wiper/TCON after successful restore writes, compare both, retain the first write/read error, report `REGISTER_MISMATCH` on disagreement, and clear the CLI flag only on verified success within this restore path. |
| 9 | Partly valid. Seven IDF no-argument groups lacked guards; alias drift was real. Claimed general trailing-token acceptance is false. | Reconciled guards and alias sets, preserved separate implementations, added behavior/contract tests, and fixed IDF `begin` partially changing config on invalid input. No shared implementation or example removal. |
| 10 | Valid duplication, with distinct guards/filtering that must survive. | Centralized failure accounting in `_recordFailure()` and removed redundant filters. Local callback failures do not degrade device health. Success, counters, timing, recovery, and uncertainty behavior are regression-tested. |
| 11 | Helper absent, but proposed formula and "safe" contract are incorrect. | Rejected the new API. Corrected the maintained device reference with a units-correct, upward-rounded conservative model and explicit infeasible-result handling. No invented CLI voltage measurement. |
| 12 | Valid job/rollback coverage gaps. | Added busy/offline/lifecycle matrices, both phases of multi-instruction failures, decrement execution, both resolutions, all cited chunk boundaries, invalid configuration, and begin rollback assertions. |
| 13 | Valid stub/framing gaps. | Stub tracks STOP and both addresses, bounds readable bytes to staged data, and resets between tests. Actual adapter tests exercise repeated-start, read-only, short-read, write-phase failure, General Call addressing, and ambiguous-write uncertainty. |
| 14 | Valid dead/fragile guards and latent HIL help false positives. | Removed dead checks/allow-list paths, shared contract constants, checked handler searches, propagated child exit status, and used a single-pass comment/literal lexer. Detailed help is recognized by its first token. Mutation tests verify failures. |
| 15 | Valid import side effect when generated files are stale. | Invoke automatic sync only for a real SCons environment. Import and `check` remain read-only; CLI sync and PlatformIO generation still work. Scratch-copy tests prove both behaviors. |
| 16 | Valid public-documentation coverage gap; the original gate still checked syntax/cross-references. | Broad input now includes `src` and the docs directory. Added a separate strict public-API gate, documented snapshot fields/generated macros, and pinned Doxygen 1.15.0 with a SHA-256-verified download in CI. |
| 17 | Closed correctly: installed pinned builder selects the framework manifest first. | Kept `src_dir = .`, existing tolerant version matching, and `*.orig` ignore. No source-tree restructuring. Generated artifacts are checked after builds. |

## Corrections To The Proposed Solutions

### Startup topology

The audit's "any partial target first" rule would reconnect a stale wiper
on `0xF8 -> 0xFB`, `0xFD -> 0xFB`, or `0xF7 -> 0xFB`. The implemented
decision uses the already-read TCON value. A shutdown target is written first;
otherwise leaving shutdown or setting any previously cleared terminal bit
requires wiper-first. All other cases retain TCON-first.

Tests cover 11 current/target pairs and failures on either write. A failed
write preserves the original status and the affected register's uncertainty.
The existing wiper-first `restorePowerOnDefaults()` is retained. This does
not make an arbitrary analog circuit transition atomic; external isolation,
SHDN, WLAT, and power sequencing remain the application's responsibility.

### Transport evidence and uncertainty

The pinned [ESP-IDF 6.0.1 I2C implementation](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_driver_i2c/i2c_master.c)
uses `ESP_ERR_INVALID_RESPONSE` for failed transactions, including NACKs and
an internal transaction-timeout path. Neither combined transfers nor
receive-only calls can infer a definite address NACK from that result.
`ESP_ERR_INVALID_STATE` is not an address-NACK certificate. The example now
returns `I2C_ERROR` for ambiguous responses, `I2C_TIMEOUT` when the framework
explicitly returns `ESP_ERR_TIMEOUT`, and preserves the native detail.

The [Arduino-ESP32 3.3.11 Wire source](https://github.com/espressif/arduino-esp32/blob/3.3.11/libraries/Wire/src/Wire.cpp)
also loses information: `requestFrom()` returns a byte count even when its
internal operation fails; zero is not proof of absence. `endTransmission()`
folds `ESP_FAIL` and `ESP_ERR_NOT_FOUND` into result 2. The example therefore
uses generic `I2C_ERROR` for these ambiguous cases. Explicit timeout and
data-NACK statuses remain distinct when the interface exposes them. Tests
prove an ambiguous write sets uncertainty instead of pretending the address
was rejected before any mutation.

Consequently, neither example promises `DEVICE_NOT_FOUND` from its combined
`begin()`/`probe()` read. The core still produces that status whenever an
application transport supplies a definite `I2C_NACK_ADDR`. Adding a probe
after a failure would not establish whether the original write was accepted,
so no retry or inference transaction was added. The Arduino bus-reset
callback also now propagates a failed `Wire.begin()` instead of returning
fake success.

### Current-limit calculation

The local Microchip DS20005304B PDF, page 42 / Table 5-3, confirms the audit's
eight printed integers, but they conflict with the table's stated rounding
direction. The proposed expression also forgets to convert milliamps to
amperes. Using it literally gives zero at 36 V after `floor`, not the claimed
published values.

With consistent units, rounding down still exceeds the model's current
limit: a 5 k / 8-bit device at minimum RAB and code 91 draws about 25.22 mA
at 36 V. A conservative result uses
`ceil(abs(VBW) / ((Imax_mA / 1000) * (0.8 * RAB / maxCode)))`:
92/46, 92/46, 36/18, and 18/9 for 5/10/50/100 k in 8-bit/7-bit order.
An out-of-range result means no feasible code; clamping it would hide that.

[`DEVICE_REFERENCE.md`](DEVICE_REFERENCE.md) now records these distinctions,
the A-W upper-bound difference, and the separate current/thermal/measurement
requirements. A byte-returning `minSafeWiperCode()` cannot express invalid or
infeasible inputs, violates the repository's fallible-operation convention,
and has no present caller with measured voltage. Documentation is the simpler
proper solution here.

### CLI and tooling scope

The IDF value parsers already consumed the entire argument string, rejecting
the audit's `addr`, `res`, `rab`, `frac`, `shutdown`, `mode`, `inc`/`dec`, and
`stress` trailing-junk examples. The missing no-argument guards were real and
are fixed. Both mode parsers now accept the union of their existing spellings.
Arduino token overflow is rejected rather than silently accepting a prefix;
malformed `begin` commands cannot trigger a restart or partially change config.
The additional `mid` discrepancy is fixed: both examples select the POR
midpoint (`0x7F`/`0x3F`) instead of Arduino rounding the fractional midpoint
one code higher. Host tests cover both variants.

The examples continue to share a command contract, as `AGENTS.md` requires.
Host tests compile the real Arduino CLI and extracted native-IDF functions;
they exercise input/restore failures without pretending to be hardware tests.
Contract mutation tests supplement these behavioral checks.

Simply stripping strings before comments is also insufficient: quote marks
inside comments can hide following code. The shared lexer recognizes comments
and ordinary/raw literals in one pass while preserving line breaks. The
strict Doxygen gate is limited to the public API; example documentation does
not need dozens of unrelated comment edits to enable a useful API check.

## Compatibility And Migration

Metadata is advanced from 1.1.0 to **2.0.0** because public structure members
were removed. This is a development version change, not a tagged release or
production-readiness claim.

- Remove uses of `Config::allowAlternateAddressRange`,
  `DeviceInfo::usingAlternateAddressRange`, `cmd::ALT_MIN_ADDRESS`, and
  `cmd::ALT_MAX_ADDRESS`; use the hardware-strapped `0x3C..0x3F` address.
- Remove `addr_alt` automation; both CLIs and HIL accept only the device range.
- Handle `Err::OFFLINE` with explicit recovery; `BUSY` denotes an active job.
- Treat polling without a started job as `INVALID_PARAM`.
- Expect generic I2C errors where a framework cannot prove the failed phase.
  Rebuild dependent code; the removed fields change structure layout.

## Validation And Remaining Evidence

| Check | Result |
|---|---|
| Native PlatformIO tests | 88/88 passed (78 baseline plus 10 regression groups). |
| Arduino CLI host regression executable | Passed register-restore success/ignored-write/mismatch/read-failure/write-failure, malformed-command, alias, and both-variant midpoint cases. |
| Tooling regression suite | 10/10 passed, including compiled native-IDF console/parser/error-mapping behavior and scratch-copy guard/import mutations. |
| HIL parser suite and parser self-test | 19/19 passed; self-test passed. No serial port opened. |
| Core compilation | Clean optimized compile with the audit's full GCC warning set, including conversion, sign-conversion, shadow, and null-dereference checks. |
| Native-IDF syntax | Nine console/target configurations compiled against available ESP-IDF 5.5.5 SDK headers. This is supplementary, not pinned 6.0.1 build evidence. |
| Doxygen 1.15.0 | Full manual and strict public-API gate passed without diagnostics. Deliberate undocumented-symbol and missing-parameter mutations both fail the strict gate. |
| Contract/version/artifact checks | Passed. |
| Full `python tools/validate.py` | First run passed software tests but hit a local PlatformIO Python-environment mismatch before Arduino builds; rerun and final CI results are recorded below when available. |

The initial local build failure came from inherited `PLATFORMIO_CORE_DIR=C:\pio`,
whose package environment disagreed with the selected Python. Selecting
`$env:PLATFORMIO_CORE_DIR = Join-Path $env:USERPROFILE '.platformio'` is a
process-local workaround, not a repository or global environment change.

Physical ESP-IDF console HIL, analog restoration/accuracy, SHDN/WLAT, injected
electrical faults, rail cycling, General Call isolation, and production
qualification remain outside this software-only run. Follow
[`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md) for those
measurements; no result here claims they passed.
