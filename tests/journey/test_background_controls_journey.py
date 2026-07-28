from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from harness import Workspace, executable_under_test


PGN = """[Event \"Background controls target\"]
[Result \"*\"]

1. e4 e5 2. Nf3 *
"""


@unittest.skipUnless(executable_under_test(), "OMACHESS_BINARY is not set")
class BackgroundControlsJourney(unittest.TestCase):
    def test_deep_link_opens_a_standalone_workspace_on_the_job_record(self) -> None:
        with tempfile.TemporaryDirectory(prefix="omachess-background-controls-") as directory:
            root = Path(directory)
            pgn = root / "target.pgn"
            pgn.write_text(PGN, encoding="utf-8")

            with Workspace(executable_under_test(), import_pgn=pgn) as source:
                source.click("importPgnButton")
                imported = source.screen_when(
                    lambda screen: any(
                        text == "Background controls target"
                        for name, text in screen.labels.items()
                        if name.startswith("libraryTitle:")
                    )
                )
                record_id = next(
                    name.removeprefix("libraryTitle:")
                    for name, text in imported.labels.items()
                    if name.startswith("libraryTitle:")
                    and text == "Background controls target"
                )
                data_root = source.workspace_root
                source.stop(cleanup=False)
                with Workspace(
                    executable_under_test(),
                    data_home=data_root,
                    launch_arguments=("--record", record_id),
                ) as linked:
                    screen = linked.screen_when(lambda current: current.active_record_id == record_id)
                    self.assertEqual(screen.active_record_id, record_id)
                    self.assertTrue(any(name.endswith(record_id) for name in screen.labels))
