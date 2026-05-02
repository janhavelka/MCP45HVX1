# MCP45HVX1 Digital Potentiometer Implementation Manual

This driver follows the workspace-standard I2C library pattern: injected
transport callbacks, explicit `Status` returns, managed lifecycle, health
counters, native fake-bus tests, and example-only Arduino helpers.

## Driver Model

The core library never calls `Wire` directly. Applications provide:

- `Config::i2cWrite`
- `Config::i2cWriteRead`
- `Config::i2cUser`

The example adapter in `examples/common/I2cTransport.h` maps `TwoWire` errors to
the driver `Status` model.

## Initialization

`begin()` validates configuration, reads Wiper 0 and TCON0 using the documented
random-read command format, and caches the values. It does not write the analog
state by default.

Optional startup writes are available:

- `Config::writeInitialWiper`
- `Config::writeInitialTcon`

Use these only when changing the wiper/terminal state during MCU startup is
intentional for the hardware design.

## Register Access

Only two addresses are implemented:

- `0x00`: Volatile Wiper 0, R/W/INC/DEC
- `0x04`: Volatile TCON0, R/W

All other addresses are rejected in software before I2C access. This avoids
intentionally generating the device's documented reserved-address NACK state.

MCP45HVX1 reads return two bytes. The first byte is the compatibility MSB and
should be `0x00`; the second byte is the 8-bit register value.

## Wiper Handling

The configured resolution controls input validation:

- `Resolution::Bits7`: `0x00-0x7F`
- `Resolution::Bits8`: `0x00-0xFF`

`incrementWiper()` and `decrementWiper()` send the documented command-byte-only
INC/DEC frames. The driver cache clamps at full-scale and zero-scale after
successful commands.

## TCON Handling

TCON reserved bits `[7:4]` are forced high on all driver writes. Public helpers
operate on the documented lower bits:

- `R0HW`: software shutdown when cleared
- `R0A`: P0A terminal connection
- `R0W`: P0W terminal connection
- `R0B`: P0B terminal connection

## Health Tracking

Tracked public I2C operations update:

- last success/error timestamp
- last error `Status`
- consecutive failures
- total failures/successes
- READY/DEGRADED/OFFLINE state

`probe()` uses the raw path and does not update health. `recover()` uses tracked
reads so communication failures are visible in health counters.

## General Call

The driver exposes helpers for the documented General Call frames. It does not
attempt to configure `GCEN` because the register location is not documented in
the available source notes.

