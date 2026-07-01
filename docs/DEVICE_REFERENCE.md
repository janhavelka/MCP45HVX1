# MCP45HVX1 Device Reference

This is the maintained device-facing reference for the driver. It consolidates
the previous datasheet extraction notes, register reference, and errata
implementation notes into one concise document.

Source documents are kept in [`reference-pdfs/`](reference-pdfs/). This file is
not hardware validation evidence.

Primary source: DS20005304B Rev B, June 2023. Errata gate: DS80000649B.
Typical curves and supplemental application notes are reference material for
manual review; graph values should be checked against the source PDFs.

## Device Family

Supported variants:

| Variant | Resolution | Taps | Wiper POR/BOR default |
|---|---:|---:|---:|
| MCP45HV31 | 7-bit | 128 | `0x3F` |
| MCP45HV51 | 8-bit | 256 | `0x7F` |

The 7-bit variant has 127 resistor steps and the 8-bit variant has 255
resistor steps. Both variants share the same I2C protocol, package options,
TCON register, SHDN/WLAT behavior, supply constraints, and temperature grade.

Nominal RAB options are `5 kOhm`, `10 kOhm`, `50 kOhm`, and `100 kOhm`.
Resistance helper APIs are ideal calculations only. They do not model tolerance,
wiper resistance, leakage, INL/DNL, temperature, rail voltage, or board loading.

The driver treats the part as volatile-only. No user-accessible non-volatile
memory API is exposed because the implemented register map used by this driver
contains volatile Wiper 0 and TCON0 state only.

The datasheet title mentions "Non-Volatile Memory", but the accessible register
body documents volatile Wiper/TCON registers and optional factory/custom default
settings only. Treat user-programmable NVM as unsupported unless Microchip
documentation proves otherwise.

## Packages And Ordering

Package options:

| Code | Package | Pins | Exposed pad | Notes |
|---|---|---:|---|---|
| `ST` | TSSOP-14 | 14 | No | `thetaJA` about `100 C/W`; package body about `5.10 mm x 6.40 mm` |
| `MQ` | VQFN-20 5 mm x 5 mm | 20 + EP | Yes, pin 21 | `thetaJA` about `36.1 C/W`; EP is connected to die substrate; connect to `V-` or leave floating, not DGND |

NC pins are not functional device pins. The source extraction contains both a
VQFN leave-unconnected note and a package-noise note suggesting ties to `VL` or
`DGND`; treat NC handling as a board-design review item and do not connect NC
pins to any analog/high-voltage net.

Ordering encodes resolution, nominal RAB, temperature grade, and package. The
standard temperature grade documented locally is `E` (`-40 C..+125 C`). Custom
POR/BOR wiper defaults may be available through Microchip's non-standard
customer authorization process; standard parts use mid-scale defaults.

## Address Policy

Documented 7-bit I2C addresses are `0x3C..0x3F` from A1:A0 strap pins.

The extracted source material also contains a conflicting `0x5C..0x5F` address
hint in command figures. The driver rejects that alternate range unless
`Config::allowAlternateAddressRange` is explicitly enabled for
hardware-verification builds. Address strap behavior must be validated on the
actual board before release.

MCP45HVX1 uses the MCP44XX/MCP45XX/MCP46XX command-byte shape for compatibility,
but it does not share their I2C address range. The SPI MCP41HVX1 family is a
separate device family and is not covered by this driver.

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

Pin mapping summary:

| TSSOP-14 | VQFN-20 | Symbol | Role |
|---:|---:|---|---|
| 1 | 1 | `VL` | Digital supply input |
| 2 | 2 | `SCL` | I2C clock input |
| 3 | 3 | `A1` | Address strap bit 1 |
| 4 | 4 | `SDA` | I2C bidirectional data |
| 5 | 5 | `A0` | Address strap bit 0 |
| 6 | 6 | `WLAT` | Wiper latch enable |
| 7 | 8, 9, 10, 17, 18, 19, 20 | `NC` | No connect |
| 8 | 7 | `SHDN` | External active-low hardware shutdown |
| 9 | 11 | `DGND` | Digital ground |
| 10 | 12 | `V-` | Analog negative supply |
| 11 | 13 | `P0B` | Potentiometer terminal B |
| 12 | 14 | `P0W` | Potentiometer wiper terminal |
| 13 | 15 | `P0A` | Potentiometer terminal A |
| 14 | 16 | `V+` | Analog positive supply |
| - | 21 | `EP` | VQFN exposed pad, tied to `V-` or floating |

A0 and A1 should be tied statically high or low; they are not intended as
runtime-selected pins. SDA is open-drain and requires an external pull-up sized
for bus capacitance, speed, controller limits, and the selected `VL`; no fixed
pull-up resistor value was found in the extracted text.

When `SHDN` is low, I2C continues to operate and TCON register bits retain their
stored values, but physical terminal behavior is overridden: P0A is
disconnected and P0W is shorted to P0B. Tie `SHDN` high if hardware shutdown is
not used.

When `WLAT` is high, I2C Wiper register updates can be accepted and read back
while physical wiper movement is held. Tie `WLAT` low if synchronized wiper
updates are not used. If WLAT is used, keep it stable around the ACK rising edge:
the maintained safe rule is at least 10 ns setup before SCL rising edge and
250 ns hold after SCL rising edge. The source note also describes an
indeterminate transition window from 10 ns before to 200 ns after that edge; use
the stricter 250 ns hold rule.

## Electrical And Safety Notes

Software cannot enforce analog rail voltage, terminal current, thermal limits,
or load impedance. Use the hardware validation procedure before claiming analog,
high-voltage, or safe-load behavior.

Key rail constraints:

| Quantity | Maintained value or rule |
|---|---|
| `VL` digital supply | `1.8 V..5.5 V` when `VL >= V- + 2.7 V`; otherwise use the stricter documented operating condition |
| `V+ - V-` analog span | recommended operating span `10 V..36 V`; never exceed `36 V` |
| Fully specified analog performance | requires the datasheet operating region; the extracted notes call out `V+ >= 20 V` relative to `V-` unless otherwise noted |
| `VL - V-` | must not exceed `5.5 V` |
| DGND | must remain between `V-` and `V+` |
| Terminal voltages | P0A, P0W, and P0B must remain between `V-` and `V+` |
| Operating temperature | `-40 C..+125 C` |
| Absolute rail ratings | old extraction recorded `V+` to DGND up to `+40 V` and `V-` to DGND down to `-40 V`; use source PDF for absolute-maximum review |
| ESD summary | old extraction recorded HBM `+/-5 kV` and MM `+/-400 V` |

The `VL - V- <= 5.5 V` constraint is easy to violate in dual-supply systems.
For example, `V- = -12 V` and `VL = 3.3 V` gives a 15.3 V delta and is outside
the maintained rail rule. Board rail sequencing or level shifting must prevent
that condition.

Driver terminal-current helper limits:

| RAB option | Helper limit |
|---|---:|
| `R5K` | `25.0 mA` |
| `R10K` | `12.5 mA` |
| `R50K` / `R100K` | `6.5 mA` |

These are documentation/helper limits, not runtime enforcement. The old
extraction also records endpoint terminal-current cases up to `36 mA`; review
the source electrical table before designing endpoint or pulsed-current loads.

Resistance option tolerances are about `+/-20%` in the old extraction. RAB
power and RBW power limits depend on actual resistance, code, current, voltage,
temperature, package thermal resistance, and board layout. Use measured loads
and source-PDF tables before claiming safe-load or high-voltage behavior.

Other useful electrical facts preserved from the extraction:

- SDA is the only digital output pin; SCL, A0, A1, WLAT, and SHDN are inputs.
- Digital input thresholds are Schmitt-triggered and scale with `VL`.
- The source notes list typical terminal capacitances around `75 pF` for P0A
  and P0B and `120 pF` for P0W at mid-scale. Treat these as design-review
  inputs, not driver behavior.
- Wiper resistance varies with rail voltage, terminal voltage, code, RAB option,
  and temperature. Do not use a single software constant for accuracy claims.
- Recommended bypass retained from the extraction: `0.1 uF` on `VL` within
  4 mm of the pin and `0.1 uF` on `V+` as close as possible. The extracted text
  did not clearly state a `V-` bypass value.
- Multi-layer PCBs with low-inductance ground/return planes are preferred for
  low-noise analog performance; breadboard-style wiring is not suitable for
  precision or high-voltage validation.

## I2C Interface

Supported I2C modes in the local extraction:

| Mode | Speed |
|---|---:|
| Standard | `100 kHz` |
| Fast | `400 kHz` |
| High-Speed | `1.7 MHz` with the extracted 400 pF bus condition |
| High-Speed | `3.4 MHz` appears in the timing table with the tighter 100 pF bus condition |

The protocol narrative emphasizes `1.7 MHz`, while the timing table also
contains `3.4 MHz` rows. Treat HS-mode speed selection as a board-level timing
review item and do not claim an HS-mode result without measured bus evidence.

Additional protocol facts:

- 10-bit I2C addressing is not supported.
- The device does not stretch SCL.
- SDA implements slope control and the SCL/SDA inputs include Schmitt-trigger
  behavior and spike suppression according to mode.
- A Stop condition resets the MCP45HVX1 I2C interface state.
- In High-Speed mode, the host sends the HS Master Mode Code first. The device
  detects it but does not ACK that byte, then returns to Fast/Standard behavior
  on Stop.
- The device memory address pointer defaults to Wiper 0 (`0x00`) after POR/BOR
  or the documented software-reset sequence.
- `readLastAddress()` reads from the most recent valid address pointer; random
  reads first set the pointer with a command byte and repeated Start.
- Mid-transfer Stop/Restart before the expected data ACK aborts the write; data
  is not written in that documented abort case.
- Invalid address/command combinations produce NACK and require the host to
  issue a new Start condition before sending valid traffic.

The documented software-reset sequence resets only the I2C interface state, not
Wiper/TCON contents:

1. Issue a Start condition.
2. Clock out nine `1` bits on SDA with SCL toggling.
3. Issue a second Start condition.
4. Issue a Stop condition.

The core cannot generate that waveform directly because transport ownership
belongs to the application. Use `Config::busReset` for board-specific support.

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

Continuous protocol support from the datasheet:

| Operation | Single bit clocks | Continuous bit clocks | Driver note |
|---|---:|---:|---|
| Write Data | 29 | `18n + 11` | Driver uses explicit bounded single-register writes |
| Read Data | 29 | `18n + 11` | Driver supports direct and last-address reads |
| Random Read | 40 | - | Implemented as command-byte write plus read |
| Increment / Decrement | 20 | `9n + 11` | Driver chunks multi-step helpers and poll jobs |

The old extraction records a source contradiction where a continuous-write
figure mentions Wiper address `0x01`, while the register table marks
`0x01..0x03` reserved. The maintained driver treats `0x01` as reserved.

## Wiper 0

Wiper writes accept raw codes within the active variant range:

- MCP45HV31: `0x00..0x7F`
- MCP45HV51: `0x00..0xFF`

Code `0x00` is zero scale, with P0W at P0B. Code `0x7F` is full scale for the
7-bit device and mid-scale for the 8-bit device. Code `0xFF` is full scale for
the 8-bit device. Step resistance is idealized as `RAB / 127` for MCP45HV31 and
`RAB / 255` for MCP45HV51.

Increment and decrement commands clamp at the variant endpoints. Multi-step
helpers and poll jobs split long step sequences into bounded command chunks.

Boundary behavior is not an error: increment at full scale and decrement at zero
scale are ignored by the device rather than NACKed.

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

Power-state facts retained from the old initialization notes:

- POR occurs when `VL` rises above the POR threshold from a de-energized state.
- BOR occurs when `VL` drops below the BOR threshold and restores the same
  volatile defaults as POR.
- POR/BOR is not rate-dependent in the extracted timing notes.
- VDPOR max `1.8 V` ensures Wiper reset in the maintained notes; VAPOR max
  `6.0 V` is the analog threshold note used in the old docs.
- The old reset-region table says serial traffic is ignored below BOR, is
  unknown between BOR and the minimum digital operating region, and is accepted
  only when `VL` and analog rails are in a valid operating state.
- If `V+` powers before `VL`, the old pin notes say the wiper is forced to
  mid-scale after the analog POR threshold is crossed. If `V+` powers after
  `VL` has exceeded the digital POR threshold, the wiper follows the Wiper
  register value after the analog POR threshold is crossed.
- Before POR completes, Wiper/terminal behavior is not a software-verifiable
  output state. Hold the application in reset or safe state until rails and
  `TBORD` are satisfied.

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

## Known Open Questions And Contradictions

These items were important in the original `00-08` extraction set and remain
preserved here:

- The cover title mentions non-volatile memory, while the accessible register
  descriptions expose only volatile Wiper/TCON RAM and factory/custom default
  behavior.
- `GCEN` is referenced by General Call prose, but no reliable register address,
  bit position, reset value, or access type was identified in the local source
  pages. The driver does not expose it.
- The I2C address conflict (`0x3C..0x3F` versus `0x5C..0x5F`) must be resolved
  by hardware evidence for a release board.
- The continuous-write figure note mentions address `0x01`, but the register
  table marks `0x01..0x03` reserved.
- The prose around maximum increment counts uses `100h` / `80h`; the maintained
  writable maximum code remains `0xFF` / `0x7F`.
- The extracted text does not clearly specify SCL/SDA pull-up resistor values;
  size pull-ups from bus capacitance, selected speed, and controller limits.
- The extracted text did not clearly specify `V-` bypass requirements.
- Behavior at `V+ = V-` and terminal behavior during analog rail ramp before
  POR completion are not described well enough to claim analog output behavior.
- Typical curve values from DS20005307A are graph-heavy and require direct PDF
  review before being used as numeric requirements.
- Application equations, accuracy tables, bandwidth, settling-time tables, log
  attenuation examples, and package land-pattern dimensions were intentionally
  not copied into this concise driver reference. They remain source-PDF review
  material for board design and analog-performance claims.
