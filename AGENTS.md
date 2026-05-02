# MCP45HVX1 Repository Notes

This repository follows the same driver layout used by the sibling I2C device
libraries in this workspace.

- Keep library code transport-agnostic. Core files under `include/` and `src/`
  must use injected callbacks from `Config`, not direct `Wire` calls.
- Keep example-only Arduino helpers under `examples/common/`.
- Use `Status` for all fallible public operations.
- Keep all writes scoped to this repository.
- Run `pio test -e native` and the scripts under `tools/` after changes when
  PlatformIO is available.

