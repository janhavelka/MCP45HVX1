# MCP45HVX1 Device Reference

Device facts the driver depends on, and the assumptions it makes where the
sources are ambiguous. Source PDFs are in [`reference-pdfs/`](reference-pdfs/).

Primary source: **DS20005304B** (Rev B, © 2014-2023). Errata gate:
**DS80000649B** (Rev B, 7/2015). Section and table numbers below refer to
DS20005304B. This file is not hardware validation evidence.

## Device Family

| Variant | Resolution | Taps | Resistor steps | Wiper POR/BOR default |
|---|---:|---:|---:|---:|
| MCP45HV31 | 7-bit | 128 | 127 | `0x3F` |
| MCP45HV51 | 8-bit | 256 | 255 | `0x7F` |

Both variants share the I2C protocol, package options, TCON register,
SHDN/WLAT behavior, supply constraints, and temperature grade. Nominal RAB
options are 5 k, 10 k, 50 k, and 100 kOhm (ordering codes `-502`, `-103`,
`-503`, `-104`), each ±20%.

The driver treats the part as volatile-only. §4.4 states the memory space
"contains only volatile locations", so no non-volatile API is exposed.

## I2C Addressing

**The 7-bit address range is `0x3C..0x3F`.** §6.2.4: "The A6:A2 address bits
are fixed to `01111` and the device appends the value of following two address
pins (A1 and A0)." Table 6-2 gives `0111 1'b + A1:A0`, and Figure 6-9 shows the
same bit layout. Up to four devices can share a bus by strapping A1/A0.

Table 6-2 Note 1 is the source of a common confusion:

> The fixed portion of the I2C address is different than the
> MCP44XX/MCP45XX/MCP46XX family (`0101 11'`, `0101 1'`, or `0101'`). This
> allows the maximum number of both standard and high-voltage devices on the
> single I2C bus.

The second sentence is the point: the two families are given disjoint fixed
bits *so that they can coexist*, not so that one may borrow the other's range.

Decoding Note 1 in the same notation used for this part (the apostrophe ends
the fixed portion; the remaining bits are strap pins, seven bits total):

| Note 1 form | 7-bit address | 8-bit write control byte |
|---|---|---|
| `0101 11'` + A0 | `0x2E..0x2F` | `0x5C`, `0x5E` |
| `0101 1'` + A1:A0 | `0x2C..0x2F` | `0x58`..`0x5E` |
| `0101'` + A2:A1:A0 | `0x28..0x2F` | `0x50`..`0x5E` |

So `0x5C..0x5F` are *control bytes* of the standard-voltage family, not 7-bit
addresses. Earlier revisions of this repository treated `0x5C..0x5F` as a
possible alternate 7-bit address range for MCP45HVX1. That reading was wrong
twice: the values belong to a different family, and they were transcribed from
the shifted control-byte column. As a 7-bit address `0x5C` is `0b1011100`,
whose fixed bits `10111` match neither `01111` (high-voltage) nor any `0101x`
(standard-voltage) form — it addresses no device in either family.

`Config::allowAlternateAddressRange` still accepts the range so an existing
hardware-verification build keeps working, but it is not supported by the
datasheet and is scheduled for removal — see finding 2 in
[`CODE_AUDIT.md`](CODE_AUDIT.md).

A1/A0 are Schmitt-trigger inputs with no documented internal pull resistors and
must be tied statically high or low (§6.1.3). SDA is open-drain and needs an
external pull-up sized from bus capacitance, speed, and the selected `VL`; the
datasheet specifies no value.

10-bit addressing is not supported.

## Register Map

Table 4-7. The address field in the command byte is four bits, so only
`0x00..0x0F` is addressable.

| Address | Name | Allowed commands | Reset |
|---:|---|---|---:|
| `0x00` | Volatile Wiper 0 | Read, Write, Increment, Decrement | `0x7F` 8-bit / `0x3F` 7-bit |
| `0x01`-`0x03` | Reserved | none | - |
| `0x04` | Volatile TCON0 | Read, Write | `0xFF` |
| `0x05`-`0x0F` | Reserved | none | - |

Increment and decrement are valid only on Wiper 0 (§7.1). Any invalid
address/command combination is NACKed and puts the device into an error state
that only a Start condition clears (§4.4.1.1, §7.3). The driver rejects
reserved addresses before issuing any I2C traffic.

Table 7-2 prints the reserved range as `05h-FFh` and several figures mention a
wiper at address `01h`. Both are boilerplate inherited from the dual-pot
MCP46XX datasheet; Table 4-7 governs and `0x01` is reserved on this part.

## Command Byte

```text
bit 7..4: AD3:AD0 register address
bit 3..2: C1:C0 command
bit 1..0: D9:D8 unused data bits, written as 0 by this driver
```

D9:D8 exist only for MCP44XX/45XX/46XX code compatibility; Table 7-2 Note 1
says the device ignores them.

| C1:C0 | Driver constant | Wiper 0 byte | TCON0 byte |
|---:|---|---:|---:|
| `00` | `Command::WriteData` | `0x00` | `0x40` |
| `01` | `Command::Increment` | `0x04` | invalid |
| `10` | `Command::Decrement` | `0x08` | invalid |
| `11` | `Command::ReadData` | `0x0C` | `0x4C` |

### Frames

- **Write** (§7.4.1, Figure 7-2): `S, addr+W, cmd, data, P` — three bytes.
  A Stop or Restart before the data ACK aborts the write; nothing is stored.
- **Increment / decrement** (§7.6-7.7): `S, addr+W, cmd [, cmd ...], P`. The
  wiper moves after each command acknowledge. The datasheet sets no limit on
  the number of repeats; `cmd::MAX_COMMAND_CHUNK` bounds what the driver emits
  so one transaction fits a typical transport buffer.
- **Random read** (§7.5, Figure 7-5): `S, addr+W, cmd(read), Sr, addr+R,
  byte0, byte1, NACK, P`.
- **Read from last address** (Figure 7-4): `S, addr+R, byte0, byte1, NACK, P`
  — no write phase and no command byte at all. The driver issues this as a
  write-read with a zero-length TX phase.

**Reads always return two bytes.** §7.5 Note: "The MSB of the 16 read bits is
all 0's to maintain read command format compatibility". The driver enforces
`byte0 == 0x00` by default (`Config::requireReadMsbZero`) and returns `byte1`
as the register value. This holds for TCON0 as well as Wiper 0.

The device retains its memory address pointer across Repeated Start and Stop
(Figure 7-4 Note 3) and resets it to `0x00` on POR/BOR (Note 4). The datasheet
never states that continuous reads auto-increment the pointer, so the driver
does not assume it.

The device does not stretch SCL (§6.2.2). A Stop condition resets the I2C
interface of every MCP45HVX1 on the bus (§6.2.1.5).

## Wiper 0

| Code | Meaning |
|---:|---|
| `0x00` | Zero scale, W connected to B. Decrement is ignored. |
| `0x3F` / `0x7F` | Mid scale (7-bit / 8-bit) — the POR/BOR default |
| `0x7F` / `0xFF` | Full scale, W connected to A (7-bit / 8-bit). Increment is ignored. |

Increment and decrement **saturate**; they do not wrap (§7.6, §7.7, Tables 7-4
and 7-5). The driver's cached wiper prediction saturates identically.

§7.6's prose says the value increments "up to `100h` … and `80h`". That
contradicts Table 5-2, Table 7-4, §5.1, and the 256/128-tap specification; it
is inherited from the 257-tap MCP4xx1 family. The correct maxima are `0xFF` and
`0x7F`, which is what the driver enforces.

## TCON0

Register 4-1. Bits 7:4 are `R-1`: read-only, always read as `1`.

| Bit | Mask | Name | 1 | 0 |
|---:|---:|---|---|---|
| 7:4 | `0xF0` | Reserved | reads `1111` | not writable |
| 3 | `0x08` | `R0HW` | normal operation | software shutdown |
| 2 | `0x04` | `R0A` | P0A connected | P0A disconnected |
| 1 | `0x02` | `R0W` | P0W connected | P0W disconnected |
| 0 | `0x01` | `R0B` | P0B connected | P0B disconnected |

POR/BOR loads `0xFF` (§4.4.1.2). The driver forces bits 7:4 high on every
write (`sanitizeTcon()`) and rejects a readback whose reserved bits are not all
high. TCON bits do not affect the Wiper register.

Driver presets:

| Preset | TCON |
|---|---:|
| Potentiometer | `0xFF` |
| Rheostat B-W | `0xFB` |
| Rheostat A-W | `0xFE` |
| Wiper floating | `0xFD` |
| Software shutdown | `0xF7` |

Any other valid combination decodes as `TerminalMode::Custom`, which is a
decode result only and is rejected by `setTerminalMode()`.

## SHDN And WLAT

Both are board-owned hardware pins. The core never drives them.

**SHDN** is active low (§5.7.1). When asserted: P0A is disconnected, P0W is
shorted to P0B, and the serial interface keeps running. It **overrides** the
TCON bits without corrupting them (§4.3.1.1); releasing it restores TCON
control. Internally the SHDN pin and `R0HW = 0` are OR-ed into the same
shutdown control (Figures 4-6 / 5-12), so either one produces the shutdown
topology. Tie SHDN high when hardware shutdown is unused.

**WLAT** is a wiper latch (§4.3.2). **High inhibits** transfer from the Wiper
register to the wiper; **low allows** it. I2C writes are still accepted and
read back while WLAT is high. Tie WLAT low if synchronized updates are unused.
Timing (Table 1-2): `TWLSU` ≥ 10 ns setup, `TWLHD` ≥ 250 ns hold relative to
the SCL rising edge of the write-data ACK bit, `TWLATL` ≥ 2 µs high or low.
Note 9 marks the window from 10 ns before to 200 ns after that edge as
indeterminate.

Consequently **register readback proves register contents only**. It does not
prove analog movement when WLAT, SHDN, or external circuitry overrides the
terminals.

## Resistance Math

Equation 5-4, simplified model (`RFS = RZS = 0`):

```text
RS  = RAB / 255   (8-bit)      RS  = RAB / 127   (7-bit)
RBW = n * RS
RAW = (FSV - n) * RS           FSV = 255 (8-bit) or 127 (7-bit)
```

**The denominator is 255 / 127, not 256 / 128.** Confirmed by the DC
characteristics table, Equation 5-2, and Equation B-1
(`RS(ideal) = RAB / (2^N - 1)`). This is what `stepResistanceOhms()`,
`resistanceBToWOhms()`, and `resistanceAToWOhms()` implement.

The detailed model adds the zero-scale and full-scale switch resistances
(`RBW = RZS + n*RS`, `RAW = RFS + (FSV-n)*RS`). Wiper resistance `RW` is **not**
part of Equation 5-4; §5.2 treats it as a function of wiper code, rail voltage,
terminal voltage, RAB, and temperature (typ 75 Ω, max 170 Ω at V± = ±18 V,
code `0x00`, `IW` = 1 mA). The driver's helpers are therefore ideal values and
model no tolerance, wiper resistance, leakage, INL/DNL, temperature, or board
loading.

## Electrical Limits

Terminal / wiper current `IT`, `IW` (DC characteristics), which is what
`maxTerminalCurrentMilliAmps()` reports:

| RAB option | Max continuous terminal current |
|---|---:|
| `-502` (5 k) | 25 mA |
| `-103` (10 k) | 12.5 mA |
| `-503` (50 k) | 6.5 mA |
| `-104` (100 k) | 6.5 mA |

36 mA applies only when the current path is the analog switch alone
(`IBW` at zero scale, `IAW` at full scale).

Table 5-3 gives a minimum safe wiper code at `VBW` = 36 V, below which `IW`
exceeds specification: 5 k and 10 k → `N` ≥ 91 (8-bit) / 45 (7-bit); 50 k →
35 / 17; 100 k → 17 / 8. The driver does not enforce this; a rheostat
application near 36 V must clamp the code itself.

Rails: `VL` 1.8-5.5 V; `V+ − V−` recommended operating span 10-36 V, never
above 36 V; `VL − V−` must not exceed 5.5 V; DGND and all terminal voltages
must stay between `V−` and `V+`; operating temperature −40 to +125 °C. The
`VL − V− ≤ 5.5 V` rule is easy to violate in dual-supply designs — `V− = −12 V`
with `VL = 3.3 V` is a 15.3 V delta and out of spec.

Software cannot enforce any of this. Use the hardware validation procedure
before claiming analog, high-voltage, or safe-load behavior.

## I2C Bus Speeds

| Mode | Max SCL | Bus capacitance | `VL` |
|---|---:|---:|---|
| Standard | 100 kHz | 400 pF | 1.8-5.5 V |
| Fast | 400 kHz | 400 pF | 2.7-5.5 V |
| High-Speed | 1.7 MHz | 400 pF | 4.5-5.5 V |
| High-Speed | 3.4 MHz | 100 pF | 4.5-5.5 V |

High-Speed mode is entered with the HS master code, which the device detects
but does not ACK, and exits on the next Stop (§6.2.6). §5.2.4 additionally
requires `VL − V− ≥ 2.7 V` for the interface to run at its maximum frequency.

## Reset And Startup

POR/BOR loads the mid-scale wiper default and TCON `0xFF`, and resets the
address pointer to `0x00`. `VPOR`/`VBOR` are below 1.8 V and POR/BOR is not
rate dependent. `TBORD`, the delay after the device exits reset with
`VL > VBOR`, is 10 µs typical and 20 µs maximum.

Between `VPOR/VBOR` and 2.7 V the device may respond to serial traffic while
not meeting datasheet electrical specifications (§4.2.1); below the BOR
threshold the interface is disabled and volatile registers can be corrupted by
an interrupted write (§4.2.2.1). Hold the application in a safe state until the
rails and `TBORD` are satisfied — the core owns no delays or rail sequencing.

Wiper settling time (Table 1-1, typical, ±1 LSb, `CL` = 50 pF): 1 µs for 5 k
and 10 k, 2.5 µs for 50 k, 5 µs for 100 k.

There is **no software device reset**. §8.2 documents an I2C *interface* reset
only:

1. Start condition
2. Nine `1` bits clocked out on SDA
3. Second Start condition
4. Stop condition

This resets the I2C state machine and leaves Wiper/TCON untouched. The core
cannot generate that waveform because it does not own the bus; supply it
through `Config::busReset`. `resetI2cState()` running that callback
successfully does not prove device presence, refresh the cache, clear
uncertainty, or bring an OFFLINE driver back to READY.

## General Call

General Call address byte is `0x00`. Table 6-3 and Figure 6-11 define the
commands; bit 0 of the second byte is the General Call / Hardware General Call
selector and bit 1 is a don't-care.

| Command byte | Driver helper | Payload |
|---:|---|---|
| `0x80` | `generalCallWriteWiper()` | one Wiper 0 data byte |
| `0xC0` | `generalCallWriteTcon()` | one TCON0 data byte |
| `0x84` | `generalCallIncrementWiper()` | none |
| `0x88` | `generalCallDecrementWiper()` | none |

Only one General Call command may follow a General Call control byte; further
commands are ignored and NACKed (§6.2.7). The reserved I2C-specification codes
(`0000 011'`, `0000 010'`, `0000 000'`) are NACKed by this device.

`GCEN` appears in General Call prose but no register address, bit position,
reset value, or access type for it is identifiable in the datasheet. The driver
does not expose it.

General Call helpers require `Config::allowGeneralCall = true`. When disabled
they return `UNSUPPORTED` without bus traffic or cache changes, and that
configuration gate is checked before the OFFLINE latch. A successful broadcast
marks the affected cache unknown, because a General Call ACK is not
device-specific.

## Errata Gate — DS80000649B

Three issues, all in the I2C interface, documented against Rev A1 silicon. The
errata lists no per-issue fix status for Rev A2. Marking guide: date code
`1518NNN` and earlier is Rev A1; `1520NNN` and later is Rev A2.

1. **Cross-client data capture.** "The MCP45HVX1 interface will accept the
   serial data that was intended for other devices on the I2C bus" whenever
   another device ACKs its own control byte, and interprets that data as its
   own commands. A read can also corrupt the other device's data by wired-AND
   on SDA. **Workaround: a dedicated bus with no other I2C devices.** This is
   the broadest issue and applies to *all* traffic, not just General Call.
2. **General Call address decode.** Invalid address/command combinations in the
   second byte do not abort; the device substitutes the address from the
   previous valid command, ACKs, and executes. Workaround: separate bus.
3. **Hardware General Call bit ignored.** Bit 0 of the second byte is not
   decoded, so a Hardware General Call whose upper bits match a device command
   is executed instead of skipped. **No workaround.**

Treat Rev A1, unreadable markings, or unknown silicon as affected. Record the
package marking and date code, the datasheet and errata revisions reviewed, and
either isolated-bus evidence or an explicit shared-bus risk acceptance before
enabling output-changing General Call use. See
[`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md).

## Packages And Pins

| TSSOP-14 | VQFN-20 | Symbol | Role |
|---:|---:|---|---|
| 1 | 1 | `VL` | Digital supply input |
| 2 | 2 | `SCL` | I2C clock input |
| 3 | 3 | `A1` | Address strap bit 1 |
| 4 | 4 | `SDA` | I2C bidirectional data |
| 5 | 5 | `A0` | Address strap bit 0 |
| 6 | 6 | `WLAT` | Wiper latch enable |
| 7 | 8-10, 17-20 | `NC` | No connect |
| 8 | 7 | `SHDN` | External active-low hardware shutdown |
| 9 | 11 | `DGND` | Digital ground |
| 10 | 12 | `V-` | Analog negative supply |
| 11 | 13 | `P0B` | Potentiometer terminal B |
| 12 | 14 | `P0W` | Potentiometer wiper terminal |
| 13 | 15 | `P0A` | Potentiometer terminal A |
| 14 | 16 | `V+` | Analog positive supply |
| - | 21 | `EP` | VQFN exposed pad; tie to `V-` or leave floating, never DGND |

TSSOP-14 `thetaJA` is about 100 °C/W; VQFN-20 about 36.1 °C/W. Do not connect
NC pins to any analog or high-voltage net. Recommended bypass: 0.1 µF on `VL`
within about 4 mm of the pin and 0.1 µF on `V+` as close as possible.

Ordering codes encode resolution, nominal RAB, temperature grade (`E`,
−40 to +125 °C), and package (`ST` = TSSOP-14, `MQ` = VQFN-20). Custom POR/BOR
wiper defaults exist only through Microchip's non-standard authorization
process; standard parts are mid-scale.

## Known Source Contradictions

Resolved by this reference; listed so a future reader does not rediscover them:

| Item | Resolution |
|---|---|
| Address `0x3C-0x3F` vs `0x5C-0x5F` | `0x3C-0x3F`. `0x5C-0x5F` is the standard-voltage family, per Table 6-2 Note 1. |
| §7.6 "increments to `100h` / `80h`" | Wrong. Maxima are `0xFF` / `0x7F`. |
| Table 7-2 reserved range `05h-FFh` | Only `00h-0Fh` is addressable (4-bit address field). |
| Figures naming a wiper at `01h` | MCP46XX boilerplate; `0x01` is reserved here. |
| Figures 7-7/7-8 showing `AD4:AD1` | Typo; the field is always `AD3:AD0`. |
| Random read 48 vs 40 bit clocks (Table 7-1 vs 7-3) | Unresolved in the datasheet; not load-bearing. |
| Cover title mentions non-volatile memory | The accessible register map is volatile only (§4.4). |
| Continuous-read pointer behavior | Never stated to auto-increment; do not assume it. |

Typical performance curves (DS20005307A) are graph-only and must be read from
the source PDF before being used as numeric requirements.
