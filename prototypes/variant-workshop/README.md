# PROTOTYPE — Variant Workshop loop

**Throwaway.** Answers [Prototype the Variant Workshop loop](https://github.com/AyushJ1001/omachess/issues/11).
Not production code. Do not merge to main as-is.

## Question

What is the smallest coherent in-app create → validate → play → analysis
interaction for the constrained Variant Workshop, and does it feel like part of
the same Omachess workspace as standard chess?

It also carries the pieces deferred here by
[Decide the Variant Workshop capability envelope](https://github.com/AyushJ1001/omachess/issues/9):
board preset list, piece catalogue presentation, pocket UI for drops, and
interaction fidelity.

## Variants

| Key | Name | Structural idea |
| --- | --- | --- |
| A | Cockpit inspector | The workshop is the existing three-pane cockpit; the right rail swaps from Live Position Analysis to a Definition inspector. A Variant Definition is just another library record. |
| B | Guided build stepper | The builder takes over the window: Board → Pieces → Position → Rules → Validate → Play, one decision per step, then an explicit handoff back to the ordinary workspace. |
| C | Definition + console | Workshop as build tooling: a structured definition document on the left, board preview plus a Problems / Engine log / Compiled INI console on the right, with Validate and Play as run buttons. |
| D | Guided cockpit (A + B) | A's cockpit and full-size board with B's guided sequence folded into the right rail: five numbered steps, one open at a time, Back / Continue in the rail footer. Passing validation turns the rail into Live Position Analysis with an Edit definition link. |

Switch with the floating bar, `←`/`→`, or `?variant=A|B|C|D`.

In D the board is the work surface for every step: the piece tray sits under it
during Starting position, and rule families with a board footprint (promotion
rank, castling target files, goal squares) are drawn on it while Rules is open.

## Scenarios

The strip drives all three layouts through the same mock definition —
*Wayfarer Chess*, a 10×8 board with one custom Betza piece:

1. **Library & create** — where variants live next to games and studies.
2. **Board & geometry** — presets gated by the detected engine build.
3. **Pieces & Betza** — built-in catalogue plus one thin custom piece (type `yQ` in the Betza field to see an atom rejected).
4. **Starting position** — click the tray then the board; FEN and Rule-valid banner update.
5. **Rule families** — curated palette, out-of-scope families shown struck through; turn on Extinction to force a win-condition conflict, or drops to see the pocket row.
6. **Validate → Playable** — the five-stage pipeline; the first run fails on an ambiguous piece letter with a fix action, the second passes.
7. **Play the variant** — Played Game under a frozen Variant Snapshot.
8. **Variant analysis** — Live Position Analysis with the generic-evaluation disclosure.
9. **Capability gate & snapshot** — a stock 8×8 build gates the palette; editing a definition that records already use.

Engine build (`full` / `small` / `none`) is switchable from the strip to test
gating in any scenario.

## Run

```bash
python3 -m http.server 8765 --directory prototypes/variant-workshop
```

Open http://127.0.0.1:8765/?variant=A

Keys: `1`–`9` scenarios · `←`/`→` variants · `V` validate · `E` cycle engine
build · `?` help.
