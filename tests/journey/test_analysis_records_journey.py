"""Journeys through durable Analysis Records and pinned engine evidence."""

from __future__ import annotations

import stat
import tempfile
import textwrap
import time
import unittest
from pathlib import Path

from harness import Workspace, executable_under_test
from test_engine_journey import fake_engine


CHECKMATE = "f2f3 e7e5 g2g4 d8h4"


def budget_engine(path: Path) -> None:
    """A capability-limited engine whose protocol makes compilation observable."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "#!/usr/bin/env python3\n"
        + textwrap.dedent(
            f"""
            import sys
            for raw in sys.stdin:
                command = raw.strip()
                if command == "uci":
                    print("id name Stockfish 18", flush=True)
                    print("id author Omachess tests", flush=True)
                    print("option name Threads type spin default 1 min 1 max 2", flush=True)
                    print("option name Hash type spin default 16 min 16 max 64", flush=True)
                    print("option name MultiPV type spin default 1 min 1 max 2", flush=True)
                    print("option name Backend type combo default CPU var CPU var CUDA", flush=True)
                    print("uciok", flush=True)
                elif command == "isready":
                    print("readyok", flush=True)
                elif command.startswith("go "):
                    print("info depth 8 multipv 1 score cp 22 pv e2e4 e7e5", flush=True)
                    print("info depth 8 multipv 2 score cp 18 pv d2d4 d7d5", flush=True)
                    print("info depth 8 multipv 3 score cp 12 pv c2c4 c7c5", flush=True)
                    print("bestmove e2e4", flush=True)
                elif command == "quit":
                    break
            """
        ),
        encoding="utf-8",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def library_ids(screen) -> set[str]:
    return {
        name.split(":", 1)[1]
        for name in screen.labels
        if name.startswith(("libraryTitle:", "library:", "tabTitle:"))
    }


class AnalysisRecordsJourney(unittest.TestCase):
    def test_analysis_budget_compiles_capabilities_and_corrects_its_estimate(self) -> None:
        root = tempfile.TemporaryDirectory(prefix="omachess-analysis-budget-")
        self.addCleanup(root.cleanup)
        data_home = Path(root.name)
        engine = data_home / "xdg_data_home" / "omachess" / "engines" / "stockfish" / "stockfish"
        budget_engine(engine)
        workspace = Workspace(
            executable_under_test(),
            data_home=data_home,
            environment={"OMACHESS_TEST_ENGINE_DEADLINE_MS": "250"},
        )
        workspace.start()
        self.addCleanup(workspace.stop)
        workspace.click("engineProfilesButton")
        workspace.click("engineConsent:stockfish")
        workspace.screen_when(
            lambda screen: screen.labels.get("engineState:stockfish") == "Ready"
        )
        workspace.play_all(CHECKMATE)
        completed = workspace.screen_when(
            lambda screen: "computerAnalysisButton" in screen.labels
        )
        self.assertIn("Quick", completed.labels["analysisBudget:quick"])
        self.assertIn("Standard", completed.labels["analysisBudget:standard"])
        self.assertIn("Deep", completed.labels["analysisBudget:deep"])
        self.assertIn("1 s", completed.labels["analysisBudget:standard"])
        self.assertIn("two lines", completed.labels["analysisBudget:standard"])
        self.assertIn("Moderate", completed.labels["analysisBudget:standard"])

        workspace.click("analysisBudget:deep")
        selected = workspace.screen_when(
            lambda screen: screen.labels.get("analysisBudgetSelection", "").startswith("Deep")
        )
        initial_estimate = selected.labels["computerAnalysisEstimate"]
        started = time.monotonic()
        workspace.click("computerAnalysisButton")
        finished = workspace.screen_when(
            lambda screen: screen.labels.get("computerAnalysisState") == "Complete"
        )
        self.assertLess(time.monotonic() - started, 4.0)
        self.assertIn("Corrected", finished.labels["computerAnalysisEstimate"])
        self.assertNotEqual(initial_estimate, finished.labels["computerAnalysisEstimate"])
        disclosure = finished.labels["computerAnalysisDisclosure"]
        self.assertIn("5 s", disclosure)
        self.assertIn("Engine limit: go movetime 5000 ms", disclosure)
        self.assertIn("3 requested", disclosure)
        self.assertIn("2 effective", disclosure)
        self.assertIn("capped", disclosure)
        self.assertIn("Backend preserved", disclosure)


    def test_computer_analysis_survives_restart_with_every_position_reviewed(self) -> None:
        root = tempfile.TemporaryDirectory(prefix="omachess-computer-analysis-")
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
        completed = workspace.screen_when(
            lambda screen: "(" in screen.status()
            and bool(library_ids(screen))
            and "computerAnalysisButton" in screen.labels
        )
        source_id = next(iter(library_ids(completed)))

        workspace.click("computerAnalysisButton")
        workspace.screen_when(
            lambda screen: screen.labels.get("computerAnalysisStatus") == "5 / 5 positions"
        )
        finished = workspace.screen_when(
            lambda screen: screen.labels.get("computerAnalysisState") == "Complete"
        )
        analysis_id = next(iter(library_ids(finished) - {source_id}))
        self.assertEqual(finished.labels["computerEvaluationCount"], "5 positions")
        self.assertIn("computerEvaluation:1", finished.labels)
        self.assertIn("computerEvaluation:5", finished.labels)
        self.assertIn("computerGlyph:1", finished.labels)
        self.assertIn("computerSideline:1", finished.labels)
        self.assertEqual(finished.labels["defaultAnalysis"], "Default Analysis")

        workspace.restart()
        restored = workspace.screen_when(
            lambda screen: screen.labels.get("computerEvaluationCount") == "5 positions"
        )
        self.assertEqual(restored.labels["computerAnalysisState"], "Complete")
        self.assertEqual(restored.labels["computerEvaluation:1"], "After ply 0 · +0.22")
        self.assertEqual(restored.labels["computerGlyph:1"], "?")
        self.assertEqual(restored.labels["computerSideline:1"], "e2e4 e7e5")
        self.assertIn("computerEvaluation:5", restored.labels)
        self.assertEqual(restored.labels["defaultAnalysis"], "Default Analysis")

    def test_cancelling_computer_analysis_does_not_create_an_analysis_record(self) -> None:
        root = tempfile.TemporaryDirectory(prefix="omachess-cancel-analysis-")
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
        before = workspace.screen_when(
            lambda screen: "(" in screen.status()
            and "computerAnalysisButton" in screen.labels
        )
        ids = library_ids(before)

        workspace.click("computerAnalysisButton")
        workspace.click("cancelComputerAnalysisButton")
        cancelled = workspace.screen_when(
            lambda screen: screen.labels.get("computerAnalysisState") == "Cancelled"
        )
        self.assertEqual(library_ids(cancelled), ids)

    def test_derive_diverge_and_derive_again_keeps_every_record_independent(self) -> None:
        with Workspace(executable_under_test()) as workspace:
            workspace.play_all(CHECKMATE)
            completed = workspace.screen_when(
                lambda screen: "(" in screen.status() and bool(library_ids(screen))
            )
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
