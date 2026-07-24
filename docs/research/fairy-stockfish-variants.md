# Fairy-Stockfish's runtime variant boundary

Research snapshot: Fairy-Stockfish source commit
[`c19b5f6`](https://github.com/fairy-stockfish/Fairy-Stockfish/tree/c19b5f6c66894fdb0e88d0dd100e3885f744760a)
(2026-07-23) and official wiki commit
`833767be5798bfb28a3acba3d61e954f40ea6383` from the
[official Fairy-Stockfish wiki](https://github.com/fairy-stockfish/Fairy-Stockfish/wiki).

## Decision

Fairy-Stockfish is suitable as the play-and-analysis authority for Omachess's
v0.1 Variant Workshop **when Omachess generates a constrained subset of its
runtime INI format and validates the complete generated variant before making it
playable**. It is not a runtime for arbitrary user-written chess rules.

A compatible large-board build can load, without recompilation, rectangular
two-player variants up to 12 files by 10 ranks; a starting position; built-in or
limited-Betza pieces; and combinations from a substantial fixed catalogue of
movement, promotion, drop, capture, castling, region, repetition, and win/draw
rules. Anything that needs a new rule primitive, a movement modifier outside the
implemented Betza subset, a larger or non-rectangular board, more than two
players, or a known-incompatible rule combination requires an engine code
change, not merely workshop data.

The workshop therefore needs its own canonical variant model, capability gates
for the particular engine binary, a deterministic INI compiler, and a validation
smoke test. The INI file should be treated as an engine adapter artifact, not as
Omachess's product/domain model.

## What can be defined at runtime

### Board and position

- The engine's large-board build represents a rectangular grid of at most
  **12 files × 10 ranks (120 squares)**. A normal build is limited to 8×8.
  These are compile-time limits, not a preference in the configuration schema
  ([board representation](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/types.h#L494-L565),
  [compile flags and defaults](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/Makefile#L337-L354)).
  Fairy-Stockfish's own new-variant guide explicitly says boards over 8×8 need
  `largeboards=yes` and boards over 10×12 are unsupported
  ([guide](https://github.com/fairy-stockfish/Fairy-Stockfish/wiki/New-Variant-Cheat-Sheet#6-testing-your-variant)).
- `maxFile` and `maxRank` select a rectangle within that compiled capacity.
  Square sets can restrict movement, promotion, drops, flags, walls, and other
  rules to named squares, files, or ranks
  ([schema](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variants.ini#L120-L165)).
  This can emulate irregular playable areas, but it does not create a different
  topology: the underlying board and directions remain rectangular.
- `startFen` supplies the initial placement and state. Variant FEN supports the
  configured piece alphabet and geometry, side to move, castling/en-passant
  state, promoted markers, pockets for drop variants, move counters, and
  check-count state where enabled. The first-party validator checks these fields,
  including geometry, pockets, royal-king counts, castling state, and
  variant-specific fields
  ([FEN validator](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/apiutil.h#L981-L1135)).

### Pieces and movement

- A variant may select from the engine's catalogue of chess, regional, and fairy
  pieces (pawn, knight, bishop, rook, queen, king, fers, alfil, archbishop,
  chancellor, amazon, shogi pieces, cannons, hoppers, and others)
  ([catalogue](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variants.ini#L43-L85)).
- It also has **25 ordinary custom-piece slots**, plus a separately reserved
  custom royal/king movement slot. Every configured type uses a unique
  single-letter piece character; the validator reports ambiguous characters
  ([custom-piece schema](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variants.ini#L87-L107),
  [slot representation](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/types.h#L405-L430),
  [ambiguity check](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/parser.cpp#L552-L576)).
- Custom movement is not arbitrary code. It is a Fairy-Stockfish subset of
  Betza notation: supported leaper and rider atoms; forward/back/side
  directions; move-only and capture-only modalities; initial moves; limited or
  unlimited orthogonal, diagonal, and knight-direction riders; orthogonal and
  diagonal hoppers/grasshoppers; and selected lame leapers
  ([documented subset](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variants.ini#L100-L107),
  [actual parser](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/piece.cpp#L32-L170)).
  New atoms or modifiers require C++ changes.
- Per-colour, per-piece mobility regions can further restrict where a type may
  move. Piece values can be supplied for the generic middlegame/endgame
  evaluation
  ([piece and region parsing](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/parser.cpp#L249-L321),
  [value schema](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variants.ini#L109-L118)).

### Rule catalogue

Runtime configuration is compositional but finite. The current schema includes:

- promotions and demotions, asymmetric promotion types/regions and limits;
- pawn-like double/triple steps and en passant;
- configurable castling and Chess960 castling;
- checking, mandatory capture/drop, drops, capture-to-hand, drop restrictions,
  and gating;
- atomic blast, petrification, enclosure/flipping, wall placement, pass moves,
  and several regional-game rules;
- repetition and move-count adjudication, stalemate/checkmate values,
  extinction, flag/goal regions, check counting, connection goals, and material
  or regional counting.

The authoritative option list and meanings are maintained in
[`variants.ini`](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variants.ini#L160-L299).
Variants can inherit a built-in or earlier configured variant and override only
their differences
([inheritance format](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variants.ini#L33-L41)).

This catalogue can express games well outside orthodox chess—Fairy-Stockfish's
official configuration includes tic-tac-toe and Connect Four—but it cannot
express a genuinely new rule mechanic. The project's own contributor guide
directs unsupported rules to additions in `Variant`, the parser, move
generation, position mutation, legality, or game-end C++ code
([new-rule procedure](https://github.com/fairy-stockfish/Fairy-Stockfish/wiki/New-Variant-Cheat-Sheet#4-if-your-game-rules-arent-supported)).

## What “immediately playable and analyzable” requires

1. **A capable engine build.** The stock Makefile defaults to neither
   `LARGEBOARDS` nor `ALLVARS`. `LARGEBOARDS` raises the geometry from 8×8 to
   12×10; `ALLVARS` raises the maximum generated move list from 1,024 to 8,192
   for heavyweight/high-branching games
   ([build defaults](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/Makefile#L92-L99),
   [move-list limits](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/types.h#L232-L244)).
   The UCI interface does not advertise these compile-time capabilities, so
   Omachess must either depend on a known build or probe it.
2. **A loadable, non-colliding definition.** The engine can load an INI at
   startup or through the `VariantPath` UCI option, after which it adds the name
   to `UCI_Variant`
   ([loader](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/ucioption.cpp#L70-L85)).
   A runtime definition cannot replace an already registered name, and an
   inherited parent must already exist
   ([registration rules](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variant.cpp#L2177-L2197)).
3. **A valid starting position and consistent rule combination.** The official
   `check <path>` mode catches unknown keys, invalid values and FENs, ambiguous
   piece letters, contradictions, and several unsupported combinations. Known
   incompatibilities include drops plus walling; royal kings plus blast,
   enclosure-flipping, or duck walling; royal/pseudo-royal pieces plus blast
   immunity or mutual immunity; and safe-flag goals plus blast
   ([consistency checks](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/parser.cpp#L552-L646)).
4. **Validation before runtime loading.** This is a sharp integration hazard:
   the runtime `VariantPath` loader instantiates the parser with consistency
   checks disabled, while `check` emits human-readable diagnostics to stderr
   rather than returning a structured validation result
   ([unchecked runtime path](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/ucioption.cpp#L70-L77),
   [checked parser path](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/variant.cpp#L2183-L2205)).
   Omachess should validate its own model, run the engine's check mode in a
   disposable process, then smoke-test loading, the starting position, legal
   move generation, and a bounded search before declaring a variant usable.
5. **A rule authority for the GUI.** UCI reports the variant's board size,
   pocket count, GUI template, and starting FEN when it is selected, but does
   not expose the complete rule definition or a structured legal-move API
   ([UCI metadata](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/ucioption.cpp#L158-L165)).
   Omachess must retain the source variant model for rendering, and either use
   a first-party Fairy-Stockfish binding or another deliberate adapter for legal
   moves and game results. The maintained Python binding exposes runtime config
   loading, legal moves, FEN updates, SAN, and game-result calls
   ([binding API](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/pyffish.cpp#L99-L140),
   [exported operations](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/pyffish.cpp#L403-L421));
   the first-party JavaScript/Wasm binding accepts config content and validates
   variant FENs as well
   ([JS adapter](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/ffishjs.cpp#L468-L503)).
6. **An evaluation strategy.** A new variant normally has no matching NNUE
   network. Fairy-Stockfish selects NNUE by variant-name/alias-prefixed network
   filenames and otherwise falls back to its handcrafted evaluator
   ([network selection](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/evaluate.cpp#L69-L110),
   [fallback](https://github.com/fairy-stockfish/Fairy-Stockfish/blob/c19b5f6c66894fdb0e88d0dd100e3885f744760a/src/evaluate.cpp#L1605-L1616)).
   This makes immediate search and an evaluation possible, not automatically
   trustworthy. Omachess should describe fresh-variant analysis as generic,
   display the evaluator/network actually in use, and let creators set piece
   values. It must not imply that an engine evaluation proves the new game is
   balanced or that its strength is known.

## Recommended v0.1 product boundary

The first workshop should compile a small product vocabulary into the larger
engine schema:

- one rectangular board, capped at the detected build's geometry;
- two sides and alternating turns;
- a starting-position editor that always produces validated variant FEN;
- predefined pieces plus movement assembled only from the documented
  Fairy-Stockfish Betza subset;
- piece values and colour/piece mobility regions;
- a curated set of independently testable rule families (for example standard
  checkmate/royal play, promotion, castling, drops/capture-to-hand, mandatory
  capture, extinction, flag regions, and connection goals);
- explicit exclusion of two-board play, walling, blast/petrification,
  enclosure/flipping, gating, regional counting/chasing, and arbitrary raw INI
  in the first public workflow unless a later prototype demonstrates their UI
  and interaction cost;
- generated INI retained alongside Omachess's portable canonical definition;
- a “generic classical evaluation” badge for new variants, upgraded only when a
  compatible variant-specific network is positively identified.

This recommendation is deliberately narrower than what Fairy-Stockfish can
parse. The engine schema contains coupled switches and protocol/UI behaviours;
exposing all of them as independent toggles would let users construct variants
the engine itself flags as contradictory or that Omachess cannot render and
operate correctly.

## Newly surfaced decisions

1. **Choose Omachess's canonical variant schema and versioning boundary.** Which
   concepts are stable Omachess data, and which remain Fairy-Stockfish adapter
   details?
2. **Choose the in-app rules-authority adapter.** Should v0.1 embed/link
   `pyffish`, embed `ffish.js`/Wasm, build a native binding, or communicate with
   a process adapter for legal moves, SAN/FEN, and game results?
3. **Define and prototype the exact v0.1 workshop rule palette.** Which rule
   families can be presented without allowing invalid combinations or requiring
   special board interaction?
4. **Set the engine capability contract.** Will Omachess require/package a known
   large-board + all-variants Fairy-Stockfish build, or probe arbitrary installed
   binaries and progressively disable workshop features?
5. **Define variant validation and failure isolation.** What exact pipeline,
   time limits, diagnostics mapping, and subprocess containment must a generated
   variant pass before Omachess marks it playable?
6. **Define evaluation truthfulness.** How should the UI distinguish a search
   result backed by generic handcrafted evaluation from one using a compatible
   NNUE network, and how should creator-supplied piece values be explained?
7. **Decide whether raw INI import is in v0.1.** Import is materially different
   from creation because it may contain every advanced rule and movement that
   the constrained workshop intentionally excludes.
