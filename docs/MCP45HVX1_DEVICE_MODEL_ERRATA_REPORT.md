# MCP45HVX1 Device Model And Errata Gate Report

Prompt: 04 - Datasheet device model, POR/BOR, SHDN/WLAT, fraction contract, and
errata release gate.

## Datasheet Facts Verified

- MCP45HV31 is the 7-bit/128-tap variant: max Wiper code `0x7F`, POR/BOR Wiper
  default `0x3F`.
- MCP45HV51 is the 8-bit/256-tap variant: max Wiper code `0xFF`, POR/BOR Wiper
  default `0x7F`.
- Wiper0 is register `0x00`; TCON0 is register `0x04`.
- TCON0 POR/BOR default is `0xFF`; reserved bits `[7:4]` are forced high by the
  driver and expected high on readback.
- Standard I2C address range remains `0x3C-0x3F`; alternate `0x5C-0x5F` remains
  explicit opt-in because the datasheet extract is contradictory.
- The maintained timing fact is `TBORD` = 10 us typical / 20 us maximum after
  reset exit (`VL > VBOR`). No separate host-facing `tPOR` delay name is used by
  the core contract.

## Docs Fixed

- README, public Doxygen, implementation manual, initialization notes, modes
  notes, register map, and register reference now state that SHDN is an external
  active-low pin and WLAT is external to core ownership.
- Documentation now says Wiper/TCON register readback proves volatile register
  contents only; it does not prove physical analog movement when WLAT, SHDN, or
  external circuitry overrides the output.
- Startup documentation now requires the application to ensure rails, reset,
  SHDN, WLAT, and board timing are stable before `begin()`. The core does not
  insert arbitrary POR/BOR delays and `Config::nowMs` is used for timestamps,
  not startup delays.
- Package and README descriptions no longer claim production readiness without
  hardware evidence.

## Errata Gate Added

- `SiliconErrataInfo` exposes release-gate, shared-bus risk-acceptance, and
  General Call isolated-bus evidence flags.
- Arduino and ESP-IDF CLIs print those errata gate fields.
- `docs/MCP45HVX1_HARDWARE_VALIDATION.md`, `docs/register_reference.md`, and
  `docs/MCP45HVX1_RELEASE_CHECKLIST.md` require silicon/errata review before
  production release.
- Output-changing General Call commands require isolated-bus evidence; shared
  bus deployments require documented risk acceptance and cannot claim General
  Call safety without logged evidence.

## Behavior/Doc Mismatches Fixed

- `writeWiperFraction()` remains rejection-based for high-voltage
  output-changing writes. Header and README now state that out-of-range or NaN
  input returns `Err::INVALID_PARAM`; only pure conversion helpers clamp.
- Direct 7-bit Wiper writes above `0x7F` are documented as rejected before I2C
  access instead of relying on ambiguous silicon behavior.
- APIs named shutdown are documented as TCON/R0HW software shutdown controls,
  not external SHDN pin controls.

## Tests Added

- Native tests now assert fraction out-of-range and NaN rejection without
  changing the fake device state.
- Native tests now assert HV31/HV51 POR defaults, Wiper/TCON register addresses,
  standard/alternate address constants, and `DeviceInfo` defaults.
- Native tests now assert TCON reserved-bit sanitization and the actual payload
  sent for a low-nibble TCON write.
- Native tests now assert the new errata gate fields.

## Validation

- `python tools/check_core_timing_guard.py` - PASSED.
- `python tools/check_cli_contract.py` - PASSED.
- `python tools/check_idf_example_contract.py` - PASSED.
- `python tools/check_generated_artifacts.py` - PASSED.
- `python scripts/generate_version.py check` - PASSED; `Version.h` up to date.
- `python -m platformio test -e native` - PASSED; 55 test cases succeeded.
- `python -m platformio run -e esp32s3dev` - SUCCESS.
- `python -m platformio run -e esp32s2dev` - SUCCESS.
- `python -m platformio pkg pack` - SUCCESS; generated
  `MCP45HVX1-1.0.0.tar.gz` and the artifact was removed before commit.

## Remaining Hardware Validation

- Actual board/silicon address range, errata applicability, General Call
  behavior, WLAT behavior, SHDN behavior, analog output movement, wiper current,
  and safe-load limits still require logged hardware validation.
- Software readback cannot prove analog output movement under WLAT, SHDN, or
  external circuit override.
