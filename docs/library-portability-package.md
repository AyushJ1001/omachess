# The Library Portability Package

A Library Portability Package is the versioned, documented export of your
Omachess library. It is the supported way to move a complete library between
machines or installs. (The on-disk SQLite Live Store is internal and is not an
interchange format; see [backup.md](backup.md) for the filesystem backup
contract.)

Export and restore both choose their file through the desktop's portal file
dialog, so the package lands wherever you would put any other document.

## What a package carries

- **Game Records** — every Played Game and Analysis Record, with its starting
  position, move tree, Game Metadata, result, clock history, and archive
  status. Archived records travel too.
- **Durable annotations** — the annotations, sidelines, Pinned Engine Lines,
  and Computer Analysis evaluations an Analysis Record owns, including which
  analysis is the Default Analysis.
- **Source Snapshots** — the copy of a source game each derived Analysis
  Record keeps, so an analysis stays meaningful on its own.
- **Record Graph relationships** — the provenance edges between records.
- **Studies** — each Study's name, creation time, membership, and order.
- **Variant Definitions** — the player-created Variant Definitions in the
  library.
- **The portable preferences subset** — currently the Save Mode. Preferences
  that describe one machine's session (which tabs were open, which record was
  active) are deliberately left behind.

It does **not** carry engine binaries, Engine Profiles, transient sessions, or
caches. Every package states this in its own `description` field, so the file
explains itself without this page.

## Format

The package is a single UTF-8 JSON document, conventionally named
`*.omalib`. Its first two fields are:

```json
{
  "format_version": 1,
  "description": "Omachess Library Portability Package. …"
}
```

`format_version` is the contract. Omachess reads only the version it
understands. Restoring a package written in any other version **fails closed**:
Omachess reports the version it found and the version it reads, and changes
nothing in your library. A file that is not a readable package at all is
refused the same way.

## Restoring: empty, or an explicit replacement

Identities are never silently merged across libraries. A restore therefore has
exactly two outcomes:

- **Into an empty library** — the package's contents become the library.
- **Into a library that already holds work** — Omachess stops and states
  exactly what would be removed and what would take its place, and the restore
  proceeds only when you confirm the replacement. Replacement is wholesale: the
  records, Studies, Variant Definitions, and portable preferences already in the
  library are removed. Like a Permanent Purge, that removal has no in-app undo,
  so export the current library first if you want to keep it.

A restore is one transaction. If anything goes wrong partway, the library is
left exactly as it was.
