# Changelog

All notable changes to bc-hrbl (CLI) are documented here.

## [1.0.0]

Initial public release of the `bc-hrbl` command-line driver on top of
the `libbc-hrbl` library (also 1.0.0).

### Added

- Subcommand `verify` — integrity check, reports status name on failure.
- Subcommand `query` — dotted-path lookup on a mmap'd `.hrbl`.
- Subcommand `inspect` — pretty-JSON dump to stdout.
- Subcommand `convert` — JSON ↔ `.hrbl` bootstrap (one-way JSON parser
  provided as an ergonomic entry point; the binary format remains the
  source of truth).
- Built on bc-runtime CLI framework (`--help`, `--version`, `--threads`,
  structured error collector).

### Quality

- Full sanitizer coverage (asan / tsan / ubsan / memcheck).
- cppcheck clean.
- End-to-end tests exercising the installed binary.
- Fuzz harness on the JSON → `.hrbl` convert path.

[1.0.0]: https://github.com/Unmanaged-Bytes/bc-hrbl/releases/tag/v1.0.0
