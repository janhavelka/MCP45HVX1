# MCP45HVX1 Code Audit

Findings from a full review of the library against DS20005304B (Rev B) and
DS80000649B (Rev B, 7/2015), plus the examples, tests, and repository tooling.

Fixes that were certain and self-contained have already been applied and are
listed in the `Unreleased` section of [`../CHANGELOG.md`](../CHANGELOG.md).
This document covers what is **left**: issues whose fix changes a public
contract, needs a hardware or ESP-IDF build to verify, or is large enough to
deserve a decision before it is made.

Each item states the defect, why it matters, and a concrete proposal.

## How to read this document

Every finding below was **independently re-verified against the current working
tree** in a second pass, one investigation per finding. Claims were checked by
running the code — building and running the test suite, compiling the driver
under the stated warning set, executing the Python checkers against scratch
copies, running Doxygen with each candidate setting, and reading the pinned
PlatformIO platform's own sources — not by re-reading the first pass.

That second pass changed the document materially. Findings 6, 16, and 17 had
their central claim refuted; findings 9 and 11 had load-bearing numbers or
formulas corrected; one bullet in *Verified correct* cited a regression test
that does not exist. A third pass then re-measured the numeric claims the
second pass introduced and corrected finding 16's warning counts again. Where a
claim was wrong it has been replaced by the measured result and the correction
is stated in place, so an independent implementer can see what was checked and
how.

Treat line-number citations as navigation aids rather than as verified
anchors: they were checked in bulk but the tree moves under them.

**Status column**: `Open` means the defect is present in the working tree and
the proposal is not implemented. `Closed` means the finding was investigated
and does not hold. Two entries are mixed — `Docs fixed, code Open` (2) and
`Partly fixed` (9) — because part of the work is already in the working tree
and part is not; each body says which half is which. For any half marked as
already done, an implementer's job is to confirm the implementation is correct,
not to write it.

## Summary

| # | Issue | Severity | Kind | Status |
|---|---|---|---|---|
| 1 | ESP-IDF example CLI cannot read a command | High | Bug | Open |
| 2 | `0x5C-0x5F` "alternate address range" is a misreading | High | Wrong assumption | Docs fixed, code Open |
| 3 | OFFLINE and job-busy share `Err::BUSY` | Medium | API contract | Open |
| 4 | `pollJob()` returns `OK` when no job was started | Medium | API contract | Open |
| 5 | `begin()` startup-write order is unsafe in one direction | Medium | Output safety | Open |
| 6 | `MAX_COMMAND_CHUNK` is a driver constant with a transport constraint | Low-Med | Design | Open |
| 7 | ESP-IDF transport cannot report `DEVICE_NOT_FOUND` | Medium | Bug | Open |
| 8 | Arduino `restoreSnapshot()` does not verify the restore | Medium | Output safety | Open |
| 9 | Two CLI implementations kept in sync by a Python contract | Medium | Structural | Partly fixed |
| 10 | `_updateHealth()` and `_recordFailure()` are near-duplicates | Low | Refactor | Open |
| 11 | No helper for the Table 5-3 minimum safe wiper code | Low | Missing feature | Open |
| 12 | Poll-job state machine is largely untested | Medium | Test coverage | Open |
| 13 | Wire test stub does not model repeated-start or RX length | Low | Test quality | Open |
| 14 | Contract checkers have dead and fragile checks | Low | Tooling | Open |
| 15 | `generate_version.py` writes files on import | Low | Tooling | Open |
| 16 | Doxygen "warnings as errors" gates almost nothing | Low | CI | Open |
| 17 | ESP32 builds were believed to rewrite `idf_component.yml` | Low | Build/tooling | Closed |

Severity changes from the first pass: **6** Medium -> Low-Med (the claimed
uncertainty latch does not occur with the in-repo transport), **17** Medium ->
Low and Closed (the component manager rewrites the *framework's* manifest, not
this repository's).

### Fixed during verification

Two defects were found and repaired while checking the findings above, because
both were live faults in the working tree rather than proposals:

- The documentation CI job was failing. Cross-references from
  `docs/DEVICE_REFERENCE.md:62` and `docs/MCP45HVX1_API_CONTRACT.md:76` to this
  file could not resolve because `docs/CODE_AUDIT.md` was not in the Doxyfile's
  `INPUT` list, and `WARN_AS_ERROR = FAIL_ON_WARNINGS` turns an unresolved
  `\ref` into a hard error. Added to `INPUT`; `doxygen Doxyfile` now emits zero
  diagnostics. See finding 16.
- `scripts/generate_version.py`'s new tolerant version regex ended in `\s*$`,
  which under `re.MULTILINE` eats the trailing newline when `version:` is the
  file's last line. Changed to `[^\S\n]*$`. See finding 17.

---

## 1. The ESP-IDF example CLI cannot read a command

**Severity: High.** `examples/espidf_basic/main/main.cpp:2000-2018`

`app_main()` sets both streams unbuffered (2001-2002), initialises the bus and
driver, prints the help banner, then loops on `fgets(line, ..., stdin)`
(2011-2017). It never installs the console UART driver.

ESP-IDF's UART VFS is non-blocking by default — the documentation states
plainly that with the default functions "Read is non-blocking", and that
blocking, interrupt-driven reads require installing the UART driver and calling
`uart_vfs_dev_use_driver()`. Without that, `fgets()` returns `nullptr`
immediately, so the loop reprints the `> ` prompt roughly every millisecond
forever and no command is ever assembled. Worse, a non-blocking read that
returns `-1`/`EWOULDBLOCK` latches the stream's error indicator, so without a
`clearerr(stdin)` the loop can stay dead even once bytes do arrive.

A repository-wide grep for
`uart_driver_install|uart_vfs|vfs_dev|usb_serial_jtag|esp_console|linenoise|CONFIG_ESP_CONSOLE`
returns zero hits outside this document. There is no `sdkconfig.defaults`
anywhere in the repo, so CI builds against stock defaults and nothing enables
blocking stdin.

`main/CMakeLists.txt:4` already lists `vfs` in `REQUIRES` while no VFS or UART
API is called anywhere in the file — the dependency is left over from a fix
that was never written. Note that it cannot simply be deleted: `vfs` is
contract-pinned by `tools/check_cli_contract.py:93` (`IDF_REQUIRED_COMPONENTS`,
enforced at 350-352) and `tools/check_idf_example_contract.py:156,243`.
`check_idf_example_contract.py:45` also mandates the literal token `fgets`, so
any fix must keep `fgets` as the line reader.

This has gone unnoticed because CI only *builds* the ESP-IDF example — the
`idf-build` job's only command is `idf.py set-target <target> build`, with no
flash, monitor, QEMU, or pytest-embedded step — and every recorded HIL run
drove the Arduino CLI on an ESP32-S2.

**Proposal.** Install the console UART driver and switch stdin to blocking
before the command loop:

```c
#include "driver/uart.h"
#include "driver/uart_vfs.h"

static void initConsole(void) {
  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
  uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM,
                                        ESP_LINE_ENDINGS_CR);
  uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM,
                                        ESP_LINE_ENDINGS_CRLF);
  ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
  uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
}
```

Add `esp_driver_uart` to `REQUIRES`. The component is pinned to ESP-IDF
`>=6.0.1` (`idf_component.yml:10`, `ci.yml:158`,
`check_idf_example_contract.py:53`), so `driver/uart_vfs.h` is the correct
header; the pre-5.3 `esp_vfs_dev.h` spelling is not needed here.

CI builds `esp32s3` and `esp32s2`, both of which default to a UART console, so
the `#if` branch above is the live one — the ESP32-S2 has no USB-Serial-JTAG
peripheral at all. Still handle the USB-Serial-JTAG case
(`usb_serial_jtag_driver_install()` plus `usb_serial_jtag_vfs_use_driver()`)
for anyone flashing an S3 devkit over its native USB port; select on the
`CONFIG_ESP_CONSOLE_*` symbols.

Verify on hardware — a build alone will not catch a regression here. The
cheapest lasting guard is to run one HIL session against the ESP-IDF image; the
existing runner already targets either CLI.

## 2. The `0x5C-0x5F` "alternate address range" is a misreading

**Severity: High** (wrong documented assumption, 103 references across 15
files). `include/MCP45HVX1/CommandTable.h:24-28`, `Config.h:96`, both CLIs,
three Python checkers, and the docs.

**Status: docs corrected, code untouched.** `docs/DEVICE_REFERENCE.md`,
`docs/MCP45HVX1_API_CONTRACT.md`, and the changelog now state the facts.
`git diff HEAD` shows zero changes to the concept in `include/`, `src/`,
`test/`, `examples/`, or `tools/`.

The repository treats `0x5C-0x5F` as a disputed alternate address range that a
hardware-verification build may opt into. It is not disputed. DS20005304B
§6.2.4 says the fixed bits are `01111`, Table 6-2 says `0111 1'b + A1:A0`
(= `0x3C..0x3F`), and Table 6-2 **Note 1** says:

> The fixed portion of the I2C address is different than the
> MCP44XX/MCP45XX/MCP46XX family (`0101 11'`, `0101 1'`, or `0101'`). This
> allows the maximum number of both standard and high-voltage devices on the
> single I2C bus.

The second sentence — dropped from earlier quotations of this note — is the
decisive one: the ranges are disjoint *so both families can share a bus*, not
so one may borrow the other's.

The repo's reading is wrong twice over. `0101 11'` + A0 is 7-bit `0x2E..0x2F`;
`0x5C..0x5F` are that pair's *8-bit control bytes*, so the value was
transcribed from the shifted column as well as from the wrong family. Taken at
face value as a 7-bit address, `0x5C` is `0b1011100`, whose fixed bits `10111`
match neither `01111` (high-voltage) nor any `0101x` (standard-voltage) form:
it addresses no device in either family.

The cost is not just documentation. `test_basic.cpp:415-418`
(`test_begin_address_matrix`) asserts that `begin()` **succeeds** on
`0x5C`-`0x5F` when `allowAlternateAddressRange` is set (411-414 assert the
matching rejection when it is not), so a test certifies an
address range that belongs to different silicon; `test_basic.cpp:666-667`
pins the two constants. On a shared bus, `0x5C` is an address collision, not an
MCP45HVX1.

**Proposal.** Remove the concept:

- delete `cmd::ALT_MIN_ADDRESS` / `ALT_MAX_ADDRESS`, `Config::allowAlternateAddressRange`,
  `DeviceInfo::usingAlternateAddressRange`, and `_isAlternateAddress()`;
- collapse `_isValidAddress()` to the `0x3C..0x3F` range check — note that
  `_isPrimaryAddress()` then becomes identical to it, so delete one of the two
  rather than leaving both;
- drop the four alternate rows and the `expectAlternate` column from the
  address test table, keeping the existing assertion that everything outside
  `0x3C..0x3F` is rejected;
- remove the `addr_alt` command and the `addr`/`cfg` reporting of the flag from
  both CLIs, including the ESP-IDF CLI's `parseAnySupportedAddressArg()`, which
  exists only to straddle the two ranges.

The three Python contract checkers are hard CI gates that currently *require*
the command to exist, so they must change in the same commit or the build
breaks:

| File | Sites |
|---|---|
| `include/MCP45HVX1/CommandTable.h` | 24-28 |
| `include/MCP45HVX1/Config.h` | 96 |
| `include/MCP45HVX1/MCP45HVX1.h` | 89, 608-610 |
| `src/MCP45HVX1.cpp` | 83-84, 309, 1229-1239 |
| `test/test_basic.cpp` | 361, 411-420, 428, 435, 666-667 |
| `examples/01_basic_bringup_cli/main.cpp` | 26 lines, incl. `handleAlternateAddress` 1153-1159 |
| `examples/espidf_basic/main/main.cpp` | 31 lines, incl. 430-478, 1586-1624 |
| `tools/check_cli_contract.py` | 31, 101 |
| `tools/check_idf_example_contract.py` | 73, 253, 274 |
| `tools/run_hil_mcp45hvx1.py` | 138-140, 248 |
| `README.md` | config-table row (158) |
| `docs/MCP45HVX1_API_CONTRACT.md` | 69-76, 174 |
| `docs/DEVICE_REFERENCE.md` | the "still accepts it" paragraph |
| `docs/MCP45HVX1_HARDWARE_VALIDATION.md` | 140-144 (the opt-in paragraph; the "Alternate candidate" column has already been removed) |

This removes a public `Config` field and a `DeviceInfo` field, so it belongs in
a minor or major bump with a changelog note.

If you would rather keep an escape hatch for an unexpected board, the honest
form is a single `Config::i2cAddressOverride` that accepts any 7-bit address
with no pretence that a particular range is documented.

## 3. OFFLINE and job-busy are both `Err::BUSY`

**Severity: Medium.** `src/MCP45HVX1.cpp:1418-1420` and `975-977`

Both `_offlineStatus()` and `_jobBusyStatus()` return `Err::BUSY`. The two
conditions need opposite responses: job-busy means "retry shortly, it will
clear itself"; OFFLINE means "this is latched, you must call `recover()`". A
caller can only tell them apart by `strcmp` on `Status::msg`, and
`test/test_basic.cpp:1953` currently encodes exactly that. `Status.h:19` also
documents `BUSY` as "Device is busy", which OFFLINE is not.

**Proposal.** Append `OFFLINE` to `Err` — the enum is documented as append-only
in `docs/MCP45HVX1_API_CONTRACT.md:18`, so existing values are unaffected. (The
`// I2C transport details (append-only ...)` comment at `Status.h:23` scopes
only the transport subgroup; appending a non-transport code after `I2C_BUS`
lands it visually inside that block, so move or restate the comment.) Return it
from `_offlineStatus()` and update the tests to assert the code instead of the
message.

`Err` currently has 15 values (0-14); `OFFLINE` would be 15. Nothing breaks:
`platformio.ini` uses `-Wall -Wextra -Werror=return-type` with no
`-Wswitch-enum`, every switch has a `default:`, no tool in `tools/` enumerates
`Err` names, and `run_hil_mcp45hvx1.py:244-251` matches failure names
generically as `[A-Z_]+`.

Four example sites must be updated, not the two the earlier draft named — until
then `OFFLINE` prints as "UNKNOWN" and colours red:

| Site | Effect if missed |
|---|---|
| `examples/common/HealthView.h:53-88` `errName()` | prints `UNKNOWN` |
| `examples/espidf_basic/main/main.cpp:548-567` `errName()` | prints `UNKNOWN` |
| `examples/01_basic_bringup_cli/main.cpp:154-187` `statusCause()` | "internal or unknown error" |
| `examples/01_basic_bringup_cli/main.cpp:189-196` `statusColor()` | offline turns red rather than yellow |

Docs to update: the API contract status table (`:18`, `:28`) and `:235-237`,
which says "BUSY" about the offline case, plus the code comment at
`src/MCP45HVX1.cpp:1198-1200`. Tests: one string assertion
(`test_basic.cpp:1953`) and four `Err::BUSY` code assertions (1956, 1959, 1962,
1970).

**`_isAmbiguousStateWriteFailure()` must list `OFFLINE` among the non-ambiguous
codes**, exactly as `BUSY` is today (`src/MCP45HVX1.cpp:1355-1373`, exclusion
list at 1361-1368). This was
confirmed by execution, not just by reading: the four General Call helpers
(`src/MCP45HVX1.cpp:707, 725, 742, 759`) pass their status through the
classifier, and `_generalCallWrite()` returns `_offlineStatus()` at
`1205-1207`. The classifier is a pure function of `st.code` with
`default: return true` — it cannot see that no bus traffic occurred — so an
unlisted `OFFLINE` would set `hardwareStateUncertain()` on a call that issued
zero writes. That is precisely why `BUSY` sits in the exclusion list today.
(The other four call sites gate OFFLINE before the transport call and are
unaffected.)

## 4. `pollJob()` returns `OK` when no job was started

**Severity: Medium.** `src/MCP45HVX1.cpp:235-240`, `pollJob()`

```cpp
if (!_job.snapshot.active) {
  return _job.snapshot.status;   // Status::Ok() when no job ever ran
}
```

Polling before any `start*Job()` returns success, indistinguishable from a job
that completed successfully. Confirmed by execution both before and after
`begin()`. The initial state is unambiguous: `JobSnapshot`
(`MCP45HVX1.h:59-65`) default-initialises `type = JobType::None`,
`active = false`, `status = Status::Ok()`, and `_resetJob()`
(`src/MCP45HVX1.cpp:970-973`) restores exactly that from `begin()`, the
`begin()` rollback, and `end()`.

**Proposal.** Return `Status::Error(Err::INVALID_PARAM, "No active job")` when
`_job.snapshot.type == JobType::None`, keeping the "return the completed job's
final status" behavior for every other case. One test. The same code/message
pair already exists at `src/MCP45HVX1.cpp:953-955` in
`_pollJobOneInstruction()`, so this is consistent rather than novel.

The related stale-snapshot half of this problem (a zero-step job leaving the
previous job's result visible) is already fixed in the working tree and covered
by `test_zero_step_job_does_not_publish_a_stale_snapshot()`. The two do not
conflict: after a zero-step job `type != JobType::None`, so the new guard
correctly leaves that path returning `OK`.

## 5. `begin()` writes TCON before the wiper

**Severity: Medium** (output safety, warm restarts only).
`src/MCP45HVX1.cpp:131-143`

With both startup writes enabled, `begin()` writes TCON0 (131-136) and then
Wiper 0 (138+). `restorePowerOnDefaults()` (385-401) had the same order and has
been changed to wiper-first, because its target TCON always reconnects every
terminal — that change is in the working tree but **not yet committed**; HEAD
still writes TCON first there too. `begin()` is genuinely ambiguous, so it was
left alone:

- if `initialTcon` **connects** terminals (`0xFF`, the default) and the device
  is not at POR — a warm restart, an external reset that missed the part —
  TCON-first connects the terminals onto the *stale* wiper code before the
  wiper write lands;
- if `initialTcon` **disconnects** terminals (shutdown, floating wiper),
  TCON-first is the safer order, because the wiper then moves while
  disconnected.

`begin()` does read Wiper 0 and TCON0 first (lines 104 and 110) but never
writes the read-back wiper value, so a warm device really does keep a stale
code. Two qualifications on the hazard, neither of which removes it:

- The first case bites only when the device's **current** TCON is
  disconnecting. If a warm device already holds `0xFF`, the terminals are
  already connected onto the stale code before `begin()` is called, and no
  ordering helps.
- `requirePowerOnDefaults = true` (lines 115-124) aborts unless TCON and the
  wiper are both at POR, so that configuration is immune. The default is
  `false`, so the hazard is live under default config.

**Proposal.** Order by intent rather than by a fixed rule, in `begin()` only:

```cpp
// A TCON that disconnects terminals is applied first so the wiper moves while
// disconnected. A TCON that connects them is applied last so the terminals are
// never connected onto a stale wiper code.
const bool tconDisconnects =
    (sanitizeTcon(_config.initialTcon) & cmd::TCON_IMPLEMENTED_MASK) !=
    cmd::TCON_IMPLEMENTED_MASK;
```

then write TCON first when `tconDisconnects`, otherwise write the wiper first.
`sanitizeTcon()` is a public `static constexpr` member
(`MCP45HVX1.h:504-506`) and `cmd::TCON_IMPLEMENTED_MASK` is `0x0F`
(`CommandTable.h:116-117`), so the expression compiles as written. Using the
0x0F mask rather than `TCON_TERMINAL_MASK` (0x07) is deliberate: it classifies
the software-shutdown preset `0xF7` as disconnecting, which is the desired
side. All five presets classify correctly — `0xFF` connects; `0xF7`, `0xFB`,
`0xFD`, `0xFE` disconnect.

For a *partial* TCON such as rheostat `0xFB` the "wiper moves while
disconnected" rationale does not strictly hold — the wiper still moves through
a connected B-W path — but TCON-first is no worse there than today.

**No existing test needs changing.** All three `writeInitialTcon` tests
(`test/test_basic.cpp:742, 772, 813`) use `initialTcon = 0x07`, which
`sanitizeTcon()` turns into `0xF7`; that is classified as disconnecting, so
they keep the current TCON-first path and their failure-injection skip counts
stay valid. What is needed is a **new** test on the new branch: `initialTcon =
0xFF` with `writeErrorAfterMutationSkip = 1`, asserting the wiper landed and
TCON is the register left uncertain.

State the rule in the API contract next to the `restorePowerOnDefaults()`
sentence.

If you prefer the simplest possible rule instead, wiper-first unconditionally is
never *worse* than today for the connecting case and only marginally worse for
the disconnecting one (the wiper moves while still connected, which is what the
caller asked for anyway).

## 6. `MAX_COMMAND_CHUNK` is a driver constant expressing a transport constraint

**Severity: Low-Med** (design). `include/MCP45HVX1/CommandTable.h:137-143`

The datasheet sets no limit on continuous INC/DEC command sequences, so the
64-byte chunk is purely a transport-buffer accommodation. The driver hands up
to 64 bytes to `Config::i2cWrite` with no way to ask the transport whether it
can carry that, so a mismatch is discovered per call at runtime instead of at
configuration time.

**The originally reported failure mode was wrong.** An oversized write against
this repository's own Arduino adapter is *not* misclassified: the adapter
returns `Err::INVALID_PARAM` (`examples/common/I2cTransport.h:81-84`),
`_isAmbiguousStateWriteFailure()` explicitly excludes that code
(`src/MCP45HVX1.cpp:1362`), and `_i2cWriteTracked()` short-circuits it before
`_updateHealth()`, so it is not even counted against device health. This is
asserted by `test/test_basic.cpp:2504-2505`. No `hardwareStateUncertain()`
latch occurs.

Ambiguity arises only for a third-party transport that maps the failure to a
generic code. In the most realistic such case — a Wire that truncates, giving
`written != len` and `Err::I2C_ERROR` — `endTransmission(true)` has already
shipped the partially buffered INC bytes, so the wiper genuinely *has* moved by
an unknown amount and the ambiguous classification is the **correct** answer,
not a defect.

A `static_assert` in the Arduino adapter now catches the buffer mismatch at
compile time (`I2cTransport.h:30-34`; it fired immediately against the test
stub, which had no declared buffer size), and the comment on the constant now
says what the bound is for. Both changes are uncommitted, so nothing in HEAD
catches this yet, and neither helps a third-party transport.

**Proposal.** Make it configurable rather than constant:

```cpp
/// Largest number of command bytes the driver may put in one transaction.
/// Clamped to [1, cmd::MAX_COMMAND_CHUNK]. Set this to the transport's usable
/// buffer size when it is smaller than the default.
uint8_t maxCommandChunk = static_cast<uint8_t>(cmd::MAX_COMMAND_CHUNK);
```

Validate it in `begin()` and use `_config.maxCommandChunk` in **all three**
places that currently hardcode the constant — `_sendWiperStepCommand()`
(`src/MCP45HVX1.cpp:1161-1163`) and *both* chunk planners, `_startStepJob()`
(813-814) and `_pollJobOneInstruction()` (903-905). Missing a planner would
leave `instructionsPlanned` inconsistent with the actual poll count. Keep
`cmd::MAX_COMMAND_CHUNK` as the compile-time upper bound that sizes the stack
buffer at line 1164.

Implementation notes:

- The lower clamp bound matters: `_startStepJob()` computes
  `(steps + chunk - 1U) / chunk` and divides by zero at `chunk == 0`. At
  `chunk == 1` the worst case is 255 planned instructions, which still fits the
  `uint8_t` field.
- Append the field **after `offlineThreshold`** (`Config.h:110`), at the end of
  the struct. Inserting it mid-struct silently shifts every positional
  aggregate initializer.
- The explicit `static_cast` is because `cmd::MAX_COMMAND_CHUNK` is `size_t`;
  the project builds `-std=c++17` with `-Wall -Wextra` and no `-Wconversion`,
  so the narrowing would be silent rather than caught.
- Adding a field changes `sizeof(Config)`. That is an ABI break only for anyone
  linking a prebuilt archive; as a source-distributed library everything
  recompiles together.

## 7. The ESP-IDF transport cannot report `DEVICE_NOT_FOUND`

**Severity: Medium.** `examples/espidf_basic/main/main.cpp:68-82`

`mapI2c()` maps `ESP_ERR_INVALID_RESPONSE` and `ESP_ERR_NOT_FOUND` to
`I2C_NACK_ADDR` (lines 78-79), but `i2c_master_transmit()` and
`i2c_master_transmit_receive()` report an unacknowledged address as
`ESP_ERR_INVALID_STATE`, which falls through to `Err::I2C_BUS` at line 81.

Both arms of the existing mapping are in fact inert: `ESP_ERR_NOT_FOUND` comes
only from `i2c_master_probe()`, which the example compares to `ESP_OK` directly
at line 978 and never routes through `mapI2c()`, and `ESP_ERR_INVALID_RESPONSE`
is not produced by the `esp_driver_i2c` master API at all.

Two consequences, both verified end to end against the driver:

- `Err::DEVICE_NOT_FOUND` is unreachable from `begin()` and `probe()`. Its only
  producer is `_presenceReadFailureStatus()` (`src/MCP45HVX1.cpp:1258-1263`),
  which requires `st.code == Err::I2C_NACK_ADDR`.
- Worse, every write to an absent device is classified as an *ambiguous* state-
  write failure. `_isAmbiguousStateWriteFailure()` (`src/MCP45HVX1.cpp:1355-1373`)
  excludes `DEVICE_NOT_FOUND`, `REGISTER_MISMATCH`, `I2C_NACK_ADDR` and the
  local codes, then `default: return true`. `Err::I2C_BUS` is not excluded, so
  it reaches all eight `_markHardwareStateUncertain()` call sites (668, 708,
  726, 743, 760, 830, 892, 1171) and spuriously latches
  `hardwareStateUncertain()`.

The equivalent Arduino defect is fixed in the current working tree but not yet
committed (`examples/common/I2cTransport.h:134-140`: a zero-byte
`requestFrom()` is now reported as `I2C_NACK_ADDR`). The ESP-IDF side needs the
same treatment, but the exact `esp_err_t` differs by IDF version, so it should
be confirmed against the pinned v6.0.1 on hardware rather than changed blind.

**Proposal.** Add the mapping — but scope it to the transfer call, not to
`mapI2c()` as a whole:

```c
// Only for the result of i2c_master_transmit / _transmit_receive / _receive.
if (err == ESP_ERR_INVALID_STATE) {
  return Status::Error(Err::I2C_NACK_ADDR, msg, err);
}
```

A blanket `INVALID_STATE -> I2C_NACK_ADDR` inside `mapI2c()` would be wrong:
`i2cWrite()` (158-162) and `i2cWriteRead()` (172-180) also funnel
`ensureDevice()`'s result through it, and `i2c_master_bus_add_device()` plus
the explicit guards at lines 86 and 113 return `ESP_ERR_INVALID_STATE` for a
device-registration failure that is not an address NACK. Either split the
mapping into a transfer-specific helper or pass a flag.

Confirm with a deliberate probe of an empty address on the bench; the CLI's
`scan` command makes that a one-line check.

## 8. The Arduino CLI does not verify its baseline restore

**Severity: Medium** (output safety).
`examples/01_basic_bringup_cli/main.cpp:1819-1836`

After `selftest output` or `stress_mix`, the Arduino CLI restores the baseline
Wiper/TCON, checks only that the two writes were ACKed, and reports the restore
as passed (`main.cpp:1777-1786` and `2132-2135`). The ESP-IDF example does it
properly (`examples/espidf_basic/main/main.cpp:1305-1339`): it re-reads the
snapshot, compares both registers against the baseline, and sets or clears
`gOutputStateUncertain` from the result.

On a part where `WLAT` or `SHDN` can hold the physical output, a write ACK is
specifically the evidence that does *not* establish the restore.

**Proposal.** Port the ESP-IDF verification block into the Arduino
`restoreSnapshot()` — read back, compare both registers, leave
`gOutputStateUncertain` set when the comparison fails, and **clear it on
success**, which the Arduino version never does today. Roughly fifteen lines;
`readSnapshot()` and `Err::REGISTER_MISMATCH` are already available to the
example and `health_view::errName()` already renders the latter
(`examples/common/HealthView.h:79`). This makes the `dirty=` field (just
repaired to report the CLI's own flag) mean something.

This is the same defect as drift (b) in finding 9; fixing it closes both.

## 9. Two CLI implementations kept in sync by a Python contract

**Severity: Medium** (structural). ~4,300 lines across two files.

| Component | Lines (HEAD) |
|---|---:|
| Library (`include/` + `src/`, all six files) | 2,585 |
| Arduino CLI example | 2,291 |
| ESP-IDF CLI example | 2,017 |
| CLI contract checkers | 783 |

The two examples together are about 1.7x the size of the library they
demonstrate — each one alone is slightly smaller than it — and 783 lines of
Python (`check_cli_contract.py` 403 + `check_idf_example_contract.py` 380 at
HEAD; 791 in the working tree) exist largely to assert that two hand-maintained
copies of the same CLI still agree. Part of that Python checks single-example properties that would survive
any restructuring (README tokens, HIL-runner coverage, the "raw ANSI only in
`Log.h`" rule, General Call subcommand coverage), so it is not 783 lines of
pure duplication tax.

The review found the drift that arrangement produces:

| # | Drift | State |
|---|---|---|
| a | `verbose` no-argument semantics differed | **Fixed** (working tree) |
| b | Restore verification exists in one copy only | Open — see finding 8 |
| c | The `dirty=` field lost its meaning in one copy | **Fixed** (working tree) |
| d | `requireNoArgs` coverage differs | Open |
| e | The two `mode` parsers accept different long-form aliases | Open, cosmetic |

Detail on the two open ones:

**(d) is the serious one, and was understated.** Seven command groups are
argument-guarded in the Arduino CLI and unguarded in the ESP-IDF CLI:
`version`/`ver`, `scan`, `info`, `errata`, and — output-changing — `zero`,
`mid`, `max`. Nothing is guarded in the IDF that is not guarded in the Arduino.
Beyond `requireNoArgs`, the Arduino also rejects trailing tokens throughout its
handlers via `noMoreArgs()` (47 call sites); the ESP-IDF CLI has no equivalent,
so `addr`, `res`, `rab`, `frac`, `shutdown`, `mode`, `inc`/`dec` and `stress`
all silently ignore junk after the first value.

**(e) is real but has no user-visible effect.** The two parsers share `pot`,
`bw`, `aw`, `float`, `shutdown` and the long form `potentiometer`; they diverge
on `rheostat-bw`/`rheostat-aw` versus `rheostat_bw`/`rheostat_aw`, and
`wiper-floating` versus `floating`, and the Arduino additionally accepts `b-w`
and `a-w`. The help text is byte-identical in both and advertises only
`pot|bw|aw|float|shutdown` — all five of which both parsers accept — so every
divergent spelling is undocumented on both sides.

**Proposal**, in increasing order of effort:

1. **Reconcile the divergences now** and extend the contract checkers to the
   behavior the help tables do not encode: argument aliases and no-argument
   enforcement. Note that the obvious version of this step is already done —
   `tools/check_idf_example_contract.py:223-227` already compares the two
   `COMMAND_HELP` tables against *each other*, row by row across seven fields.
   That is exactly why the `mode` help text is byte-identical while the parsers
   diverge, and it is the sharpest evidence for the finding: a table-level
   contract cannot see inside a parser.
2. **Extract the framework-independent half** — command table, argument
   parsers, output formatting — into a header written in plain C++17 with
   `snprintf` and a `void (*write)(const char*)` sink. Both examples keep their
   own I/O and transport.

   This does not merely need to clear the "no Arduino facades" rule (it does:
   such a header is not Arduino code, and `IDF_FORBIDDEN_RE` bans only the
   named Arduino tokens). It **reverses two written rules** and needs an
   owner's decision first:

   - `AGENTS.md:18-26` and `docs/IDF_PORT.md:17-23` both state that the two
     examples "share a command contract, **not implementation source**."
   - `AGENTS.md` also defines `examples/common/` as holding **Arduino-only**
     helpers. That is accurate today: all eight headers there except
     `BuildConfig.h` include `<Arduino.h>` or `<Wire.h>`, and
     `check_cli_contract.py:11-20` hard-requires all eight names. The directory
     would have to be split into Arduino-only and framework-neutral halves.
3. **Ship one example.** A library this size does not need two 2,000-line
   diagnostic shells. A small ESP-IDF example proving the component builds and
   talks to the part, plus the full Arduino CLI, would cover both audiences.

Option 2 is the best value if the architecture rule is revisited; option 1 is
worth doing regardless and does not require that decision.

## 10. `_updateHealth()` and `_recordFailure()` are near-duplicates

**Severity: Low** (refactor). `src/MCP45HVX1.cpp:1430-1497`

The failure half of `_updateHealth()` (1438-1440, 1452-1467) and the body of
`_recordFailure()` (1479-1481, 1483-1496) are **17 character-identical lines**:
stamp `_lastError`/`_lastErrorMs`, saturate `_totalFailures` and
`_consecutiveFailures`, and pick DEGRADED or OFFLINE against the threshold.

They differ in four ways, not one:

1. `_updateHealth()` guards on `!_initialized` (1431); `_recordFailure()` has
   no such guard and will mutate health state on an un-`begin()`-ed driver.
2. `_updateHealth()` early-returns only on `inProgress()`; `_recordFailure()`
   returns on `ok() || inProgress()`.
3. `_updateHealth()` owns the whole success path (1442-1450) - `_lastOkMs`,
   `_totalSuccess`, reset `_consecutiveFailures`, `_driverState = READY`.
   `_recordFailure()` has none of it.
4. `_recordFailure()` filters the four locally generated codes
   `INVALID_CONFIG`, `INVALID_PARAM`, `NOT_INITIALIZED`, `UNSUPPORTED`
   (1474-1476). At its four current call sites (1077, 1083, 1105, 1113) every
   status is `REGISTER_MISMATCH`, so that filter is inert today.

**Proposal.** Give `_recordFailure()` the filter and the counting, and have
`_updateHealth()` delegate:

```cpp
Status MCP45HVX1::_updateHealth(const Status& st) {
  if (!_initialized || st.inProgress()) return st;
  if (st.ok()) { /* success path, unchanged */ }
  return _recordFailure(st);
}
```

**Two behavioral changes fall out, both improvements.** A transport callback
(`i2cWrite` / `i2cWriteRead`) that returns `UNSUPPORTED` *or* `NOT_INITIALIZED`
would stop counting against device health. `UNSUPPORTED` is filtered today only
in `_busResetTracked()` (1023); `NOT_INITIALIZED` is filtered at no call site
at all, and `_updateHealth()`'s own `!_initialized` guard does not catch it -
that guard blocks the driver's own guard-clause codes, not a callback's return
value.

`INVALID_CONFIG`/`INVALID_PARAM` are already filtered at every failure-reaching
call site, so those are unchanged. Once the filter is centralised, **five**
call-site filters become redundant and can be deleted, not just
`_i2cWriteTracked()`'s: lines 1011-1013, 1022-1025, 1070-1072, 1098-1100, and
1214-1216. That leaves one place that decides what a health failure is.

## 11. No helper for the minimum safe wiper code

**Severity: Low** (missing feature, but this is a high-voltage part).

DS20005304B Table 5-3 (page 42) specifies a minimum wiper code below which the
wiper current exceeds specification in a rheostat configuration at
`VBW` = 36 V:

| RAB | Min `N` 8-bit | Min `N` 7-bit |
|---|---:|---:|
| 5 k | 91 | 45 |
| 10 k | 91 | 45 |
| 50 k | 35 | 17 |
| 100 k | 17 | 8 |

These values are verified against the datasheet and match
`docs/DEVICE_REFERENCE.md:243-246`. The library exposes
`maxTerminalCurrentMilliAmps()` (25 / 12.5 / 6.5 / 6.5 mA) but nothing that
helps a caller stay inside it. This is the one datasheet limit that a digital
command can violate directly.

**Proposal.** Add a static helper next to the other resistance math
(declaration beside `stepResistanceOhms()` at `MCP45HVX1.h:412`, definition at
`src/MCP45HVX1.cpp:466`):

```cpp
/// Lowest wiper code whose worst-case R_BW keeps terminal current within the
/// datasheet limit for a given B-to-W voltage.
///
/// Unlike every other resistance helper in this class, this one deliberately
/// assumes R_AB(MIN) - 20% below nominal - because Table 5-3 does. Using the
/// nominal step resistance here would return codes below the datasheet
/// minimum. DS20005304B Table 5-3.
static uint8_t minSafeWiperCode(float voltageBToW, ResistanceOption option,
                                Resolution resolution);
```

**The formula must use worst-case, not nominal, step resistance:**

```
RS_MIN = 0.8 * nominalResistanceOhms(option) / maxWiperCode(resolution)
N_min  = floor(voltageBToW / (maxTerminalCurrentMilliAmps(option) * RS_MIN))
```

Table 5-3 is computed from `RS(MIN)`, and page 42 states plainly that "the
application must assume that the RAB resistance is the minimum RAB value". All
eight rows reproduce exactly with the expression above. The `RS(MIN)` values
are 15.686 / 31.373 / 156.863 / 313.725 ohm (8-bit) and 31.496 / 62.992 /
314.961 / 629.921 ohm (7-bit), matching the datasheet's own column.

Computing this from the library's nominal `stepResistanceOhms()` instead -
`ceil(V / (Imax * RS_nominal))`, the obvious first guess - yields 74/74/29/15
(8-bit) and 37/37/15/8 (7-bit), which is **below the datasheet minimum in seven
of the eight cells and therefore unsafe**. A 5 k part at `RAB(MIN)` driven at
code 74 gives `R_BW` = 1161 ohm and 31 mA against a 25 mA limit.

One decision to make: Table 5-3 truncates rather than rounds up (its own Note 3
says "rounded up", which is a datasheet inconsistency). `floor` reproduces all
eight published rows verbatim; `ceil` would be one code safer but would fail a
test written as "reproduces Table 5-3". Recommend `floor` plus the test, since
matching the published table is the auditable property.

Clamp the result to the code range, and let the CLI warn before a `wiper 0` on
a live 36 V rheostat.

## 12. The poll-job state machine is largely untested

**Severity: Medium** (coverage).

The suite is at **78 cases and all 78 pass** (verified by a native
`g++ -std=c++17` build against the vendored Unity and `src/MCP45HVX1.cpp`,
clean under `-Wall -Wextra -Werror=return-type`). All job-API usage is confined
to `test_basic.cpp:1141-1354` and `2339-2398`. Every one of the following was
re-checked call site by call site and confirmed never exercised:

- `JobType::DecrementWiper` executing at all - it appears only at line 2347
  (`NOT_INITIALIZED`) and line 2396 (`startDecrementWiperJob(0)`, which returns
  before `_startJob`, so `pollJob()` is never called and the
  `case JobType::DecrementWiper:` arm never runs)
- `ReadSnapshot` failing at either phase
- `SetTerminal` failing on the phase-1 write, including the uncertainty marking
- `Recover` failing at phase 1 with the OFFLINE re-latch - the existing
  `test_recover_job_keeps_offline_latch_until_full_readback` (1343) drives both
  phases to *success*
- any `start*Job()` returning `BUSY` because another job is active - the only
  BUSY assertions in the job tests are on `dev.readWiper()` (1146, 1351)
- any `start*Job()` returning offline-`BUSY`;
  `test_offline_blocks_normal_operations_without_bus_io` calls no `start*Job()`
  at all
- `startSetWiperJob()` rejecting an out-of-resolution code - no 7-bit driver
  ever starts a wiper job, so that error is unreachable in test
- `probe()` / General Call rejected while a job is active
- `end()` called with a job active

Three of these (ReadSnapshot phase failure, Recover re-latch, SetTerminal write
failure) have **synchronous twins that are tested**, so the risk is drift
between the blocking and job implementations of duplicated logic rather than
wholly unverified behaviour.

**Proposal.** One table-driven test over
`{SetWiper, ReadSnapshot, SetTerminal, Increment, Decrement, Recover}` x
`{busy-reject, offline-reject, fail-at-each-phase}`, using the existing
`failReadCall` (`test_basic.cpp:64`) and `writeErrorAfterMutationRemaining`
(`:61`) hooks. That is one new test function covering most of the list above,
and it is where the largest untested surface in the driver currently sits.

Also worth folding in while there: `begin()` never tests rejection of an
invalid `Config::resolution` or of an out-of-range `initialWiperCode` - both
confirmed absent - and **none** of the four `resetAfterFailedBegin` paths
(`src/MCP45HVX1.cpp:106, 112, 117, 121`) asserts that the rollback cleared
config, cache, and counters. The one test that does assert full rollback,
`test_failed_begin_clears_stale_runtime_snapshot`, sets `i2cWrite = nullptr`
and so exercises the config-validation early return at line 78 instead; the
TCON-read failure path at line 112 is unreached by any test. The rollback is
behaviourally identical either way, so this is a coverage gap rather than a
latent bug.

## 13. The Wire test stub does not model the protocol details that matter

**Severity: Low** (test quality). `test/stubs/Wire.h` (98 lines)

The stub now declares `I2C_BUFFER_LENGTH 64`, sizes both buffers from it, and
returns Arduino's `1` ("data too long") on overflow - all uncommitted. Note
that the overflow branch is currently dead through the transport, because
`wireWrite()` rejects `len > WIRE_BUFFER_LIMIT` with `INVALID_PARAM` before
Wire is touched. Three gaps remain:

- `endTransmission(bool stop)` **discards `stop`** (`(void)stop;`).
  Repeated-start versus STOP is the one framing detail that decides whether a
  register read works on real silicon, and no test can observe it.
- `_setReadData()` does not set `_rxLen`; the length comes only from
  `requestFrom()`. Staging two bytes and requesting four yields
  `available() == 4` with two bytes of residue - and because `TwoWire Wire;` is
  a file-scope global (`test_basic.cpp:15`) never reset between tests, that
  residue is genuinely stale data from an earlier test, not zeros.
- `_addr` is captured in `beginTransmission()` but is private with no accessor,
  so no test can assert that a General Call went to `0x00` and a register write
  to `0x3C`. Worse, `requestFrom(uint8_t addr, size_t len)` opens with
  `(void)addr;` - the **read-phase address is discarded outright**, so an
  accessor on `_addr` alone would still not let a test check the read phase.

**Proposal.** Record `stop`, capture the address in *both* phases, expose
`lastStop()` and `lastAddress()`, and have `_setReadData()` bound the readable
length. Then add one test asserting that a random read issues the write phase
with `stop == false` followed by the read phase.

That test closes a real execution gap, not just an assertion gap. The existing
`test_example_transport_maps_wire_errors_and_read_only_transactions` calls
`wireWriteRead()` only with `txLen == 0` (four times), so the `if (txLen > 0)`
branch never runs - the transport's entire repeated-start write phase,
including its `endTransmission(false)` and the
`mapWireResult(result, "I2C write phase failed")` error return, is currently
**unexecuted by the suite**.

## 14. The contract checkers have dead and fragile checks

**Severity: Low** (tooling). `tools/check_cli_contract.py`, `check_idf_example_contract.py`, `check_core_timing_guard.py`, `run_hil_mcp45hvx1.py`

The `actions/upload-artifact` version pin has been removed already, since it
was blocking a legitimate CI update: `ci.yml:165` is now `@v7`, and
`check_idf_example_contract.py:62-66` replaced the exact-major token with a
`CI_REQUIRED_PATTERNS` regex. Remaining, each reproduced by running the tool
against a scratch copy:

- **A check that cannot fail.** `check_cli_contract.py:316-317` requires the
  CLI source to contain `cfg` or `settings`. Both are already in
  `MANDATORY_COMMANDS` (lines 40-41), enforced at line 250 by a loop that
  requires the literal `strcmp(command, "cfg") == 0` to appear. Any text that
  reaches line 316 has therefore already been proven to contain `cfg`. Dead by
  construction — delete it.

- **Two unguarded searches.** `check_cli_contract.py:311` uses
  `text[text.find("void handleGeneralCall"):]`; on a rename `find` returns
  `-1`, `text[-1:]` is one character, and the loop reports a "missing handler
  branch" failure whose message names the wrong problem. In practice this is
  reachable only by a *partial* rename: a plain rename trips the
  `COMMAND_ACTIONS` token check at line 304 first, which reports accurately.
  And because `fail()` raises `SystemExit`, exactly **one** misleading failure
  is printed, not six.

  `check_idf_example_contract.py:268` uses `code_only.index("void handleCommand")`
  for the same technique on a different symbol, which raises an uncaught
  `ValueError` and prints a Python traceback in CI — verified by renaming the
  symbol in a scratch copy. Both should test the result and `fail()` with the
  real reason.

- **An ignored exit code.** `check_cli_contract.py:354-355` runs the IDF checker
  via `runpy` and discards `main()`'s return value. It works today only because
  every failure path in that module raises `SystemExit` (its sole `raise` is
  inside `fail()`). Patching its tail to `return 1` in a scratch copy makes the
  wrapper print `CLI contract PASSED` and exit 0. This is not hypothetical
  style drift: `check_core_timing_guard.py:144` already uses `return 1` rather
  than `fail()`, so both conventions coexist in this repo. Capture and
  propagate the value, and move the shared constants into a small
  `contract_common.py` instead of each module `runpy`-ing the other.

- **Dead allow-list machinery.** `check_core_timing_guard.py:37-38` declares
  `ALLOWED_CALL_COUNTS` and `ALLOWED_INCLUDE_COUNTS`, both permanently empty.
  26 lines are strictly unreachable (99-105, 107-117, 126-131, plus the two
  declarations); the surrounding allow-list-shaped region 95-131 still runs but
  collapses to exactly the rule below. The call path's message (line 97) is
  accurate; the include path (121-124) phrases the same thing as a count
  mismatch (`observed=1, expected=0`). Delete the machinery and state the rule
  plainly: any forbidden call in `include/` or `src/` is an error. Keep the
  `Dict` import — it is still used by the `observed_*` annotations.

- **Comment stripping in the wrong order.** `check_core_timing_guard.py:41-44`
  strips block comments, then line comments, then string literals, so a string
  containing `//` or `/*` swallows what follows — a false *negative* for
  exactly the `millis()`/`Serial` usage the guard exists to catch. Both cases
  reproduce against the real function:

  ```
  IN : const char* kUrl = "https://x/y"; uint32_t t = millis();
  OUT: const char* kUrl = "https:            -> millis() missed

  IN : const char* kPat = "/*"; ... Serial.println(1); ... "*/";
  OUT: const char* kPat = "";                -> Serial missed
  ```

  The `/*` case is worse than a line: `BLOCK_COMMENT_RE` uses `re.DOTALL`, so
  it swallows everything up to the next `*/` across many lines. Strip strings
  first. The identical ordering bug is in
  `check_idf_example_contract.py:126-131` and must be fixed there too. No such
  string exists in `include/` or `src/` today, so this is a latent trap rather
  than a live miss.

- **`command_failed()` heuristic.** `run_hil_mcp45hvx1.py:238` treats any output
  containing `Usage:` as a failure unless the command is exactly `help` or `?`.
  The safe plan sends `help wiper`, `? health`, and `help gc` (lines 33-39),
  none of which match the exemption — confirmed by running the function:

  ```
  'help'       + 'Usage: gc arm' -> False
  'help wiper' + 'Usage: gc arm' -> True    <- false failure
  '? health'   + 'Usage: gc arm' -> True
  'help gc'    + 'Usage: gc arm' -> True
  ```

  Compare `command.split()[0]` instead. The heuristic is **inert against both
  CLIs today**: the Arduino CLI contains no `Usage:` string at all, and while
  the ESP-IDF CLI has 39, every one is an argument-error path that a help
  command cannot reach (`handleHelp` prints `Usage: help [command]` only when a
  second argument is present, and `printCommandHelp` emits
  `Section:`/`Aliases:`/`Syntax:` and never `Usage:`). It is one
  argument-handling change away from misfiring, and **no test covers it** —
  both `test_run_hil_mcp45hvx1_parser.py:162` and the in-tree self-test at
  `run_hil_mcp45hvx1.py:302` exercise only the exact string `help`.

## 15. `generate_version.py` writes files when imported

**Severity: Low** (tooling). `scripts/generate_version.py:367-372`

```python
if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

result = main([])   # runs on ANY import - and regenerates three tracked files
if result != 0:
    raise SystemExit(result)
```

The `__main__` branch raises, so the unguarded call runs on every non-`__main__`
load. It exists for PlatformIO's `extra_scripts` SCons import
(`platformio.ini:8` and `:87`), but it means any tool that imports or `runpy`s
this module regenerates `include/MCP45HVX1/Version.h`, `idf_component.yml`, and
`Doxyfile` — all three git-tracked. `_sync_outputs()` (line 288) writes only
files whose content differs, so the effect is "rewrites whichever are stale",
not "always writes three". Reproduced: tampering with `Version.h` in a scratch
copy and then calling `runpy.run_path()` on the script silently restored it.

Nothing imports it today — `tools/validate.py:39` and `ci.yml:78` both invoke
it as a subprocess with `check`, which hits the `__main__` branch and writes
nothing. But the repository does use `runpy.run_path` in its checkers
(`check_cli_contract.py:354`, `check_idf_example_contract.py:154`), so it is
one refactor away from a self-mutating CI check.

**Proposal.** Guard it on the SCons environment that is the only legitimate
caller: `if ENV is not None: result = main([])`. `ENV` is set at lines 30-35
from `Import("env")` and is already used this way at lines 41-42, 145, and 155.

## 16. Doxygen "warnings as errors" gates almost nothing

**Severity: Low** (CI). `Doxyfile:19, 54-58`, `.github/workflows/ci.yml:128-140`

The job is named "Build documentation with warnings as errors" and sets
`WARN_AS_ERROR = FAIL_ON_WARNINGS`, but the Doxyfile also sets
`EXTRACT_ALL = YES`, `WARN_IF_UNDOCUMENTED = NO`, and `WARN_NO_PARAMDOC = NO`.
Between them, the two warning classes that would actually catch a regression —
a new undocumented public symbol and a missing `@param` — are disabled. Only
doc *syntax* errors can fail the job.

**The obvious one-line fix does not work.** An earlier draft proposed setting
`WARN_NO_PARAMDOC = YES` and predicted it "should pass as-is". It does pass —
but only because it is a **strict no-op**. Measured with Doxygen 1.15.0 against
this tree, the output is byte-identical to baseline. Doxygen gates paramdoc
warnings behind the other two settings:

| `EXTRACT_ALL` | `WARN_IF_UNDOCUMENTED` | `WARN_NO_PARAMDOC` | paramdoc warnings |
|---|---|---|---:|
| YES | YES | YES | 0 |
| YES | NO | YES | 0 |
| NO | YES | YES | 31 |
| NO | NO | YES | 0 |

So `EXTRACT_ALL = NO` **and** `WARN_IF_UNDOCUMENTED = YES` must both change for
`WARN_NO_PARAMDOC` to do anything at all. The underlying belief about doc
quality was right for the part that matters: all 31 paramdoc warnings are in
`examples/`, and `include/` contributes none.

Measured warning counts for the whole `INPUT` set:

| Config | Diagnostics |
|---|---:|
| As committed | 0 |
| `+ WARN_NO_PARAMDOC = YES` | 0 (no-op) |
| `EXTRACT_ALL = NO` + `WARN_IF_UNDOCUMENTED = YES` | 72 |
| All three | 103 |

Of the 72, only **22 are in `include/`** — the other 50 are in `examples/`.
The `include/` set is tractable: 14 undocumented `SettingsSnapshot` members
(`MCP45HVX1.h:113-117, 120-128`), 7 macros in the *generated* `Version.h`
(which means fixing the template in `scripts/generate_version.py`, not the
header), and `cmd::ALT_MAX_ADDRESS`, which finding 2 deletes anyway.

Three further weaknesses in the gate:

- **`src/` is not in `INPUT` at all**, so every doc comment in
  `src/MCP45HVX1.cpp` — the entire implementation — is unchecked. Adding `src`
  introduces zero new warnings today, so it is free.
- **`INPUT` enumerates `docs/*.md` file by file.** Any newly added doc is
  silently unscanned, and a cross-reference *to* an unscanned doc is a hard
  error. That is exactly how this document broke the CI job when it was added:
  references to `CODE_AUDIT.md` from `docs/DEVICE_REFERENCE.md:62` and
  `docs/MCP45HVX1_API_CONTRACT.md:76` could not resolve. Fixed by adding
  `docs/CODE_AUDIT.md` to `INPUT`; verified `doxygen Doxyfile` now emits zero
  diagnostics.
- **Doxygen is unpinned in CI** (`apt-get install --yes doxygen` on
  `ubuntu-latest`), so the gate's behaviour tracks whatever Ubuntu ships and can
  change with no repository edit.

**Proposal.** Drop `WARN_NO_PARAMDOC = YES` as a standalone change — it buys
nothing. Instead:

1. Add `src` to `INPUT` and pin the Doxygen version in CI. Both are free.
2. If a real gate is wanted, scope it to the public API first: with
   `INPUT = include` plus `EXTRACT_ALL = NO`, `WARN_IF_UNDOCUMENTED = YES`,
   `WARN_NO_PARAMDOC = YES`, the count is **22** — the same 22 `include/`
   diagnostics counted above, since the headers contribute no paramdoc
   warnings. Narrowing `INPUT` also requires dropping or repointing
   `USE_MDFILE_AS_MAINPAGE`, which otherwise adds a spurious 23rd warning.
   Widening the strict gate to the examples costs 103.

## 17. ESP32 builds do not rewrite this repo's `idf_component.yml`

**Severity: Low** (downgraded — the described scenario cannot occur with the
pinned platform). `platformio.ini`, `idf_component.yml`, `scripts/generate_version.py`

`platformio.ini` sets `[platformio] src_dir = .`, so the repository root *is*
the project source directory. The pioarduino platform's
`builder/frameworks/component_manager.py` (confirmed present at the pinned
55.03.311) does everything the original report described: it runs
unconditionally from `arduino.py:495`, backs a manifest up to
`idf_component.yml.orig` (`_create_backup`, line 324), and writes it back
through `yaml.dump()` (`_save_component_yml`, line 382). A round-trip of this
repository's own manifest through that path really does alphabetize the keys
and strip the quoting, turning `version: "1.1.0"` into `version: 1.1.0`.

**But it never reaches this repository's manifest.**
`_get_or_create_component_yml()` checks the **Arduino framework directory
first** and returns as soon as it finds a manifest there:

```python
framework_yml = str(Path(afd) / "idf_component.yml") if afd else ""
if framework_yml and os.path.exists(framework_yml):
    self._create_backup(framework_yml)
    return framework_yml          # returns here; project_src_dir never reached
```

`framework-arduinoespressif32` always ships one, so for `framework = arduino`
the `project_src_dir` fallback is effectively unreachable. The evidence is on
disk: the `.orig` backup and the rewritten manifest are both in the **framework
package**, not the repo —

```
.platformio/packages/framework-arduinoespressif32/idf_component.yml       3873 bytes
.platformio/packages/framework-arduinoespressif32/idf_component.yml.orig  4233 bytes
```

— and there is no `.orig` anywhere in the repository. `git log -p --all --
idf_component.yml` shows only `version: "1.1.0"` and `version: "1.0.0"`; a
`yaml.dump` form has **never** been committed, and the current working tree
file is canonical (quoted, hand-ordered, with only deliberate edits to
`files.exclude`).

So the CI-breaking sequence the original report described is not reachable on
the pinned platform. Whatever produced the original `RuntimeError` was not this
mechanism.

**What is worth keeping.** Both mitigations are cheap and correct, and remain
in place: the version regex accepts either quoting form and reports a clear
"Out of date" instead of a traceback, and `*.orig` is gitignored.

Two corrections to the original write-up, both now applied:

- **The regex had a trailing-newline bug.** `^version:\s*"?[^"\s]+"?\s*$`
  under `re.MULTILINE` greedily consumes the trailing newline when `version:`
  is the file's last line — which is precisely where `yaml.dump` puts it,
  alphabetically last. The sync path therefore stripped the final newline in
  the exact scenario the mitigation existed for. Changed to
  `[^\S\n]*$`; `generate_version.py check` passes.
- **"`python scripts/generate_version.py` restores the canonical file in one
  command" was false.** `_expected_outputs()` derives the expected content
  *from the current file*, so a sync re-quotes only the `version:` line. The
  alphabetized key order, the lost quoting on every other value, and the
  missing trailing newline all survive. The real recovery is
  `git checkout -- idf_component.yml`. Document that, not the sync.

**On the `src_dir = src` proposal: do not do it for this reason.** It was
tested and is mechanically viable — `+<../examples/...>` globs correctly under
PlatformIO 6.1.19, `-Iinclude`/`-Iexamples` still resolve from the project
root, and the native test environment is unaffected because `test_dir` is
independent of `src_dir`. But it fixes nothing, since the framework manifest
takes precedence regardless. It is also mildly *worse* in the fallback case: if
the framework manifest were ever absent, `src_dir = src` would have the
component manager create a new `src/idf_component.yml`, and `library.json`
exports `"src/**"`, so that stray file would ship inside the published library.
With `src_dir = .` any such file lands at the tracked root where it is visible.

If the restructuring is ever done for other reasons, note that all three
`build_src_filter` blocks must change — none survives as written, since
`+<src/**>` would become `src/src/**`. Note also that `+<include/**>` is
decorative in all three environments today: headers are not compilation units
and PlatformIO filters by source extension.

## Verified correct

Recorded so the same ground is not re-covered. Each was re-checked against
DS20005304B in a second, adversarial pass and found right unless noted.

- **Resistance math.** `RS = RAB/255` (8-bit) and `RAB/127` (7-bit), per
  Equation 5-2, Equation 5-4, the DC characteristics table, and Equation B-1.
  Not 256/128. `stepResistanceOhms()`, `resistanceBToWOhms()`, and
  `resistanceAToWOhms()` (`src/MCP45HVX1.cpp:466-479`) implement the simplified
  model exactly, and the datasheet's own equations correctly exclude the wiper
  resistance `RW`. Checked numerically: `R_BW + R_AW == RAB` at every code, and
  the endpoints are exact.
- **Command byte encoding.** `AD3:AD0 | C1:C0 | D9:D8`, giving `0x00`/`0x04`/
  `0x08`/`0x0C` for Wiper 0 and `0x40`/`0x4C` for TCON0. Figure 7-1 confirms
  `00 = Write, 01 = Increment, 10 = Decrement, 11 = Read`, so `0x04` is
  increment and `0x08` decrement as the driver assumes.
- **Read framing.** Two bytes, first always `0x00`, data in the second — §7.5
  and Table 7-2 note 3 apply this to TCON0 as well as Wiper 0, so
  `requireReadMsbZero = true` is a correct default. Figure 7-4 confirms the
  last-address read takes no write phase, which is what the zero-length TX
  phase at `src/MCP45HVX1.cpp:1095` implements.
- **TCON0.** Register 4-1: bits 7:4 are `R-1` "forced to 1"; `R0HW` is
  1 = normal / 0 = shutdown; `R0A`/`R0W`/`R0B` are 1 = connected; POR `0xFF`.
  All five driver presets were confirmed by running the real driver against a
  simulated device — `0xFF`, `0xFB`, `0xFE`, `0xFD`, `0xF7` — and all five
  round-trip through `setTerminalMode()` -> `decodeTcon()` under
  `test_terminal_mode_and_terminal_bits_round_trip()` (`test_basic.cpp:2430`).
- **Wiper limits.** `0xFF`/`0x7F` maxima, `0x7F`/`0x3F` POR defaults, and
  increment/decrement saturating rather than wrapping (Tables 7-4 and 7-5).
  The cache prediction saturates identically via `boundedAdd`/`boundedSub`
  (`src/MCP45HVX1.cpp:16-23`, used at `1176-1181`); verified empirically that
  the cache still matches the device after over-increment and over-decrement in
  both resolutions, including a multi-chunk burst that saturates mid-sequence.
  The datasheet's own `100h`/`80h` prose in §7.6 contradicts its own Table 7-4
  and is a known error inherited from the 257-tap family.
- **Terminal current limits.** 25 / 12.5 / 6.5 / 6.5 mA for `-502`/`-103`/
  `-503`/`-104`, matching both the Absolute Maximum Ratings and the electrical
  table.
- **General Call encodings.** `0x80`, `0xC0`, `0x84`, `0x88`, matching
  Table 6-3 and Figure 6-11, sent to `0x00`, one command per control byte
  (enforced by `_generalCallWrite()`'s `len > 1` rejection). Note there is no
  MCP45HVX1 reset General Call: `0000 011` is an I2C-spec reserved command the
  part NAKs, and the driver correctly does not offer one.
- **Chunk planning arithmetic** — correct, but **not test-covered**. An earlier
  draft of this document claimed it was "verified at steps in {1, 63, 64, 65,
  128, 129, 192, 255}" by test. No such test exists and none ever did.
  Committed coverage is a single 66-step job
  (`test_job_step_sequence_runs_one_chunk_per_poll()`, `test_basic.cpp:1259`)
  plus a 130-step failure case at `:1295`. The arithmetic itself was checked by
  driving the real driver against a simulated device at all eight values, where
  `_startStepJob()`'s `(steps + 63) / 64` (`src/MCP45HVX1.cpp:813-814`) agrees
  with the actual poll count from `_pollJobOneInstruction()` (`:903-905`)
  exactly. Adding that table-driven test belongs with finding 12.
- **Core code quality.** `src/MCP45HVX1.cpp` compiles clean under
  `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
  -Wold-style-cast -Wdouble-promotion -Wfloat-equal -Wnull-dereference
  -Wcast-qual`. Re-run and confirmed silent on GCC 15.2 (MinGW-W64 UCRT), both
  `-fsyntax-only` and `-O2 -c` so that `-Wnull-dereference` actually runs its
  analysis. Not verified against clang, which is not installed here.
- **Test suite.** 78 cases, all passing, built natively against the vendored
  Unity with `-Wall -Wextra -Werror=return-type` and no warnings.
- **The HIL runner is not tied to one machine.** `--port` has no default and is
  required except in `--dry-run` and `--parser-self-test`, the two modes that
  open no serial port; board, environment, and fixture notes are all arguments
  defaulting to `""`; `pyserial` is imported lazily inside `import_serial()`.
  No hardcoded port or absolute path appears in the file. Only the committed
  session reports were machine-specific, and those have been removed.
