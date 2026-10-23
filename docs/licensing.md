# Licensing and asset policy

## Code

The port is licensed under **GNU GPL version 2 or any later version**
(`GPL-2.0-or-later`), following the license notices in the OpenTyrian source from
which parts of the portable game logic were adapted. The complete GPL version 2
text is in [LICENSE](../LICENSE); credits and dependency notices are in
[NOTICE](../NOTICE). Port-specific contributions use the same GPL terms.

This repository contains the port and its converters, not a vendored copy of
OpenTyrian, SGDK or a compiler. [bootstrap.py](../tools/bootstrap.py) obtains the
exact upstream revisions recorded in [dependencies.lock.json](../tools/dependencies.lock.json).
The music conversion helper compiles against downloaded OpenTyrian LDS/OPL
sources; the Genesis build links against the locally installed SGDK and libgcc.
Keep those dependencies' own license notices when redistributing their code or
permitted binaries. Downloading a dependency does not make it part of this
repository's copyright ownership.

## Tyrian game data

**Freeware is not the same as open-source or public-domain data.** The
[OpenTyrian FAQ](https://github.com/opentyrian/opentyrian/wiki/FAQ) distinguishes
its open-source engine from Tyrian's freeware release. The
[upstream README](https://github.com/opentyrian/opentyrian#game-data) directs
users to the Tyrian 2.1 archive; our downloader uses that same source and checks
its SHA-256 before extraction.

The archive contains a legacy `license.doc`. The later freeware announcement
does not provide a blanket GPL grant for all graphics, music, voices, text and
level data. We do not interpret it as permission to relicense or publish the
converted cartridge contents.

Daniel Cook separately released a
[remastered collection of ships and tiles](https://lostgarden.com/2007/04/05/free-game-graphics-tyrian-ships-and-tiles/).
That collection's permission must not be extended to the entire original game,
its music, or every picture in the data archive. This port reads the original
Tyrian 2.1 data, not the separate remastered collection.

Consequently, the public repository excludes:

- the original game archive and extracted `tyrian-data/`;
- generated sprite, picture, font, map, item/script and audio data;
- standalone title artwork derived from the original game;
- complete `.bin` cartridges, including binaries attached to releases;
- SGDK, OpenTyrian, emulator and compiler source/binary trees.

Users download the data locally and run the converters themselves. The project
does not grant additional rights to those assets. `make release` deliberately
creates a complete ROM only in the ignored local `release/` output files.
Public ROM distribution requires a separate rights review/authorization.

## Screenshots

The README gallery consists of unretouched emulator captures documenting this
port. They include copyrighted game graphics; the engine's GPL does not apply
to the depicted artwork. They are attributed to the original creators and are
not supplied as reusable game assets. Capture instructions and emulator/source
details are recorded in [screenshots/README.md](screenshots/README.md).

## Attribution

- Mega Drive port and conversion tooling: Jonathan Santos and contributors.
- OpenTyrian: The OpenTyrian Development Team and contributors.
- Original Tyrian programming/design: Jason Emery and the original team.
- Original artwork: Daniel Cook; other original contributors retain their work.
- Original music: Alexander Brandon and Andreas Molnar.
- Original publisher: Epic MegaGames; original developer: Eclipse Software.
- SGDK: Stephane Dallongeville and its contributors.

This is an unofficial project, with no claimed endorsement by those parties or
by Sega. See the downloaded dependencies for their complete upstream notices.
