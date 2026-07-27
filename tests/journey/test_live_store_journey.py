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


def library_ids(screen: Screen) -> list[str]:
    return sorted(
        name.removeprefix("libraryTitle:")
        for name in screen.labels
        if name.startswith("libraryTitle:")
    )


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
        ids = library_ids(screen)
        self.assertEqual(len(ids), 1)

        self.workspace.restart()
        # Open tabs restore the active board on restart; the Game Record also
        # remains listed in the Personal Library.
        screen = self.workspace.screen_when(
            lambda s: board_is_drawn(s) and len(s.moves()) == 3 and len(library_ids(s)) == 1
        )
        self.assertEqual(screen.moves(), ["1. e4", "1... e5", "2. Nf3"])
        self.assertEqual(screen.pieces().get("f3"), "white_knight")
        self.assertEqual(screen.pieces().get("e4"), "white_pawn")
        self.assertEqual(screen.pieces().get("e5"), "black_pawn")
        self.assertEqual(library_ids(screen), ids)


if __name__ == "__main__":
    unittest.main()
