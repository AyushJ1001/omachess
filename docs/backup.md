# Backing up Omachess

Omachess keeps your chess work in fixed XDG locations. There is no in-app
“choose library folder” in v0.1. To back up your Personal Library with your
usual tools, copy exactly these paths:

## Must copy — the Live Store

```
$XDG_DATA_HOME/omachess/
```

When `XDG_DATA_HOME` is unset, that is:

```
~/.local/share/omachess/
```

This directory is the Live Store root. It holds `live-store.sqlite` (and its
SQLite WAL companions when present). Copy the whole directory as a unit.

## Copy if preferences matter

```
$XDG_CONFIG_HOME/omachess/
```

When `XDG_CONFIG_HOME` is unset, that is:

```
~/.config/omachess/
```

Product preferences live here. Skip it if you only care about Game Records.

## Safe to skip — cache

```
$XDG_CACHE_HOME/omachess/
```

(default `~/.cache/omachess/`)

Disposable probe and cache artifacts. Omachess recreates what it needs.

## Notes

- Uninstalling the AUR package retains the data and config directories above.
- A [Library Portability Package](library-portability-package.md) is the
  documented take-away format for moving a complete library; filesystem copy of
  the paths above is the backup contract for the Live Store itself.
- The on-disk SQLite schema is internal. Do not treat the database file as a
  supported interchange API.
