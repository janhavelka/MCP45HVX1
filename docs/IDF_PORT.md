# ESP-IDF Port

The core library is framework-neutral. Public headers and `src/` do not include Arduino or ESP-IDF framework headers, and all hardware access is supplied through `Config` callbacks.

The ESP-IDF example in `examples/espidf_basic` is a native IDF application:

- entry point is `app_main()`
- I2C uses `driver/i2c_master.h`
- timestamps use an injected `Config::nowMs` callback backed by `esp_timer_get_time()`
- delays use `vTaskDelay()`
- command input uses fixed C buffers and `fgets()`

The IDF example must not include Arduino sources or use Arduino compatibility facades. `tools/check_idf_example_contract.py` enforces the native-IDF boundary and command coverage.

The contract guard also rejects advertised placeholder commands, unsafe
parse-to-byte casts, no-op color support, and `selftest output` behavior that
does not perform an explicit output-changing restore flow. The native CLI uses
bounded command-specific parsing, ANSI color controlled by `color on|off`, and
state/health/config output that includes cache-known flags and hardware
uncertainty fields.
