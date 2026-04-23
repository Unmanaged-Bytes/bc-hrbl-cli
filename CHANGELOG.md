# Changelog

All notable changes to bc-hrbl (CLI) are documented here.

## [1.1.0]

### Added

- **Quoted path segments** in `bc-hrbl query`. Segments wrapped in `'`
  or `"` are treated as literal keys, so paths containing `.` or `[`
  can now be queried. Example:
  `bc-hrbl query manifest.hrbl "files.'src/main.c'.digest"`.
  Inherited from libbc-hrbl 1.1.0.

### Dependencies

- Build-Depends `libbc-hrbl-dev (>= 1.1.0)`.

## [1.0.0]

Initial public release of the `bc-hrbl` command-line driver on top of
the `libbc-hrbl` library (also 1.0.0).

### Added

- Subcommand `verify` — integrity check, reports status name on failure.
- Subcommand `query` — dotted-path lookup on a mmap'd `.hrbl`, prints
  scalars as plain text, blocks/arrays as pretty JSON.
- Subcommand `inspect` — pretty JSON dump to stdout or `-o OUT`.
- Subcommand `convert` — bootstrap `.hrbl` from JSON and one-way
  export to JSON / YAML / INI. `--from=json` drives the writer via
  json-c; `--to=json|yaml|ini` drives the corresponding exporter in
  libbc-hrbl.
- `--help` and `--version` banners.

### Quality

- Full sanitizer coverage (asan / tsan / ubsan / memcheck) via
  `bc-sanitize`.
- cppcheck clean.
- End-to-end cmocka tests (`tests/e2e/`) execute the installed binary
  via `fork+execv` and assert exit codes + stdout contents for the
  convert-verify-query-inspect pipeline.

[1.0.0]: https://github.com/Unmanaged-Bytes/bc-hrbl-cli/releases/tag/v1.0.0
