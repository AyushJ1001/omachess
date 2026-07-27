# Omachess v0.1.0 — experimental release

v0.1 is an experimental, local-first chess workspace for Omarchy 4. It supports
local play, a persistent Personal Library, PGN import/export, position setup,
Board Themes and Piece Sets, and consent-gated local Engine Profiles.

## Protecting data during migration

Before any upgrade or migration, close Omachess and copy the complete Live
Store. This is the recovery copy if migration cannot complete. Exact XDG paths
and restore steps are in `backup.md` and `install.md`, both installed under
`/usr/share/doc/omachess/`.

Export important Game Records as PGN as an additional portable copy. PGN
export does not preserve every Personal Library relationship. The versioned
Library Portability Package described in the docs is not implemented in v0.1,
so a closed-app Live Store backup remains required for full recovery.

Omachess-owned durable data receives migration, export, and recovery
protection. Pre-1.0 presentation details and extension formats may change.

## What v0.1 does not promise

There is no stable extension API and no general Linux support outside Omarchy
4. v0.1 has no online play, accounts, cloud services, or telemetry.

## Licensing and source

Omachess is GPL-3.0-or-later. `THIRD_PARTY_NOTICES.md` lists every distributed
component and `CORRESPONDING_SOURCE.md` identifies the exact version-matched
source and reproducible build inputs.
