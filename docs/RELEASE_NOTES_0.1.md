# Omachess v0.1 — experimental release

v0.1 is an experimental, local-first chess workspace for Omarchy 4.

## v0.1.1 — the completed v0.1

v0.1.1 completes the v0.1 scope. On top of v0.1.0 it adds:

- **Live Position Analysis, Analysis Records, and Computer Analysis**, with
  Quick, Standard, and Deep budgets, honest estimates, disclosed effective
  settings, pinned engine lines, Source Snapshots, and the Record Graph.
- **Background work**: long analysis belongs to an on-demand D-Bus worker and
  continues after the workspace closes with your explicit consent, checkpointed
  at move boundaries, with a first-class Omarchy Background Controls plugin for
  monitoring, pause, resume, cancel, and deep links back to a record.
- **Studies, archive, and Permanent Purge** for organising the Personal
  Library, and the versioned **Library Portability Package** with
  empty-or-replace restore.
- **The Variant Workshop**: board presets, built-in pieces plus one custom
  Betza piece, starting positions, drops and pockets, curated rule families,
  evidence-based validation, and immutable Variant Snapshots bound to each
  Game Record.
- **Engines**: human-versus-engine play, catalog installation from upstream,
  Custom Engines by explicit path, and separate live-play and analysis
  settings.
- **Accessibility and responsive layout**: AT-SPI-legible chrome, discrete
  announcements, engine output on request, optional typed move entry, asserted
  contrast across supported palettes, and priority-based rail collapse from
  1280×800 to a 640×480 floor. The exact bar is in `accessibility.md`.

## Protecting data during migration

Before any upgrade or migration, close Omachess and copy the complete Live
Store. This is the recovery copy if migration cannot complete. Exact XDG paths
and restore steps are in `backup.md` and `install.md`, both installed under
`/usr/share/doc/omachess/`.

Export important Game Records as PGN as an additional portable copy; PGN export
does not preserve every Personal Library relationship. Since v0.1.1 the
versioned Library Portability Package described in
`library-portability-package.md` is available and preserves records,
annotations, Source Snapshots, Record Graph relationships, Studies, Variant
Definitions, and the portable preferences subset. A closed-app Live Store copy
remains the safest recovery path across a migration.

Omachess-owned durable data receives migration, export, and recovery
protection. Pre-1.0 presentation details and extension formats may change.

## What v0.1 does not promise

There is no stable extension API and no general Linux support outside Omarchy
4. v0.1 has no online play, accounts, cloud services, or telemetry, no
user-configurable keybindings, and no arbitrary variant scripting. v0.1 does
not claim workspace-wide WCAG AA conformance or unaided blind play.

## Licensing and source

Omachess is GPL-3.0-or-later. `THIRD_PARTY_NOTICES.md` lists every distributed
component and `CORRESPONDING_SOURCE.md` identifies the exact version-matched
source and reproducible build inputs.
