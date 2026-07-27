"""Journeys through Quattro Palette sync, Board Theme, and Piece Set selection.

Run against a build with:

    OMACHESS_BINARY=build/app/omachess python3 -m unittest discover tests/journey
"""

from __future__ import annotations

import time
import unittest

from harness import (
    BUILTIN_CHROME_BACKGROUND,
    BUILTIN_DARK_SQUARE,
    BUILTIN_LIGHT_SQUARE,
    PALETTE_A_DARK_SQUARE,
    PALETTE_A_LIGHT_SQUARE,
    PALETTE_B_DARK_SQUARE,
    PALETTE_B_LIGHT_SQUARE,
    VALID_PALETTE_A,
    VALID_PALETTE_B,
    Screen,
    Workspace,
    executable_under_test,
)
from test_board_journey import board_is_drawn


def themed_from(*, light: str, dark: str, chrome: str | None = None):
    """Wait until the workspace is painting the expected Board Theme / chrome."""

    def ready(screen: Screen) -> bool:
        if not board_is_drawn(screen):
            return False
        if screen.light_square.lower() != light.lower():
            return False
        if screen.dark_square.lower() != dark.lower():
            return False
        if chrome is not None and screen.chrome_background.lower() != chrome.lower():
            return False
        return True

    return ready


class PaletteJourney(unittest.TestCase):
    def test_chrome_and_board_follow_a_valid_quattro_palette_at_startup(self) -> None:
        with Workspace(
            executable_under_test(),
            palette=VALID_PALETTE_A,
            theme_name="journey-a",
        ) as workspace:
            screen = workspace.screen_when(
                themed_from(
                    light=PALETTE_A_LIGHT_SQUARE,
                    dark=PALETTE_A_DARK_SQUARE,
                    chrome=VALID_PALETTE_A["background"],
                )
            )

            self.assertEqual(screen.chrome_foreground.lower(), VALID_PALETTE_A["foreground"])
            self.assertEqual(screen.theme_name, "journey-a")
            self.assertEqual(screen.board_theme_id, "follow")
            self.assertEqual(screen.piece_set_id, "cburnett")

            lights = {square.color.lower() for square in screen.squares if square.light}
            darks = {square.color.lower() for square in screen.squares if not square.light}
            self.assertEqual(lights, {PALETTE_A_LIGHT_SQUARE.lower()})
            self.assertEqual(darks, {PALETTE_A_DARK_SQUARE.lower()})

    def test_a_missing_palette_falls_back_to_the_builtin_palette(self) -> None:
        with Workspace(executable_under_test(), install_palette=False) as workspace:
            workspace.screen_when(
                themed_from(
                    light=BUILTIN_LIGHT_SQUARE,
                    dark=BUILTIN_DARK_SQUARE,
                    chrome=BUILTIN_CHROME_BACKGROUND,
                )
            )

    def test_a_malformed_palette_at_startup_falls_back_to_builtin(self) -> None:
        with Workspace(
            executable_under_test(),
            malformed_palette=True,
            theme_name="broken",
        ) as workspace:
            workspace.screen_when(
                themed_from(
                    light=BUILTIN_LIGHT_SQUARE,
                    dark=BUILTIN_DARK_SQUARE,
                    chrome=BUILTIN_CHROME_BACKGROUND,
                )
            )

    def test_switching_the_omarchy_theme_repaints_without_restart(self) -> None:
        with Workspace(
            executable_under_test(),
            palette=VALID_PALETTE_A,
            theme_name="journey-a",
        ) as workspace:
            workspace.screen_when(
                themed_from(
                    light=PALETTE_A_LIGHT_SQUARE,
                    dark=PALETTE_A_DARK_SQUARE,
                    chrome=VALID_PALETTE_A["background"],
                )
            )

            workspace.replace_theme(VALID_PALETTE_B, name="journey-b")
            screen = workspace.screen_when(
                themed_from(
                    light=PALETTE_B_LIGHT_SQUARE,
                    dark=PALETTE_B_DARK_SQUARE,
                    chrome=VALID_PALETTE_B["background"],
                ),
                timeout=15.0,
            )
            self.assertEqual(screen.theme_name, "journey-b")
            self.assertEqual(screen.piece_set_id, "cburnett")

    def test_a_malformed_mid_session_palette_keeps_the_last_valid_palette(self) -> None:
        with Workspace(
            executable_under_test(),
            palette=VALID_PALETTE_A,
            theme_name="journey-a",
        ) as workspace:
            workspace.screen_when(
                themed_from(
                    light=PALETTE_A_LIGHT_SQUARE,
                    dark=PALETTE_A_DARK_SQUARE,
                )
            )

            workspace.replace_theme(None, name="broken", malformed=True)
            # Hold through the adapter's retry window: the Last Valid Palette
            # must stay painted, never collapse to Built-in or an error loop.
            deadline = time.monotonic() + 1.0
            while time.monotonic() < deadline:
                screen = workspace.screen()
                self.assertEqual(screen.light_square.lower(), PALETTE_A_LIGHT_SQUARE.lower())
                self.assertEqual(screen.dark_square.lower(), PALETTE_A_DARK_SQUARE.lower())
                self.assertEqual(
                    screen.chrome_background.lower(), VALID_PALETTE_A["background"].lower()
                )
                time.sleep(0.05)
            self.assertEqual(screen.theme_name, "journey-a")

    def test_pinning_the_board_theme_ignores_desktop_theme_changes(self) -> None:
        with Workspace(
            executable_under_test(),
            palette=VALID_PALETTE_A,
            theme_name="journey-a",
        ) as workspace:
            workspace.screen_when(
                themed_from(
                    light=PALETTE_A_LIGHT_SQUARE,
                    dark=PALETTE_A_DARK_SQUARE,
                )
            )

            workspace.set_board_theme("classic")
            pinned = workspace.screen_when(
                lambda s: board_is_drawn(s)
                and s.board_theme_id == "classic"
                and s.light_square.lower() == BUILTIN_LIGHT_SQUARE.lower()
                and s.dark_square.lower() == BUILTIN_DARK_SQUARE.lower()
            )
            self.assertEqual(pinned.chrome_background.lower(), VALID_PALETTE_A["background"])

            workspace.replace_theme(VALID_PALETTE_B, name="journey-b")
            screen = workspace.screen_when(
                lambda s: board_is_drawn(s)
                and s.theme_name == "journey-b"
                and s.chrome_background.lower() == VALID_PALETTE_B["background"].lower()
                and s.light_square.lower() == BUILTIN_LIGHT_SQUARE.lower()
                and s.dark_square.lower() == BUILTIN_DARK_SQUARE.lower(),
                timeout=15.0,
            )
            self.assertEqual(screen.board_theme_id, "classic")
            self.assertEqual(screen.piece_set_id, "cburnett")

    def test_piece_set_selection_is_independent_of_any_palette(self) -> None:
        with Workspace(
            executable_under_test(),
            palette=VALID_PALETTE_A,
            theme_name="journey-a",
        ) as workspace:
            workspace.screen_when(board_is_drawn)
            workspace.set_piece_set("cburnett")
            workspace.replace_theme(VALID_PALETTE_B, name="journey-b")
            screen = workspace.screen_when(
                themed_from(
                    light=PALETTE_B_LIGHT_SQUARE,
                    dark=PALETTE_B_DARK_SQUARE,
                ),
                timeout=15.0,
            )
            self.assertEqual(screen.piece_set_id, "cburnett")
            occupied = [square for square in screen.squares if square.piece]
            self.assertTrue(occupied)
            self.assertTrue(
                all("pieces/cburnett/" in square.artwork_source for square in occupied)
            )

    def test_an_unrecognised_omarchy_version_uses_the_builtin_palette(self) -> None:
        with Workspace(
            executable_under_test(),
            omarchy_version="3.0.0",
            palette=VALID_PALETTE_A,
            theme_name="ignored",
        ) as workspace:
            screen = workspace.screen_when(
                themed_from(
                    light=BUILTIN_LIGHT_SQUARE,
                    dark=BUILTIN_DARK_SQUARE,
                    chrome=BUILTIN_CHROME_BACKGROUND,
                )
            )
            self.assertNotEqual(
                screen.chrome_background.lower(), VALID_PALETTE_A["background"].lower()
            )


if __name__ == "__main__":
    unittest.main()
