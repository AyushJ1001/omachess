from __future__ import annotations

import stat
import tempfile
import textwrap
import unittest
from pathlib import Path

from harness import Workspace, executable_under_test


def live_play_engine(path: Path, replies: list[str], *, fail_during_play: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "#!/usr/bin/env python3\n"
        + textwrap.dedent(
            f"""
            import sys
            replies = iter({replies!r})
            position = ""
            for raw in sys.stdin:
                command = raw.strip()
                if command == "uci":
                    print("id name Stockfish Journey", flush=True)
                    print("option name Threads type spin default 1 min 1 max 8", flush=True)
                    print("uciok", flush=True)
                elif command == "isready":
                    print("readyok", flush=True)
                elif command.startswith("position "):
                    position = command
                elif command.startswith("go "):
                    if position == "position startpos":
                        print("bestmove e2e4", flush=True)  # readiness probe
                    elif not {fail_during_play!r}:
                        print("bestmove " + next(replies), flush=True)
                elif command == "quit":
                    break
            """
        ),
        encoding="utf-8",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


class HumanEnginePlayJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.root = tempfile.TemporaryDirectory(prefix="omachess-live-play-")
        self.data_home = Path(self.root.name)
        self.engine = (
            self.data_home
            / "xdg_data_home"
            / "omachess"
            / "engines"
            / "stockfish"
            / "stockfish"
        )
        self.environment = {"OMACHESS_TEST_ENGINE_DEADLINE_MS": "150"}

    def tearDown(self) -> None:
        self.root.cleanup()

    def ready_workspace(self, replies: list[str], *, fail_during_play: bool = False) -> Workspace:
        live_play_engine(self.engine, replies, fail_during_play=fail_during_play)
        workspace = Workspace(
            executable_under_test(),
            data_home=self.data_home,
            environment=self.environment,
        )
        return workspace

    def consent(self, workspace: Workspace) -> None:
        workspace.click("engineProfilesButton")
        workspace.click("engineConsent:stockfish")
        workspace.screen_when(
            lambda screen: screen.labels.get("engineState:stockfish") == "Ready"
        )

    def test_full_game_against_ready_engine_persists_as_completed_game(self) -> None:
        with self.ready_workspace(["e7e5", "d8h4"]) as workspace:
            self.consent(workspace)
            workspace.click("livePlaySettings:stockfish")
            workspace.select("livePlaySearchTime:stockfish", 1)
            workspace.click("playWhite:stockfish")
            focused = workspace.screen().active_focus

            self.assertEqual(
                workspace.screen().labels["livePlaySetting:stockfish"],
                "Live play · 100 ms per move",
            )
            workspace.play("f2f3")
            workspace.screen_when(lambda screen: screen.square("e5").piece == "black_pawn")
            self.assertEqual(workspace.screen().active_focus, focused)
            workspace.play("g2g4")
            screen = workspace.screen_when(lambda value: "(" in value.status())

            self.assertEqual(screen.status(), "Black wins by checkmate (0-1)")
            self.assertTrue(any("0-1" in label for label in screen.labels.values()))

    def test_non_response_reports_failure_without_corrupting_the_record(self) -> None:
        with self.ready_workspace([], fail_during_play=True) as workspace:
            self.consent(workspace)
            workspace.click("playWhite:stockfish")
            workspace.play("f2f3")
            failed = workspace.screen_when(
                lambda screen: "did not respond" in screen.labels.get("livePlayStatus", "")
            )
            self.assertEqual(failed.moves(), ["1. f3"])

            workspace.restart()
            restored = workspace.screen_when(
                lambda screen: "Restore" in screen.labels.get("restoreLabel", "")
            )
            self.assertIn("1 move", restored.labels["restoreLabel"])

    def test_illegal_engine_move_is_reported_and_not_applied(self) -> None:
        with self.ready_workspace(["a1a1"]) as workspace:
            self.consent(workspace)
            workspace.click("playWhite:stockfish")
            workspace.play("f2f3")
            failed = workspace.screen_when(
                lambda screen: "illegal or malformed" in screen.labels.get(
                    "livePlayStatus", ""
                )
            )
            self.assertEqual(failed.moves(), ["1. f3"])


if __name__ == "__main__":
    unittest.main()
