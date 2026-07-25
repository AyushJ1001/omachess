# PROTOTYPE — Core chess workspace

**Throwaway.** Answers [Prototype the core chess workspace](https://github.com/AyushJ1001/omachess/issues/10).
Not production code. Do not merge to main as-is.

## Question

What workspace layout and interaction model makes v0.1 play, analysis, engine
controls, game navigation, and the local library efficient, keyboard-first,
approachable, and visually native to Omarchy 4/Quattro — including save-mode
communication, unsaved close, partial PGN import feedback, returning-player
restore, background Computer Analysis consent, Live Position Analysis, Study
navigation of Analysis Records, and Position Setup?

## Variants

| Key | Name | Structural idea |
| --- | --- | --- |
| A | Three-pane cockpit | Library rail · board · engine/analysis rail. Familiar chess-workbench density. |
| B | Board-first palette | Near full-screen board; library/engine/setup as overlays + command palette. |
| C | Study desk | Studies as primary navigation; open records as tabs; board + tree + compact engine. |

Switch with the floating bar, `←`/`→`, or `?variant=A|B|C`.

## Scenarios

The scenario strip drives the *same* mock data through the flows the ticket owns:

1. **Play workspace** — in-progress Played Game, Live Position Analysis, clocks.
2. **Return restore** — previous records/positions restored without auto-resuming clocks or engines.
3. **Manual save dirty** — Manual Save Mode, unsaved changes, close/quit dialogs.
4. **Partial PGN import** — multi-game import with successes and failed entries + parse reasons.
5. **Analysis background consent** — closing while Computer Analysis can continue via worker.
6. **Position Setup** — FEN + piece tray for Rule-valid / Freeform Positions.
7. **Study graph** — Study contents, independent vs derived Analysis Records, Record Graph links.

## Run

```bash
python3 -m http.server 8765 --directory prototypes/core-chess-workspace
```

Open http://127.0.0.1:8765/?variant=A

Keyboard: `?` shows the shortcut cheatsheet for the active variant.
