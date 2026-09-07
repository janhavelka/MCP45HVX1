# Independent Reverification Of The Audit Resolution

Reviewed on 2026-09-06, starting with a clean tree at
`e59b267bc50aa8d5e71cb54907507d3d3fb88f76`. This checks the seven supplied
follow-up claims and all 17 dispositions in [the resolution](CODE_AUDIT_RESOLUTION.md)
against implementation and executed tests. [The original audit](CODE_AUDIT.md)
remains a historical snapshot. Source line numbers in the findings below refer
to **e59b267**, unless explicitly marked as the corrected tree.

No device was flashed, serial port opened, or analog output driven. The
evidence establishes software behavior and source-level reasoning only.

## Follow-up findings, before corrections

| Claim | Verdict | Reproduced issue |
|---|---|---|
| 1 | VALID | The bundled-CLI HIL address-NACK expectation cannot be satisfied by either supplied adapter. |
| 2 | VALID | A continued line comment hides a real `Serial` call from the timing guard. |
| 3 | PARTIALLY-VALID | IDF midpoint output lacks assertions and the migration wording is incomplete; the actual values already agree. Different expressions are not a runtime defect. |
| 4 | VALID | The Wire include condition is redundant. The requested sweep found three more redundant conditions. |
| 5 | VALID | The strict API gate depends on the broad manual's warning policy. It works at HEAD but can be silently weakened. |
| 6 | VALID | The fallible-operation convention alone does not justify rejecting a value-returning math helper. The other reasons still justify omission. |
| 7 | VALID | Neither the numeric OFFLINE value nor polling a completed zero-step job is pinned by the original tests. |

### Claim 1: transport-specific HIL expectations

`src/MCP45HVX1.cpp:1250` contains the only presence-error conversion:

```cpp
if (st.code == Err::I2C_NACK_ADDR) {
  return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
}
return st;
```

Command and actual output:

```text
rg -n I2C_NACK_ADDR examples/common/I2cTransport.h examples/espidf_basic/main/main.cpp
examples/espidf_basic/main/main.cpp:543:    case MCP45HVX1::Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
```

That sole occurrence is a display mapping, not a producer. Wire result 2 is
`I2C_ERROR` at `examples/common/I2cTransport.h:52`; read-length mismatch is
`I2C_ERROR` with the returned byte count at line 140. IDF
`ESP_ERR_INVALID_RESPONSE` becomes `I2C_ERROR` with native detail at
`examples/espidf_basic/main/main.cpp:94`. The API contract already explains
this at lines 44-56, contradicting hardware-validation line 161.

The rest of the fault table correctly requires status preservation and
uncertainty, but needed clarification that an injected data NACK or timeout
need not have a phase-specific software code. In particular, Wire's read
detail cannot preserve a backend error it never receives. No other
unconditional `DEVICE_NOT_FOUND` expectation was found in the HIL document.

### Claim 2: continued line comment false negative

At `tools/contract_common.py:10`, the comment alternative is `//[^\n]*`.
The following was run unchanged against HEAD, then placed in `src/fault.cpp`
inside a `git archive HEAD` scratch copy:

```python
src = '// note \\\n"\nSerial.println(1);\nconst char* s = "x";\n'
print(repr(strip_non_code(src)))
print('Serial visible:', 'Serial' in strip_non_code(src))
```

Actual output before correction:

```text
'         \n \n                  \n                 x";\n'
Serial visible: False
python tools/check_core_timing_guard.py
Core timing guard PASSED
exit=0
```

C++ line continuation keeps the lone quote inside the comment. The guard
instead starts a false string at that quote and consumes the real call.
This reproduces an incomplete fix of original finding 14.

### Claim 3: midpoint values, history, and missing assertion

`git show 1faad98:<path>` and `git show HEAD:<path>` produced:

```text
1faad98:examples/01_basic_bringup_cli/main.cpp:2211:
  const MCP45HVX1::Status st = gDev.writeWiperFraction(0.5f);
1faad98:examples/espidf_basic/main/main.cpp:1772:
  const uint8_t v = (strcmp(cmd, "zero") == 0) ? 0U : (strcmp(cmd, "mid") == 0 ? max / 2U : max);
HEAD:examples/01_basic_bringup_cli/main.cpp:2197:
  const MCP45HVX1::Status st = gDev.writeWiper(porWiperCode(gConfig.resolution));
HEAD:examples/espidf_basic/main/main.cpp:1740:
  const uint8_t v = (strcmp(cmd, "zero") == 0) ? 0U : (strcmp(cmd, "mid") == 0 ? max / 2U : max);
```

The current expressions both yield 127/63. Arduino tests assert actual writes
of `0x7F` and `0x3F` at `tests/test_arduino_cli.cpp:148` and 152. IDF's only
midpoint assertion is `assert(commands.back() == "mid");` at
`tests/test_idf_console.cpp:126`, which verifies input assembly only.
`tools/test_contract_tools.py:33` extracts parsers, input, and error mapping;
it does not extract the preset calculation. Thus the missing assertion is
real, but changing the correct IDF runtime expression is unnecessary.

The changelog's statement that both examples now select POR codes is true as
a description of the resulting behavior. It is misleading as change history
and omits the Arduino output change from `0x80`/`0x40`. The historical code
above confirms that only Arduino changed. Fractional rounding remains in
`src/MCP45HVX1.cpp:445` and is separately tested.

### Claim 4: redundant conditions

`tools/check_core_timing_guard.py:72` inserts into `observed_wire_includes`
only when `wire_include_count > 0`. Its line 92 condition `if count != 0:`
therefore has no false branch for any collected entry. The adjacent include
loop already appends its diagnostic without that condition.

The sweep read all `tools/check_*.py` and `tools/contract_common.py` branches.
It also established:

- IDF line 213 checks `if cmd in dispatch:` after every key has already been
  required by the mandatory-command loop. Running
  `sorted(set(idf.IDF_COMMAND_ACTIONS) - set(MANDATORY_COMMANDS))` prints `[]`.
- IDF line 301 checks `if "selftest output" in text:` after line 276 already
  requires the longer `printWarning("selftest output` token. Its missing-help
  failure is unreachable; the behavioral token checks remain necessary.
- Shared helper line 186 skips empty command names. Every current caller
  uses `parse_command_help_specs()`, whose canonical-name regex requires
  at least one character; `aliases.split()` emits no empty strings. The skip
  has no caller that can reach it. Duplicate-name rejection is still needed.

These are redundant tests under established invariants. No claim is made
that every unexercised branch is dead: missing files, malformed source,
subprocess failures, and absent optional text can reach other guards.

### Claim 5: Doxygen inheritance

`Doxyfile.api:2` says `@INCLUDE = Doxyfile` and has no `WARN_AS_ERROR` setting;
`Doxyfile:57` supplies `FAIL_ON_WARNINGS`. Fresh archives were created with
`git archive HEAD` and extracted with Python `tarfile`. Each invocation
started without `.pio`.

Adding `void auditUndocumented();` immediately after the public `begin()`
declaration reproduces the dependency:

```text
doxygen Doxyfile.api                  # unchanged parent
error: Member auditUndocumented() ... is not documented.
exit=1
doxygen Doxyfile.api                  # parent WARN_AS_ERROR = NO
warning: Member auditUndocumented() ... is not documented.
exit=0
```

The current gate is effective; the reported defect is its reliance on an
unrelated inherited policy, not a failure to detect documentation today.

### Claim 6: helper rejection rationale

`AGENTS.md:27` requires "Every fallible public operation returns `Status`."
It does not require every helper to return `Status`. Header declarations
confirmed with `rg -n` include:

```text
include/MCP45HVX1/MCP45HVX1.h:366:  static uint8_t codeFromFraction(float fraction, Resolution resolution);
include/MCP45HVX1/MCP45HVX1.h:389:  static constexpr uint8_t maxWiperCode(Resolution resolution) {
include/MCP45HVX1/MCP45HVX1.h:415:  static float stepResistanceOhms(ResistanceOption option, Resolution resolution);
include/MCP45HVX1/MCP45HVX1.h:420:  static constexpr float maxTerminalCurrentMilliAmps(ResistanceOption option) {
```

The other cited resistance/default/fraction helpers likewise return plain
values. A new helper still needs a way to represent invalid voltage and an
infeasible result, and there is still no caller with measured voltage.
Those are sufficient reasons to omit it. The correction removes the
unsupported shortcut in the argument; it does not introduce the API.

### Claim 7: regression gaps

`rg -n 'zero_step|15|OFFLINE' test/test_basic.cpp` finds OFFLINE behavior
assertions, but no assertion of its numeric value. Header `Status.h:30`
appends `OFFLINE` after values 0-14 without an explicit initializer.

The complete body of
`test_zero_step_job_does_not_publish_a_stale_snapshot()` at lines 2365-2393
calls `pollJob()` only for the preceding ReadSnapshot job. After
`startIncrementWiperJob(0)` and `startDecrementWiperJob(0)` it inspects
snapshots but never polls. Consequently the new no-job guard and zero-step
completion are not tested together. Both runtime paths are currently correct.

## Corrections applied

- The line-comment regex now consumes backslash-LF and backslash-CRLF
  continuations in the same pass. It preserves lengths, offsets, and line
  breaks required by function extraction. It does not claim to implement all
  C++ preprocessing, including arbitrary token-splicing or macro expansion.
- Fifteen adversarial comment/literal inputs cover strings containing `//`
  and `/*`, apostrophes in comments, unmatched quotes in comments, raw-string
  delimiters, ordinary/wide character literals, escaped apostrophes/quotes,
  digit separators, and nested-looking comment text. Additional LF/CRLF tests
  cover both the false negative and continued comment contents staying hidden.
- IDF host tests compile the actual zero/mid/max branch and its real argument
  guard, capturing the `writeWiper()` arguments. They assert all three exact
  codes in each resolution, reject trailing junk, and require readback calls.
  The production preset implementations are unchanged.
- A compile-time test pins `Err::OFFLINE == 15`. The existing zero-step test
  now polls both job types, requires OK, and verifies no additional bus calls.
- Removed the four proven redundant conditions described in claim 4.
- `Doxyfile.api` explicitly sets `WARN_AS_ERROR = FAIL_ON_WARNINGS`.
- Corrected HIL adapter expectations, README certainty wording, the resolution's
  helper/lexer/coverage explanations, and Arduino midpoint migration notes.
  The API contract lists the exact zero/mid/max codes and distinguishes
  `mid` from `frac 0.5`. The midpoint runtime fix remains under Fixed, with an
  explicit 2.0.0 migration note for operators and scripts.
- Linked this review from both audit documents and excluded it from both
  package exports, consistently with the other maintainer audit reports.

No driver or example runtime code required a change in this follow-up.

## Independent re-audit of the original 17 findings

The verdicts below assess the resolution at the starting commit, including
explicitly rejected proposals. Evidence includes the baseline native executable
(`88 Tests 0 Failures 0 Ignored`), Arduino host executable, tooling suite,
scratch mutations, and the source checks described below.

| # | Verdict | Implementation evidence and result |
|---|---|---|
| 1 | DISPOSITION-CORRECT | IDF `initConsole()` at line 1974 installs the selected backend and sets `O_NONBLOCK`; `pollConsole()` at 2022 bounds reads with `count < LINE_LEN`, retains fragments, and calls `clearerr(stdin)` for transient errors. The compiled IDF console/parser test passes fragmentation, CR/LF, overflow, control-byte, transient-error and EOF cases. "Never reads any command" was correctly rejected as an overstatement. Physical console operation remains unvalidated here. |
| 2 | DISPOSITION-CORRECT | Core `_isValidAddress()` at line 1230 is `address >= cmd::MIN_ADDRESS && address <= cmd::MAX_ADDRESS`; constants are `0x3C` and `0x3F`. No alternate-address API or handler remains. `test_begin_address_matrix:PASS` exercises every byte, with invalid addresses bus-silent. Removed-name occurrences are historical docs or rejection tests. |
| 3 | DISPOSITION-CORRECT | `_offlineStatus()` at 1412 returns `Err::OFFLINE`; `_jobBusyStatus()` retains `BUSY`; the ambiguity classifier excludes both. Both CLI error displays know OFFLINE. `test_job_busy_offline_and_end_guards_are_bus_silent:PASS`. The missing numeric assertion is claim 7, not an incorrect enum value. |
| 4 | DISPOSITION-CORRECT | `pollJob()` at 249 rejects `JobType::None` before returning an inactive job's own status. `_startStepJob()` at 819 resets and publishes a typed zero-step snapshot. `test_poll_without_a_started_job_is_invalid:PASS`; the added zero-step poll assertions also pass. |
| 5 | DISPOSITION-CORRECT | The core compares current and requested TCON at lines 135-137. Actual-driver execution of all 256 pairs passed; full truth table below. Existing 11-pair failure-injection test also passes. The audit's target-only classification was correctly rejected. |
| 6 | DISPOSITION-CORRECT | `MAX_COMMAND_CHUNK` stays 64; Wire checks `len > WIRE_BUFFER_LIMIT` at line 83 before touching Wire, and has a compile-time capacity assertion at 33. The scratch harness rejected oversize write/write-read with zero Wire transactions and no uncertainty/health failure. `test_step_chunk_boundaries_and_variant_saturation:PASS`. The proposed config field has no required current caller. |
| 7 | DISPOSITION-CORRECT | Pinned framework source confirms loss of phase/cause information. IDF mapping preserves `err`; Wire result 2 and zero-length reads use `I2C_ERROR`. `test_esp32_wire_ambiguous_nack_preserves_uncertainty:PASS` and compiled IDF mapping assertions pass. Exact source evidence below. The stale HIL expectation is claim 1. |
| 8 | DISPOSITION-CORRECT | Arduino `restoreSnapshot()` writes both registers, preserves the first failed status, reads back after successful writes, compares Wiper/TCON, and reports `REGISTER_MISMATCH`. `Arduino CLI restore and argument regression tests passed`, including ignored writes and either write/read failing. Register readback is correctly distinguished from physical output. |
| 9 | DISPOSITION-CORRECT | `check_argument_contract()` enforces both no-argument guards and mode-alias sets; both parser behavior tests reject trailing junk. IDF `begin` stages candidate address/resolution before assigning `gCfg`. Contract and Arduino malformed-command tests pass. Separate implementations comply with AGENTS.md. Missing IDF preset output coverage is claim 3. |
| 10 | DISPOSITION-CORRECT | `_updateHealth()` at 1441 delegates failure handling to `_recordFailure()`, whose local-error filter at 1448 preserves device health. Success retains counter saturation, timestamping and READY behavior. `test_local_callback_errors_preserve_health_and_certainty:PASS`; existing recovery/counter tests also pass. |
| 11 | DISPOSITION-CORRECT | No `minSafeWiperCode()` API was added. DEVICE_REFERENCE lines 240-278 explicitly distinguish the printed floor values from conservative ceiling values, units and infeasibility. Independent arithmetic and PDF inspection agree with the resolution's numbers; table below. Claim 6 corrects the rationale without reversing the disposition. |
| 12 | DISPOSITION-CORRECT | Added busy/offline/end matrix, failure-at-each-instruction matrix, chunk boundary/saturation matrix, 7-bit code rejection and failed-begin rollback tests all pass. The zero-step polling omission was a remaining coverage gap, now addressed under claim 7. |
| 13 | DISPOSITION-CORRECT | Wire stub records STOP, repeated-start and both addresses, bounds RX by staged bytes, and is reset in `setUp()` at line 332. `test_wire_random_read_uses_repeated_start_and_distinct_addresses:PASS` covers actual adapter repeated-start, read-only, short-read, General Call address and write-phase errors. |
| 14 | IMPLEMENTED-INCORRECTLY | Shared constants, checked function extraction, propagated IDF exit code and detailed-help HIL handling are implemented and their mutation/parser tests pass. However, claim 2 reproduces a remaining false negative in the replacement lexer; claim 4 finds remaining redundant branches. Those portions of the resolution were incomplete and are corrected here. |
| 15 | DISPOSITION-CORRECT | `generate_version.py` calls automatic sync only under `if ENV is not None:`. All three VersionScriptTests pass: import/runpy preserve stale files, check is read-only, sync preserves manifest newline, and SCons still generates metadata. |
| 16 | DISPOSITION-CORRECT | Broad INPUT contains `src` and `docs`; the API config uses `EXTRACT_ALL = NO`, both documentation warning classes and XML output below `.pio`. CI pins Doxygen 1.15.0 and verifies the download hash. Fresh-checkout tests prove both undocumented-symbol and missing-parameter failures at HEAD. Claim 5 strengthens policy independence. |
| 17 | DISPOSITION-CORRECT | Installed pinned `component_manager.py:298-301` selects and returns the framework manifest before considering `project_src_dir`; the framework manifest exists. `src_dir = .` is retained. Generator import/sync tests pass; no root-manifest reformat was observed. Builds and artifact/version checks provide the additional validation recorded below. |

### Finding 5: all 256 startup pairs

A scratch C++ harness includes the existing `test/test_basic.cpp` with its
`main` renamed, reuses `FakeBus`, and links the real core and Unity. For every
current/target low nibble, it starts at Wiper `0x11`, requests `0x66`, enables
both startup writes, and observes `writeByte0Log[0]`. It asserts two writes,
the final register values, and that any TCON-first transition introduces no
new enabled terminal switch before the wiper write. For `R0HW=0`, terminal
switch bits are overridden; every such target must be TCON-first.

Actual output (`T` = TCON first; `W` = Wiper first):

```text
current\target 0123456789ABCDEF
0              TTTTTTTTWWWWWWWW
1              TTTTTTTTWWWWWWWW
2              TTTTTTTTWWWWWWWW
3              TTTTTTTTWWWWWWWW
4              TTTTTTTTWWWWWWWW
5              TTTTTTTTWWWWWWWW
6              TTTTTTTTWWWWWWWW
7              TTTTTTTTWWWWWWWW
8              TTTTTTTTTWWWWWWW
9              TTTTTTTTTTWWWWWW
A              TTTTTTTTTWTWWWWW
B              TTTTTTTTTTTTWWWW
C              TTTTTTTTTWWWTWWW
D              TTTTTTTTTTWWTTWW
E              TTTTTTTTTWTWTWTW
F              TTTTTTTTTTTTTTTT
PASS: 256/256 startup pairs; zero stale-code reconnections; all shutdown targets first
PASS: oversized write/write-read rejected INVALID_PARAM before Wire I/O; no uncertainty or health failure
```

This is a proof about ordering new terminal-switch connections when **both**
startup writes are requested. It does not make a transition atomic, remove
pre-existing stale output, or promise a new wiper code when the caller disables
the wiper write. SHDN/WLAT and the actual circuit still require measurement.

### Finding 7: pinned transport sources

The raw [ESP-IDF v6.0.1 source](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_driver_i2c/i2c_master.c#L579-L605)
sets timeout status when the transaction event wait expires. Its
[transaction completion logic](https://github.com/espressif/esp-idf/blob/v6.0.1/components/esp_driver_i2c/i2c_master.c#L717-L724)
returns `ESP_ERR_INVALID_RESPONSE` whenever status is not DONE. Thus NACK and
that timeout path share a result, including receive-only transactions.
Other paths return an explicit timeout and remain distinguishable. The
original audit's claim that INVALID_RESPONSE is never produced is false.

The pinned [Arduino-ESP32 3.3.11 Wire source](https://github.com/espressif/arduino-esp32/blob/3.3.11/libraries/Wire/src/Wire.cpp#L468-L474)
maps both `ESP_FAIL` and `ESP_ERR_NOT_FOUND` to 2, while explicit timeout maps
to 5. Its [read paths](https://github.com/espressif/arduino-esp32/blob/3.3.11/libraries/Wire/src/Wire.cpp#L525-L547)
return `rxLength`, losing the underlying error. Zero bytes therefore cannot
distinguish absence from timeout. This does **not** mean Wire loses every
write timeout; it loses sufficient information to prevent a definite address
classification for result 2 or failed reads.

Core `src/MCP45HVX1.cpp:1361` excludes `I2C_NACK_ADDR` from ambiguous write
failures. Inventing that status would bypass uncertainty even after a possibly
accepted state write. The conservative mapping is correct; a later probe
cannot reconstruct the original transaction's history.

### Finding 11: independent calculation and datasheet inspection

The local DS20005304B page 42 was rendered and visually checked: Table 5-3
prints 91/45, 91/45, 35/17 and 17/8, while Note 3 describes upward rounding.
For the table's simplified model, independently compute
`RS_min = 0.8 * RAB / maxCode`, then
`ceil(36 / ((Imax_mA / 1000) * RS_min))`.

Actual Python results; every printed-code current exceeds its limit, while
every ceiling-code current is within it:

| RAB (ohm) | maxCode | RS_min (ohm) | Printed code | Current at printed code (mA) | Limit (mA) | Ceiling code | Current at ceiling code (mA) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 5000 | 255 | 15.686275 | 91 | 25.219780 | 25 | 92 | 24.945652 |
| 5000 | 127 | 31.496063 | 45 | 25.400000 | 25 | 46 | 24.847826 |
| 10000 | 255 | 31.372549 | 91 | 12.609890 | 12.5 | 92 | 12.472826 |
| 10000 | 127 | 62.992126 | 45 | 12.700000 | 12.5 | 46 | 12.423913 |
| 50000 | 255 | 156.862745 | 35 | 6.557143 | 6.5 | 36 | 6.375000 |
| 50000 | 127 | 314.960630 | 17 | 6.723529 | 6.5 | 18 | 6.350000 |
| 100000 | 255 | 313.725490 | 17 | 6.750000 | 6.5 | 18 | 6.375000 |
| 100000 | 127 | 629.921260 | 8 | 7.143750 | 6.5 | 9 | 6.350000 |

These are model calculations, not measured analog limits or accuracy. The
maintained [device reference](DEVICE_REFERENCE.md) already documents the
divergence, current-unit conversion, infeasible results, and external thermal
and electrical measurements. Its calculation required no correction.

### Finding 16 and new assertions: negative tests

Before correction, fresh archives with no `.pio` gave API Doxygen exit 0 for
clean headers and exit 1 for either an undocumented public member or removing
`/// @param config` from `begin()`. Thus the existing clean-checkout fix holds.
After correction, fresh archives overlaid with the edited files give these
same results **even with `WARN_AS_ERROR = NO` in the parent**:

```text
Fresh API gate; relaxed parent; mutation= clean:  exit=0
Fresh API gate; relaxed parent; mutation= symbol: exit=1
error: Member auditUndocumented() ... is not documented.
Fresh API gate; relaxed parent; mutation= param:  exit=1
error: parameters of member MCP45HVX1::MCP45HVX1::begin are not documented
```

Additional deliberate scratch regressions produced:

```text
IDF max / 2U changed to (max + 1U) / 2U:
  Assertion failed: gDev.writes == expected && presetReadbacks == 3
  tooling test exit=1
OFFLINE assigned value 16:
  error: static assertion failed: Err::OFFLINE must retain its append-only numeric value
  compiler exit=1
pollJob rejects a completed zero-step increment:
  88 Tests 1 Failures 0 Ignored
  native executable exit=1
Original continued-comment fault with corrected lexer:
  Core timing guard FAILED:
  - framework tokens forbidden in core file src/fault.cpp: {'Serial': 1}
  guard exit=1
```

These expected failures show the new checks can detect the defects they pin.

## Documentation consistency and introduced defects

The historical audit remains wrong about the target-only startup proposal,
ESP-IDF return codes, Arduino zero-byte reads proving absence, IDF trailing
argument acceptance, and the proposed floor formula's units and safety. The
resolution explicitly rejects those claims, and the current code supports
the rejection. Rewriting historical proposals would obscure what was reviewed;
both documents now link to this verification.

The resolution's replacement lexer left the continued-comment false negative
unresolved. Running the same input against the pre-resolution lexer with
`git show 1faad98:tools/check_core_timing_guard.py` also prints
`Serial visible: False`; this is an incomplete fix, not a newly introduced
runtime regression. Claim 2 demonstrates it and the fix above removes it.
The transport correction left a stale HIL expectation, and the Arduino midpoint change left
incomplete migration/coverage statements. Those documentation and testing
defects are corrected here. The new API Doxygen configuration introduced the
policy dependency in claim 5, while its output-directory fix remains correct.
No new core protocol,
startup-ordering, error-classification, or job-state defect was reproduced.

The scan covered README, every maintained `docs/*.md`, CHANGELOG, and both CLI
help tables. Generic "mid-scale" help did not promise `0x80`, but did not
communicate the changed exact code either; the API contract and migration note
now do. Historical validation counts in the earlier resolution describe that
run; this follow-up expands assertions while retaining 88 Unity test groups
and raises tooling tests from 10 to 11.

## Validation and CI evidence

The requested pre-change gate suite passed in full: CLI/core/artifact/IDF
guards, 10 tooling tests, 19 HIL parser tests, generated-version check, both
Doxygen 1.15.0 configurations, and the direct GCC/Unity native executable
(`88 Tests 0 Failures 0 Ignored`). Commands used the supplied include paths
and warning flags; the Windows executable lived in a temporary directory.

The independent reproduction files and full logs were written outside the
repository under the session's `mcp45hvx1-reaudit-*` temporary directory.
Key outputs are preserved above so the findings do not depend on those files
remaining available. The source matrices are bounded host simulations.

GitHub authentication succeeded; there is no PR for branch `main`.
[CI run 33987750277](https://github.com/janhavelka/MCP45HVX1/actions/runs/33987750277)
for the exact starting commit is successful. `gh run view ... --json
conclusion,headSha,jobs` reported:

```text
HEAD: e59b267bc50aa8d5e71cb54907507d3d3fb88f76 conclusion: success
native-tests: success
build (esp32s3dev): success
documentation: success
validate-library: success
idf-build (esp32s3): success
build (esp32s2dev): success
idf-build (esp32s2): success
```

The earlier failed run was superseded by this successful run. No live CI
failure remained to repair. Local follow-up edits are not represented by that
remote result.

The initial full local validator installed the pinned Arduino dependencies
but failed to find `xtensa-esp32s3-elf-g++`. Inspection found the installed
compiler under an extra `xtensa-esp-elf/bin` directory. A process-local PATH
adjustment makes that compiler discoverable; appending it preserves the host
GCC runtime's precedence (prepending it caused a host DLL entry-point error).
No system PATH, package layout, platform pin, or firmware source was changed
to accommodate this local installation issue. The retry used:

```powershell
$env:PLATFORMIO_CORE_DIR = Join-Path $env:USERPROFILE '.platformio'
$env:PLATFORMIO_PACKAGES_DIR = 'C:\pio\packages'
$env:PLATFORMIO_PLATFORMS_DIR = 'C:\pio\platforms'
$env:PLATFORMIO_CACHE_DIR = 'C:\pio\.cache'
$env:PATH = $env:PATH + ';C:\pio\packages\toolchain-xtensa-esp-elf\xtensa-esp-elf\bin'
python tools/validate.py
```

Post-change standalone gates passed with 88 native tests, 11 tooling tests,
19 HIL parser tests, and both Doxygen configurations. PlatformIO package export
contained 34 entries and no audit/test/tool/build/PDF leaks; both export
manifests explicitly exclude this new report. `git diff --check`, the generated
version check, and the generated-artifact guard passed. `git diff --quiet --
src include examples` returned 0, confirming no runtime code changes.

The full validator retry **passed** with exit 0. Actual final output included:

```text
88 test cases: 88 succeeded in 00:00:03.992
esp32s3dev     SUCCESS   00:01:19.345
esp32s2dev     SUCCESS   00:01:00.684
python tools/validate.py exit=0
```

That run also passed standalone core/Arduino compilation, the Arduino CLI host
executable, native-IDF extracted behavior tests, all contract/parser/version
checks, and the HIL parser self-test. Doxygen and generated-artifact/version
checks were also run after the final report and firmware builds. The pinned
native ESP-IDF 6.0.1 build evidence remains the reviewed CI run above; no local
native-IDF SDK build or hardware-validation result is claimed. At the end of
that 2026-09-06 run, changes remained local and uncommitted; no remote CI run
had been initiated for those edits.

## Final review closure, 2026-09-07

The follow-up review started with the 15 modified files and this untracked
report on `e59b267`. All seven corrections above were checked against their
current callers, status mappings, guards, and regression assertions. The
continued-comment reproducer was also rerun against HEAD and the corrected
lexer: `Serial visible` changed from `False` to `True`.

| Final review item | Verified disposition |
|---|---|
| Missing documentation index entry | Valid. Added this report to the Maintained Docs table in `docs/README.md`, with its maintainer-only purpose and export exclusion. Both package manifests already excluded it and keep doing so. |
| Mode aliases absent from documentation | Valid documentation gap; command parity already held. Retained all 14 spellings and the existing contract pin to preserve existing CLI usage. The root README now maps the nine aliases to the five canonical modes for both examples; the short help synopses keep the canonical names. |
| Uppercase `parseTerminal()` comparisons | Valid dead branches. Both call sites are in `handleCommand()`, which calls `lowerAscii(cmd)` on the entire trimmed line before `splitWhitespace(cmd)`. Both calls receive a copy of those normalized arguments. Removed only the uppercase comparisons; uppercase commands still reach the same lowercase branches. |

The docs index link resolves under the recursive Doxygen input. A direct
comparison also confirmed that the README table contains every spelling in
`MODE_ALIASES`. The only additional executable-source change is the removal
of those three unreachable comparisons in the native ESP-IDF example.

### Evidence still open

The hardware validation procedure still has **71 rows containing `Pending`**.
Every output-changing, analog, SHDN/WLAT, rail-cycling, fault-injection, and
General Call gate remains unmeasured. These are hardware-evidence tasks and
are not closed by this code change.

`initConsole()` still has **no runtime validation**. The ESP-IDF CI jobs build
the example; no recorded HIL run has driven that CLI. The extracted
`pollConsole()` host tests cover input accumulation and error handling, but
do not execute the UART or USB-Serial-JTAG driver installation or the VFS
line-ending configuration. This remains a runtime-evidence task.

A possible follow-up is a bounded ESP32-S3 UART smoke test using Espressif's
[ESP-IDF 6.0.1 QEMU support](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-guides/tools/qemu.html).
Build with the UART console, boot the image, assert a prompt, send `version`
with LF/CR/CRLF terminators, and assert one response and one new prompt per
command. Retain the UART transcript and fail on timeout or panic. Estimated
effort: **half to one engineering day**, including a feasibility check that
the example's I2C startup reaches the prompt under emulation. That path has
not been tried here; any emulator limitation would need a separately scoped
decision. A UART run would reduce the UART/VFS risk while leaving USB console
installation and physical behavior unvalidated. No emulator test or CI change
was implemented in this closure.

### Closure validation

The requested commands were rerun successfully on Windows with the supplied
GCC flags and include paths; only `/tmp/t` was replaced by a temporary
`native-tests.exe`. Actual output excerpts from the requested suite:

```text
IDF example contract PASSED
CLI contract PASSED
Core timing guard PASSED
Generated artifact check PASSED
IDF example contract PASSED
PASS: IDF console input, parsers, conservative transport mapping, and both-variant preset writes
Ran 11 tests in 2.888s
OK
Ran 19 tests in 0.530s
OK
88 Tests 0 Failures 0 Ignored
OK
```

`python scripts/generate_version.py check` reported all three generated files
up to date. Doxygen 1.15.0 produced no output for either `Doxyfile` or
`Doxyfile.api`; both exited 0 with zero diagnostics. The strict API policy was
also independently checked in a temporary project with the parent policy
set to `WARN_AS_ERROR = NO`: a documented public type exited 0, while an
undocumented member exited 1 with the expected diagnostic.

The required `python tools/validate.py` completed with exit 0, using the
process-local PlatformIO settings documented above. It passed every guard,
the HIL parser self-test, standalone core and Arduino compilation, the Arduino
CLI host regression executable, native tests, and both Arduino builds:

```text
88 test cases: 88 succeeded in 00:00:07.906
esp32s3dev     SUCCESS   00:01:51.033
esp32s2dev     SUCCESS   00:01:04.872
python tools/validate.py exit=0
Package entries=34; audit/test/tool/build leaks=0; both manifests exclude the report
```

Full command output is retained outside the repository in the session's
`mcp45hvx1-audit-close-56a46d87c5a04deba2cbdf95b9ec55f9` temporary directory:
`requested-suite.log`, `validate.log`, `strict-api-negative-check.log`, and
`package.log`. Native test/build counts above are software evidence only.

GitHub Actions was rechecked during closure. Run `33987750277` for `e59b267`
still has all seven jobs successful, and there is no PR for `main`. No active
CI failure required a fix. That remote result applies to the starting commit;
this closure was validated locally and no push or new remote CI run was made.
