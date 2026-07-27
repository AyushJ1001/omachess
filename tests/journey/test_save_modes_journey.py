"""Journeys through Autosave Mode, Manual Save Mode, and unsaved close."""

from __future__ import annotations

import unittest

from harness import Workspace, executable_under_test


def record_ids(screen) -> list[str]:
    return [
        name.removeprefix("tabTitle:")
        for name in screen.labels
        if name.startswith("tabTitle:")
    ]


class SaveModesJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.screen_when(lambda s: len(s.squares) == 64)

    def test_autosave_mode_has_no_dirty_state_or_close_question_and_survives_restart(self) -> None:
        self.workspace.play("e2e4")
        screen = self.workspace.screen_when(lambda s: s.moves() == ["1. e4"])
        record_id = record_ids(screen)[0]
        self.assertNotIn("dirtyState", screen.labels)

        self.workspace.click(f"closeTab:{record_id}")
        self.workspace.screen_when(lambda s: record_id not in record_ids(s))
        self.assertNotIn("unsavedCloseTitle", self.workspace.screen().labels)

        self.workspace.restart()
        self.workspace.click(f"library:{record_id}")
        screen = self.workspace.screen_when(lambda s: s.moves() == ["1. e4"])
        self.assertEqual(screen.pieces().get("e4"), "white_pawn")

        self.workspace.close_window()
        self.workspace.wait_until_closed()

    def test_manual_save_mode_can_cancel_or_discard_game_record_close(self) -> None:
        self.workspace.click("manualSaveMode")
        self.workspace.play("e2e4")
        screen = self.workspace.screen_when(lambda s: "dirtyState" in s.labels)
        record_id = record_ids(screen)[0]

        self.workspace.click(f"closeTab:{record_id}")
        self.workspace.screen_when(lambda s: "unsavedCloseTitle" in s.labels)
        self.workspace.click("cancelUnsavedClose")
        self.assertIn(record_id, record_ids(self.workspace.screen()))

        self.workspace.click(f"closeTab:{record_id}")
        self.workspace.click("discardUnsavedClose")
        self.workspace.screen_when(lambda s: record_id not in record_ids(s))
        self.workspace.click(f"library:{record_id}")
        screen = self.workspace.screen_when(lambda s: record_id in record_ids(s))
        self.assertEqual(screen.moves(), [])
        self.assertEqual(screen.pieces().get("e2"), "white_pawn")

        self.workspace.play("e2e4")
        self.workspace.click(f"closeTab:{record_id}")
        self.workspace.click("saveUnsavedClose")
        self.workspace.screen_when(lambda s: record_id not in record_ids(s))
        self.workspace.click(f"library:{record_id}")
        self.workspace.screen_when(lambda s: s.moves() == ["1. e4"])

    def test_manual_save_mode_explicit_save_survives_restart_and_workspace_close_is_guarded(self) -> None:
        self.workspace.click("manualSaveMode")
        self.workspace.play("d2d4")
        self.workspace.screen_when(lambda s: "dirtyState" in s.labels)
        self.workspace.click("saveRecord")
        self.workspace.screen_when(lambda s: "dirtyState" not in s.labels)

        self.workspace.play("d7d5")
        self.workspace.close_window()
        self.workspace.screen_when(lambda s: "unsavedCloseTitle" in s.labels)
        self.workspace.click("cancelUnsavedClose")
        self.assertNotIn("unsavedCloseTitle", self.workspace.screen().labels)

        self.workspace.restart()
        self.workspace.screen_when(lambda s: "restoreButton" in s.labels)
        self.workspace.click("restoreButton")
        screen = self.workspace.screen_when(lambda s: s.moves() == ["1. d4"])
        self.assertEqual(screen.pieces().get("d4"), "white_pawn")
        self.assertEqual(screen.pieces().get("d7"), "black_pawn")


if __name__ == "__main__":
    unittest.main()
