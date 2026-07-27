"""Journeys through reversible archive and irreversible Permanent Purge."""

from __future__ import annotations

import unittest

from harness import Workspace, executable_under_test
from test_analysis_records_journey import CHECKMATE, library_ids


def visible_library_ids(screen) -> set[str]:
    return {
        name.split(":", 1)[1]
        for name in screen.labels
        if name.startswith("libraryTitle:")
    }


class ArchivePurgeJourney(unittest.TestCase):
    def test_archive_is_reversible_and_purge_keeps_derived_snapshot_readable(self) -> None:
        with Workspace(executable_under_test()) as workspace:
            workspace.play_all(CHECKMATE)
            completed = workspace.screen_when(
                lambda screen: "(" in screen.status() and bool(library_ids(screen))
            )
            source_id = next(iter(visible_library_ids(completed)))

            workspace.click("deriveAnalysisButton")
            derived = workspace.screen_when(
                lambda screen: len(visible_library_ids(screen)) == 2
                and "sourceSnapshotMoves" in screen.labels
            )
            analysis_id = next(iter(visible_library_ids(derived) - {source_id}))

            workspace.click(f"recordGraphSource:{source_id}")
            workspace.click(f"archiveRecord:{source_id}")
            archived = workspace.screen_when(
                lambda screen: f"library:{source_id}" not in screen.labels
            )
            self.assertNotIn(f"libraryTitle:{source_id}", archived.labels)
            self.assertIn(f"libraryTitle:{analysis_id}", archived.labels)

            workspace.restart()
            archived_after_restart = workspace.screen_when(
                lambda screen: f"libraryTitle:{source_id}" not in screen.labels
            )
            self.assertIn(f"libraryTitle:{analysis_id}", archived_after_restart.labels)

            workspace.click("toggleArchivedView")
            archived_view = workspace.screen_when(
                lambda screen: f"unarchiveRecord:{source_id}" in screen.labels
            )
            workspace.click(f"unarchiveRecord:{source_id}")
            restored = workspace.screen_when(
                lambda screen: f"libraryTitle:{source_id}" in screen.labels
            )
            self.assertIn(f"libraryTitle:{source_id}", restored.labels)

            workspace.click(f"purgeRecord:{source_id}")
            confirmation = workspace.screen_when(
                lambda screen: "permanentPurgeWarning" in screen.labels
            )
            self.assertIn("irreversible", confirmation.labels["permanentPurgeWarning"])
            self.assertIn("no in-app undelete", confirmation.labels["permanentPurgeWarning"])
            workspace.click("confirmPermanentPurge")
            after_source_purge = workspace.screen_when(
                lambda screen: f"libraryTitle:{source_id}" not in screen.labels
                and screen.labels.get("sourceSnapshotMoves") == "4 moves"
            )
            self.assertIn(f"libraryTitle:{analysis_id}", after_source_purge.labels)
            self.assertEqual(after_source_purge.labels["sourceSnapshotMoves"], "4 moves")

            workspace.click(f"purgeRecord:{analysis_id}")
            workspace.click("confirmPermanentPurge")
            gone = workspace.screen_when(
                lambda screen: f"libraryTitle:{analysis_id}" not in screen.labels
            )
            self.assertNotIn(f"libraryTitle:{analysis_id}", gone.labels)


if __name__ == "__main__":
    unittest.main()
