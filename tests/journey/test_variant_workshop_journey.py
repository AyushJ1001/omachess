"""Journeys through the Variant Workshop Board and Pieces steps."""

from __future__ import annotations

import unittest

from harness import Workspace, executable_under_test


def unavailable_presets(screen):
    return {
        name.removeprefix("boardPreset:")
        for name, text in screen.labels.items()
        if name.startswith("boardPreset:") and "Unavailable:" in text
    }


class VariantWorkshopJourney(unittest.TestCase):
    def test_capable_build_offers_all_presets_and_geometry_changes_immediately(self):
        with Workspace(
            executable_under_test(), engine_capabilities="largeboards"
        ) as workspace:
            workspace.click("newVariantButton")
            screen = workspace.screen_when(
                lambda s: s.labels.get("workshopStepHeading") == "1. Board"
            )
            self.assertEqual(unavailable_presets(screen), set())
            self.assertEqual(len(screen.squares), 64)
            self.assertEqual(
                screen.labels.get("libraryMeta:variant-draft"),
                "Draft Variant Definition",
            )

            workspace.click("boardPreset:max-12x10")
            screen = workspace.screen_when(lambda s: len(s.squares) == 120)
            self.assertEqual(len({square.x for square in screen.squares}), 12)
            self.assertEqual(len({square.y for square in screen.squares}), 10)

            workspace.restart()
            screen = workspace.screen_when(
                lambda s: len(s.squares) == 120
                and s.labels.get("libraryMeta:variant-draft")
                == "Draft Variant Definition"
            )
            self.assertEqual(screen.labels["workshopStatus"], "Draft Variant Definition · v1")

    def test_stock_build_visibly_gates_every_large_board(self):
        with Workspace(executable_under_test(), engine_capabilities="stock") as workspace:
            workspace.click("newVariantButton")
            screen = workspace.screen_when(
                lambda s: len(unavailable_presets(s)) == 3
            )
            self.assertEqual(
                unavailable_presets(screen),
                {"grand-10x8", "wide-10x10", "max-12x10"},
            )
            self.assertTrue(
                all(
                    "supports boards up to 8×8" in screen.labels[f"boardPreset:{preset}"]
                    for preset in unavailable_presets(screen)
                )
            )

    def test_pieces_catalogue_and_rejected_betza_atom(self):
        with Workspace(executable_under_test()) as workspace:
            workspace.click("newVariantButton")
            workspace.click("workshopContinue")
            screen = workspace.screen_when(
                lambda s: s.labels.get("workshopStepHeading") == "2. Pieces"
            )
            self.assertEqual(
                {
                    name.removeprefix("piece:")
                    for name in screen.labels
                    if name.startswith("piece:")
                },
                {"K", "Q", "R", "B", "N", "P", "A", "C", "M", "F", "W", "G", "O"},
            )
            workspace.enter_text("customPieceName", "Sentinel")
            workspace.enter_text("customPieceLetter", "S")
            workspace.enter_text("customPieceBetza", "yQ")
            workspace.click("saveCustomPiece")
            screen = workspace.screen_when(
                lambda s: "Unsupported Betza atom: y"
                in s.labels.get("betzaErrorLabel", "")
            )
            self.assertEqual(screen.labels["betzaErrorLabel"], "Unsupported Betza atom: y")


if __name__ == "__main__":
    unittest.main()
