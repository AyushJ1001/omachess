"""Journeys through durable Analysis Records and pinned engine evidence."""

from __future__ import annotations

import os
import signal
import subprocess
import tempfile
import unittest
from pathlib import Path

from harness import Workspace, executable_under_test
from test_engine_journey import fake_engine


CHECKMATE = "f2f3 e7e5 g2g4 d8h4"


def library_ids(screen) -> set[str]:
    return {
        name.split(":", 1)[1]
        for name in screen.labels
        if name.startswith(("libraryTitle:", "library:", "tabTitle:"))
    }


def background_worker_pid() -> int:
    try:
        output = subprocess.check_output(
            ["busctl", "--user", "--no-pager", "status", "com.omachess.Omachess.BackgroundWorker"],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        raise unittest.SkipTest("busctl could not inspect the D-Bus background worker") from error
    for line in output.splitlines():
        if line.startswith("PID="):
            return int(line.split("=", 1)[1])
    raise unittest.SkipTest("D-Bus background worker PID was not reported")


class AnalysisRecordsJourney(unittest.TestCase):
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

    def test_closing_a_running_analysis_requires_an_explicit_choice(self) -> None:
        root = tempfile.TemporaryDirectory(prefix="omachess-background-consent-")
        self.addCleanup(root.cleanup)
        data_home = Path(root.name)
        engine = data_home / "xdg_data_home" / "omachess" / "engines" / "stockfish" / "stockfish"
        fake_engine(engine, "ready", data_home / "engine-executed")
        with Workspace(executable_under_test(), data_home=data_home,
                       environment={"OMACHESS_TEST_ENGINE_DEADLINE_MS": "150"}) as workspace:
            workspace.click("engineProfilesButton")
            workspace.click("engineConsent:stockfish")
            workspace.screen_when(lambda screen: screen.labels.get("engineState:stockfish") == "Ready")
            workspace.play_all(CHECKMATE)
            workspace.screen_when(lambda screen: "computerAnalysisButton" in screen.labels)
            workspace.click("computerAnalysisButton")
            workspace.close_window()
            prompt = workspace.screen_when(lambda screen: "backgroundConsentContinue" in screen.labels)
            self.assertIn("backgroundConsentStop", prompt.labels)
            workspace.click("backgroundConsentStop")
            workspace.wait_until_closed()

    def test_consented_close_continues_background_analysis_and_imports_on_next_launch(self) -> None:
        root = tempfile.TemporaryDirectory(prefix="omachess-background-continue-")
        self.addCleanup(root.cleanup)
        data_home = Path(root.name)
        engine = data_home / "xdg_data_home" / "omachess" / "engines" / "stockfish" / "stockfish"
        fake_engine(engine, "slow-analysis", data_home / "engine-executed")
        environment = {"OMACHESS_TEST_ENGINE_DEADLINE_MS": "2000"}
        workspace = Workspace(executable_under_test(), data_home=data_home, environment=environment)
        workspace.start()
        self.addCleanup(workspace.stop)
        workspace.click("engineProfilesButton")
        workspace.click("engineConsent:stockfish")
        workspace.screen_when(lambda screen: screen.labels.get("engineState:stockfish") == "Ready")
        workspace.play_all(CHECKMATE)
        workspace.screen_when(lambda screen: "computerAnalysisButton" in screen.labels)
        workspace.click("computerAnalysisButton")
        workspace.close_window()
        workspace.screen_when(lambda screen: "backgroundConsentContinue" in screen.labels)
        workspace.click("backgroundConsentContinue")
        workspace.wait_until_closed()
        workspace.stop(cleanup=False)

        relaunched = Workspace(executable_under_test(), data_home=data_home, environment=environment)
        relaunched.start()
        self.addCleanup(relaunched.stop)
        imported = relaunched.screen_when(
            lambda screen: screen.labels.get("computerEvaluationCount") == "5 positions",
            timeout=20.0,
        )
        self.assertEqual(imported.labels["computerAnalysisState"], "Complete")
        self.assertEqual(imported.labels["defaultAnalysis"], "Default Analysis")

    def test_worker_crash_recovers_as_interrupted_and_resume_imports_from_checkpoint(self) -> None:
        root = tempfile.TemporaryDirectory(prefix="omachess-background-interrupted-")
        self.addCleanup(root.cleanup)
        data_home = Path(root.name)
        engine = data_home / "xdg_data_home" / "omachess" / "engines" / "stockfish" / "stockfish"
        fake_engine(engine, "slow-analysis", data_home / "engine-executed")
        workspace = Workspace(
            executable_under_test(),
            data_home=data_home,
            environment={"OMACHESS_TEST_ENGINE_DEADLINE_MS": "2000"},
        )
        workspace.start()
        self.addCleanup(workspace.stop)
        workspace.click("engineProfilesButton")
        workspace.click("engineConsent:stockfish")
        workspace.screen_when(lambda screen: screen.labels.get("engineState:stockfish") == "Ready")
        workspace.play_all(CHECKMATE)
        workspace.screen_when(lambda screen: "computerAnalysisButton" in screen.labels)
        workspace.click("computerAnalysisButton")
        workspace.screen_when(
            lambda screen: screen.labels.get("computerAnalysisStatus") == "1 / 5 positions",
            timeout=20.0,
        )

        os.kill(background_worker_pid(), signal.SIGKILL)
        interrupted = workspace.screen_when(
            lambda screen: screen.labels.get("computerAnalysisState") == "Interrupted"
            and "resumeComputerAnalysisButton" in screen.labels,
            timeout=20.0,
        )
        self.assertIn("dismissComputerAnalysisButton", interrupted.labels)

        workspace.click("resumeComputerAnalysisButton")
        finished = workspace.screen_when(
            lambda screen: screen.labels.get("computerEvaluationCount") == "5 positions",
            timeout=20.0,
        )
        self.assertEqual(finished.labels["computerAnalysisState"], "Complete")

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
