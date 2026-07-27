"""Journeys through the Library Portability Package.

A player's whole library has to be able to move: out to a file they keep, and
back into an Omachess that holds nothing yet. It never merges into a library
that already holds work, and a package this build cannot read changes nothing.
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from harness import Workspace, executable_under_test
from test_analysis_records_journey import CHECKMATE, library_ids


def visible_library_ids(screen) -> set[str]:
    return {
        name.split(":", 1)[1]
        for name in screen.labels
        if name.startswith("libraryTitle:")
    }


def study_titles(screen) -> set[str]:
    return {
        text
        for name, text in screen.labels.items()
        if name.startswith("studyTitle:")
    }


def study_members(screen) -> list[str]:
    members = []
    for name in screen.labels:
        if name.startswith("studyMember:"):
            _, _, rest = name.partition("studyMember:")
            _, position, record_id = rest.split(":", 2)
            members.append((int(position), record_id))
    return [record_id for _, record_id in sorted(members)]


def exported_library(package: Path, *, directory: Path) -> dict:
    """Play a game, derive an analysis, make a Study, and export the library."""
    with Workspace(
        executable_under_test(),
        data_home=directory,
        environment={"OMACHESS_TEST_EXPORT_PACKAGE": str(package)},
    ) as workspace:
        workspace.play_all(CHECKMATE)
        completed = workspace.screen_when(
            lambda screen: "(" in screen.status() and bool(library_ids(screen))
        )
        source_id = next(iter(visible_library_ids(completed)))

        workspace.click("deriveAnalysisButton")
        derived = workspace.screen_when(
            lambda screen: len(visible_library_ids(screen)) == 2
        )
        analysis_id = next(iter(visible_library_ids(derived) - {source_id}))

        workspace.enter_text("newStudyName", "Travelling study")
        workspace.click("createStudyButton")
        created = workspace.screen_when(
            lambda screen: "Travelling study" in study_titles(screen)
            and any(name.startswith("addActiveToStudy:") for name in screen.labels)
        )
        study_id = next(
            name.removeprefix("addActiveToStudy:")
            for name in created.labels
            if name.startswith("addActiveToStudy:")
        )
        workspace.click(f"addActiveToStudy:{study_id}")
        workspace.screen_when(lambda screen: study_members(screen) == [analysis_id])

        # Save Mode is the portable preference subset, so it has to travel.
        workspace.click("manualSaveMode")
        workspace.screen_when(lambda screen: "manualSaveMode" in screen.labels)

        workspace.click("exportLibraryPackageButton")
        exported = workspace.screen_when(
            lambda screen: "libraryPackageMessage" in screen.labels
            and "Exported" in screen.labels["libraryPackageMessage"]
        )
        message = exported.labels["libraryPackageMessage"]

    return {
        "source_id": source_id,
        "analysis_id": analysis_id,
        "study_id": study_id,
        "members": [analysis_id],
        "message": message,
    }


class LibraryPackageJourney(unittest.TestCase):
    def test_a_library_moves_into_an_empty_omachess_and_comes_back_whole(self) -> None:
        with tempfile.TemporaryDirectory(prefix="omachess-package-") as scratch:
            root = Path(scratch)
            package = root / "library.omalib"
            library = exported_library(package, directory=root / "source")

            self.assertTrue(package.is_file())
            document = json.loads(package.read_text(encoding="utf-8"))
            self.assertEqual(document["format_version"], 1)
            for promised in (
                "Game Record",
                "Source Snapshot",
                "Record Graph",
                "Study",
                "Variant Definition",
                "portable preferences",
            ):
                self.assertIn(promised, document["description"])
            self.assertIn("format version 1", library["message"])

            with Workspace(
                executable_under_test(),
                data_home=root / "target",
                environment={"OMACHESS_TEST_RESTORE_PACKAGE": str(package)},
            ) as workspace:
                empty = workspace.screen_when(lambda screen: not visible_library_ids(screen))
                self.assertEqual(visible_library_ids(empty), set())

                workspace.click("restoreLibraryPackageButton")
                restored = workspace.screen_when(
                    lambda screen: visible_library_ids(screen)
                    == {library["source_id"], library["analysis_id"]}
                )
                self.assertIn("Restored", restored.labels["libraryPackageMessage"])
                self.assertIn("Travelling study", study_titles(restored))
                self.assertEqual(study_members(restored), library["members"])
                # Manual Save Mode travelled with the library: only that mode
                # offers an explicit save.
                self.assertIn("saveRecord", restored.labels)

                # The Record Graph relationship and the Source Snapshot came
                # with the records, and Manual Save Mode came with them.
                workspace.click(f"library:{library['analysis_id']}")
                analysis = workspace.screen_when(
                    lambda screen: f"recordGraphSource:{library['source_id']}" in screen.labels
                )
                self.assertEqual(analysis.labels["sourceSnapshotMoves"], "4 moves")

                workspace.restart()
                after_restart = workspace.screen_when(
                    lambda screen: visible_library_ids(screen)
                    == {library["source_id"], library["analysis_id"]}
                )
                self.assertIn("Travelling study", study_titles(after_restart))

    def test_restoring_into_a_library_that_holds_work_replaces_only_when_told_to(self) -> None:
        with tempfile.TemporaryDirectory(prefix="omachess-package-") as scratch:
            root = Path(scratch)
            package = root / "library.omalib"
            library = exported_library(package, directory=root / "source")

            with Workspace(
                executable_under_test(),
                data_home=root / "target",
                environment={"OMACHESS_TEST_RESTORE_PACKAGE": str(package)},
            ) as workspace:
                workspace.play_all("d2d4 d7d5")
                populated = workspace.screen_when(
                    lambda screen: bool(visible_library_ids(screen))
                )
                own_id = next(iter(visible_library_ids(populated)))

                workspace.click("restoreLibraryPackageButton")
                asked = workspace.screen_when(
                    lambda screen: "libraryReplacementWarning" in screen.labels
                )
                warning = asked.labels["libraryReplacementWarning"]
                self.assertIn("replaces this library", warning)
                self.assertIn("Nothing is merged", warning)
                self.assertIn("1 Game Record", warning)
                # Nothing has moved while the question is open.
                self.assertIn(own_id, visible_library_ids(asked))

                workspace.click("cancelLibraryReplacement")
                kept = workspace.screen_when(
                    lambda screen: "libraryReplacementWarning" not in screen.labels
                )
                self.assertIn(own_id, visible_library_ids(kept))

                workspace.click("restoreLibraryPackageButton")
                workspace.screen_when(
                    lambda screen: "libraryReplacementWarning" in screen.labels
                )
                workspace.click("confirmLibraryReplacement")
                replaced = workspace.screen_when(
                    lambda screen: visible_library_ids(screen)
                    == {library["source_id"], library["analysis_id"]}
                )
                self.assertNotIn(own_id, visible_library_ids(replaced))
                self.assertIn("Restored", replaced.labels["libraryPackageMessage"])

    def test_a_package_from_an_incompatible_version_changes_nothing(self) -> None:
        with tempfile.TemporaryDirectory(prefix="omachess-package-") as scratch:
            root = Path(scratch)
            package = root / "library.omalib"
            exported_library(package, directory=root / "source")

            document = json.loads(package.read_text(encoding="utf-8"))
            document["format_version"] = 99
            future = root / "future.omalib"
            future.write_text(json.dumps(document), encoding="utf-8")

            with Workspace(
                executable_under_test(),
                data_home=root / "target",
                environment={"OMACHESS_TEST_RESTORE_PACKAGE": str(future)},
            ) as workspace:
                workspace.play_all("d2d4 d7d5")
                populated = workspace.screen_when(
                    lambda screen: bool(visible_library_ids(screen))
                )
                own_id = next(iter(visible_library_ids(populated)))

                workspace.click("restoreLibraryPackageButton")
                refused = workspace.screen_when(
                    lambda screen: "libraryPackageMessage" in screen.labels
                    and "version 99" in screen.labels["libraryPackageMessage"]
                )
                self.assertIn("Nothing was changed", refused.labels["libraryPackageMessage"])
                self.assertNotIn("libraryReplacementWarning", refused.labels)
                self.assertEqual(visible_library_ids(refused), {own_id})

                workspace.restart()
                after_restart = workspace.screen_when(
                    lambda screen: visible_library_ids(screen) == {own_id}
                )
                self.assertEqual(visible_library_ids(after_restart), {own_id})


if __name__ == "__main__":
    unittest.main()
