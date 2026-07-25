# Omachess

Omachess is a local-first, all-in-one chess solution built specifically for Omarchy, for playing, studying, and creating forms of chess.

## Games and analysis

**Game Record**:
The canonical record of a chess starting position and its move tree, used for both played games and open-ended analysis. Participation, clock, and result information belong to the record only when applicable.
_Avoid_: Game document, analysis document

**Played Game**:
A Game Record that follows the local play lifecycle: ready to start, in progress, optionally suspended and resumed, then completed with a result.
_Avoid_: Live game

**Completed Game**:
A Played Game that has ended with a result. Its starting position, played moves, side assignments, clock history, and result are immutable; its Game Metadata remains correctable. Analysis or continued play creates linked Game Records rather than changing its history.
_Avoid_: Closed game, finished record

**Game Metadata**:
Editable information describing or organizing a Game Record without changing its chess history, such as player names, event, date, title, tags, or archive status.
_Avoid_: Game facts, Engine Profile

**Suspended Game**:
A Played Game paused between local play sessions. Its position and clock values are preserved, and its clocks do not run while suspended.
_Avoid_: Paused game, saved game

**Analysis Record**:
An open-ended, self-contained Game Record for persistently exploring a main line, sidelines, and annotations. It may be one of multiple analyses derived from a Completed Played Game; the source and each analysis retain navigable relationships while changing independently.
_Avoid_: Analysis document, analyzed game

**Source Snapshot**:
The copy of a Completed Played Game's moves and applicable metadata retained by a derived Analysis Record. It keeps the analysis meaningful and allows equivalent chess content to be recreated even if the source record is unavailable.
_Avoid_: Temporary game, source cache

**Live Position Analysis**:
Transient engine evaluation of any current Rule-valid Position, including an evaluation and one or more principal variations. It is independent of any Game Record type or lifecycle; changing the selected position changes what is analyzed.
_Avoid_: Computer Analysis, Game Review

**Computer Analysis**:
A requested, finite engine pass over a Completed Game that produces a persistent Analysis Record with per-move evaluations, standard annotation glyphs, and selected better-line sidelines.
_Avoid_: Live Position Analysis, Game Review

**Default Analysis**:
The optional Computer Analysis designated as the primary analysis directly associated with a Completed Game. A Completed Game has at most one.
_Avoid_: Live analysis, default engine

**Pinned Engine Line**:
An evaluation and variation from Live Position Analysis explicitly preserved in an Analysis Record with the engine and search context that produced it.
_Avoid_: Saved evaluation, live evaluation

**Record Graph**:
The local graph formed by independent Game Records and the bidirectional provenance relationships created when one record is derived from another.
_Avoid_: Online network, move tree

**Study**:
A named, ordered local collection of Completed Games and Analysis Records. It groups records without merging their move trees or identities; an eligible Game Record may belong to any number of Studies, while unfinished Played Games cannot belong to one.
_Avoid_: Analysis Record, repertoire

**Personal Library**:
The local collection of a player's persisted Game Records and Studies.
_Avoid_: Game history, memory

**Archived Game Record**:
A Game Record retained with its identity and relationships intact but omitted from the Personal Library's default views.
_Avoid_: Deleted game, closed game

**Saved Snapshot**:
The latest durable state of a Game Record to which Omachess returns after unsaved changes are lost or discarded.
_Avoid_: Save point, backup

**Save Mode**:
The global preference controlling when a Game Record's Saved Snapshot advances. Autosave Mode advances it as the record changes; Manual Save Mode advances it only when the player explicitly saves.
_Avoid_: Temporary mode

## Positions

**Rule-valid Position**:
A piece arrangement to which the selected chess rules can be applied coherently, whether or not normal play could have produced it.
_Avoid_: Legal position

**Reachable Position**:
A Rule-valid Position that can arise from its rules' normal starting position through permitted moves.
_Avoid_: Possible position

**Freeform Position**:
A piece arrangement explored without requiring the selected chess rules to operate coherently. It permits manual analysis but does not support a Played Game, result detection, clocks, or guaranteed engine use.
_Avoid_: Illegal position

**Position Setup**:
Creating or changing a position directly through FEN or manual piece placement, removal, replacement, and relocation rather than through played moves.
_Avoid_: Freeform Position, Variant Workshop

## Engine integration

**Engine Profile**:
Curated identity, presentation, capability, and rating-context information for a recognized Chess Engine.
_Avoid_: Engine metadata, engine preset

**Recognized Engine**:
A Chess Engine that Omachess can identify and present automatically through an Engine Profile.
_Avoid_: Common engine, built-in engine

**Custom Engine**:
A Chess Engine that a player registers manually because Omachess does not recognize it automatically.
_Avoid_: Niche engine, unknown engine

**Engine Discovery**:
The process of finding installed Chess Engines and matching them to Engine Profiles without player configuration.
_Avoid_: Engine detection, auto-setup

## Desktop integration

**Quattro Palette**:
The semantic color set exposed by the active Omarchy 4 theme for Omachess to translate into its own visual roles.
_Avoid_: Omarchy palette, shell theme

**Built-in Palette**:
An Omachess-owned color set available independently of Quattro.
_Avoid_: Default palette

**Last Valid Palette**:
The most recently accepted Quattro Palette, retained when a later palette is structurally incompatible.
_Avoid_: Cached palette

## Chess variants

**Variant Workshop**:
The part of Omachess where a player creates a playable Chess Variant from a constrained catalogue of supported boards, pieces, positions, and rules.
_Avoid_: Variant editor, variant builder
