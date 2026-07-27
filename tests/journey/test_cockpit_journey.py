"""Journeys through the hybrid cockpit: library rail, tabs, board, right rail.

Run against a build with:

    OMACHESS_BINARY=build/app/omachess python3 -m unittest discover tests/journey

Assertions are only about what the running application puts on screen after
public operations. They never open the Live Store or read its tables.
"""

from __future__ import annotations

import unittest

from harness import Screen, Workspace, executable_under_test


def board_is_drawn(screen: Screen) -> bool:
    if len(screen.squares) != 64:
        return False
    if not all(square.size > 0 for square in screen.squares):
        return False
    places = {(round(square.x, 3), round(square.y, 3)) for square in screen.squares}
    return len(places) == 64


def library_ids(screen: Screen) -> list[str]:
    """Game Record ids currently listed in the Personal Library rail."""
    return sorted(
        name.removeprefix("libraryTitle:")
        for name in screen.labels
        if name.startswith("libraryTitle:")
    )


def open_tab_ids(screen: Screen) -> list[str]:
    """Record ids currently open as tabs."""
    return sorted(
        name.removeprefix("tabTitle:")
        for name in screen.labels
        if name.startswith("tabTitle:")
    )


def cockpit_chrome_is_up(screen: Screen) -> bool:
    return (
        board_is_drawn(screen)
        and "libraryHeading" in screen.labels
        and "rightRailHeading" in screen.labels
    )


class CockpitJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.screen_when(cockpit_chrome_is_up)

    def test_open_two_records_switch_close_and_restart(self) -> None:
        # Play the first Game Record.
        self.workspace.play_all("e2e4 e7e5")
        screen = self.workspace.screen_when(lambda s: len(library_ids(s)) == 1)
        first_ids = library_ids(screen)
        self.assertEqual(len(first_ids), 1)
        first_id = first_ids[0]
        self.assertIn(first_id, open_tab_ids(screen))
        self.assertEqual(screen.moves(), ["1. e4", "1... e5"])
        self.assertEqual(screen.palette_source, "quattro")
        self.assertTrue(screen.light_square.startswith("#"))
        self.assertTrue(screen.dark_square.startswith("#"))
        self.assertNotEqual(screen.light_square, screen.dark_square)

        # Start a second Game Record.
        self.workspace.click("newGameButton")
        self.workspace.screen_when(lambda s: s.moves() == [])
        self.workspace.play_all("d2d4")
        screen = self.workspace.screen_when(lambda s: len(library_ids(s)) == 2)
        ids = library_ids(screen)
        self.assertEqual(len(ids), 2)
        second_id = next(record_id for record_id in ids if record_id != first_id)
        self.assertEqual(screen.moves(), ["1. d4"])
        self.assertEqual(screen.pieces().get("d4"), "white_pawn")

        # Open the first record from the library — it becomes a tab and the
        # board/rail switch to its content.
        self.workspace.click(f"library:{first_id}")
        screen = self.workspace.screen_when(
            lambda s: s.moves() == ["1. e4", "1... e5"] and first_id in open_tab_ids(s)
        )
        self.assertIn(second_id, open_tab_ids(screen))
        self.assertEqual(screen.pieces().get("e4"), "white_pawn")
        self.assertEqual(screen.pieces().get("e5"), "black_pawn")
        self.assertNotIn("d4", screen.pieces())

        # Switch to the second tab — board and right-rail moves follow.
        self.workspace.click(f"tab:{second_id}")
        screen = self.workspace.screen_when(lambda s: s.moves() == ["1. d4"])
        self.assertEqual(screen.pieces().get("d4"), "white_pawn")
        self.assertNotIn("e4", screen.pieces())

        # Close the second tab; both records remain in the library.
        self.workspace.click(f"closeTab:{second_id}")
        screen = self.workspace.screen_when(
            lambda s: second_id not in open_tab_ids(s) and first_id in open_tab_ids(s)
        )
        self.assertEqual(set(library_ids(screen)), {first_id, second_id})
        self.assertEqual(screen.moves(), ["1. e4", "1... e5"])

        # Restart: library and remaining tab state survive.
        self.workspace.restart()
        screen = self.workspace.screen_when(
            lambda s: cockpit_chrome_is_up(s)
            and set(library_ids(s)) == {first_id, second_id}
            and first_id in open_tab_ids(s)
            and "restoreButton" in s.labels
        )
        self.assertNotIn(second_id, open_tab_ids(screen))
        self.workspace.click("restoreButton")
        screen = self.workspace.screen_when(
            lambda s: s.moves() == ["1. e4", "1... e5"]
        )
        self.assertEqual(screen.moves(), ["1. e4", "1... e5"])
        self.assertEqual(screen.pieces().get("e4"), "white_pawn")
        self.assertEqual(screen.pieces().get("e5"), "black_pawn")
        # Chrome comes from the Quattro Palette; the board honours Board Theme
        # and Piece Set.
        self.assertEqual(screen.palette_source, "quattro")
        self.assertTrue(screen.light_square.startswith("#"))
        self.assertTrue(screen.dark_square.startswith("#"))
        self.assertNotEqual(screen.light_square, screen.dark_square)
        self.assertEqual(screen.piece_set_id, "cburnett")
        self.assertTrue(
            all(square.artwork_ready for square in screen.squares if square.piece),
            "occupied squares must draw from the current Piece Set",
        )


if __name__ == "__main__":
    unittest.main()
