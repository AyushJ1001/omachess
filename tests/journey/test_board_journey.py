"""Journeys through the walking skeleton: launch, look, flip, resize.

Run against a build with:

    OMACHESS_BINARY=build/app/omachess python3 -m unittest discover tests/journey

Set OMACHESS_TEST_QPA=wayland to run the same journeys against a real
compositor instead of the offscreen platform.
"""

from __future__ import annotations

import unittest

from harness import Screen, Workspace, executable_under_test

BACK_RANK = ["rook", "knight", "bishop", "queen", "king", "bishop", "knight", "rook"]
FILES = "abcdefgh"

STARTING_POSITION = {
    **{f"{file}1": f"white_{role}" for file, role in zip(FILES, BACK_RANK)},
    **{f"{file}2": "white_pawn" for file in FILES},
    **{f"{file}7": "black_pawn" for file in FILES},
    **{f"{file}8": f"black_{role}" for file, role in zip(FILES, BACK_RANK)},
}


def board_is_drawn(screen: Screen) -> bool:
    """All 64 squares are laid out, each in its own place.

    Squares briefly share a position while the scene is being laid out, so a
    journey waits for the settled board rather than the first one it sees.
    """
    if len(screen.squares) != 64:
        return False
    if not all(square.size > 0 for square in screen.squares):
        return False
    places = {(round(square.x, 3), round(square.y, 3)) for square in screen.squares}
    return len(places) == 64


def board_drawn_from(corner: str):
    """A wait condition: the settled board starts at `corner` top-left."""
    return lambda screen: board_is_drawn(screen) and screen.top_left_square().name == corner


class BoardJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)

    def test_opening_omachess_shows_the_standard_starting_position(self) -> None:
        screen = self.workspace.screen_when(board_is_drawn)

        self.assertTrue(screen.visible)
        self.assertEqual(screen.app_id, "com.omachess.Omachess")
        self.assertEqual(screen.title, "Omachess")
        self.assertEqual(len(screen.squares), 64)
        self.assertEqual(screen.pieces(), STARTING_POSITION)

        # White is at the bottom, so a8 is drawn top-left and h1 bottom-right.
        self.assertEqual(screen.top_left_square().name, "a8")
        self.assertEqual(screen.bottom_right_square().name, "h1")

        # The board is square, evenly divided, and inside the window.
        sizes = {round(square.size, 3) for square in screen.squares}
        self.assertEqual(len(sizes), 1)
        for square in screen.squares:
            self.assertTrue(square.visible)
            self.assertGreaterEqual(square.x, 0)
            self.assertLessEqual(square.x + square.size, screen.width + 0.001)
            self.assertLessEqual(square.y + square.size, screen.height + 0.001)

    def test_flipping_the_board_moves_black_to_the_bottom_and_back(self) -> None:
        self.workspace.screen_when(board_is_drawn)

        self.workspace.press_key("f")
        flipped = self.workspace.screen_when(board_drawn_from("h1"))
        self.assertEqual(flipped.bottom_right_square().name, "a8")
        # Flipping reorders the board; it never moves a piece.
        self.assertEqual(flipped.pieces(), STARTING_POSITION)

        # The same intent through the toolbar takes the same path back.
        self.workspace.click("flipButton")
        restored = self.workspace.screen_when(board_drawn_from("a8"))
        self.assertEqual(restored.bottom_right_square().name, "h1")
        self.assertEqual(restored.pieces(), STARTING_POSITION)

    def test_omachess_plays_chess_without_touching_the_network(self) -> None:
        self.workspace.screen_when(board_is_drawn)
        self.workspace.press_key("f")
        self.workspace.screen_when(board_drawn_from("h1"))

        # Starting up and playing opens no IP socket: no account, no hosted
        # backend, no telemetry.
        self.assertEqual(self.workspace.open_network_sockets(), [])

    def test_the_window_resizes_and_the_board_stays_whole(self) -> None:
        self.workspace.screen_when(board_is_drawn)

        for width, height in ((640, 480), (1280, 900), (500, 820)):
            self.workspace.resize(width, height)
            screen = self.workspace.screen_when(
                lambda s, w=width, h=height: (s.width, s.height) == (w, h) and board_is_drawn(s)
            )
            self.assertEqual(len(screen.pieces()), 32)
            for square in screen.squares:
                self.assertLessEqual(square.x + square.size, screen.width + 0.001)
                self.assertLessEqual(square.y + square.size, screen.height + 0.001)


if __name__ == "__main__":
    unittest.main()
