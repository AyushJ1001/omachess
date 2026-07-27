"""Journeys through Position Setup as a distinct workspace activity."""

from __future__ import annotations

import unittest

from harness import Workspace, executable_under_test


START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
KINGS_FEN = "4k3/8/8/8/8/8/8/4K3 w - - 0 1"


class PositionSetupJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.click("positionSetupButton")

    def test_fen_round_trip_and_invalid_fen_explains_the_problem(self) -> None:
        self.workspace.enter_text("fenInput", KINGS_FEN)
        self.workspace.click("applyFenButton")
        screen = self.workspace.screen_when(
            lambda s: s.labels.get("positionClassLabel") == "Rule-valid Position"
        )
        self.assertEqual(screen.labels.get("fenInput"), KINGS_FEN)
        self.assertEqual(
            screen.pieces(), {"e1": "white_king", "e8": "black_king"}
        )

        self.workspace.enter_text("fenInput", KINGS_FEN.replace(" w ", " x "))
        self.workspace.click("applyFenButton")
        screen = self.workspace.screen_when(
            lambda s: "side to move" in s.labels.get("fenErrorLabel", "")
        )
        self.assertEqual(
            screen.pieces(), {"e1": "white_king", "e8": "black_king"}
        )

        self.workspace.enter_text("fenInput", "not a fen")
        self.workspace.click("applyFenButton")
        screen = self.workspace.screen_when(
            lambda s: "six fields" in s.labels.get("fenErrorLabel", "")
        )
        self.assertEqual(
            screen.pieces(), {"e1": "white_king", "e8": "black_king"}
        )

    def test_manual_placement_can_produce_a_rule_valid_position(self) -> None:
        self.workspace.enter_text("fenInput", "8/8/8/8/8/8/8/8 w - - 0 1")
        self.workspace.click("applyFenButton")
        self.workspace.click("tray:white_king")
        self.workspace.click_square("e1")
        self.workspace.click("tray:black_king")
        self.workspace.click_square("e8")
        self.workspace.click("relocatePieceTool")
        self.workspace.click_square("e8")
        self.workspace.click_square("d8")
        self.workspace.click("tray:white_queen")
        self.workspace.click_square("e1")
        self.workspace.screen_when(
            lambda s: s.labels.get("positionClassLabel") == "Freeform Position"
        )
        self.workspace.click("tray:white_king")
        self.workspace.click_square("e1")
        screen = self.workspace.screen_when(
            lambda s: s.labels.get("positionClassLabel") == "Rule-valid Position"
        )
        self.assertEqual(
            screen.pieces(), {"e1": "white_king", "d8": "black_king"}
        )
        self.workspace.click("startSetupGameButton")
        screen = self.workspace.screen_when(
            lambda s: "positionClassLabel" not in s.labels
        )
        self.assertEqual(screen.status(), "Draw by insufficient material (1/2-1/2)")

    def test_freeform_position_visibly_forfeits_rule_capabilities(self) -> None:
        self.workspace.enter_text("fenInput", START_FEN)
        self.workspace.click("applyFenButton")
        self.workspace.click("removePieceTool")
        self.workspace.click_square("e1")
        screen = self.workspace.screen_when(
            lambda s: s.labels.get("positionClassLabel") == "Freeform Position"
        )
        self.assertEqual(
            screen.labels.get("positionCapabilitiesLabel"),
            "No clocks · No result detection · Cannot start a Played Game · Engine use not guaranteed",
        )
        self.assertEqual(screen.labels.get("rightRailHeading"), "Position Setup")


if __name__ == "__main__":
    unittest.main()
