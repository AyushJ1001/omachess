"""The experimental v0.1 contract is discoverable inside the workspace."""

from __future__ import annotations

import unittest

from harness import Workspace, executable_under_test
from test_cockpit_journey import cockpit_chrome_is_up


class ReleaseGuidanceJourney(unittest.TestCase):
    def test_the_workspace_exposes_migration_export_and_recovery_guidance(self) -> None:
        with Workspace(executable_under_test()) as workspace:
            workspace.screen_when(cockpit_chrome_is_up)
            workspace.click("experimentalReleaseButton")
            screen = workspace.screen_when(lambda s: "experimentalReleaseTitle" in s.labels)

            self.assertEqual(screen.labels["experimentalReleaseButton"], "v0.1 experimental")
            guidance = screen.labels["experimentalReleaseGuidance"].lower()
            for topic in ("migration", "export", "recovery", "live store"):
                self.assertIn(topic, guidance)


if __name__ == "__main__":
    unittest.main()
