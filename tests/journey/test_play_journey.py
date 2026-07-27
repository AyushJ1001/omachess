"""Journeys through playing a game: pick up, drop, promote, navigate, finish.

Run against a build with:

    OMACHESS_BINARY=build/app/omachess python3 -m unittest discover tests/journey

Every move here is played the way a player plays it — by pressing squares on
the board — and every assertion is about what the running application then puts
on screen. The rules, the notation, and the result all come from the core, so
these journeys check that a real game is playable end to end without knowing
any chess themselves.
"""

from __future__ import annotations

import unittest

from harness import Screen, Workspace, executable_under_test

# --- Scripted games -------------------------------------------------------
#
# Each line is a complete game to one terminal result, written as the squares
# each move joins. They are played through the board, so a passing journey
# means the application reached that result by being played, not by being told.

# The fool's mate: the shortest possible checkmate.
CHECKMATE = "f2f3 e7e5 g2g4 d8h4"

# Sam Loyd's ten-move stalemate: Black has no legal move and is not in check.
STALEMATE = (
    "e2e3 a7a5 d1h5 a8a6 h5a5 h7h5 a5c7 a6h6 h2h4 f7f6 "
    "c7d7 e8f7 d7b7 d8d3 b7b8 d3h7 b8c8 f7g6 c8e6"
)

# Both knights out and back twice over, which brings the starting position
# about for the third time.
REPETITION = "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8"

# A hundred half-moves with no capture and no pawn move, which is the shortest
# a game can be and still reach the fifty-move rule. Found by playing quiet
# legal moves that never repeat a position.
FIFTY_MOVE = (
    "b1a3 g8f6 g1h3 f6g8 a3b5 b8c6 b5a3 g8f6 a3b1 a8b8 h1g1 c6e5 b1a3 f6g8 "
    "a3c4 e5g4 c4a3 g8h6 h3f4 g4e3 f4d5 e3c4 a1b1 h8g8 a3b5 c4e5 d5f4 h6g4 "
    "f4e6 g4e3 e6d4 g8h8 b1a1 e3d5 b5c3 d5b6 d4f5 e5g6 f5h6 g6e5 c3b5 h8g8 "
    "b5a3 e5c4 g1h1 c4e3 h1g1 e3d5 h6f5 g8h8 f5h4 d5c3 a1b1 c3b5 h4f5 b6a4 "
    "f5d4 b5d6 d4b3 b8a8 b3a1 a8b8 a3b5 a4c3 b5a3 d6b5 a1b3 c3e4 b3d4 e4f6 "
    "d4e6 f6g8 a3c4 b5d6 e6f4 g8h6 f4d5 h6f5 d5c3 f5g3 c3b5 g3f5 b1a1 f5h4 "
    "g1h1 d6f5 h1g1 h8g8 c4a3 g8h8 b5d4 f5h6 a1b1 h4f5 d4c6 f5h4 c6b4 h6g8 "
    "b4d5 b8a8"
)

# A game where both sides trade everything away, down to what cannot mate.
# The pawn on c7 promotes to a bishop on b8, so this line also plays a real
# promotion.
INSUFFICIENT_MATERIAL = (
    "g2g3 e7e5 f2f4 e5f4 g3f4 d7d6 g1f3 d8f6 e2e4 f6f4 f1d3 f4f3 d1f3 c8e6 "
    "f3f7 e6f7 e4e5 f7a2 e5d6 a2b1 d6c7 b1c2 c7b8b a8b8 d3h7 c2h7 a1a7 h7d3 "
    "a7b7 h8h2 b7g7 b8b2 h1h2 b2d2 h2d2 f8g7 d2d3 g8h6 c1h6 g7h6 d3e3 h6e3"
)

# A pawn walked to the seventh rank, so the next move offers a promotion.
BEFORE_PROMOTION = "g2g4 h7h5 g4h5 g7g6 h5g6 g8f6 g6g7 d7d5"


def board_is_drawn(screen: Screen) -> bool:
    """All 64 squares are laid out, each in its own place."""
    if len(screen.squares) != 64:
        return False
    if not all(square.size > 0 for square in screen.squares):
        return False
    places = {(round(square.x, 3), round(square.y, 3)) for square in screen.squares}
    return len(places) == 64


def game_is_over(screen: Screen) -> bool:
    return "(" in screen.status()


class PlayJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.screen_when(board_is_drawn)

    def test_picking_up_a_piece_shows_only_where_it_may_go(self) -> None:
        self.workspace.click_square("e2")
        screen = self.workspace.screen_when(lambda s: s.selected() == "e2")
        # A pawn on its starting square may step once or twice, and nowhere
        # else — the core said so, and only those squares are marked.
        self.assertEqual(screen.targets(), {"e3", "e4"})

        # A piece that has nowhere to go cannot be picked up at all.
        self.workspace.click_square("e1")
        screen = self.workspace.screen_when(lambda s: s.selected() is None)
        self.assertEqual(screen.targets(), set())

    def test_playing_moves_shows_them_in_san_and_says_whose_turn_it_is(self) -> None:
        screen = self.workspace.screen()
        self.assertEqual(screen.status(), "White to move")

        self.workspace.play("e2e4")
        screen = self.workspace.screen_when(lambda s: s.pieces().get("e4") == "white_pawn")
        self.assertEqual(screen.moves(), ["1. e4"])
        self.assertEqual(screen.status(), "Black to move")
        self.assertNotIn("e2", screen.pieces())
        # The move just played is marked on both of its squares.
        self.assertEqual({square.name for square in screen.squares if square.last_move},
                         {"e2", "e4"})

        self.workspace.play("e7e5")
        self.workspace.play("g1f3")
        screen = self.workspace.screen_when(lambda s: len(s.moves()) == 3)
        self.assertEqual(screen.moves(), ["1. e4", "1... e5", "2. Nf3"])
        self.assertEqual(screen.pieces().get("f3"), "white_knight")

    def test_an_illegal_destination_leaves_the_board_alone(self) -> None:
        before = self.workspace.screen().pieces()

        # A pawn cannot jump three squares, so dropping it on e5 does nothing.
        self.workspace.play("e2e5")
        screen = self.workspace.screen()
        self.assertEqual(screen.pieces(), before)
        self.assertEqual(screen.moves(), [])
        self.assertEqual(screen.status(), "White to move")

        # Nor can a player move a piece that is not theirs to move.
        self.workspace.play("e7e5")
        self.assertEqual(self.workspace.screen().pieces(), before)

    def test_a_promotion_offers_a_choice_of_piece(self) -> None:
        self.workspace.play_all(BEFORE_PROMOTION)
        self.workspace.screen_when(lambda s: s.pieces().get("g7") == "white_pawn")

        # Dropping the pawn on the last rank asks which piece it becomes
        # instead of choosing one silently.
        self.workspace.click_square("g7")
        self.workspace.click_square("h8")
        screen = self.workspace.screen_when(lambda s: s.promotion_choices())
        self.assertEqual(screen.promotion_choices(), {"queen", "rook", "bishop", "knight"})
        self.assertEqual(screen.pieces().get("h8"), "black_rook")

        self.workspace.click("promote:rook")
        screen = self.workspace.screen_when(lambda s: s.pieces().get("h8") == "white_rook")
        self.assertEqual(screen.moves()[-1], "5. gxh8=R")
        self.assertEqual(screen.promotion_choices(), set())

    def test_navigating_the_game_changes_the_position_on_screen(self) -> None:
        self.workspace.play_all("e2e4 e7e5 g1f3 b8c6")
        live = self.workspace.screen_when(lambda s: len(s.moves()) == 4)

        self.workspace.click("backwardButton")
        screen = self.workspace.screen_when(lambda s: "c6" not in s.pieces())
        self.assertEqual(screen.pieces().get("c8"), "black_bishop")
        # Navigating changes what is shown, never the record.
        self.assertEqual(screen.moves(), live.moves())
        # No piece may be picked up while an earlier position is on screen.
        self.workspace.click_square("f3")
        self.assertIsNone(self.workspace.screen().selected())

        self.workspace.click("startButton")
        screen = self.workspace.screen_when(lambda s: s.pieces().get("e2") == "white_pawn")
        self.assertEqual(len(screen.pieces()), 32)

        self.workspace.click("endButton")
        screen = self.workspace.screen_when(lambda s: s.pieces() == live.pieces())
        self.assertEqual(screen.status(), "White to move")
        # Play carries on from the live position.
        self.workspace.play("f1b5")
        self.assertEqual(self.workspace.screen_when(lambda s: len(s.moves()) == 5).moves()[-1],
                         "3. Bb5")

    def test_pieces_are_drawn_from_the_piece_set_at_every_board_size(self) -> None:
        for width, height in ((640, 480), (1400, 1000), (520, 900)):
            self.workspace.resize(width, height)
            screen = self.workspace.screen_when(
                lambda s, w=width, h=height: (s.width, s.height) == (w, h) and board_is_drawn(s)
            )
            # All 32 pieces are still on a whole board inside the window,
            # each drawn from its own Cburnett file.
            self.assertEqual(len(screen.pieces()), 32)
            for square in screen.squares:
                if not square.piece:
                    continue
                self.assertTrue(
                    square.artwork_ready,
                    f"{square.name} did not draw {square.piece} at {width}x{height}",
                )
                self.assertTrue(
                    square.artwork_source.endswith(f"pieces/cburnett/{square.piece}.svg"),
                    f"{square.name} drew {square.piece} from {square.artwork_source}",
                )
            sizes = {round(square.size, 3) for square in screen.squares}
            self.assertEqual(len(sizes), 1)
            for square in screen.squares:
                self.assertLessEqual(square.x + square.size, screen.width + 0.001)
                self.assertLessEqual(square.y + square.size, screen.height + 0.001)

            # And a game is still playable at this size.
            self.workspace.click_square("e2")
            self.assertEqual(self.workspace.screen_when(lambda s: s.selected() == "e2").targets(),
                             {"e3", "e4"})
            self.workspace.click_square("e2")

    def assert_game_ends_with(self, script: str, result: str) -> None:
        """Plays `script` through the board and checks the reported result."""
        self.workspace.play_all(script)
        screen = self.workspace.screen_when(game_is_over, timeout=30.0)
        self.assertEqual(screen.status(), result)
        # A finished game accepts no more moves.
        self.workspace.click_square("e2")
        self.assertIsNone(self.workspace.screen().selected())

    def test_a_game_can_be_played_to_checkmate(self) -> None:
        self.assert_game_ends_with(CHECKMATE, "Black wins by checkmate (0-1)")

    def test_a_game_can_be_played_to_stalemate(self) -> None:
        self.assert_game_ends_with(STALEMATE, "Draw by stalemate (1/2-1/2)")

    def test_a_game_can_be_played_to_a_draw_by_repetition(self) -> None:
        self.assert_game_ends_with(REPETITION, "Draw by threefold repetition (1/2-1/2)")

    def test_a_game_can_be_played_to_a_draw_by_the_fifty_move_rule(self) -> None:
        self.assert_game_ends_with(FIFTY_MOVE, "Draw by the fifty-move rule (1/2-1/2)")

    def test_a_game_can_be_played_to_a_draw_by_insufficient_material(self) -> None:
        self.assert_game_ends_with(
            INSUFFICIENT_MATERIAL, "Draw by insufficient material (1/2-1/2)"
        )


if __name__ == "__main__":
    unittest.main()
