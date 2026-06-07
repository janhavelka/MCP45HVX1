# MCP45HVX1 Release Checklist

This checklist is a release gate for production firmware and hardware claims.
Passing software tests alone is not enough to claim General Call safety, analog
accuracy, or production readiness.

## Silicon And Errata

- Record package marking, date code, datasheet revision, and errata revision.
- Review `DS80000649B` and any newer Microchip errata before release approval.
- Treat Rev A1 through `1518NNN`, unreadable markings, or unknown silicon as
  affected until proven otherwise.
- If silicon is affected or unknown, use an isolated MCP45HVX1 bus or record
  explicit shared-bus risk acceptance.
- Do not enable output-changing General Call commands on a shared bus without
  isolated-bus evidence and documented system-level acceptance.

## Startup And Pins

- Confirm VL, V+, V-, reset, SHDN, and WLAT are stable before `begin()`.
- Account for `TBORD` = 10 us typical / 20 us maximum after reset exit.
- Document SHDN active-low hardware behavior and WLAT board strap/control.
- Verify that any optional startup Wiper/TCON writes are intentional for the
  analog circuit and have a recovery path if they fail.

## Hardware Validation

- Validate safe load, rail limits, wiper current, and analog output externally.
- Confirm register readback is not treated as proof of physical movement when
  WLAT, SHDN, or external circuitry can override the output.
- Log HIL measurements for Wiper, TCON, shutdown, WLAT, and General Call cases.
- Record whether the standard address range `0x3C-0x3F` or alternate range
  `0x5C-0x5F` was verified on the actual board.

## Software Release

- Run the repo validation scripts and PlatformIO native/ESP32 builds.
- Confirm generated artifacts and version metadata match the intended release.
- Keep release notes explicit about remaining hardware-validation limitations.
