"""Journeys through durable Analysis Records and pinned engine evidence."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from harness import Workspace, executable_under_test
from test_engine_journey import fake_engine


CHECKMATE = "f2f3 e7e5 g2g4 d8h4"


def library_ids(screen) -> set[str]:
    return {
        name.removeprefix("libraryTitle:")
        for name in screen.labels
        if name.startswith("libraryTitle:")
    }


class AnalysisRecordsJourney(unittest.TestCase):
    def test_derive_diverge_and_derive_again_keeps_every_record_independent(self) -> None:
        with Workspace(executable_under_test()) as workspace:
            workspace.play_all(CHECKMATE)
            completed = workspace.screen_when(lambda screen: "(" in screen.status())
            source_id = next(iter(library_ids(completed)))

            workspace.click("deriveAnalysisButton")
            first = workspace.screen_when(
                lambda screen: len(library_ids(screen)) == 2
                and screen.labels.get("sourceSnapshotMoves") == "4 moves"
            )
            first_id = next(iter(library_ids(first) - {source_id}))
            self.assertIn(f"recordGraphSource:{source_id}", first.labels)
            workspace.enter_text("metadata:title", "First exploration")
            workspace.click("saveMetadataButton")
            workspace.click("startButton")
            workspace.enter_text("analysisSidelineInput", "d2d4")
            workspace.click("addAnalysisSidelineButton")
            workspace.screen_when(
                lambda screen: screen.labels.get("analysisSideline:1")
                == "After ply 0 · d4"
            )
            workspace.click("endButton")
            workspace.enter_text("analysisAnnotationInput", "First branch")
            workspace.click("addAnalysisAnnotationButton")
            workspace.screen_when(
                lambda screen: screen.labels.get("analysisAnnotation:1")
                == "After ply 4 · First branch"
            )

            workspace.click(f"recordGraphSource:{source_id}")
            workspace.enter_text("metadata:title", "Corrected source")
            workspace.click("saveMetadataButton")
            workspace.click("deriveAnalysisButton")
            second = workspace.screen_when(lambda screen: len(library_ids(screen)) == 3)
            second_id = next(iter(library_ids(second) - {source_id, first_id}))
            self.assertNotEqual(first_id, second_id)
            self.assertEqual(second.labels["sourceSnapshotMoves"], "4 moves")
            workspace.enter_text("analysisAnnotationInput", "Second branch")
            workspace.click("addAnalysisAnnotationButton")
            workspace.screen_when(
                lambda screen: screen.labels.get("analysisAnnotation:1")
                == "After ply 4 · Second branch"
            )

            workspace.click(f"recordGraphSource:{source_id}")
            source = workspace.screen_when(
                lambda screen: f"recordGraphDerivation:{first_id}" in screen.labels
                and f"recordGraphDerivation:{second_id}" in screen.labels
            )
            self.assertEqual(source.labels["metadata:title"], "Corrected source")
            workspace.click(f"recordGraphDerivation:{first_id}")
            reopened = workspace.screen_when(
                lambda screen: screen.labels.get("metadata:title") == "First exploration"
            )
            self.assertIn(f"recordGraphSource:{source_id}", reopened.labels)
            self.assertEqual(reopened.labels["sourceSnapshotMoves"], "4 moves")
            self.assertEqual(
                reopened.labels["analysisAnnotation:1"], "After ply 4 · First branch"
            )
            self.assertEqual(reopened.labels["analysisSideline:1"], "After ply 0 · d4")

    def test_pinned_line_and_engine_context_survive_restart(self) -> None:
        root = tempfile.TemporaryDirectory(prefix="omachess-analysis-record-")
        self.addCleanup(root.cleanup)
        data_home = Path(root.name)
        engine = data_home / "xdg_data_home" / "omachess" / "engines" / "stockfish" / "stockfish"
        fake_engine(engine, "ready", data_home / "engine-executed")
        workspace = Workspace(
            executable_under_test(),
            data_home=data_home,
            environment={"OMACHESS_TEST_ENGINE_DEADLINE_MS": "150"},
        )
        workspace.start()
        self.addCleanup(workspace.stop)
        workspace.click("engineProfilesButton")
        workspace.click("engineConsent:stockfish")
        workspace.screen_when(
            lambda screen: screen.labels.get("engineState:stockfish") == "Ready"
        )
        workspace.play_all(CHECKMATE)
        workspace.screen_when(lambda screen: "(" in screen.status())
        workspace.click("deriveAnalysisButton")
        workspace.screen_when(lambda screen: "sourceSnapshotMoves" in screen.labels)
        live = workspace.screen_when(
            lambda screen: "analysisEvaluation" in screen.labels
            and "analysisLine:1" in screen.labels
        )
        evaluation = live.labels["analysisEvaluation"]
        variation = live.labels["analysisLine:1"].removeprefix("1. ")
        workspace.click("pinEngineLineButton")
        pinned = workspace.screen_when(lambda screen: "pinnedEngineEvaluation:1" in screen.labels)
        self.assertEqual(pinned.labels["pinnedEngineEvaluation:1"], evaluation)
        self.assertEqual(pinned.labels["pinnedEngineVariation:1"], variation)
        self.assertEqual(pinned.labels["pinnedEngineName:1"], "Stockfish 18")
        self.assertEqual(
            pinned.labels["pinnedEngineSearch:1"], "depth 8 · movetime 250 ms"
        )

        workspace.restart()
        restored = workspace.screen_when(
            lambda screen: "pinnedEngineEvaluation:1" in screen.labels
        )
        self.assertEqual(restored.labels["pinnedEngineEvaluation:1"], evaluation)
        self.assertEqual(restored.labels["pinnedEngineVariation:1"], variation)
        self.assertEqual(restored.labels["pinnedEngineName:1"], "Stockfish 18")


if __name__ == "__main__":
    unittest.main()
