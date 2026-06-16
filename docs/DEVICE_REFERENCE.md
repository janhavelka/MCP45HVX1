# MCP45HVX1 Device Reference

This is the maintained device-facing reference for the driver. It consolidates
the previous datasheet extraction notes, register reference, and errata
implementation notes into one concise document.

Source documents are kept in [`reference-pdfs/`](reference-pdfs/). This file is
not hardware validation evidence.

## Device Family

Supported variants:

| Variant | Resolution | Taps | Wiper POR/BOR default |
|---|---:|---:|---:|
| MCP45HV31 | 7-bit | 128 | `0x3F` |
| MCP45HV51 | 8-bit | 256 | `0x7F` |

Nominal RAB options are `5 kOhm`, `10 kOhm`, `50 kOhm`, and `100 kOhm`.
Resistance helper APIs are ideal calculations only. They do not model tolerance,
wiper resistance, leakage, INL/DNL, temperature, rail voltage, or board loading.

The driver treats the part as volatile-only. No user-accessible non-volatile
memory API is exposed because the implemented register map used by this driver
contains volatile Wiper 0 and TCON0 state only.

## Address Policy

Documented 7-bit I2C addresses are `0x3C..0x3F` from A1:A0 strap pins.

The extracted source material also contains a conflicting `0x5C..0x5F` address
hint in command figures. The driver rejects that alternate range unless
`Config::allowAlternateAddressRange` is explicitly enabled for
hardware-verification builds. Address strap behavior must be validated on the
actual board before release.

## Pins And Physical Overrides

The core driver does not own pins, rails, GPIO control, delays, or reset
sequencing.

Important pins:

- `SCL` / `SDA`: application-owned I2C bus lines.
- `A1` / `A0`: address strap pins.
- `WLAT`: external wiper-latch pin. When asserted, physical output can remain
  held even while Wiper register writes are accepted and read back.
- `SHDN`: external active-low shutdown pin. It can override terminal output
  without corrupting Wiper/TCON register contents.
- `V+`, `V-`, `VL`, and grounds: board-level rail and reference ownership.

Register readback proves volatile register contents only. It does not prove
analog movement when WLAT, SHDN, or external circuitry overrides terminals.

## Electrical And Safety Notes

Software cannot enforce analog rail voltage, terminal current, thermal limits,
or load impedance. Use the hardware validation procedure before claiming analog,
high-voltage, or safe-load behavior.

Driver terminal-current helper limits:

| RAB option | Helper limit |
|---|---:|
| `R5K` | `25.0 mA` |
| `R10K` | `12.5 mA` |
| `R50K` / `R100K` | `6.5 mA` |

These are documentation/helper limits, not runtime enforcement.

## Register Map

Implemented volatile registers:

| Address | Name | Access | Reset |
|---:|---|---|---:|
| `0x00` | Volatile Wiper 0 | R/W/INC/DEC | `0x7F` 8-bit, `0x3F` 7-bit |
| `0x04` | Volatile TCON0 | R/W | `0xFF` |

Reserved ranges:

- `0x01..0x03`
- `0x05..0xFF`

The driver rejects reserved addresses before issuing I2C transactions.

## Command Byte

```text
bit 7..4: AD3:AD0 register address
bit 3..2: C1:C0 command
bit 1..0: D9:D8 compatibility bits, written as 0 by this driver
```

Command values:

| C1:C0 | Driver constant | Meaning |
|---:|---|---|
| `00` | `Command::WriteData` | Write register data byte |
| `01` | `Command::Increment` | Increment Wiper 0 |
| `10` | `Command::Decrement` | Decrement Wiper 0 |
| `11` | `Command::ReadData` | Read selected register |

Register reads return two bytes. With the default configuration, the driver
requires the first byte to be `0x00` and returns the second byte as register
data.

## Wiper 0

Wiper writes accept raw codes within the active variant range:

- MCP45HV31: `0x00..0x7F`
- MCP45HV51: `0x00..0xFF`

Increment and decrement commands clamp at the variant endpoints. Multi-step
helpers and poll jobs split long step sequences into bounded command chunks.

## TCON0

TCON0 controls software terminal switches. It is not the external SHDN pin.

| Bit | Mask | Name | Meaning when 1 | Meaning when 0 |
|---:|---:|---|---|---|
| `[7:4]` | `0xF0` | Reserved | Read as `1111` | Driver never writes 0 |
| `3` | `0x08` | `R0HW` | Normal operation | Software shutdown |
| `2` | `0x04` | `R0A` | P0A connected | P0A disconnected |
| `1` | `0x02` | `R0W` | P0W connected | P0W disconnected |
| `0` | `0x01` | `R0B` | P0B connected | P0B disconnected |

Driver presets:

| Preset | TCON value |
|---|---:|
| Potentiometer | `0xFF` |
| Rheostat B-W | `0xFB` |
| Rheostat A-W | `0xFE` |
| Wiper floating | `0xFD` |
| Software shutdown | `0xF7` |

Valid lower-bit combinations that do not match a named preset decode as
`TerminalMode::Custom`. `Custom` is a read/decode result and is not accepted by
`setTerminalMode()`.

## General Call

The driver sends only documented General Call command forms and does not expose
the undocumented `GCEN` bit.

| Command byte | Driver helper | Payload |
|---:|---|---|
| `0x80` | `generalCallWriteWiper()` | One Wiper 0 data byte |
| `0xC0` | `generalCallWriteTcon()` | One TCON0 data byte |
| `0x84` | `generalCallIncrementWiper()` | None |
| `0x88` | `generalCallDecrementWiper()` | None |

General Call helpers require `Config::allowGeneralCall = true`. When disabled,
they return `UNSUPPORTED` without bus traffic or cache changes. Successful
General Call helpers mark the affected cache unknown because ACK is broadcast
and not device-specific.

General Call output-changing commands are release gated. Review DS80000649B and
newer errata against the actual package marking/date code. Use an isolated bus
for affected or unknown silicon unless the release record contains explicit
shared-bus risk acceptance.

## Startup And Reset

`begin()` is read-only by default. It reads Wiper and TCON before optional
startup writes. Optional startup writes are enabled only by explicit
configuration flags.

POR/BOR defaults:

- Wiper: `0x3F` for 7-bit variants, `0x7F` for 8-bit variants
- TCON0: `0xFF`

DS20005304B specifies `TBORD`, delay after device exits reset state with
`VL > VBOR`, as 10 us typical and 20 us maximum. The driver does not own host
delays or rail sequencing; applications must ensure rails, SHDN, and WLAT are
stable before calling `begin()`.

`resetI2cState()` runs only an optional application-provided bus/software-reset
callback. A successful callback does not prove device presence, clear hardware
uncertainty, refresh cache, or mark an offline driver ready.

`recover()` reads Wiper then TCON with tracked transactions. Both reads must
succeed before the driver returns to READY and before all affected hardware
uncertainty can clear.

## Errata Gate

DS80000649B documents I2C-interface anomalies that can affect shared-bus and
General Call behavior. Treat Rev A1 through `1518NNN`, unreadable markings, or
unknown silicon as affected until reviewed.

Release validation must record:

- package marking/date code
- datasheet and errata revisions reviewed
- isolated-bus evidence or shared-bus risk acceptance for General Call
- address strap behavior for the populated board
- SHDN/WLAT wiring and physical behavior
- analog output movement and safe-load/current measurements

Use [`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md) for
the evidence template.
