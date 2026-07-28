# Omachess

A local-first, all-in-one chess workspace built for [Omarchy](https://omarchy.org)
4 (Quattro): play, study, and create forms of chess on your own machine.

No account, no hosted backend, no cloud sync, no telemetry. Your games live in
your own XDG directories, and chess never leaves the device.

Omachess is GPL-3.0-or-later, built from source as a single AUR package.

```bash
git clone https://aur.archlinux.org/omachess.git
cd omachess
makepkg -si
```

Full install, backup, and removal guidance: [`docs/install.md`](docs/install.md).

## v0.1 — an experimental release

v0.1 is a public experimental release meant for real personal play and
analysis. Your data is protected — migrations are transactional and fail
closed, and export and recovery paths are documented — while pre-1.0
presentation and formats may still change.

### Playing

- Local human-versus-human play, and human-versus-engine play with any Ready
  engine.
- Optional clocks; suspend a Played Game between sessions and resume it with
  its position and clock values intact.
- A Completed Game's chess history and result are immutable; its Game Metadata
  stays correctable.

### Analysing

- Live Position Analysis of the selected position, with principal variations,
  and Pinned Engine Lines that keep the engine and search context that produced
  them.
- Analysis Records derived from a Completed Game: independent, with a Source
  Snapshot and navigable provenance through the Record Graph.
- Computer Analysis over a whole game with Quick, Standard, and Deep budgets,
  honest duration estimates, and disclosed effective settings.
- Long analysis belongs to an on-demand D-Bus worker, so it survives closing
  the workspace — with your explicit consent — and appears in a first-class
  Omarchy Background Controls plugin with pause, resume, cancel, and deep links
  back to the record.

### Keeping

- A Personal Library of Game Records, Studies, and Variant Definitions in a
  SQLite Live Store.
- Archive to hide a record without breaking its identity or relationships;
  Permanent Purge when you mean it.
- Autosave Mode or Manual Save Mode with visible dirty state.
- Multi-game PGN import with per-entry results, PGN export, and a versioned
  Library Portability Package that restores empty-or-replace, never a silent
  merge.

### Engines

- Recognised engines discovered in the App Engine Store and known system
  locations — never arbitrary scanning or execution.
- Stockfish, Leela, and Reckless are curated for upstream installation; Komodo
  is detect-only. No play engine is bundled.
- First-contact consent plus a live UCI probe before an engine is Ready, and
  Custom Engines by explicit path.

### Creating

- A guided Variant Workshop in the ordinary cockpit: board presets, built-in
  pieces plus one custom Betza piece, starting positions, drops and pockets,
  and curated rule families.
- A definition becomes Playable only after schema validation, deterministic
  compile, an isolated consistency check, a bounded smoke test, a capability
  gate, and a Rule-valid start. Each Game Record binds an immutable Variant
  Snapshot, so later edits never rewrite history.

### Fitting the desktop

- Chrome and the default Board Theme follow the active Quattro Palette, with
  Last Valid and Built-in fallbacks so theme churn never blocks startup.
- Keyboard-complete chrome, a command palette (`Ctrl+K`), fixed bindings that
  avoid `Super`, and AT-SPI-legible surfaces with discrete announcements.
- Rails collapse by priority from 1280×800 down to a 640×480 floor. See
  [`docs/accessibility.md`](docs/accessibility.md) for the exact bar v0.1
  claims — and what it does not.

## What v0.1 does not do

Online play, Lichess, accounts, matchmaking, cloud sync, and telemetry are out
of scope, as are lessons, coaching, repertoire training, and tournaments. There
is no general Linux support outside Omarchy 4, no Flatpak or AppImage, no
stable extension API, and no user-configurable keybindings. Variant creation is
deliberately constrained: no arbitrary rule scripting, no raw INI import, no
custom artwork import, and no variant publishing. v0.1 does not claim
workspace-wide WCAG AA conformance or unaided blind play.

## On the road to 1.0

Nothing here is promised, and none of it is hidden in v0.1:

- A stable public extension boundary, and the API that goes with it.
- Fuller accessibility, including unaided blind play and a broader conformance
  claim.
- Remappable keybindings.
- Adaptive resource policy that responds to load, thermals, and battery rather
  than to explicit budgets alone.
- Broader variant support beyond the v0.1 constrained palette.
- Richer review — the kind of game review that explains, not just evaluates.
- Wider distribution, once "supported" can mean more than Omarchy 4.

Optional network use, where it ever appears, stays a feature about the network
(engine acquisition today, perhaps Lichess later) and never a requirement for
playing chess.

## Building and contributing

[`CONTRIBUTING.md`](CONTRIBUTING.md) covers the toolchain, the one-command
CMake build, and the test suites. [`CONTEXT.md`](CONTEXT.md) is the project
glossary — Game Record, Played Game, Board Theme, Variant Definition — and code,
comments, and commits use its vocabulary.

Two rules shape the codebase:

- **The Rules Authority is vendored Fairy-Stockfish.** Every legal move, SAN
  string, FEN, and result comes from it. Nothing above that bridge decides a
  chess question.
- **Journey tests are the acceptance seam.** They launch the real binary, drive
  it, and assert what ends up on screen — never QML structure, Rust helpers, or
  storage layout.

## Licensing

Omachess and its owned assets are GPL-3.0-or-later. The Cburnett Piece Set
ships under GPL-2.0-or-later, and official engine artwork carries its own
provenance. [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md) lists
every distributed component;
[`docs/CORRESPONDING_SOURCE.md`](docs/CORRESPONDING_SOURCE.md) identifies the
exact version-matched source and build inputs for each release.
