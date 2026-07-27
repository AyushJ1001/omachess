"""Keyboard-complete chrome and its shared command palette."""

from __future__ import annotations

import unittest

from harness import Workspace, executable_under_test
from test_cockpit_journey import cockpit_chrome_is_up, library_ids, open_tab_ids


def palette_actions(screen) -> set[tuple[str, str]]:
    return {
        (
            text,
            screen.labels[name.replace("paletteTitle:", "paletteBinding:")],
        )
        for name, text in screen.labels.items()
        if name.startswith("paletteTitle:")
    }


def all_palette_actions(workspace) -> set[tuple[str, str]]:
    found = set()
    for _ in range(30):
        found.update(palette_actions(workspace.screen()))
        workspace.press_key("down")
    return found


class CommandPaletteJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.screen_when(cockpit_chrome_is_up)

    def test_every_registered_action_is_discoverable_and_keyboard_reachable(self) -> None:
        self.workspace.play_all("e2e4 e7e5")
        screen = self.workspace.screen_when(lambda s: len(library_ids(s)) == 1)
        record_id = library_ids(screen)[0]

        expected = {
            ("Command palette", "Ctrl+K"),
            ("New game", "Ctrl+N"),
            ("Flip board", "F"),
            ("First position", "Home"),
            ("Previous position", "Left"),
            ("Next position", "Right"),
            ("Latest position", "End"),
            ("Focus next pane", "Alt+Right"),
            ("Focus previous pane", "Alt+Left"),
            ("Follow desktop Board Theme", "Alt+T"),
            ("Use classic Board Theme", "Alt+Shift+T"),
            ("Use slate Board Theme", "Alt+S"),
            ("Use walnut Board Theme", "Alt+W"),
            ("Use cburnett Piece Set", "Ctrl+Shift+P"),
            ("Open " + screen.labels[f"libraryTitle:{record_id}"], "Alt+1"),
            ("Switch to " + screen.labels[f"tabTitle:{record_id}"], "Ctrl+1"),
            ("Close " + screen.labels[f"tabTitle:{record_id}"], "Ctrl+W"),
        }
        self.workspace.press_key("ctrl+k")
        palette = self.workspace.screen_when(lambda s: "commandPaletteTitle" in s.labels)
        self.assertEqual(all_palette_actions(self.workspace), expected)
        self.assertTrue(all("Super" not in binding for _, binding in expected))
        self.workspace.press_key("ctrl+k")

        before = screen.top_left_square().name
        self.workspace.press_key("f")
        self.assertNotEqual(self.workspace.screen().top_left_square().name, before)

        self.workspace.press_key("home")
        self.assertEqual(self.workspace.screen_when(lambda s: s.moves() == ["1. e4", "1... e5"]).labels["reviewLabel"][:9], "Reviewing")
        self.workspace.press_key("end")
        self.assertNotIn("reviewLabel", self.workspace.screen().labels)

        self.workspace.press_key("ctrl+n")
        self.workspace.screen_when(lambda s: s.moves() == [])
        self.workspace.press_key("alt+1")
        self.workspace.screen_when(lambda s: s.moves() == ["1. e4", "1... e5"])
        self.workspace.press_key("ctrl+w")
        self.workspace.screen_when(lambda s: record_id not in open_tab_ids(s))

    def test_palette_invokes_the_same_registered_action(self) -> None:
        before = self.workspace.screen().top_left_square().name
        self.workspace.press_key("ctrl+k")
        screen = self.workspace.screen_when(lambda s: "commandPaletteTitle" in s.labels)
        self.assertIn(("Flip board", "F"), palette_actions(screen))
        self.workspace.click("paletteAction:flip")
        self.workspace.screen_when(
            lambda s: "commandPaletteTitle" not in s.labels
            and s.top_left_square().name != before
        )

    def test_pane_traversal_and_async_updates_preserve_focus(self) -> None:
        self.workspace.play_all("e2e4")
        self.workspace.screen_when(lambda s: len(library_ids(s)) == 1)
        self.workspace.press_key("alt+right")
        library_focus = self.workspace.screen().active_focus
        self.assertTrue(
            library_focus.startswith("pane:library") or library_focus.startswith("library:")
        )

        self.workspace.press_key("down")
        self.assertTrue(self.workspace.screen().active_focus.startswith("library:"))

        self.workspace.press_key("alt+right")
        self.assertTrue(self.workspace.screen().active_focus.startswith("pane:board"))
        self.workspace.press_key("alt+right")
        right_focus = self.workspace.screen().active_focus
        self.assertTrue(right_focus.startswith("pane:right") or right_focus.startswith("move:"))
        self.workspace.press_key("alt+left")
        self.assertTrue(self.workspace.screen().active_focus.startswith("pane:board"))

        focused = self.workspace.screen().active_focus
        self.workspace.replace_theme(
            {
                "mode": "dark",
                "accent": "#7aa2f7",
                "selection": "#292e42",
                "muted": "#414868",
                "background": "#182010",
                "dark_background": "#10140a",
                "darker_background": "#080a05",
                "lighter_background": "#d0e0c0",
                "foreground": "#eef4e8",
                "red": "#f7768e",
                "yellow": "#e0af68",
                "green": "#9ece6a",
                "orange": "#eb927b",
            },
            name="focus-stability",
        )
        self.workspace.screen_when(lambda s: s.theme_name == "focus-stability")
        self.assertEqual(self.workspace.screen().active_focus, focused)


if __name__ == "__main__":
    unittest.main()
