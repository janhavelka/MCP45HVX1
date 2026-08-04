# ESP-IDF Port

The driver core is framework-neutral. Public headers and `src/` do not include
Arduino, ESP-IDF, FreeRTOS, `Wire`, logging, or framework timing headers.
Applications inject all I2C and timing behavior through `Config` callbacks.

## Native Example Boundary

The ESP-IDF example in `examples/espidf_basic` is a native IDF application:

- entry point is `app_main()`
- I2C uses `driver/i2c_master.h`
- timestamps use `esp_timer_get_time()` through `Config::nowMs`
- delays use `vTaskDelay()`
- command input uses fixed C buffers

The ESP-IDF example must not include Arduino sources or compatibility facades
such as `Arduino.h`, `Wire.h`, `String`, `Serial`, `TwoWire`,
`ArduinoCompat`, or `IdfArduinoCompat`.

The Arduino example and ESP-IDF example share a command contract, not
implementation source. `tools/check_idf_example_contract.py` enforces the
native-IDF boundary and command coverage.

## CLI Behavior

The native ESP-IDF CLI mirrors the Arduino diagnostic CLI behavior:

- command input is normalized to lowercase and split on whitespace
- `help <command>` and `? <command>` show aliases, safety, syntax, and examples
  without executing the selected command
- byte-sized and output-changing arguments use bounded parsers
- `color on|off` controls ANSI status/warning output
- `selftest safe` remains read-only
- `selftest output` is explicit, output-changing, verifies writes, restores
  baseline Wiper/TCON state, and marks uncertainty on restore failure
- `stress` is read-only; `stress_mix` is output-changing and state-restoring
- `state`, `drv`/`health`, and `cfg` expose address, variant, resolution, RAB,
  cache-known flags, last status, and uncertainty
- `gc arm` prints bus-wide, DS80000649B, and isolated-bus-evidence warnings
  before enabling one General Call command

The example sets `Config::allowGeneralCall = true` only for diagnostic CLI use.
That is not production bus-manager policy; application firmware must make its
own explicit General Call decision.

## Build Evidence

The repository configures pure ESP-IDF CI builds for `esp32s3` and `esp32s2`
using ESP-IDF `v6.0.1`. Local ESP-IDF evidence exists only when `idf.py` is on
`PATH` and an actual build log is recorded.

Use:

```bash
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

When local ESP-IDF tooling is unavailable, cite the relevant CI workflow run
instead. For v1.0.0 release prep on this machine, `idf.py` was not on `PATH`,
so pure ESP-IDF evidence must come from the GitHub Actions run triggered by the
release tag. Do not claim a pure ESP-IDF build passed without a local log or a
reviewed CI log for the target commit.

## Contract Guards

Run these checks after IDF-example changes:

```bash
python tools/check_idf_example_contract.py
python tools/check_cli_contract.py
```

The guards reject stale docs saying the native ESP-IDF example is absent,
placeholder command behavior, unsafe parse-to-byte casts, no-op color support,
Arduino compatibility leakage, and `selftest output` implementations that do
not perform a real output-changing restore flow.
