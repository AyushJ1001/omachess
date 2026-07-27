from __future__ import annotations

import os
import stat
import tempfile
import textwrap
import unittest
from pathlib import Path

from harness import Workspace, executable_under_test


def fake_engine(path: Path, behavior: str, execution_log: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "#!/usr/bin/env python3\n"
        + textwrap.dedent(
            f"""
            import pathlib, sys, time
            pathlib.Path({str(execution_log)!r}).write_text("executed")
            behavior = {behavior!r}
            for raw in sys.stdin:
                command = raw.strip()
                if command == "uci":
                    if behavior == "startup-timeout":
                        time.sleep(5)
                    elif behavior == "malformed":
                        print("this is not uci", flush=True)
                        print("uciok", flush=True)
                    else:
                        name = "Definitely Not Stockfish" if behavior == "identity-mismatch" else "Stockfish 18"
                        print("id name " + name, flush=True)
                        print("id author The Stockfish developers", flush=True)
                        print("option name Threads type spin default 1 min 1 max 1024", flush=True)
                        print("option name Style type combo default Normal var Solid var Normal var Risky", flush=True)
                        if behavior == "registration":
                            print("registration error", flush=True)
                        if behavior != "missing-uciok":
                            print("uciok", flush=True)
                elif command == "isready":
                    if behavior != "readiness-timeout":
                        print("readyok", flush=True)
                elif command.startswith("go "):
                    if behavior == "search-timeout":
                        time.sleep(5)
                    else:
                        move = "garbage" if behavior == "malformed" else "e2e4"
                        print("bestmove " + move, flush=True)
                elif command == "quit":
                    if behavior == "shutdown-timeout":
                        time.sleep(5)
                    break
            """
        ),
        encoding="utf-8",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


class EngineJourney(unittest.TestCase):
    def setUp(self) -> None:
        self.root = tempfile.TemporaryDirectory(prefix="omachess-engine-")
        self.data_home = Path(self.root.name)
        self.store = self.data_home / "xdg_data_home" / "omachess" / "engines"
        self.log = self.data_home / "engine-executed"
        self.environment = {
            "OMACHESS_TEST_ENGINE_DEADLINE_MS": "150",
        }

    def tearDown(self) -> None:
        self.root.cleanup()

    def run_workspace(self, behavior: str = "ready") -> Workspace:
        fake_engine(self.store / "stockfish" / "stockfish", behavior, self.log)
        return Workspace(
            executable_under_test(),
            data_home=self.data_home,
            environment=self.environment,
        )

    def test_discovery_requires_consent_and_does_not_execute_the_engine(self) -> None:
        with self.run_workspace() as workspace:
            screen = workspace.screen_when(
                lambda value: value.labels.get("engineState:stockfish") == "Consent required"
            )
            self.assertEqual(screen.labels["engineName:stockfish"], "Stockfish")
            self.assertEqual(screen.labels["engineRating:stockfish"], "≈ 3600 Elo estimate")
            self.assertFalse(self.log.exists())

    def test_consent_runs_a_complete_probe_and_captures_identity_and_options(self) -> None:
        with self.run_workspace() as workspace:
            workspace.click("engineConsent:stockfish")
            screen = workspace.screen_when(
                lambda value: value.labels.get("engineState:stockfish") == "Ready"
            )
            self.assertTrue(self.log.exists())
            self.assertEqual(screen.labels["engineIdentity:stockfish"], "Stockfish 18")
            self.assertEqual(screen.labels["engineOptions:stockfish"], "2 UCI options")

    def test_identity_mismatch_downgrades_the_known_profile(self) -> None:
        with self.run_workspace("identity-mismatch") as workspace:
            workspace.click("engineConsent:stockfish")
            screen = workspace.screen_when(
                lambda value: value.labels.get("engineState:stockfish")
                == "Ready — identity mismatch"
            )
            self.assertEqual(screen.labels["engineIdentity:stockfish"], "Definitely Not Stockfish")

    def test_registration_is_recognized_but_unsupported(self) -> None:
        with self.run_workspace("registration") as workspace:
            workspace.click("engineConsent:stockfish")
            screen = workspace.screen_when(
                lambda value: "registration" in value.labels.get("engineState:stockfish", "")
            )
            self.assertEqual(
                screen.labels["engineState:stockfish"],
                "Recognized — unsupported registration required",
            )

    def test_probe_failures_never_claim_readiness(self) -> None:
        for behavior in (
            "startup-timeout",
            "readiness-timeout",
            "search-timeout",
            "shutdown-timeout",
            "missing-uciok",
            "malformed",
        ):
            with self.subTest(behavior=behavior):
                if self.log.exists():
                    self.log.unlink()
                with self.run_workspace(behavior) as workspace:
                    workspace.click("engineConsent:stockfish")
                    screen = workspace.screen_when(
                        lambda value: value.labels.get("engineState:stockfish", "").startswith(
                            "Probe failed"
                        )
                    )
                    self.assertNotIn("Ready", screen.labels["engineState:stockfish"])


if __name__ == "__main__":
    unittest.main()
