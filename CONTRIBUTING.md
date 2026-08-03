# Contributing

Thank you for considering contributing to this project!

## Quick Start

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Run the repository checks: `python tools/validate.py`
5. Commit with a clear message: `git commit -m "feat: add X"`
6. Push and open a Pull Request

On Windows, individual PlatformIO commands must use the repository wrapper,
for example `.\scripts\pio.cmd run -e esp32s3dev -e esp32s2dev`. Other
platforms may use `pio` from `PATH`. The aggregate validator selects the
correct command automatically.

## Guidelines

### Code Style
- Follow the existing local style; this repository does not currently carry a
  formatter configuration file.
- Use `constexpr` instead of macros for constants
- Prefer explicit over implicit
- No heap allocations in steady-state library code
- Keep core code under `include/` and `src/` framework-neutral. Arduino and
  ESP-IDF APIs belong in examples or adapters.

### Commits
- Use [Conventional Commits](https://www.conventionalcommits.org/) format:
  - `feat:` new feature
  - `fix:` bug fix
  - `docs:` documentation only
  - `refactor:` code change that neither fixes a bug nor adds a feature
  - `test:` adding or updating tests
  - `chore:` maintenance tasks

### Pull Requests
- Keep PRs focused (one feature/fix per PR)
- Update documentation if needed
- Add changelog entry under `[Unreleased]`
- Run the relevant local checks from `docs/MCP45HVX1_RELEASE_CHECKLIST.md` and
  ensure CI passes

### What We Accept
- Bug fixes
- Documentation improvements
- Performance improvements (with benchmarks)
- New examples (if they demonstrate a common use case)
- Hardware-validation evidence and release-gate documentation

### What We Probably Won't Accept
- Breaking API changes without discussion
- Heavy dependencies
- Platform-specific code in the library core
- Features that add heap allocations in steady state
- Claims of hardware, analog, high-voltage, or industry readiness without logs
  and measurement evidence

## Questions?

Open a GitHub Discussion or Issue for questions.
