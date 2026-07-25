# PROTOTYPE — Omachess background controls in the Omarchy shell

**Throwaway.** Answers [Prototype Omachess background controls in the Omarchy shell](https://github.com/AyushJ1001/omachess/issues/18).
Not production code. Do not merge to main as-is.

## Question

What Omarchy shell plugin makes all active Omachess background jobs visible and
controllable—including progress, pause, resume, cancel, completion, failure,
and opening the standalone workspace—while keeping job execution safe across
shell restarts?

## Product constraints already locked

- Chess workspace is a **standalone** Wayland window (not hosted in the shell).
- Background Computer Analysis runs in an **Omachess-owned worker** that survives
  closing the workspace (with consent).
- A **first-class Omarchy shell plugin** is the control surface for active jobs.
- Job execution must remain safe if `omarchy-shell` restarts.

## Variants

| Key | Name | Structural idea |
| --- | --- | --- |
| A | Bar chip + popup | Native Omarchy pattern (audio / model-usage): compact bar chip, click opens anchored job list with full controls. |
| B | Inline job pills | Expanding bar segment: each job is a pill with progress and primary actions; no popup required. |
| C | Overlay jobs palette | Minimal bar badge; summon a keyboard-first fullscreen-ish overlay (clipboard / image-picker style) for all job control. |

Switch with the floating bar, `←`/`→`, or `?variant=A|B|C`.

## What to judge

1. **Visibility** — do you notice running jobs without opening Omachess?
2. **Control density** — can you pause / resume / cancel without friction?
3. **Open workspace** — is returning to the standalone chess window obvious?
4. **Restart safety** — hit **Restart shell**: jobs must keep running; the plugin reattaches.
5. **Idle presence** — should anything show when there are zero jobs?

## Simulated surface (all variants share)

- **Worker** — Omachess background process; owns job state and engine work.
- **Shell plugin** — control surface only; reads status, sends commands.
- Sample jobs: running Computer Analysis, paused analysis, completing export-like batch, failed analysis.

## Run

```bash
python3 -m http.server 8766 --directory prototypes/omachess-shell-jobs
```

Open http://127.0.0.1:8766/?variant=A

Keyboard: `←`/`→` cycle variants · `r` restart shell · `n` inject a new job · `?` help
