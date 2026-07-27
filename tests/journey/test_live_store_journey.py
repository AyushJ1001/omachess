"""Journeys through Live Store persistence: play, restart, restore.

Run against a build with:

    OMACHESS_BINARY=build/app/omachess python3 -m unittest discover tests/journey

Assertions are only about what the running application puts on screen after
public operations. They never open the Live Store or read its tables.
"""

from __future__ import annotations

import time
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

    def test_a_timed_game_suspends_and_restores_without_running_until_resumed(self) -> None:
        self.workspace.select("clockPicker", 2)
        self.workspace.play_all("e2e4 e7e5 g1f3")
        screen = self.workspace.screen_when(lambda s: len(s.moves()) == 3)
        self.assertEqual(screen.moves(), ["1. e4", "1... e5", "2. Nf3"])
        self.assertEqual(screen.pieces().get("f3"), "white_knight")
        ids = library_ids(screen)
        self.assertEqual(len(ids), 1)

        self.workspace.click("suspendGameButton")
        screen = self.workspace.screen_when(lambda s: "resumeGameButton" in s.labels)
        frozen_clocks = (
            screen.labels["whiteClockLabel"],
            screen.labels["blackClockLabel"],
        )
        time.sleep(0.3)
        screen = self.workspace.screen()
        self.assertEqual(
            (screen.labels["whiteClockLabel"], screen.labels["blackClockLabel"]),
            frozen_clocks,
        )

        self.workspace.restart()
        screen = self.workspace.screen_when(
            lambda s: "restoreButton" in s.labels and len(library_ids(s)) == 1
        )
        self.assertEqual(
            screen.labels["restoreLabel"],
            "Restore suspended Played Game · 3 moves",
        )
        self.assertEqual(screen.moves(), [])

        self.workspace.click("restoreButton")
        screen = self.workspace.screen_when(
            lambda s: len(s.moves()) == 3 and "resumeGameButton" in s.labels
        )
        self.assertEqual(screen.moves(), ["1. e4", "1... e5", "2. Nf3"])
        self.assertEqual(screen.pieces().get("f3"), "white_knight")
        self.assertEqual(screen.pieces().get("e4"), "white_pawn")
        self.assertEqual(screen.pieces().get("e5"), "black_pawn")
        self.assertEqual(library_ids(screen), ids)
        self.assertEqual(
            (screen.labels["whiteClockLabel"], screen.labels["blackClockLabel"]),
            frozen_clocks,
        )
        time.sleep(0.3)
        self.assertEqual(
            (
                self.workspace.screen().labels["whiteClockLabel"],
                self.workspace.screen().labels["blackClockLabel"],
            ),
            frozen_clocks,
        )

        self.workspace.click("resumeGameButton")
        self.workspace.screen_when(
            lambda s: s.labels.get("blackClockLabel") != frozen_clocks[1]
        )


if __name__ == "__main__":
    unittest.main()
