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
The local collection of a player's persisted Game Records, Studies, and player-created Variant Definitions.
_Avoid_: Game history, memory

**Live Store**:
The app-managed local durable store that is the source of truth for the Personal Library, Record Graph, unfinished sessions, and other Omachess-owned durable state.
_Avoid_: Database, app data folder, save directory

**Library Portability Package**:
The versioned, documented export of a player's Omachess library that preserves Game Records, durable annotations, Source Snapshots, Record Graph relationships, Studies, Variant Definitions, and the portable preferences subset for backup and migration.
_Avoid_: Full backup zip, database dump, library archive

**Archived Game Record**:
A Game Record retained with its identity and relationships intact but omitted from the Personal Library's default views.
_Avoid_: Deleted game, closed game

**Permanent Purge**:
Irreversible removal of a Game Record, Study, or eligible Variant Definition from the Live Store. It is distinct from archive and does not provide undelete inside Omachess.
_Avoid_: Delete, remove, trash, soft delete

**Saved Snapshot**:
The latest durable state of a Game Record to which Omachess returns after unsaved changes are lost or discarded.
_Avoid_: Save point, backup

**Save Mode**:
The global preference controlling when a Game Record's Saved Snapshot advances. Autosave Mode advances it as the record changes; Manual Save Mode advances it only when the player explicitly saves.
_Avoid_: Temporary mode

## Positions

**Latest Position**:
The position after the last move of a Game Record. It is the only position of a Played Game in which a move may be played, and the position whose result the record reports.
_Avoid_: live position, current position, Live Position Analysis

**Displayed Position**:
The position of a Game Record the player is currently looking at. It is the Latest Position unless the player has navigated to an earlier one, in which case the record is being reviewed and no move may be played.
_Avoid_: current position, selected position

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

**Move Offer**:
Everything a player may do with one piece on one destination square in the Displayed Position: the two squares, and the pieces a promoting pawn may become. Offers come from the Rules Authority, so a workspace can show a player where a picked-up piece may go without deciding it.
_Avoid_: legal move list, candidate move

**Termination**:
Why a Played Game ended — checkmate, stalemate, insufficient material, the fifty-move rule, threefold repetition, or a rule belonging to the Chess Variant. It is reported alongside the result, so a draw says which draw it was.
_Avoid_: game over reason, end state

## Engine integration

**Rules Authority**:
The single component that answers every chess question in Omachess: which moves are legal, how a move reads in notation, what a position's FEN is, and whether and how a game has ended. Vendored Fairy-Stockfish is that component, and no other part of Omachess decides any of those, so nothing can drift from it.
_Avoid_: chess engine, move generator, Chess Engine

**Engine Profile**:
Curated identity, presentation, capability, and rating-context information for a recognized Chess Engine, including official artwork and an approximate display rating.
_Avoid_: Engine metadata, engine preset

**Display Rating**:
An approximate, player-editable Elo shown on an Engine Profile for presentation. It is not a live engine fact and does not gate readiness or search strength.
_Avoid_: Engine Elo, UCI_Elo, true rating

**Recognized Engine**:
A Chess Engine that Omachess can identify and present automatically through an Engine Profile.
_Avoid_: Common engine, built-in engine

**Engine Catalog**:
The curated set of engines Omachess offers for recognition and optional install from each engine’s upstream source into the App Engine Store.
_Avoid_: Package search, engine marketplace, Wikipedia engine list

**App Engine Store**:
Omachess-private storage of engines installed on demand from upstream, not system-wide packages and not binaries shipped inside the Omachess package.
_Avoid_: Bundled engines, system engine install, user-wide engine install

**Custom Engine**:
A Chess Engine that a player registers manually by executable path because it is outside the Engine Catalog or not auto-discovered.
_Avoid_: Niche engine, unknown engine

**Engine Discovery**:
Finding catalog engines already present in the App Engine Store or as known system installs, then matching them to Engine Profiles—without scanning or executing arbitrary unknown binaries.
_Avoid_: Engine detection, auto-setup, filesystem engine scan

**Engine Readiness**:
Whether a Chess Engine may be used for play or analysis after consent and a successful live UCI probe. Presence of a package or file alone is not readiness.
_Avoid_: Installed engine, available engine

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

**Board Theme**:
The contrasting square colors and board overlay colors in use, selected by default from the active Quattro Palette and pinnable by the player to an Omachess-owned set independent of the desktop theme.
_Avoid_: Board skin, board colors

**Piece Set**:
The artwork used to draw pieces. A player chooses it independently of any palette; its light and dark identities belong to the artwork rather than being derived from a theme.
_Avoid_: Piece theme, piece colors

**Background Job**:
Long-running Omachess work owned by an Omachess background worker—not by the shell—so it can continue after the workspace closes and across `omarchy-shell` restarts. v0.1 exposes every such long task through a general job list (for example Computer Analysis and other lengthy library or engine work), each advertising only the controls it supports.
_Avoid_: Shell job, background task, engine job

**Background Controls Plugin**:
The first-class Omarchy shell bar-widget plugin that shows active Background Jobs when any exist, with a summary chip and popup for progress and job-specific actions, desktop notifications on completion or failure, and deep links that open the standalone Omachess workspace on the job’s record.
_Avoid_: Omachess shell app, tray app, status applet

## Chess variants

**Chess Variant**:
A complete set of playable chess rules: board geometry, piece set, starting position, and rule families that together define legal play and game endings. Standard chess is the built-in default Chess Variant; workshop creations are additional player-defined ones.
_Avoid_: Fairy-Stockfish INI, engine config

**Variant Definition**:
The portable, versioned Omachess description of a Chess Variant. It is the canonical product object that Omachess stores, edits, validates, and migrates in the player's local library. Fairy-Stockfish INI and similar engine packaging are compiled from it as adapter artifacts, never the source of truth. Its schema is intended to grow across Omachess releases without abandoning existing definitions.
_Avoid_: variants.ini, engine config, raw INI

**Draft Variant Definition**:
A Variant Definition that is not yet Playable—incomplete, failing validation, or re-edited after a previous Playable status—so it cannot start a Played Game or promise engine analysis under those rules.
_Avoid_: Invalid variant, temporary variant

**Playable Variant Definition**:
A Variant Definition that has passed Omachess schema validation, deterministic engine-adapter compile, isolated Fairy-Stockfish consistency check, isolated bounded smoke test, and the current engine capability gate, and that carries a Rule-valid starting position.
_Avoid_: Validated variant, published variant

**Variant Snapshot**:
The immutable copy of a Variant Definition bound into a Game Record when play or analysis under that Chess Variant begins. Later edits to the library Variant Definition do not change records that already hold a snapshot.
_Avoid_: Live variant binding, mutable ruleset

**Variant Workshop**:
The part of Omachess where a player creates a playable Chess Variant from a constrained catalogue of supported boards, pieces, positions, and rules.
_Avoid_: Variant editor, variant builder
