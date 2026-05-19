# Changelog

## Unreleased

- ESP-IDF component metadata and a pure ESP-IDF `examples/espidf_basic` build of
  the full bring-up CLI.
- `examples/common/IdfArduinoCompat.h` example shim that provides the small
  Arduino surface used by the CLI while routing I2C through ESP-IDF v6
  `i2c_master_*` APIs.
- ESP-IDF port implementation notes in `docs/IDF_PORT_IMPLEMENTATION.md`.
- Core time fallback is now platform-aware: Arduino/native test builds use
  `millis()`, while ESP-IDF builds use `esp_timer_get_time()`.
- Example helpers now gate Arduino headers behind
  `MCP45HVX1_EXAMPLE_PLATFORM_IDF` so the same CLI source can compile for both
  frameworks.
- `library.json` now declares both `arduino` and `espidf` framework support.
- Doxygen input now covers the ESP-IDF port notes, implementation notes, shared
  CLI source, native IDF entry point, and example-only IDF shims.
- The ESP-IDF example adapter now sends General Call address `0x00` through
  ESP-IDF defined I2C operations with manual address bytes instead of relying on
  normal device-handle addressing for a reserved address.
- `tools/check_cli_contract.py` now validates the ESP-IDF wrapper macro,
  shared-source include, and required CMake dependencies.
- The ESP-IDF CLI parity is structural through shared source; pure IDF
  `idf.py` builds and hardware validation remain pending until an IDF toolchain
  and target hardware are available.
- Latched `OFFLINE` behavior for normal public I2C operations. Once the health
  threshold is reached, normal operations return `BUSY` with
  `Driver is offline; call recover()` and do not touch the bus until
  `recover()` succeeds.
- Native coverage for the latched `OFFLINE` guard, explicit recovery path, and
  static helper API usage.
- Added GitHub Actions CI for ESP32-S3/ESP32-S2 builds, native tests, contract
  checks, package validation, and version checks.
- ESP32 example builds now pin pioarduino `platform-espressif32` 54.03.20 and
  rely on the Arduino framework-provided `Wire` library instead of an explicit
  `lib_deps = Wire` entry.
- README documentation now covers thread/ISR expectations, the latched recovery
  model, and the remaining hardware-validation caveats.
- Failed explicit recovery attempts that begin from `OFFLINE` now reassert the
  latch after any later recovery-step failure, using `_reassertOfflineLatch()`.

## 1.0.0

- Initial production-style MCP45HVX1 driver.
- Added injected I2C transport, status/error model, health tracking, register
  helpers, wiper and TCON APIs, General Call helpers, examples, tests, and docs.
