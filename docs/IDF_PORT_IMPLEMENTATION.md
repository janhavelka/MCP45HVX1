# MCP45HVX1 ESP-IDF Port Implementation Notes

Date: 2026-05-19.
Branch: `feature/mcp45hvx1-idf-port`.

## Scope

- Kept `include/MCP45HVX1/` and `src/MCP45HVX1.cpp` as a framework-neutral
  driver core with application-owned I2C callbacks.
- Added ESP-IDF component metadata and a native IDF entry point for the full
  bring-up CLI.
- Added an example-only ESP-IDF compatibility layer so the Arduino and ESP-IDF
  examples share one CLI command implementation.
- Preserved the full MCP45HVX1 example surface across both frameworks: scan,
  diagnostics, begin/reconfigure, probe/recover, settings, health, wiper and
  TCON access, terminal modes, direct register access, last-address reads,
  General Call flows, volatile default restore, interface reset, self-test, and
  stress flows.

## Files Added

- `CMakeLists.txt`
- `idf_component.yml`
- `examples/common/IdfArduinoCompat.h`
- `examples/espidf_basic/CMakeLists.txt`
- `examples/espidf_basic/main/CMakeLists.txt`
- `examples/espidf_basic/main/main.cpp`

## Audit Resolution

- `docs/IDF_PORT.md` blocker: missing root `CMakeLists.txt`.
  - Resolved with an IDF component that builds `src/MCP45HVX1.cpp` and exports
    `include/`.
- `docs/IDF_PORT.md` blocker: missing `idf_component.yml`.
  - Resolved with metadata for ESP32-S2/S3 and IDF `>=6.0.1`.
- `docs/IDF_PORT.md` blocker: Arduino-only default timing fallback.
  - Resolved with `esp_timer_get_time()` for ESP-IDF builds when
    `Config::nowMs` is not provided.
- `docs/IDF_PORT.md` blocker: missing IDF example with Arduino feature parity.
  - Resolved with `examples/espidf_basic`, which includes the same
    `examples/01_basic_bringup_cli/main.cpp` command implementation under
    `MCP45HVX1_EXAMPLE_PLATFORM_IDF=1`.
  - The IDF shim supplies the Arduino-shaped console, GPIO, timing, and
    `TwoWire` adapter surface through native ESP-IDF APIs.
- MCP45HVX1 transport pitfall:
  - The IDF adapter preserves last-address reads with `i2c_master_receive()`.
  - General Call writes use `i2c_master_execute_defined_operations()` with
    manual address bytes through `I2C_DEVICE_ADDRESS_NOT_USED`, avoiding normal
    device-handle addressing for reserved address `0x00`.
  - `tools/check_cli_contract.py` statically guards the `gc arm/disarm/wiper/tcon/inc/dec`
    command surface and the manual-address General Call shim tokens.
- Arduino-ESP32 pitfall:
  - Do not infer native IDF mode from `ESP_PLATFORM`; Arduino-ESP32 defines it
    too. The shared CLI uses the explicit `MCP45HVX1_EXAMPLE_PLATFORM_IDF` flag.

## Remaining Hardware Checks

- Build the IDF example for `esp32s3` and `esp32s2`; this shell did not have
  `idf.py` on PATH during the implementation pass.
- Run scan/probe and disconnected-device timeout checks on hardware through
  both Arduino and ESP-IDF entry points.
- Verify wiper/TCON readback, terminal modes, last-address reads, General Call
  reset, volatile default restore, interface reset, stress, and bus-recovery
  flows on the target board.

## Verification

- `python -m platformio test -e native`: passed.
- `python -m platformio run -e esp32s3dev`: passed.
- `python -m platformio run -e esp32s2dev`: passed.
- `python tools/check_cli_contract.py`: passed.
- `python tools/check_core_timing_guard.py`: passed.
- `python scripts/generate_version.py check`: passed.
- `doxygen Doxyfile`: completed.
- `python tools/validate.py`: completed; its internal `pio` shortcut reported
  `SKIP: pio not found`, so PlatformIO builds were run separately via
  `python -m platformio`.
- `git diff --check`: passed during the implementation pass.
