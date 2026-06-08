# MCP45HVX1 Repository Notes

This repository follows the same driver layout used by the sibling I2C device
libraries in this workspace.

- Keep library code transport-agnostic. Core files under `include/` and `src/`
  must use injected callbacks from `Config`, not direct `Wire` calls.
- Core/public headers and `src/` must not require Arduino or ESP-IDF framework
  headers.
- Keep example-only Arduino helpers under `examples/common/`.
- Arduino examples may use Arduino APIs. ESP-IDF examples must be native IDF
  applications using `app_main`, `driver/i2c_master.h`, `esp_timer`,
  `vTaskDelay`, and fixed C buffers or native console APIs.
- ESP-IDF examples must not include Arduino CLI sources or use Arduino
  compatibility facades such as `ArduinoCompat`, `IdfArduinoCompat`,
  `Arduino.h`, `Wire.h`, `String`, `Serial`, or `TwoWire`.
- Maintain CLI command parity through repo-local command contracts/checkers, not
  by sharing Arduino implementation source.
- Use `Status` for all fallible public operations.
- Keep all writes scoped to this repository.
- Run `pio test -e native` and the scripts under `tools/` after changes when
  PlatformIO is available.

# MCP45HVX1 hardening rules

- Work in small chronological chunks. After each chunk, run available checks,
  commit, sync/push, and stop unless the active prompt explicitly says this is a
  final cleanup or merge-preparation pass.
- Core code in `include/` and `src/` must remain framework-neutral: no Arduino,
  Wire, ESP-IDF, FreeRTOS, Serial, framework logging, framework delays, global
  bus ownership, pin ownership, or task ownership.
- MCP45HVX1 core must use injected/non-owning I2C transport. Applications and
  examples own bus, locking, timeout policy, bus reset, pins, and hardware
  lifecycle.
- Preserve I2C error distinctions where possible: invalid config, invalid
  argument, not initialized, device not found, address NACK, data NACK, timeout,
  bus error, unsupported command/register, and uncertain hardware state.
- MCP45HVX1 is a high-voltage volatile digital potentiometer. Wiper, TCON, and
  output-changing operations can affect real analog/high-voltage circuits.
- Default CLI, HIL, and selftest behavior must be safe/read-only or
  state-restoring. Output-changing tests must be explicit opt-in and must
  restore baseline or mark uncertainty.
- MCP45HV31 and MCP45HV51 resolution differences must be respected: HV31 is
  7-bit/128 taps/POR `0x3F`; HV51 is 8-bit/256 taps/POR `0x7F`.
- Standard I2C address range is `0x3C..0x3F`. Alternate `0x5C..0x5F`, if used,
  must be explicit opt-in and not claimed verified without evidence.
- SHDN and WLAT are hardware pins. Core must not silently assume control over
  them. Register readback can differ from physical output if WLAT, SHDN, or
  external circuitry overrides behavior.
- Software cannot enforce analog rail/current safety. Documentation and HIL
  must require external measurement and safe-load validation.
- Do not claim hardware validation, ESP-IDF build success, analog accuracy,
  General Call safety, release readiness, production readiness, or
  industry-grade readiness without logged evidence.
- Treat prompt files as historical after completion. Do not redo completed
  hardening prompts unless a regression is found.
