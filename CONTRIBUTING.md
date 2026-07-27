# Contributing to Omachess

Omachess is a local-first Qt 6/QML Wayland workspace backed by a Rust core.
It needs no account, no hosted backend, and no network access to build or run.

## What you need

On Omarchy (Arch), everything is in the official repositories:

```bash
sudo pacman -S --needed base-devel cmake ninja rust qt6-base qt6-declarative \
  qt6-svg python sqlite
```

| Tool | Why |
| --- | --- |
| CMake ≥ 3.24, Ninja | builds the workspace and drives cargo |
| Rust (cargo) | builds the core |
| A C++ toolchain | builds vendored Fairy-Stockfish, which the core links |
| Qt 6.5+ Quick and Quick Controls | the workspace window |
| Qt 6 SVG | draws the vector Piece Set artwork |
| SQLite 3 | system library linked by the Live Store (`rusqlite`) |
| Python 3.11+ | runs the journey tests |

## Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/app/omachess
```

The one CMake build covers everything: it invokes `cargo` for the Rust core,
links it into the workspace binary as a static library, and builds the QML
module. There is no separate cargo step to remember.

The core's own `build.rs` compiles vendored Fairy-Stockfish and the rules
bridge, so the first build spends a minute or two on the engine and then reuses
it. Nothing is downloaded: the engine source is in the repository. The core
depends on the Live Store crate, which links against system SQLite.

For a release build, configure with `-DCMAKE_BUILD_TYPE=Release`. Installing
(`cmake --install build`) puts `omachess` on the path and installs the launcher
entry, the hicolor icon, and the installed documentation.

## Packaging

`packaging/PKGBUILD` is the AUR recipe: it builds Omachess from a signed source
tarball, depends hard on Omarchy 4, and installs nothing but program files, the
launcher entry, its icon, and `docs/install.md`. It has no install scriptlet,
no Hyprland rules, and no Omarchy hooks. Regenerate `packaging/.SRCINFO` with
`makepkg --printsrcinfo -p PKGBUILD > .SRCINFO` whenever the recipe changes;
a packaging test fails if the two drift apart.

`com.omachess.Omachess` is the desktop entry ID, the icon name, and the Wayland
app ID, and it is **fixed permanently for v0.1**. Changing it breaks every
player's window rules and keybindings, so it is not a refactor. `docs/install.md`
is the player-facing install, backup, export, and removal guidance and ships
inside the package.

Backup paths for the Live Store and preferences are documented in
[`docs/backup.md`](docs/backup.md).

## Tests

```bash
ctest --test-dir build --output-on-failure
```

That runs three suites:

- **Core unit tests** (`cargo test`) cover the Rust core's own behaviour.
- **Journey tests** launch the real `omachess` binary, drive it, and assert
  what ends up on screen.
- **Packaging tests** stage an installation with `cmake --install`, then assert
  what a player who installed the `omachess` package actually gets: the
  launcher entry, the icon, the desktop identity, the installed footprint, and
  the same workspace launching from the installed binary.

Journey tests are the highest acceptance seam in this project: they assert
externally observable behaviour of the running application, never QML
structure, Rust helpers, or storage layout. Prefer adding a journey over
testing an implementation detail.

They run on Qt's `offscreen` platform by default so they need no compositor.
To run the same journeys against a real Wayland session:

```bash
OMACHESS_BINARY=build/app/omachess OMACHESS_TEST_QPA=wayland \
  python3 -m unittest discover -s tests/journey -v
```

## How the pieces fit together

```
vendor/          Fairy-Stockfish, the Rules Authority
core/            the Rust core: owns all chess state
  rules/         the C bridge to the Rules Authority
  include/       the command-and-event C ABI header
store/           the Live Store: SQLite Personal Library persistence
app/src/         the workspace: C++ glue around the ABI
app/qml/         the workspace: how a game looks
  pieces/        Piece Set artwork
packaging/       the AUR recipe and its .SRCINFO, desktop entry, and icon
tests/journey/   launch-drive-assert tests against the real application
<<<<<<< HEAD
docs/backup.md   which XDG paths a player should copy
=======
tests/packaging/ the same, against a staged installation
>>>>>>> origin/main
```

## The Rules Authority

Every legal move, every SAN string, every FEN, and every game result comes from
vendored Fairy-Stockfish, through `core/rules/omachess_rules.h`. Nothing above
that bridge decides a chess question — not the Rust core, not the C++ glue, not
QML. A second implementation of any rule, however small, can only drift from
the engine, so there is not one anywhere.

`vendor/fairy-stockfish/OMACHESS-VENDORING.md` records the pinned commit, what
was removed, and how to update it.

The workspace holds no chess state. It submits **commands** describing player
intent (`{"type":"flip_board"}`) and applies the **events** the core answers
with (`{"type":"board_changed", ...}`), both as UTF-8 JSON across the C ABI in
`core/include/omachess_core.h`. Anything the player sees must arrive as an
event; if the workspace can compute it locally, that is a bug in the seam.

`app/src/TestChannel.h` documents the control socket journey tests use. It is
inert unless `OMACHESS_TEST_CHANNEL` is set, and it adds no behaviour of its
own: it synthesises ordinary input and reports what is on screen.

## Domain language

`CONTEXT.md` is the project's glossary. Use its terms — Game Record, Played
Game, Board Theme, Variant Definition — in code, comments, and commits, and
extend it rather than inventing parallel vocabulary.
