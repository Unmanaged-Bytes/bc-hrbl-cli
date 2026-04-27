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
- `bc-hrbl inspect <file> [-o OUT]` — dump to stdout / file as pretty JSON.
- `bc-hrbl convert --from=json <in> [-o OUT]` — bootstrap `.hrbl` from
  a JSON document (via json-c).
- `bc-hrbl convert --to=json <in> [-o OUT]` — one-way JSON export.
- `bc-hrbl convert --to=yaml <in> [-o OUT]` — one-way YAML export
  (via libyaml).
- `bc-hrbl convert --to=ini <in> [-o OUT]` — one-way INI export.

## Build

Requires bc-* sibling libs installed via `bc-install` (rolling).

```
meson setup build --buildtype=release
meson compile -C build
./build/src/bc-hrbl --help
```

## License

MIT — see [LICENSE](LICENSE).
