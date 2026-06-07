# MCP45HVX1 Docs Release Metadata Report

Branch: `hardening/mcp45hvx1-industry-readiness`

Prompt: `07_docs_release_metadata_packaging_and_stale_reports`

## Release Claim Changes

- README now states the current status as a pre-production candidate pending
  hardware validation.
- Changelog wording no longer describes `1.0.0` as production-style.
- No hardware smoke, high-voltage, General Call safety, pure local ESP-IDF
  build, or industry-grade readiness claim is made without evidence.
- Release checklist explicitly separates local validation, remote CI evidence,
  and hardware/HIL evidence.

## Stale Docs Updated

- `docs/cli_output_style_alignment_report.md`
  - Marked as historical.
  - Superseded the stale limitation that said runtime color toggles did not
    exist.
- `docs/MCP45HVX1_CLI_PARITY_AND_COLOR_REPORT.md`
  - Marked as historical evidence.
  - Points current ESP-IDF behavior to
    `docs/MCP45HVX1_ESP_IDF_PARITY_REPORT.md`.
- `docs/hardware_validation.md`
  - Kept as a legacy lowercase path.
  - Redirects readers to the authoritative
    `docs/MCP45HVX1_HARDWARE_VALIDATION.md`.
- `SECURITY.md`
  - Removed stale persistent-storage side-effect wording.
  - Clarified that the core has no persistent storage and that transport,
    locking, timeout, recovery, and output-changing safety policies are
    application responsibilities.
- `CONTRIBUTING.md`
  - Removed the stale missing-formatter reference.
  - Points contributors to the release checklist and hardware-evidence
    requirements.

## New Or Rebuilt Authoritative Docs

- `docs/MCP45HVX1_API_CONTRACT.md`
  - Covers transport ownership, callback lifetime, threading/ISR limits,
    address/variant/RAB policy, volatile Wiper/TCON state, uncertainty,
    startup writes, probe/reset/recover behavior, raw writes, General Call, and
    high-voltage safety.
- `docs/MCP45HVX1_HARDWARE_VALIDATION.md`
  - Covers safe read-only checks, output-changing checks, analog measurements,
    SHDN/WLAT behavior, POR/BOR rail cycling, address straps, I2C fault
    injection, General Call isolation, errata review, low-voltage-first
    validation, high-voltage evidence, and an evidence template.
- `docs/MCP45HVX1_RELEASE_CHECKLIST.md`
  - Rebuilt as an executable release gate covering clean tree, version check,
    native tests, guards, Arduino S2/S3 builds, pure ESP-IDF evidence, package
    check, HIL evidence, analog evidence, errata decisions, changelog/tag
    policy, generated artifacts, package size, and signoff.

## Metadata And Packaging

- `library.json` now defines an explicit package export policy.
- `idf_component.yml` now defines matching file exclusions for ESP-IDF
  component packaging.
- Normal PlatformIO packages include headers, source, examples, metadata, and
  current core docs.
- Large reference PDFs, extracted datasheet markdown, tests, tools, CI
  metadata, and local build output are excluded from normal package archives.
- `tools/check_generated_artifacts.py` now scans ignored root package archives
  such as `*.tar.gz`, `*.tgz`, and `*.zip`, so a PlatformIO package artifact
  cannot remain hidden by `.gitignore`.
- Repository docs footprint before package export filtering was recorded as
  22,853,832 bytes, mostly reference PDFs and extracted markdown.
- `CHANGELOG.md` keeps version `1.0.0` as an engineering/pre-production package
  version until release checklist evidence is complete.
- No release tag exists; tag creation remains pending explicit release
  approval.

## Remaining Release Blockers

- No HIL/hardware evidence bundle is recorded.
- No analog low-voltage measurement evidence is recorded.
- No high-voltage evidence is recorded and no high-voltage validation claim is
  allowed.
- No General Call isolated-bus evidence or shared-bus risk acceptance is
  recorded.
- No silicon marking/date-code errata review is recorded.
- Remote CI for the final release commit was not observed in this prompt.
- Release tag is pending; no tag was created.

## Local Results

- `python tools/check_core_timing_guard.py` - passed.
- `python tools/check_cli_contract.py` - passed.
- `python tools/check_idf_example_contract.py` - passed.
- `python tools/check_generated_artifacts.py` - passed.
- `python scripts/generate_version.py check` - passed; `Version.h` was up to
  date.
- `python -m platformio test -e native` - passed, 60/60 tests.
- `python -m platformio run -e esp32s3dev` - passed.
- `python -m platformio run -e esp32s2dev` - passed.
- `python -m platformio pkg pack` - passed and produced
  `MCP45HVX1-1.0.0.tar.gz`.
- Package artifact size was 78,026 bytes after export filtering.
- Package content spot check found no PDFs, extracted-doc trees, tests, tools,
  CI metadata, local build output, or nested archives.
- The generated package tarball was removed.
- `python tools/check_generated_artifacts.py` - passed again after artifact
  removal.

PlatformIO printed its existing obsolete-core warning during PlatformIO
commands, but the required test, build, and package commands completed
successfully.
