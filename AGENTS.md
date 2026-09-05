# MCP45HVX1 Repository Notes

Working notes for anyone — human or agent — changing this repository.

## Build And Check

Run `python tools/validate.py` after changes. It compiles the core standalone,
builds both Arduino environments, runs the native tests, and runs every contract
guard. On Windows it selects the PlatformIO installation through
`.\scripts\pio.cmd`; elsewhere it uses `pio` from `PATH`.

## Layout Rules

- `include/` and `src/` are the library. They must stay framework-neutral: no
  Arduino, `Wire`, ESP-IDF, FreeRTOS, `Serial`, framework logging, framework
  delays, global bus ownership, pin ownership, or task ownership. All bus and
  timing behavior arrives through `Config` callbacks.
- `examples/common/` holds Arduino-only helpers shared by the Arduino example.
  These target ESP32; they are not framework-neutral and are not library API.
- `examples/espidf_basic/` must be a native ESP-IDF application: `app_main()`,
  `driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, fixed C buffers. It must not
  include Arduino sources or compatibility facades (`Arduino.h`, `Wire.h`,
  `String`, `Serial`, `TwoWire`, `ArduinoCompat`).
- The two examples share a command contract, not implementation source.
  `tools/check_cli_contract.py` and `tools/check_idf_example_contract.py`
  enforce that contract.
- Every fallible public operation returns `Status`.

## Design Rules

- Prefer deleting code over adding it. Before writing something new, check
  whether an existing owner, module, or API can be extended instead.
- No placeholder types, speculative extension points, or abstractions without a
  concrete current caller or test.
- No unbounded waits, retries, loops, allocations, or buffers in steady paths.
  Every hardware operation that can block needs a timeout and an observable
  failure path.
- Recovery must be bounded, deterministic, and testable. Never hide a hardware
  failure behind a silent retry or a fake success.
- The application owns the I2C bus, locking, timeout policy, bus reset, pins,
  rails, and hardware lifecycle. The driver owns chip protocol only.
- Preserve the I2C error distinctions the `Err` enum makes: invalid config,
  invalid argument, not initialized, device not found, address NACK, data NACK,
  timeout, bus error, unsupported, and uncertain hardware state.

## Device Rules

- MCP45HV31 is 7-bit / 128 taps / POR `0x3F`. MCP45HV51 is 8-bit / 256 taps /
  POR `0x7F`. Respect the configured variant in every code-valued path.
- The 7-bit I2C address range is `0x3C..0x3F` (fixed bits `01111` plus A1:A0),
  per DS20005304B §6.2.4 and Table 6-2.
- SHDN and WLAT are hardware pins the core does not own. Register readback
  proves register contents, never physical output, when WLAT, SHDN, or external
  circuitry overrides the terminals.
- Wiper, TCON, and General Call operations drive a real high-voltage analog
  output. Default CLI, HIL, and self-test behavior must be read-only or
  state-restoring; output-changing paths are explicit opt-in and must restore
  the baseline or mark uncertainty.
- Software cannot enforce analog rail voltage, terminal current, or thermal
  limits. Documentation and HIL must require external measurement.
- Do not claim hardware validation, analog accuracy, General Call safety, or
  production readiness without logged evidence.
