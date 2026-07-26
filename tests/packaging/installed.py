"""What an installed Omachess looks like on disk.

These helpers read a staged installation — the tree `cmake --install` (and
therefore `makepkg`) produces — so packaging journeys assert what a player
actually gets, not what the source tree happens to contain.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

_JOURNEY = Path(__file__).resolve().parent.parent / "journey"
if str(_JOURNEY) not in sys.path:
    sys.path.append(str(_JOURNEY))

from harness import JourneyError, Workspace  # noqa: E402,F401  (re-exported)

# The permanent v0.1 identity: desktop entry ID, icon name, and Wayland app ID
# are all this one reverse-DNS string.
DESKTOP_ID = "com.omachess.Omachess"

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent.parent


def installed_prefix() -> Path:
    """The staged installation a packaging journey inspects.

    ctest stages one with `cmake --install`; a developer can point
    OMACHESS_INSTALL_PREFIX at any install tree, including a real one.
    """
    location = os.environ.get("OMACHESS_INSTALL_PREFIX")
    if not location:
        raise JourneyError(
            "set OMACHESS_INSTALL_PREFIX to an installed Omachess tree to run packaging journeys"
        )
    path = Path(location)
    if not path.is_dir():
        raise JourneyError(f"OMACHESS_INSTALL_PREFIX does not name a directory: {path}")
    return path


def installed_files(prefix: Path) -> list[Path]:
    return [path for path in prefix.rglob("*") if path.is_file() or path.is_symlink()]


def read_desktop_entry(path: Path) -> dict[str, str]:
    """The `[Desktop Entry]` group of a desktop file, as plain keys."""
    entry: dict[str, str] = {}
    in_group = False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            in_group = line == "[Desktop Entry]"
            continue
        if in_group and "=" in line:
            key, _, value = line.partition("=")
            entry[key.strip()] = value.strip()
    return entry


def read_pkgbuild() -> str:
    return (REPOSITORY_ROOT / "packaging" / "PKGBUILD").read_text(encoding="utf-8")
