# Local ROM output

Run `make release` after the setup described in the [main README](../README.md).
It builds the game and places these **local-only** files here:

- `tyrian-md.bin` — the complete 8 MiB / 64 Mbit SSF2 cartridge.
- `SHA256SUMS` — SHA-256 of that ROM.
- `build-info.json` — source revision, working-tree status and dependency pins.

These outputs contain or describe locally converted Tyrian data and are ignored
by Git. The public repository does **not** distribute the original assets,
converted graphics/audio/tables, or a complete ROM. GPL licensing of the engine
does not relicense the game data. Do not attach the ROM to GitHub Releases or
upload it to another public host without permission from the relevant rights
holders. See [licensing and provenance](../docs/licensing.md).
