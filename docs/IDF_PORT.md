# MCP45HVX1 ESP-IDF Port

Scope: keep the MCP45HVX1 driver usable from both Arduino/PlatformIO and pure
ESP-IDF while preserving the same bring-up example functionality.

## Result

- Core driver remains framework-neutral for I2C ownership. All bus access still
  goes through `Config::i2cWrite` and `Config::i2cWriteRead`.
- `src/MCP45HVX1.cpp` no longer requires Arduino headers for ESP-IDF builds.
- If `Config::nowMs` is not supplied, Arduino/native-test builds use
  `millis()` and ESP-IDF builds use `esp_timer_get_time() / 1000`.
- Root `CMakeLists.txt` and `idf_component.yml` make the library consumable as
  an ESP-IDF component.
- `examples/espidf_basic` builds the same CLI implementation used by
  `examples/01_basic_bringup_cli`.

## Shared Example Strategy

The Arduino CLI is the source of truth. The ESP-IDF example sets
`MCP45HVX1_EXAMPLE_PLATFORM_IDF=1`, includes
`examples/common/IdfArduinoCompat.h`, defines `Serial` and `Wire`, then includes
the Arduino CLI source.

This keeps these flows aligned across both frameworks:

- scan and bus diagnostics
- begin/reconfigure commands
- probe/recover and settings views
- Wiper and TCON reads/writes
- terminal mode helpers
- direct register access
- last-address reads
- General Call frames
- volatile default restore
- interface reset
- self-test, stress, and stress_mix

## ESP-IDF Example Glue

`examples/common/IdfArduinoCompat.h` is example-only. It is not part of the
driver API.

It provides:

- `millis`, `delay`, `delayMicroseconds`, and `yield`
- GPIO helpers used by bus recovery and interface reset
- a fixed-capacity `String` subset used by the CLI parser
- a nonblocking stdin/stdout `Serial` replacement
- a `Print` alias for helper functions that accept `Print&`
- a `TwoWire`-shaped adapter backed by ESP-IDF v6 `driver/i2c_master.h`
- direct `writeStatus` and `writeReadStatus` helpers used by
  `examples/common/I2cTransport.h` under ESP-IDF

The adapter preserves MCP45HVX1-specific transport behavior:

- `i2cWriteRead(..., txLen > 0, ...)` uses
  `i2c_master_transmit_receive()`.
- `i2cWriteRead(..., txLen == 0, ...)` uses `i2c_master_receive()` for the
  documented last-address read format.
- General Call writes to address `0x00` are sent through ESP-IDF defined I2C
  operations with manual address bytes, using a device handle configured with
  `I2C_DEVICE_ADDRESS_NOT_USED`.
- The bus reset callback stays in example glue and never moves into the driver.

ESP-IDF errors map to library `Status` values:

- `ESP_OK` -> `Status::Ok()`
- `ESP_ERR_TIMEOUT` -> `Err::I2C_TIMEOUT`
- `ESP_ERR_INVALID_ARG` -> `Err::INVALID_PARAM`
- `ESP_ERR_INVALID_RESPONSE` / `ESP_ERR_NOT_FOUND` -> `Err::I2C_BUS`
- other errors -> `Err::I2C_ERROR`

Timeouts are clamped before passing into ESP-IDF transfer APIs so an overflow
cannot become an infinite wait.

## Component Files

Core component:

```cmake
idf_component_register(
  SRCS "src/MCP45HVX1.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_timer
)
```

Example component:

```cmake
idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "." "../../common" "../../.."
  REQUIRES MCP45HVX1 esp_driver_i2c esp_driver_gpio esp_timer freertos vfs
)
```

The IDF example targets ESP32-S2 and ESP32-S3 and requires ESP-IDF `>=6.0.1`.

## Remaining Integration Notes

- Applications still own SDA/SCL pins, pull-ups, I2C clock, and bus lifetime.
  The library does not create buses or devices.
- A real ESP-IDF application can either reuse the example adapter or provide a
  smaller project-specific adapter directly from its own `i2c_master_dev_handle_t`.
- The CLI shim is intentionally narrow. If the Arduino example starts using more
  of Arduino `String`, `Print`, or `TwoWire`, extend the shim in the same commit
  as the example change.
- General Call ACKs are broadcast, so command success means the I2C frame was
  accepted on the bus, not that one specific MCP45HVX1 state was verified. The
  existing CLI keeps the explicit `gc arm` workflow and cache-invalidating
  behavior.

## Validation

Completed locally:

- `python -m platformio test -e native`
- `python -m platformio run -e esp32s3dev`
- `python -m platformio run -e esp32s2dev`
- `python tools/check_cli_contract.py`
- `python tools/check_core_timing_guard.py`
- `python scripts/generate_version.py check`
- `python tools/validate.py` (completed; its internal `pio` shortcut reported
  `SKIP: pio not found`, so PlatformIO builds were run separately via
  `python -m platformio`)

Pending in this shell:

- `idf.py build` for `examples/espidf_basic`

`idf.py` was not available on PATH during this audit, so the ESP-IDF example is
implemented and documented but still needs a real ESP-IDF toolchain build before
release.
