# Installing, backing up, and removing Omachess

Omachess is an Omarchy 4 (Quattro) application. It is local-first: no account,
no hosted backend, and no network access is needed to install or run it.

## Install

```bash
git clone https://aur.archlinux.org/omachess.git
cd omachess
makepkg -si
```

`makepkg` builds Omachess from this repository's signed source tarball and
verifies the signature against the packager key pinned in the `PKGBUILD`. The
package depends on `omarchy>=4.0.0`; it will not install on an Omarchy 3 or
plain Arch system.

Installing is all the desktop integration there is. The package registers the
desktop entry `com.omachess.Omachess.desktop` and a matching hicolor icon, so
Omachess appears in the Quattro launcher under its real name with its own
artwork the moment installation finishes. There is no launcher refresh script
to run and no configuration file to edit. The package installs no Hyprland
window rules and no Omarchy hooks: Omachess behaves as an ordinary tiled
Wayland window under dwindle and scrolling layouts.

### Desktop identity

| Identity | Value |
| --- | --- |
| Desktop entry ID | `com.omachess.Omachess` |
| Wayland app ID | `com.omachess.Omachess` |
| Icon name | `com.omachess.Omachess` |
| Command | `omachess` |

The entry ID, app ID, and icon name are deliberately one string, which is what lets the
compositor, the launcher, and desktop notifications recognise a running
Omachess as the installed application. **This identity is fixed permanently for
v0.1 and will not change**, so any window rule, keybinding, or script you write
against it keeps working. Launching Omachess from the Quattro launcher and
running `omachess` in a shell start the same workspace.

## Where your chess work lives

Everything you create — your Personal Library of Game Records, Studies, and
Variant Definitions — lives in the Live Store under your XDG data directory:

```
${XDG_DATA_HOME:-~/.local/share}/omachess/
```

Preferences live under `${XDG_CONFIG_HOME:-~/.config}/omachess/`. Nothing the
package owns is written there; those directories belong to you.

## Backup

The Live Store is a plain directory, so an ordinary file backup is enough.
Close Omachess first so the store is quiescent:

```bash
tar czf omachess-backup-$(date +%F).tar.gz \
  -C "${XDG_DATA_HOME:-$HOME/.local/share}" omachess
```

Restore by extracting that archive back over the same location.

## Export

A file backup is a copy of one Omachess version's Live Store. For moving your
library between machines and across Omachess versions, Omachess will export a
Library Portability Package: a versioned, documented export of your Game
Records, durable annotations, Source Snapshots, Record Graph relationships,
Studies, Variant Definitions, and portable preferences. That export is not in
this release yet, so until it lands, back up the directory above — and take an
export as well once it is available.

## Uninstall

```bash
sudo pacman -Rns omachess
```

That removes the program, the desktop entry, and the icon. **It leaves your
chess work alone**: the Live Store and your preferences stay exactly where they
are, so reinstalling Omachess later picks your Personal Library back up.

To remove your data as well — this is irreversible, so export first:

```bash
rm -rf "${XDG_DATA_HOME:-$HOME/.local/share}/omachess" \
       "${XDG_CONFIG_HOME:-$HOME/.config}/omachess" \
       "${XDG_STATE_HOME:-$HOME/.local/state}/omachess"
```
