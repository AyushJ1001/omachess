# Accessibility and responsive layout in Omachess v0.1

This is the bar Omachess v0.1 claims, and the bar its journey tests assert. It
is deliberately narrower than a conformance claim: v0.1 does **not** claim
workspace-wide WCAG AA conformance, and it does not claim unaided blind play.

## Assistive technology

Every chrome surface exposes an AT-SPI role, a name, and its states: the
cockpit panes, the Personal Library rail, the right rail, tabs, dialogs, the
command palette, and each Variant Workshop step. Board squares name where they
are and what stands on them.

State changes are announced discretely — one announcement per meaningful
change, never a continuous stream. Clock ticks, engine search progress, and
Background Job progress counts are deliberately silent; the job's *state* is
announced, its position counter is not.

Engine output is announced only when asked for. **Ctrl+E** ("Announce engine
output") reads the current evaluation and first line. Running analysis never
announces itself.

Nothing steals focus. Job updates, notifications, engine events, and desktop
palette changes all leave the keyboard in the pane the player put it in.

## Keyboard

Chrome is keyboard-complete and every action is in the command palette
(**Ctrl+K**). Bindings are fixed and avoid `Super`, so they cooperate with the
desktop. Traversal is two-level: **Alt+Left** / **Alt+Right** move between
panes, and the pane itself moves within. A collapsed rail is skipped rather
than being a place focus can fall into.

The board is pointer-first. Optional typed move entry (**Ctrl+M**) is there for
players who would rather not use the pointer: type a move as its two squares,
for example `e2e4`, adding `q`, `r`, `b`, or `n` to promote. The core decides
whether it is a move at all, so typed entry accepts exactly what the board
accepts.

## Contrast

The workspace derives its colours from the Quattro Palette, then raises them to
a legibility bar before painting. Body text carries 4.5:1, secondary text and
status colour carry 3:1, board squares carry 2:1, and the translucent board
marks — last move, selection, move target — must visibly change either square
colour. The corrections keep hue and saturation, so a desktop theme still looks
like itself.

The ratios actually painted are asserted for every supported palette, the
Built-in Palette, and every pinned Board Theme.

## Responsive layout

Rails collapse by priority between 1280×800 and a 640×480 floor:

| Viewport | Personal Library rail | Right rail | Board | Primary action |
| --- | --- | --- | --- | --- |
| ≥ 1280×800 | shown | shown | shown | shown |
| ≥ 1024×640 | collapsed | shown | shown | shown |
| down to 640×480 | collapsed | collapsed | shown | shown |

The board and the current surface's primary action are priority one and never
collapse. At the floor the primary action is whatever the surface on screen is
for — Continue or Validate in the Variant Workshop, Start Played Game in
Position Setup, Edit definition in variant play, Restore when a record is
offered, New game otherwise — and the command palette still reaches everything
the collapsed rails held.

Fractional scaling is passed through unrounded, so the workspace is the size
the desktop asked for and these thresholds stay honest at every viewport.
