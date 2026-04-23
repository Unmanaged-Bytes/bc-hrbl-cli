# bc-hrbl

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Language: C11](https://img.shields.io/badge/language-C11-informational)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey)

> Command-line driver for the Hash-Routed Binary Layout format `.hrbl`.
> Built on top of the [`libbc-hrbl-dev`](../../libraries/bc-hrbl) library.

## Subcommands

- `bc-hrbl verify <file>` — integrity check (magic, version, checksum,
  layout, ordering, UTF-8).
- `bc-hrbl query <file> <path>` — lookup by dotted path
  (`server.ports[0].value`).
- `bc-hrbl inspect <file>` — dump to stdout as pretty JSON.
- `bc-hrbl convert --from=json <in> --to <out>` — bootstrap `.hrbl`
  from a JSON document.
- `bc-hrbl convert --to=json <in> --to <out>` — one-way export.

## Requirements

- Debian 13 (trixie) or any Linux distro with glibc ≥ 2.38
- `meson >= 1.0`, `ninja-build`, `pkg-config`
- `libbc-hrbl-dev (>= 1.0.0)`
- `libbc-core-dev (>= 1.3.1)`, `libbc-allocators-dev (>= 1.2.0)`,
  `libbc-io-dev (>= 1.1.1)`, `libbc-concurrency-dev (>= 1.1.1)`,
  `libbc-runtime-dev (>= 1.1.2)`

## Build

```
meson setup build --buildtype=release
meson compile -C build
./build/src/bc-hrbl --help
```

## License

MIT — see [LICENSE](LICENSE).
