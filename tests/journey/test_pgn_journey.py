from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from harness import Workspace, executable_under_test


MIXED_PGN = """[Event "Imported game"]
[White "Ada"]
[Black "Grace"]
[Result "*"]

1. e4 e5 2. Nf3 *

[Event "Malformed game"]
[Result "*"]

1. e4 NotAMove *
"""


@unittest.skipUnless(executable_under_test(), "OMACHESS_BINARY is not set")
class PgnJourney(unittest.TestCase):
    def test_portal_import_reports_partial_results_and_keeps_the_valid_record(self) -> None:
        with tempfile.TemporaryDirectory(prefix="omachess-pgn-") as directory:
            pgn_path = Path(directory) / "mixed.pgn"
            pgn_path.write_text(MIXED_PGN, encoding="utf-8")
            with Workspace(executable_under_test(), import_pgn=pgn_path) as workspace:
                workspace.click("importPgnButton")
                screen = workspace.screen_when(
                    lambda current: current.labels.get("pgnImportSummary") == "1 imported · 1 failed"
                )
                self.assertIn("Imported game", screen.labels["pgnImportEntry:1"])
                self.assertIn("NotAMove", screen.labels["pgnImportEntry:2"])
                self.assertTrue(
                    any(text == "Imported game" for name, text in screen.labels.items()
                        if name.startswith("libraryTitle:"))
                )

    def test_a_selected_game_record_exports_as_standard_pgn(self) -> None:
        with tempfile.TemporaryDirectory(prefix="omachess-pgn-") as directory:
            root = Path(directory)
            source = root / "source.pgn"
            destination = root / "selected.pgn"
            source.write_text(MIXED_PGN, encoding="utf-8")
            with Workspace(
                executable_under_test(), import_pgn=source, export_pgn=destination
            ) as workspace:
                workspace.click("importPgnButton")
                screen = workspace.screen_when(
                    lambda current: current.labels.get("pgnImportSummary") == "1 imported · 1 failed"
                )
                workspace.press_key("Escape")
                record_id = next(
                    name.removeprefix("libraryTitle:")
                    for name, text in screen.labels.items()
                    if name.startswith("libraryTitle:") and text == "Imported game"
                )
                workspace.click(f"selectRecord:{record_id}")
                workspace.click("exportPgnButton")
                exported = destination.read_text(encoding="utf-8")
                self.assertIn('[Event "Imported game"]', exported)
                self.assertIn('[White "Ada"]', exported)
                self.assertIn("1. e4 e5 2. Nf3 *", exported)
