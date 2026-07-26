# Vendored Fairy-Stockfish

Fairy-Stockfish is the Rules Authority for Omachess: legal move generation,
SAN, FEN, and game results all come from here, so no second rules
implementation can drift from it.

- Upstream: <https://github.com/fairy-stockfish/Fairy-Stockfish>
- Pinned commit: `c19b5f6c66894fdb0e88d0dd100e3885f744760a` (2026-07-23), the
  same commit `docs/research/fairy-stockfish-variants.md` was researched
  against.
- Licence: GPL-3.0-or-later, see `Copying.txt`. Omachess is
  GPL-3.0-or-later too, and vendoring means Omachess distributes this source
  alongside its own.

## What was removed

`src/` is upstream's `src/` with the files Omachess does not build removed:

| Removed | Why |
| --- | --- |
| `main.cpp` | Omachess is the entry point; the engine is a library here. |
| `ffishjs.cpp`, `pyffish.cpp` | Upstream's own language bindings. `core/rules/omachess_rules.cpp` is the Omachess equivalent and is modelled on them. |
| `Makefile`, `Makefile_js` | `core/build.rs` compiles the engine, so there is one build for the whole project. |

Nothing else is edited. Keeping the tree unmodified is what makes updating the
pin a copy rather than a merge.

## How it is built

`core/build.rs` compiles these sources plus the bridge into a static library
and links it into the Rust core. It configures the engine with `LARGEBOARDS`,
`PRECOMPUTED_MAGICS`, and `ALLVARS`, which is the build the Variant Workshop
needs, and with `NNUE_EMBEDDING_OFF`, because Omachess asks the engine for
rules and never for an evaluation — no neural network ships with the app.

## Updating the pin

1. Copy upstream's `src/`, `AUTHORS`, `Copying.txt`, and `README.md` over this
   directory, then remove the files in the table above.
2. Reconcile `ENGINE_SOURCES` in `core/build.rs` with upstream's `SRCS`.
3. Update the commit above and run `ctest`. The core's rules tests assert the
   engine's answers directly, so a behaviour change shows up there.
