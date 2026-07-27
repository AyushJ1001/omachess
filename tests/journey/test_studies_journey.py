"""Journey through a durable, ordered Study."""

from __future__ import annotations

import unittest

from harness import Workspace, executable_under_test
from test_analysis_records_journey import CHECKMATE, library_ids


def study_id(screen) -> str:
    return next(
        name.removeprefix("studyTitle:")
        for name in screen.labels
        if name.startswith("studyTitle:")
    )


def ordered_members(screen, study: str) -> list[str]:
    prefix = f"studyMember:{study}:"
    members = []
    for name in screen.labels:
        if name.startswith(prefix):
            position, record_id = name.removeprefix(prefix).split(":", 1)
            members.append((int(position), record_id))
    return [record_id for _, record_id in sorted(members)]


class StudiesJourney(unittest.TestCase):
    def test_completed_game_and_two_analyses_keep_membership_order_after_restart(self) -> None:
        with Workspace(executable_under_test()) as workspace:
            workspace.play_all(CHECKMATE)
            completed = workspace.screen_when(lambda screen: "(" in screen.status())
            completed_id = next(iter(library_ids(completed)))

            workspace.enter_text("newStudyName", "Critical positions")
            workspace.click("createStudyButton")
            created = workspace.screen_when(
                lambda screen: any(
                    name.startswith("studyTitle:") and text == "Critical positions"
                    for name, text in screen.labels.items()
                )
            )
            study = study_id(created)
            workspace.click(f"addActiveToStudy:{study}")

            workspace.click("deriveAnalysisButton")
            first = workspace.screen_when(lambda screen: len(library_ids(screen)) == 2)
            first_id = next(iter(library_ids(first) - {completed_id}))
            workspace.click(f"addActiveToStudy:{study}")

            workspace.click(f"recordGraphSource:{completed_id}")
            workspace.click("deriveAnalysisButton")
            second = workspace.screen_when(lambda screen: len(library_ids(screen)) == 3)
            second_id = next(iter(library_ids(second) - {completed_id, first_id}))
            workspace.click(f"addActiveToStudy:{study}")
            workspace.screen_when(
                lambda screen: ordered_members(screen, study)
                == [completed_id, first_id, second_id]
            )

            workspace.click(f"studyMemberUp:{study}:{second_id}")
            workspace.restart()
            restored = workspace.screen_when(
                lambda screen: ordered_members(screen, study)
                == [completed_id, second_id, first_id]
            )
            workspace.click(f"studyMember:{study}:1:{second_id}")
            opened = workspace.screen_when(
                lambda screen: f"tabTitle:{second_id}" in screen.labels
            )
            self.assertEqual(ordered_members(restored, study), [completed_id, second_id, first_id])
            self.assertIn(f"tabTitle:{second_id}", opened.labels)


if __name__ == "__main__":
    unittest.main()
