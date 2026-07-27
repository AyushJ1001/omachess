# Omachess component notices

Omachess itself, including its owned code, launcher icon, and other owned
assets, is copyright its contributors and is distributed under
GPL-3.0-or-later. The complete terms are installed at
`/usr/share/licenses/omachess/LICENSE`.

## Cburnett Piece Set

Copyright Colin M.L. Burnett. The twelve SVG chess pieces in
`app/qml/pieces/cburnett/` are redistributed under GPL-2.0-or-later. Their
preferred source form is the SVG itself. The directory's `README.md` records
the author, Wikimedia Commons source URLs, retrieval date, and exact upstream
to packaged file mapping.

## Fairy-Stockfish

Fairy-Stockfish is the vendored Rules Authority and is distributed under
GPL-3.0-or-later. Its preferred source is included under
`vendor/fairy-stockfish/`, including `AUTHORS`, `Copying.txt`, upstream
`README.md`, and `OMACHESS-VENDORING.md`. The latter records the exact pinned
commit, the files omitted from upstream, and the build configuration.

### incbin

Fairy-Stockfish's source includes `src/incbin/incbin.h`, copyright Dale Weiler.
It is a separately authored public-domain component released under the
Unlicense. Its complete terms ship verbatim at
`vendor/fairy-stockfish/src/incbin/UNLICENCE`.

## Engine Profile artwork

- Stockfish: official project favicon from
  <https://github.com/official-stockfish/official-stockfish.github.io/blob/main/favicon.png>,
  distributed under the project's GPL-3.0 terms.
- Leela Chess Zero: official site logo from
  <https://github.com/LeelaChessZero/lczero.org/blob/master/static/images/logo.svg>,
  distributed under GPL-3.0-or-later.
- Reckless: official project image from
  <https://github.com/codedeliveryservice/Reckless#readme>, distributed under
  AGPL-3.0.

These marks identify their projects and do not imply endorsement. The same
provenance is retained alongside their preferred SVG source in
`app/qml/engine-art/README.md`. Omachess does not
redistribute Komodo artwork because no redistribution grant is documented.
No third-party board textures are included; Board Themes are Omachess-owned
colour definitions.

## Build-time and system components

Rust dependency names, exact versions, checksums, and transitive relationships
are locked in `Cargo.lock`. The release build may contain code from these
crates (the list conservatively includes target-specific and build-time
dependencies):

| Crate versions | Licence |
| --- | --- |
| ahash 0.8.12; bitflags 2.13.1; cfg-if 1.0.4; errno 0.3.14; fallible-iterator 0.3.0; fallible-streaming-iterator 0.1.9; fastrand 2.5.0; getrandom 0.4.3; hashbrown 0.14.5; hashlink 0.9.1; libc 0.2.189; once_cell 1.21.4; pkg-config 0.3.33; proc-macro2 1.0.107; quote 1.0.47; rusqlite 0.32.1; smallvec 1.15.2; syn 2.0.119; tempfile 3.27.0; vcpkg 0.2.15; version_check 0.9.5; windows-link 0.2.1; windows-sys 0.61.2 | MIT and/or Apache-2.0, as selected by each crate's Cargo metadata |
| libsqlite3-sys 0.30.1 | MIT |
| linux-raw-sys 0.12.1; rustix 1.1.4 | Apache-2.0 WITH LLVM-exception, Apache-2.0, or MIT |
| r-efi 6.0.0 | MIT, Apache-2.0, or LGPL-2.1-or-later |
| unicode-ident 1.0.24 | (MIT or Apache-2.0) and Unicode-3.0 |
| zerocopy 0.8.55; zerocopy-derive 0.8.55 | BSD-2-Clause, Apache-2.0, or MIT |

Each crate's canonical source archive, full licence files, authorship notices,
and checksum are identified by the crates.io package and `Cargo.lock`. Qt,
SQLite, the Rust and C++ toolchains, CMake, Ninja, Python, Omarchy, and the
hicolor icon theme are unmodified system/build dependencies and are not copied
into the Omachess package.
