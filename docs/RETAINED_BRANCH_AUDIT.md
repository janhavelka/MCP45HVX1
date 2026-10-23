# Retained branch review

Reviewed on 2026-10-23 against `main` at
`d3eae4a01113d3badee1b6156571666bffe91ba2`, after fetching `origin` and
confirming `main == origin/main`. This is the newest branch tip by commit
date. The worktree was clean before this documentation change. Source line
references below refer to that baseline, not to the older report.

**Decision:** no missing software implementation was found from the retained
production-readiness audit. Its only unique commit adds a historical report.
The port and hardening branches are already ancestors of `main`. Preserve
the remaining hardware qualification work below; the historical report branch
can then be retired after its exact tip is archived. No branches were deleted
or merged, and no driver behavior changed in this review.

## Exact branch inventory

Counts use `git rev-list --left-right --count main...origin/<branch>`:
main-only commits first, branch-only commits second. A zero branch-only count
means the tip is already in main's ancestry, even if the ref still exists.

| Retained branch | Exact tip | Main-only / branch-only | Disposition |
| --- | --- | --- | --- |
| `audit/mcp45hvx1-production-readiness-exploration` | `0b365ded60337a49f675e9c6734a0f0d8472d27c` | 45 / 1 | Report-only divergence; archive then retire after this reconciliation is accepted. |
| `feature/mcp45hvx1-idf-port` | `3cbbdcbd1f85805a806ef8c5bf919aa5cb1459fc` | 48 / 0 | Already merged; eligible for normal branch cleanup. |
| `hardening/mcp45hvx1-industry-readiness` | `650e8d2ea3a6f316ed06d5b461aed82529c081e5` | 22 / 0 | Already merged; eligible for normal branch cleanup. |

The sole unique commit, `0b365ded60337a49f675e9c6734a0f0d8472d27c`
(`docs: explore MCP45HVX1 production readiness`), adds only
`docs/MCP45HVX1_PRODUCTION_READINESS_EXPLORATION_REPORT.md` (908 lines).
`git cherry main origin/audit/mcp45hvx1-production-readiness-exploration`
reports `+` because that document patch has no identical patch in main. It
does not identify missing driver code. The report itself says exploration
only/no implementation and was written against `6985d66`.

## Reconciliation of every historical finding

The H/M/L identifiers are those in the retained report. "Implemented" means
source and regression coverage exist at the reviewed baseline; it does not
claim hardware qualification or a newly green remote CI run.

| Old finding | Current disposition and evidence |
| --- | --- |
| H1: ambiguous failed writes leave known cache | Implemented. `src/MCP45HVX1.cpp:664` routes failed state-changing register writes to `_markHardwareStateUncertain`; General Call paths at 710 and step paths at 1149 preserve uncertainty too. Readback is required per register (`src/MCP45HVX1.cpp:1368`). Mutation-then-failure regressions begin at `test/test_basic.cpp:1592`, including TCON, raw register, INC/DEC, General Call and multi-register readback. |
| H2: optional startup writes lose recovery path | Implemented. `begin()` establishes the baseline binding at `src/MCP45HVX1.cpp:127` before optional writes, preserving an initialized diagnostic/recovery path after a write failure. Tests at `test/test_basic.cpp:764` and 805 mutate hardware then fail and exercise recovery. The public contract is at `include/MCP45HVX1/MCP45HVX1.h:162`. |
| H3: hardware evidence missing | Partially closed as evidence, not a code defect. Safe-only ESP32-S2/MCP45HV51 results now exist, including an eight-hour soak and a targeted 2026-08-05 run. Output-changing and physical qualification remain open; see Q1 below. |
| H4: errata isolation not a release gate | Implemented documentation/tooling. `docs/DEVICE_REFERENCE.md` explains Rev A1/A2 markings and the dedicated-bus issue; the hardware checklist records marking/topology and the release checklist gates production claims. Physical evidence for the intended deployment remains Q1. |
| H5: IDF token-only parity, unbounded numbers, inert color, no build gate | Implemented. Native IDF uses `parseU32Bounded` at `examples/espidf_basic/main/main.cpp:278`, real conditional ANSI at 388, a null-bus scan guard at 953, safe self-test at 1321 and output-changing/restore self-test at 1402. `tools/check_idf_example_contract.py:231` onwards rejects unbounded handlers and placeholder regressions. `.github/workflows/ci.yml` builds native IDF v6.0.1 on S2/S3. Static checks/builds still do not prove real-device CLI parity. |
| M1: bus reset can falsely restore READY | Implemented. `src/MCP45HVX1.cpp:383` resets interface state without using successful reset as a tracked device success. Header contract at `include/MCP45HVX1/MCP45HVX1.h:258` explicitly retains DEGRADED/OFFLINE and uncertainty; native reset/recovery tests cover this. |
| M2: inconsistent uncertainty diagnostics | Implemented. Core snapshots contain `hardwareStateUncertain` and its error (`src/MCP45HVX1.cpp:298`); both CLIs report this state. IDF settings/state output is at `examples/espidf_basic/main/main.cpp:981` and 1008. The flag is no longer just an Arduino-local assumption. |
| M3: begin/probe collapse transport errors | Implemented. `_presenceReadFailureStatus` maps definite address NACK only (`src/MCP45HVX1.cpp:1250`); other transport errors retain code/detail. Tests `test_begin_preserves_transport_status_except_address_nack` and `test_probe_preserves_transport_status_except_address_nack` exercise the distinction. |
| M4: implicit copy/move | Implemented. All four operations are deleted at `include/MCP45HVX1/MCP45HVX1.h:142`; compile-time assertions are in `test/test_basic.cpp:28`. |
| M5: hardware checklist too narrow | Implemented procedure; pending execution is Q1. `MCP45HVX1_HARDWARE_VALIDATION.md` covers safe/output-changing paths, analog measurements, pins, rails, faults, addresses and General Call, with explicit operator gates. |
| M6: inconsistent POR/BOR timing | Superseded by consolidated reference. `docs/DEVICE_REFERENCE.md:304` records TBORD 10 us typical/20 us maximum and caller-owned rail sequencing. The hardware procedure repeats the prerequisite; the core does not add a hidden delay. |
| M7: thin API contracts | Implemented in public headers and `MCP45HVX1_API_CONTRACT.md`: callback lifetime, serialization/ISR restrictions, startup side effects, cache validity, uncertainty and recovery are explicit. Strict Doxygen jobs exist in CI. |
| M8: version/tag policy and production overclaims | Superseded by release history and maintained release checklist. Main describes published v1.1.0 and unreleased API-breaking work intended for 2.0.0; it explicitly separates pre-production software from physical qualification. Existing tags include v1.0.0 and v1.1.0. The old request to finalize 1.0.0 is obsolete. |
| L1: incomplete address matrix | Implemented with a stronger current policy. `test/test_basic.cpp:420` checks all byte values; only `0x3C..0x3F` are accepted. The old request to preserve/test the disputed alternate range is superseded by its removal, not missing functionality. |
| L2: stale reports | Superseded by maintained references/API/hardware/release documents in `docs/README.md`. The old report and obsolete CLI reports should not be restored as current guidance. |
| L3: package export uncontrolled | Implemented. `library.json:51` has explicit include/exclude rules; the component manifest excludes reference PDFs and tooling. The release checklist includes artifact inspection. |
| L4: stale security/contribution boilerplate | Closed. Current `SECURITY.md` and `CONTRIBUTING.md` no longer contain the cited NVS or absent clang-format claims. |
| L5: writeWiperFraction clamp/reject mismatch | Implemented. Header at `include/MCP45HVX1/MCP45HVX1.h:380` explicitly distinguishes the rejecting output API from the clamping conversion helper. Regressions at `test/test_basic.cpp:2189` cover both variants and invalid/nonfinite fractions. |

Additional roadmap items were also traced: the HIL runner and parser now
exist (`tools/run_hil_mcp45hvx1.py`, `tools/test_run_hil_mcp45hvx1_parser.py`);
CLI/state/health guards are in the aggregate validator; poll-chunked jobs are
implemented and tested. The old roadmap's proposed header-isolation guard is
a possible extra test improvement, not evidence of a current broken public
header or missing port. Standalone compilation is already in `tools/validate.py`.

## Open work and acceptance criteria

### Q1: physical qualification before stronger deployment claims

Priority: **required before production/high-voltage/analog/General Call claims**.
This is an evidence task. The retained branch contains no implementation that
can satisfy it, so keeping its branch open does not solve it.

Use [the hardware procedure](MCP45HVX1_HARDWARE_VALIDATION.md) and
[release checklist](MCP45HVX1_RELEASE_CHECKLIST.md). Existing safe-only runs
cover one ESP32-S2/MCP45HV51 configuration and predate the audited main; they
do not validate later code automatically.

Acceptance:

1. Record exact source commit/build, board, actual chip variant/RAB/marking,
   address straps, rails, load, SHDN/WLAT wiring, bus topology and instruments.
2. Establish the errata/isolation decision for that silicon and topology.
   General Call opt-in alone does not make Rev A1 shared-bus traffic safe.
3. Capture low-voltage Wiper/TCON changes, restore verification, analog
   measurements, SHDN/WLAT behavior, rail-cycle POR/BOR and safe injected
   fault/recovery behavior. Extend to the intended variants/frameworks.
4. Run General Call and high-voltage scenarios only with their documented
   setup/operator gates; retain evidence and label unrun rows accurately.
5. Attach raw bundles to the release/validation record and update the
   maintained ledger with measured outcomes and the exact tested revision.

No hardware was operated by this branch review.

## Reproduction and retain/retire workflow

Run these read-only commands from the repository. Substitute the recorded
baseline SHA for `main` to reproduce counts after future commits:

```powershell
git status --short --branch
git for-each-ref --sort=-committerdate --format='%(refname:short) %(objectname)' refs/remotes/origin
git rev-list --left-right --count main...origin/audit/mcp45hvx1-production-readiness-exploration
git log --format='%H %s' main..origin/audit/mcp45hvx1-production-readiness-exploration
git cherry main origin/audit/mcp45hvx1-production-readiness-exploration
git diff --stat main...origin/audit/mcp45hvx1-production-readiness-exploration
git show 0b365ded60337a49f675e9c6734a0f0d8472d27c:docs/MCP45HVX1_PRODUCTION_READINESS_EXPLORATION_REPORT.md
git merge-base --is-ancestor origin/feature/mcp45hvx1-idf-port main
git merge-base --is-ancestor origin/hardening/mcp45hvx1-industry-readiness main
```

1. Keep this reconciliation and Q1 in the current branch. Do not cherry-pick
   the old report as an unresolved bug list or wholesale merge an old snapshot.
2. Preserve the unique audit tip through an agreed archival ref or verified
   Git bundle before deleting its last branch reference. A SHA printed here
   alone does not protect an otherwise unreachable commit from garbage collection.
3. Once this record and archive are accepted, retire the report branch; clean
   up already-merged port/hardening refs through normal repository ownership.
4. Track future software changes on the current code and physical evidence
   in the maintained ledger. Rerun relevant tests and exact-final-commit CI
   for substantive changes, as required by `AGENTS.md`.

## Validation and CI

CI was inspected before editing: PlatformIO 6.1.19, S2/S3 Arduino builds,
native/CLI tests, generated/version and contract guards, packaging, strict
Doxygen, and native ESP-IDF v6.0.1 S2/S3 builds. The local aggregate command
is `python tools/validate.py`; it does not run native IDF or Doxygen.

This change is documentation-only. Local results on 2026-10-23:

- `python tools/validate.py`: PASS. All contract/version/artifact guards,
  HIL parser/tooling tests, standalone core/CLI compiles (including color-off),
  executable Arduino CLI regression, native tests **88/88**, and Arduino
  `esp32s3dev`/`esp32s2dev` builds passed; no validator step was skipped.
- `doxygen Doxyfile`: PASS.
- `git diff --check` and local documentation-link checks: PASS.

Native ESP-IDF builds, package export, and hardware runs were not repeated
for this documentation-only review. No remote workflow was dispatched and
no old CI run is presented as verification of this documentation commit.
