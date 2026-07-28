"""The v0.1 accessibility and responsive bar, asserted across the workspace.

These journeys assert public semantics — roles, names, announcements, focus,
contrast, and viewport boundaries — never widget structure. v0.1 claims this
bar; it does not claim workspace-wide WCAG AA conformance or unaided blind
play.

Run against a build with:

    OMACHESS_BINARY=build/app/omachess python3 -m unittest discover tests/journey
"""

from __future__ import annotations

import unittest

from harness import (
    VALID_PALETTE_A,
    VALID_PALETTE_B,
    Screen,
    Workspace,
    executable_under_test,
)
from test_cockpit_journey import cockpit_chrome_is_up, library_ids

# The legibility bar, by the pair a player has to read. Body text carries the
# WCAG AA ratio; secondary text and status colour carry the large-text ratio;
# board squares and translucent board marks carry the board's own bar.
CONTRAST_BAR = {
    "chrome_text": 4.5,
    "panel_text": 4.5,
    "selection_text": 4.5,
    "muted_text": 3.0,
    "focus_ring": 3.0,
    "status_danger": 3.0,
    "status_warning": 3.0,
    "status_success": 3.0,
    "board_squares": 2.0,
    "panel_surface": 1.1,
    "last_move_mark": 1.2,
    "selected_mark": 1.2,
    "move_target_mark": 1.2,
}


def board_is_drawn(screen: Screen) -> bool:
    return len(screen.squares) == 64 and all(square.size > 0 for square in screen.squares)


class SemanticsJourney(unittest.TestCase):
    """Every chrome surface answers an assistive technology."""

    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.screen_when(cockpit_chrome_is_up)

    def test_chrome_surfaces_expose_roles_names_and_states(self) -> None:
        self.workspace.play_all("e2e4 e7e5")
        screen = self.workspace.screen_when(lambda s: len(library_ids(s)) == 1)
        record_id = library_ids(screen)[0]

        # The cockpit's panes and lists name themselves.
        expected_roles = {
            "libraryRail": "pane",
            "rightRail": "pane",
            "pane:board": "grouping",
            "pane:library:list": "list",
            "pane:right:moves": "list",
            "tabBar": "pagetablist",
            "libraryHeading": "heading",
            "rightRailHeading": "heading",
            "statusLabel": "statictext",
        }
        for name, role in expected_roles.items():
            self.assertIn(name, screen.semantics, f"{name} is not on screen")
            self.assertEqual(screen.semantics[name].role, role, name)
            self.assertNotEqual(screen.semantics[name].name, "", f"{name} has no accessible name")

        # A record in the library and its tab are reachable items that carry
        # the record's own title, not an internal id.
        title = screen.labels[f"libraryTitle:{record_id}"]
        library_item = screen.semantics[f"library:{record_id}"]
        self.assertEqual(library_item.role, "listitem")
        self.assertEqual(library_item.name, title)
        self.assertTrue(library_item.focusable)

        tab = screen.semantics[f"tab:{record_id}"]
        self.assertEqual(tab.role, "pagetab")
        self.assertEqual(tab.name, screen.labels[f"tabTitle:{record_id}"])
        self.assertTrue(tab.focusable)

        # Moves are list items that read the way they are written.
        move = screen.semantics["move:1"]
        self.assertEqual(move.role, "listitem")
        self.assertEqual(move.name, "1. e4")

        # Ordinary chrome buttons carry their role and their state.
        self.assertEqual(screen.semantics["newGameButton"].role, "button")
        self.assertTrue(screen.semantics["newGameButton"].enabled)
        self.assertFalse(screen.semantics["saveRecord"].visible)

        # Board squares say where they are and what stands on them.
        self.assertEqual(screen.semantics["square:e4"].role, "cell")
        self.assertEqual(screen.semantics["square:e4"].name, "e4, white pawn")
        self.assertEqual(screen.semantics["square:a3"].name, "a3")

    def test_the_command_palette_and_dialogs_are_navigable(self) -> None:
        self.workspace.press_key("ctrl+k")
        screen = self.workspace.screen_when(lambda s: "commandPaletteDialog" in s.semantics)

        palette = screen.semantics["commandPaletteDialog"]
        self.assertEqual(palette.role, "dialog")
        self.assertEqual(palette.name, "Command palette")
        self.assertEqual(screen.semantics["commandPaletteList"].role, "list")
        flip = screen.semantics["paletteAction:flip"]
        self.assertEqual(flip.role, "listitem")
        self.assertEqual(flip.name, "Flip board")
        self.assertEqual(flip.description, "F")
        self.workspace.press_key("escape")

        # A dialog that interrupts play names itself the same way.
        self.workspace.play_all("e2e4")
        screen = self.workspace.screen_when(lambda s: len(library_ids(s)) == 1)
        self.workspace.click(f"purgeRecord:{screen.active_record_id}")
        screen = self.workspace.screen_when(lambda s: "permanentPurgeDialogBody" in s.semantics)
        self.assertEqual(screen.semantics["permanentPurgeDialogBody"].role, "dialog")
        self.assertEqual(screen.semantics["permanentPurgeDialogBody"].name, "Permanent purge")
        self.workspace.click("cancelPermanentPurge")

    def test_workshop_steps_carry_their_own_semantics(self) -> None:
        self.workspace.click("newVariantButton")
        screen = self.workspace.screen_when(lambda s: "workshopStepHeading" in s.semantics)
        self.assertEqual(screen.semantics["workshopStepHeading"].role, "heading")
        self.assertEqual(screen.semantics["workshopStepHeading"].name, "1. Board")
        self.assertEqual(screen.semantics["rightRail"].name, "Variant Workshop")

        self.workspace.click("workshopContinue")
        screen = self.workspace.screen_when(
            lambda s: s.semantics["workshopStepHeading"].name == "2. Pieces"
        )
        self.assertEqual(screen.semantics["workshopStepHeading"].role, "heading")


class AnnouncementJourney(unittest.TestCase):
    """State changes are announced once each; engine output only on request."""

    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.screen_when(cockpit_chrome_is_up)

    def test_each_meaningful_change_is_announced_exactly_once(self) -> None:
        before = self.workspace.screen().announcements

        self.workspace.play("e2e4")
        screen = self.workspace.screen_when(lambda s: "Black to move" in s.announcements)
        spoken = [line for line in screen.announcements if line not in before]
        self.assertEqual(spoken.count("Black to move"), 1)

        # Reading the same screen again says nothing new.
        self.assertEqual(self.workspace.screen().announcements, screen.announcements)

        self.workspace.play("e7e5")
        screen = self.workspace.screen_when(lambda s: s.announcements[-1] == "White to move")
        self.assertEqual(screen.announcements.count("White to move"), 1)

        # Navigating announces the position a player moved to, once.
        self.workspace.press_key("home")
        screen = self.workspace.screen_when(
            lambda s: s.announcements[-1].startswith("Reviewing")
        )
        self.assertEqual(screen.announcements[-1], "Reviewing after 0 of 2 moves")

    def test_a_running_clock_never_announces_itself(self) -> None:
        self.workspace.select("clockPicker", 3)
        self.workspace.play_all("e2e4 e7e5")
        first = self.workspace.screen_when(lambda s: len(s.moves()) == 2)
        clock_before = first.labels["whiteClockLabel"]

        # Let the clock run: it changes the screen continuously and must stay
        # out of the announcement stream.
        later = self.workspace.screen_when(
            lambda s: s.labels["whiteClockLabel"] != clock_before, timeout=5.0
        )
        self.assertEqual(later.announcements, first.announcements)

    def test_engine_output_is_announced_only_when_asked_for(self) -> None:
        self.workspace.play_all("e2e4")
        screen = self.workspace.screen_when(lambda s: "analysisStatus" in s.labels)
        quiet = screen.announcements
        self.assertTrue(
            all("Engine output" not in line for line in quiet),
            f"analysis announced itself: {quiet}",
        )

        # Analysis keeps reporting; the announcement stream stays where it was.
        self.workspace.play_all("e7e5")
        screen = self.workspace.screen_when(lambda s: len(s.moves()) == 2)
        self.assertTrue(all("Engine output" not in line for line in screen.announcements))

        # Asking for it announces exactly one line.
        self.workspace.press_key("ctrl+e")
        screen = self.workspace.screen_when(lambda s: len(s.announcements) > 0)
        self.assertNotEqual(screen.announcements[-1], "")

    def test_nothing_steals_focus(self) -> None:
        def in_library(screen: Screen) -> bool:
            return screen.active_focus.startswith(("library:", "pane:library"))

        self.workspace.press_key("alt+right")
        self.workspace.press_key("down")
        self.assertTrue(in_library(self.workspace.screen()))

        # Play, engine output, and a desktop theme change all arrive while the
        # player is reading the Personal Library. None of them takes the
        # keyboard out of the pane the player put it in.
        self.workspace.play_all("e2e4 e7e5")
        self.workspace.screen_when(lambda s: len(s.moves()) == 2)
        self.assertTrue(in_library(self.workspace.screen()))

        self.workspace.replace_theme(VALID_PALETTE_B, name="focus-stability-a11y")
        self.workspace.screen_when(lambda s: s.theme_name == "focus-stability-a11y")
        self.assertTrue(in_library(self.workspace.screen()))

        # The board is a different pane, and it keeps the keyboard the same way.
        self.workspace.press_key("alt+right")
        self.assertTrue(self.workspace.screen().active_focus.startswith("pane:board"))
        self.workspace.play_all("g1f3")
        self.workspace.screen_when(lambda s: len(s.moves()) == 3)
        self.assertTrue(self.workspace.screen().active_focus.startswith("pane:board"))


class TypedMoveJourney(unittest.TestCase):
    """A player who prefers the keyboard can enter moves without the pointer."""

    def setUp(self) -> None:
        self.workspace = Workspace(executable_under_test())
        self.workspace.start()
        self.addCleanup(self.workspace.stop)
        self.workspace.screen_when(cockpit_chrome_is_up)

    def test_typed_moves_play_and_refuse_the_same_moves_the_board_does(self) -> None:
        # Typed entry is optional: it appears when a player asks for it.
        self.assertNotIn("typedMoveInput", self.workspace.screen().labels)
        self.workspace.press_key("ctrl+m")
        screen = self.workspace.screen_when(lambda s: "typedMoveInput" in s.labels)
        self.assertEqual(screen.semantics["typedMoveInput"].name, "Typed move")
        self.assertEqual(screen.active_focus, "typedMoveInput")

        self.workspace.set_text("typedMoveInput", "e2e4")
        self.workspace.click("typedMoveSubmit")
        screen = self.workspace.screen_when(lambda s: s.moves() == ["1. e4"])
        self.assertEqual(screen.pieces().get("e4"), "white_pawn")

        # An illegal move is refused and said out loud, and play is unchanged.
        self.workspace.set_text("typedMoveInput", "e7e5")
        self.workspace.set_text("typedMoveInput", "a1a8")
        self.workspace.click("typedMoveSubmit")
        screen = self.workspace.screen_when(
            lambda s: s.labels.get("typedMoveError", "").startswith("a1a8")
        )
        self.assertEqual(screen.moves(), ["1. e4"])
        self.assertEqual(screen.announcements[-1], screen.labels["typedMoveError"])


class ContrastJourney(unittest.TestCase):
    """Contrast is asserted across the supported palettes, not assumed."""

    def assert_contrast_bar(self, screen: Screen, context: str) -> None:
        for pair, minimum in CONTRAST_BAR.items():
            self.assertIn(pair, screen.contrast, f"{context}: {pair} is not reported")
            self.assertGreaterEqual(
                screen.contrast[pair],
                minimum,
                f"{context}: {pair} is {screen.contrast[pair]:.2f}, below {minimum}",
            )

    def test_every_supported_palette_and_board_theme_clears_the_bar(self) -> None:
        for label, palette in (("palette A", VALID_PALETTE_A), ("palette B", VALID_PALETTE_B)):
            with self.subTest(palette=label):
                with Workspace(executable_under_test(), palette=palette) as workspace:
                    screen = workspace.screen_when(cockpit_chrome_is_up)
                    self.assert_contrast_bar(screen, label)

                    # Pinned Board Themes are supported palettes too.
                    for theme in ("classic", "walnut", "slate"):
                        workspace.set_board_theme(theme)
                        screen = workspace.screen_when(lambda s, t=theme: s.board_theme_id == t)
                        self.assert_contrast_bar(screen, f"{label} · {theme}")

    def test_the_builtin_palette_clears_the_bar(self) -> None:
        with Workspace(executable_under_test(), install_palette=False) as workspace:
            screen = workspace.screen_when(cockpit_chrome_is_up)
            self.assertEqual(screen.palette_source, "builtin")
            self.assert_contrast_bar(screen, "builtin")


class ViewportJourney(unittest.TestCase):
    """Rails collapse by priority; board and primary action never do."""

    def board_fits(self, screen: Screen) -> None:
        self.assertEqual(len(screen.squares), 64)
        for square in screen.squares:
            self.assertGreater(square.size, 0)
            self.assertLessEqual(square.x + square.size, screen.width + 0.001)
            self.assertLessEqual(square.y + square.size, screen.height + 0.001)

    def test_rails_collapse_by_priority_down_to_the_floor(self) -> None:
        with Workspace(executable_under_test()) as workspace:
            workspace.screen_when(cockpit_chrome_is_up)

            # The full cockpit at the design viewport.
            workspace.resize(1280, 800)
            screen = workspace.screen_when(lambda s: s.layout_mode == "full")
            self.assertTrue(screen.semantics["libraryRail"].visible)
            self.assertTrue(screen.semantics["rightRail"].visible)
            self.board_fits(screen)

            # Narrower: the Personal Library rail goes first, the right rail
            # stays.
            workspace.resize(1100, 720)
            screen = workspace.screen_when(lambda s: s.layout_mode == "compact")
            self.assertFalse(screen.semantics["libraryRail"].visible)
            self.assertTrue(screen.semantics["rightRail"].visible)
            self.assertIn("rightRailHeading", screen.labels)
            self.board_fits(screen)

            # At the floor both rails are gone; the board and this surface's
            # primary action remain usable.
            workspace.resize(640, 480)
            screen = workspace.screen_when(lambda s: s.layout_mode == "minimal")
            self.assertFalse(screen.semantics["libraryRail"].visible)
            self.assertFalse(screen.semantics["rightRail"].visible)
            self.board_fits(screen)
            self.assertEqual(screen.labels.get("primaryAction"), "New game")
            self.assertTrue(screen.semantics["primaryAction"].enabled)
            self.assertIn("railCollapseSummary", screen.labels)

            # The board still plays at the floor, the primary action still
            # acts, and pane traversal skips the rails that are not there.
            workspace.play_all("e2e4")
            workspace.screen_when(lambda s: s.pieces().get("e4") == "white_pawn")
            workspace.click("primaryAction")
            screen = workspace.screen_when(lambda s: "e4" not in s.pieces())
            workspace.press_key("alt+right")
            self.assertTrue(workspace.screen().active_focus.startswith("pane:board"))

    def test_the_current_surface_keeps_its_own_primary_action_at_the_floor(self) -> None:
        with Workspace(executable_under_test()) as workspace:
            workspace.screen_when(cockpit_chrome_is_up)
            workspace.resize(640, 480)
            workspace.screen_when(lambda s: s.layout_mode == "minimal")

            workspace.click("positionSetupButton")
            screen = workspace.screen_when(
                lambda s: s.labels.get("primaryAction") == "Start Played Game"
            )
            self.board_fits(screen)

    def test_fractional_scaling_is_handled_at_each_supported_viewport(self) -> None:
        for scale in ("1.25", "1.5"):
            with self.subTest(scale=scale):
                with Workspace(
                    executable_under_test(), environment={"QT_SCALE_FACTOR": scale}
                ) as workspace:
                    workspace.screen_when(cockpit_chrome_is_up)
                    self.assertAlmostEqual(
                        workspace.screen().device_pixel_ratio, float(scale), places=3
                    )

                    for width, height, mode in (
                        (1280, 800, "full"),
                        (1100, 720, "compact"),
                        (640, 480, "minimal"),
                    ):
                        workspace.resize(width, height)
                        screen = workspace.screen_when(
                            lambda s, w=width, h=height: (s.width, s.height) == (w, h)
                            and board_is_drawn(s)
                        )
                        self.assertEqual(screen.layout_mode, mode)
                        self.board_fits(screen)
                        sizes = {round(square.size, 3) for square in screen.squares}
                        self.assertEqual(len(sizes), 1, "squares differ in size under scaling")


if __name__ == "__main__":
    unittest.main()
