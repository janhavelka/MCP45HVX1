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
