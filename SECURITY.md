# Security Policy

## Supported Versions

| Version | Supported |
|---|---|
| 1.1.x | Current pre-production line |
| 1.0.x | Security fixes only |

## Reporting a Vulnerability

If you discover a security vulnerability within this library, please follow responsible disclosure:

1. **Do NOT** open a public GitHub issue.
2. Email the maintainer at: `info@thymos.cz`.
3. Include:
   - A description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Any suggested fixes (optional)

We will acknowledge receipt within 48 hours and aim to provide a fix or mitigation within 14 days for critical issues.

## Scope

This library is designed for embedded systems that may control real analog and
high-voltage circuits. Security and safety considerations include:
- No dynamic memory allocation in steady-state core driver code
- No network code in the core driver
- No persistent storage in the core driver or bundled examples
- I2C transport callbacks, bus ownership, locking, timeout policy, and recovery
  policy are application responsibilities
- General Call and output-changing commands require hardware validation before
  use in deployed systems

## Security Best Practices for Users

- Validate external inputs before passing values to `Config` or output-changing
  Wiper/TCON APIs.
- Serialize access to each driver instance and its shared I2C transport.
- Treat failed state-changing writes as potentially applied until readback or
  `recover()` proves the volatile state.
- Use watchdogs and independent analog safety limits in deployed hardware.
- Keep toolchains and dependencies updated.
