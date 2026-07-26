"""Journeys through the installed package: launcher identity and clean removal.

These run against a staged installation rather than a development build, so
they assert what installing the `omachess` AUR package gives a player:

    cmake --install build --prefix /usr --destdir stage
    OMACHESS_INSTALL_PREFIX=stage/usr python3 -m unittest discover tests/packaging
"""

from __future__ import annotations

import shutil
import subprocess
import unittest

from installed import (
    DESKTOP_ID,
    Workspace,
    installed_files,
    installed_prefix,
    read_desktop_entry,
)

import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent / "journey"))
from test_board_journey import STARTING_POSITION, board_is_drawn  # noqa: E402


class InstalledDesktopEntry(unittest.TestCase):
    def setUp(self) -> None:
        self.prefix = installed_prefix()
        self.entry_path = self.prefix / "share/applications" / f"{DESKTOP_ID}.desktop"

    def test_installing_registers_the_launcher_entry(self) -> None:
        self.assertTrue(self.entry_path.is_file(), f"no desktop entry at {self.entry_path}")
        entry = read_desktop_entry(self.entry_path)

        self.assertEqual(entry["Type"], "Application")
        self.assertEqual(entry["Name"], "Omachess")
        self.assertEqual(entry["Exec"], "omachess")
        self.assertEqual(entry["Terminal"], "false")
        self.assertEqual(entry["Categories"], "Game;BoardGame;")
        self.assertEqual(entry["StartupNotify"], "true")
        # The launcher shows the entry as-is: no refresh script, no hidden flag.
        self.assertNotIn("NoDisplay", entry)
        self.assertNotIn("Hidden", entry)

    def test_the_desktop_entry_is_valid_to_the_freedesktop_spec(self) -> None:
        validator = shutil.which("desktop-file-validate")
        if validator is None:
            self.skipTest("desktop-file-validate is not installed")
        result = subprocess.run(
            [validator, str(self.entry_path)], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_the_launcher_icon_is_installed_under_hicolor(self) -> None:
        icons = list((self.prefix / "share/icons/hicolor").rglob(f"{DESKTOP_ID}.*"))
        self.assertTrue(icons, "no hicolor application icon is installed")
        for icon in icons:
            self.assertEqual(icon.parent.name, "apps", f"{icon} is not in an apps directory")

        entry = read_desktop_entry(self.entry_path)
        self.assertEqual(entry["Icon"], DESKTOP_ID)

    def test_the_wayland_app_id_matches_the_desktop_entry_id(self) -> None:
        entry = read_desktop_entry(self.entry_path)
        self.assertEqual(self.entry_path.stem, DESKTOP_ID)
        self.assertEqual(entry.get("StartupWMClass"), DESKTOP_ID)

        with Workspace(self.prefix / "bin/omachess") as workspace:
            screen = workspace.screen_when(board_is_drawn)
            self.assertEqual(screen.app_id, DESKTOP_ID)

    def test_launching_by_desktop_entry_starts_the_same_workspace(self) -> None:
        """`Exec=omachess` from the launcher is the shell command a player runs."""
        entry = read_desktop_entry(self.entry_path)
        executable = self.prefix / "bin" / entry["Exec"]
        self.assertTrue(executable.is_file(), f"Exec names no installed program: {executable}")

        with Workspace(executable) as workspace:
            screen = workspace.screen_when(board_is_drawn)
            self.assertTrue(screen.visible)
            self.assertEqual(screen.title, "Omachess")
            self.assertEqual(screen.pieces(), STARTING_POSITION)


class InstalledFootprint(unittest.TestCase):
    def setUp(self) -> None:
        self.prefix = installed_prefix()
        self.files = installed_files(self.prefix)

    def test_the_package_installs_no_hyprland_rules_or_omarchy_hooks(self) -> None:
        for path in self.files:
            place = str(path.relative_to(self.prefix))
            self.assertNotIn("hypr", place.lower(), f"{place} installs compositor configuration")
            self.assertNotIn("omarchy", place.lower(), f"{place} installs an Omarchy hook")
            self.assertFalse(
                place.startswith("share/libalpm/"), f"{place} installs a pacman hook"
            )

    def test_the_package_installs_only_program_files_and_documentation(self) -> None:
        expected = {
            "bin/omachess",
            f"share/applications/{DESKTOP_ID}.desktop",
        }
        places = {str(path.relative_to(self.prefix)) for path in self.files}
        self.assertTrue(expected <= places, f"missing {sorted(expected - places)}")
        for place in places:
            self.assertTrue(
                place.startswith(("bin/", "share/applications/", "share/icons/", "share/doc/")),
                f"{place} is outside the program, launcher, icon, and documentation footprint",
            )

    def test_removal_guidance_ships_with_the_package(self) -> None:
        docs = self.prefix / "share/doc/omachess"
        self.assertTrue(docs.is_dir(), "no documentation is installed")
        text = "\n".join(path.read_text(encoding="utf-8") for path in docs.rglob("*.md")).lower()
        for topic in ("backup", "export", "uninstall"):
            self.assertIn(topic, text, f"the installed documentation never mentions {topic}")
        # Uninstalling leaves the player's chess work alone, so the guidance has
        # to say where that work lives.
        self.assertIn("xdg_data_home", text)


if __name__ == "__main__":
    unittest.main()
