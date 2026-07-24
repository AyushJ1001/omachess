# Quattro integration surface for Omachess v0.1

Research snapshot: 2026-07-24. Omarchy source was inspected at Quattro commit
[`ea549a7`](https://github.com/basecamp/omarchy/tree/ea549a74d56c7db5dc4d7ba7509a56514133996a);
that tree identifies itself as
[`4.0.0.alpha`](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/version).

## Decision

Omachess v0.1 should remain a normal standalone Wayland desktop application and
integrate with Quattro through two layers:

1. **Durable desktop contracts:** an XDG desktop entry, a hicolor application
   icon, a stable Wayland app ID, standard desktop notifications, XDG portals,
   and normal Hyprland window behavior.
2. **One narrow, replaceable Omarchy adapter:** read the active semantic palette
   from Omarchy's current-theme state and reload it when that state is replaced.
   Detect the Omarchy version and fall back to an embedded Omachess theme if the
   expected v4-alpha files are absent or malformed.

Omachess should **not** be an `omarchy-shell` plugin, import Quattro's QML
components, copy its shell styling implementation, depend on shell IPC, or
install Hyprland rules and Omarchy hooks for its ordinary operation.

This gives v0.1 native launcher, compositor, notification, and theme behavior
without coupling the chess application lifecycle to a pre-release shell.

## Integration matrix

| Area | v0.1 integration | Stability assessment |
| --- | --- | --- |
| Theme identity and palette | Read `~/.local/state/omarchy/current/theme.name` and `~/.local/state/omarchy/current/theme/colors.toml`; consume only `mode`, `background`, `foreground`, `accent`, `selection`, `muted`, and semantic status colors needed by Omachess. Watch the containing `current` directory because theme switching replaces the whole `theme` directory atomically. Validate every value and retain the last valid palette/fallback on error. | **Provisional Quattro contract.** It is the source Quattro itself consumes, but it is not a published external API, does not currently honor `XDG_STATE_HOME`, and changed during alpha. Keep all knowledge in one adapter. |
| Omachess component styling | Translate the small Omarchy palette into Omachess-owned design tokens for boards, panels, focus, selection, success, warning, and error. Maintain contrast and chess-square legibility within Omachess. | **Owned by Omachess.** Do not consume Quattro's `shell.toml`; its roles, spacing, control metrics, and QML components are shell implementation details. |
| Theme-change notification | Prefer watching the state directory from the running app. The documented `theme-set` hook may be an optional compatibility aid for development, but v0.1 must not require a user hook. | Directory watching survives ordinary app packaging and keeps the standalone boundary. Hook installation would mutate user configuration and is unnecessary. |
| Application launching | Install a reverse-DNS-named `.desktop` file (final ID subject to repository-owner decision) under the normal XDG applications directory with `Type=Application`, `Exec=omachess`, the matching symbolic `Icon`, `Terminal=false`, `Categories=Game;BoardGame;`, and startup notification. | **Durable standard.** Quattro enumerates `DesktopEntries.applications` and launches by desktop ID, so no launcher-specific registration is needed. |
| Application icon | Install a bundled SVG/PNG under the matching `hicolor` `apps` path and use its symbolic name in the desktop entry and notifications. Bundle chess-specific in-app icons instead of assuming Nerd Font glyph coverage. | **Durable standard.** Quattro searches XDG icon locations and `/usr/share/pixmaps`, then performs themed lookup. |
| Hyprland behavior | Run as a native Wayland client with the desktop-entry ID and Wayland `app_id` aligned. Open as an ordinary tiled window, support arbitrary resize and fractional scaling, and use standard activation/portal flows. Do not ship a Quattro source rule. | **Durable compositor behavior with no Omarchy-private dependency.** Quattro already applies its general rules to every ordinary window. Exact opacity, gaps, rounding, animations, and layouts are user/theme policy and must not be assumed by app layout code. |
| Notifications | Use `org.freedesktop.Notifications` through the chosen toolkit, set the application name/icon and `desktop-entry` hint, use normal/low urgency for completed analysis or background work, and reserve critical urgency for actual failures requiring attention. Query server capabilities before depending on actions or persistence. | **Durable standard.** Quattro implements a freedesktop notification server with images, actions, markup, hyperlinks, and persistence, but clients must remain valid when optional capabilities differ. Do not use Quattro's private `omarchy-glyph` hint or its action-toast app identity. |
| File dialogs and opening resources | Use the toolkit's XDG desktop portal integration for PGN import/export and URI/file opening. | **Durable desktop boundary.** Quattro installs and configures `xdg-desktop-portal-hyprland`; the app should not call Hyprland-specific chooser helpers. |
| Quickshell / shell IPC | None required for the main app. A future optional bar widget or quick action may be a separate Quattro plugin communicating with Omachess over an Omachess-owned interface. | **Explicitly excluded from the v0.1 app contract.** The shell plugin and IPC APIs are for surfaces hosted inside the long-running shell, not for making a standalone app native. |

## Evidence

### Theme values

Quattro's central color singleton says its foundational palette comes from
`theme/colors.toml`, while per-shell-surface roles come from `theme/shell.toml`;
it locates the active theme at
`~/.local/state/omarchy/current/theme` and reads both files
([`Color.qml`, lines 7–28 and 222–245](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/shell/Commons/Color.qml#L7-L28)).
The stock Tokyo Night palette demonstrates the semantic keys Omachess can map:
`mode`, `accent`, `selection`, `muted`, `background`, `foreground`, and named
colors ([`colors.toml`](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/themes/tokyo-night/colors.toml)).

Theme switching stages a new directory, replaces the active directory, writes
`theme.name`, then sends an immediate payload to the running shell
([`omarchy-theme-set`, lines 145–188](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/bin/omarchy-theme-set#L145-L188)).
That replacement behavior is why watching `colors.toml` alone is fragile:
Omachess should watch the containing state directory and reopen the file after
rename events. After the swap, Omarchy invokes user `theme-set` hooks
([`omarchy-theme-set`, lines 190–216](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/bin/omarchy-theme-set#L190-L216));
the hook runner's contract is simply executable scripts under
`~/.config/omarchy/hooks/<name>` and `<name>.d`
([`omarchy-hook`](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/bin/omarchy-hook)).

The current path cannot be treated as timeless. During this alpha, Omarchy
[moved current theme state from `~/.config` to `~/.local/state`](https://github.com/basecamp/omarchy/commit/0f5e811)
and later
[renamed foundational palette keys](https://github.com/basecamp/omarchy/commit/afa2839).
That churn supports a version-gated adapter and graceful fallback rather than
spreading raw paths and token names across the application.

`shell.toml` should remain out of the contract. Its parser feeds Quickshell-only
roles and the shell's typography, spacing, bar, and control state
([`Color.qml`, lines 167–219](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/shell/Commons/Color.qml#L167-L219)).
Those details describe how shell surfaces render, not how an independent chess
workspace should lay itself out.

### Launcher and icons

Quattro's app library enumerates standard desktop entries, resolves named icons
through the active icon theme, and launches the selected desktop ID with
`gtk-launch`
([`AppLibrary.qml`, lines 49–79](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/shell/services/AppLibrary.qml#L49-L79)).
Its fallback index scans the usual XDG icon directories plus
`/usr/share/pixmaps`
([`AppLibrary.qml`, lines 115–129](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/shell/services/AppLibrary.qml#L115-L129)).
The Freedesktop
[Desktop Entry Specification](https://specifications.freedesktop.org/desktop-entry/latest-single/)
defines `Name`, `Exec`, `Icon`, application actions, and launch field codes;
the [Icon Theme Specification](https://specifications.freedesktop.org/icon-theme/latest/)
defines application icon installation and guarantees `hicolor` as the fallback
theme. Omachess therefore needs no Quattro-only launcher manifest.

### Hyprland behavior

Quattro configures GUI toolkits for Wayland and identifies the session as
Hyprland
([`envs.lua`, lines 7–29](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/default/hypr/envs.lua#L7-L29)).
Its general window policy tags every window and applies the default opacity
after app-specific rules have had a chance to opt out
([`windows.lua`](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/default/hypr/windows.lua)).
A normal Omachess Wayland window therefore participates without an Omachess
rule. The application should expose a stable app ID for user-authored rules,
session restoration, activation, and diagnostics, while making no assumptions
about a user's chosen gaps, rounding, opacity, or layout.

Quattro includes both GTK and Hyprland XDG portal backends in its base package
set
([`omarchy-base.packages`, lines 141–142](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/install/omarchy-base.packages#L141-L142)).
Using a toolkit's portal-aware file chooser consequently fits the installed
desktop boundary without depending on Omarchy's private launch helpers.

### Notifications

Quattro hosts a Quickshell `NotificationServer` and advertises image, action,
markup, hyperlink, and persistence support
([`Service.qml`, lines 749–763](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/shell/plugins/notifications/Service.qml#L749-L763)).
That server is an implementation of the standard
[`org.freedesktop.Notifications` protocol](https://specifications.freedesktop.org/notification/latest-single/),
whose `desktop-entry` hint links a notification to the sender's desktop file
and whose app icons use freedesktop icon names or file URIs. Omachess should
talk to the standard protocol through its toolkit, not to the shell's internal
notification models or IPC.

### Why not a Quickshell plugin

The official shell README defines `omarchy-shell` as one long-running
Quickshell process whose plugins are bar widgets, panels, overlays, menus,
services, or complete bars
([`shell/README.md`, lines 1–14 and 46–89](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/shell/README.md#L1-L14)).
Its IPC methods summon and manage those hosted shell plugins
([`shell/README.md`, lines 155–179](https://github.com/basecamp/omarchy/blob/ea549a74d56c7db5dc4d7ba7509a56514133996a/shell/README.md#L155-L179)).
That is a useful future extension surface for a small Omachess bar widget, but
it is the wrong lifecycle and trust boundary for the full chess application.

## Acceptance checks for the eventual implementation

- A package install makes Omachess appear in Quattro's launcher with its real
  name and icon; no launcher refresh script or user configuration edit is
  required.
- The initial palette matches the active Quattro theme, changing themes
  repaints an open Omachess window, and a missing/malformed palette never
  prevents startup.
- The main window starts tiled, remains usable under dwindle and scrolling
  layouts, handles fractional scaling, and exposes one stable Wayland app ID.
- PGN import/export uses portal-aware dialogs.
- A test notification displays the Omachess icon and identity in Quattro, while
  the same notification code remains valid against another conforming server.
- Omachess still launches and remains usable if `omarchy-shell` is restarted or
  its IPC/plugin schema changes.

## Newly surfaced decisions

1. **Choose the v0.1 UI/runtime stack.** Which toolkit can deliver the
   keyboard-first chess UI while providing first-class Wayland app IDs,
   portal-aware dialogs, freedesktop notifications, accessibility, and a small
   filesystem-backed theme adapter?
2. **Set the Quattro compatibility floor.** Should v0.1 support any Omarchy
   `4.0.0.alpha` build exposing the probed files, or pin and test a named
   Quattro release/build once Omarchy 4 ships?
3. **Fix the application identity.** What reverse-DNS desktop/app ID and icon
   name should the AUR package own permanently?
