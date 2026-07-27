"""Journeys through Live Store persistence: play, restart, restore.

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


def restore_is_offered(screen: Screen) -> bool:
    return "restoreButton" in screen.labels


class LiveStoreJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.screen_when(board_is_drawn)

    def test_a_played_game_survives_restart_and_restore(self) -> None:
        self.workspace.play_all("e2e4 e7e5 g1f3")
        screen = self.workspace.screen_when(lambda s: len(s.moves()) == 3)
        self.assertEqual(screen.moves(), ["1. e4", "1... e5", "2. Nf3"])
        self.assertEqual(screen.pieces().get("f3"), "white_knight")

        self.workspace.restart()
        self.workspace.screen_when(board_is_drawn)
        screen = self.workspace.screen_when(restore_is_offered)
        self.assertEqual(screen.labels.get("restoreLabel"), "Restore previous game")
        # Restart shows a fresh board until the player restores deliberately.
        self.assertEqual(screen.moves(), [])
        self.assertEqual(len(screen.pieces()), 32)

        self.workspace.click("restoreButton")
        screen = self.workspace.screen_when(lambda s: len(s.moves()) == 3)
        self.assertEqual(screen.moves(), ["1. e4", "1... e5", "2. Nf3"])
        self.assertEqual(screen.pieces().get("f3"), "white_knight")
        self.assertEqual(screen.pieces().get("e4"), "white_pawn")
        self.assertEqual(screen.pieces().get("e5"), "black_pawn")
        self.assertNotIn("restoreButton", screen.labels)


if __name__ == "__main__":
    unittest.main()
