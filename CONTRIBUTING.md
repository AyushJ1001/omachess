# Contributing to Omachess

Omachess is a local-first Qt 6/QML Wayland workspace backed by a Rust core.
It needs no account, no hosted backend, and no network access to build or run.

## What you need

On Omarchy (Arch), everything is in the official repositories:

```bash
sudo pacman -S --needed base-devel cmake ninja rust qt6-base qt6-declarative python
```

| Tool | Why |
| --- | --- |
| CMake ≥ 3.24, Ninja | builds the workspace and drives cargo |
| Rust (cargo) | builds the core |
| Qt 6.5+ Quick and Quick Controls | the workspace window |
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

For a release build, configure with `-DCMAKE_BUILD_TYPE=Release`. Installing
(`cmake --install build`) puts `omachess` on the path and installs
`com.omachess.Omachess.desktop`, which gives the window its stable app ID.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

That runs two suites:

- **Core unit tests** (`cargo test`) cover the Rust core's own behaviour.
- **Journey tests** launch the real `omachess` binary, drive it, and assert
  what ends up on screen.

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
core/            the Rust core: owns all chess state
  include/       the command-and-event C ABI header
app/src/         the workspace: C++ glue around the ABI
app/qml/         the workspace: how the board looks
tests/journey/   launch-drive-assert tests against the real application
```

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
