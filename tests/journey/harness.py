"""Launch the real Omachess application and drive it like a player would.

A journey test asserts externally observable outcomes: what the running
application puts on screen, and how it answers input. It never reaches into
QML structure, Rust helpers, or storage layout, so implementations can change
freely underneath it.

The harness talks to the application over the control socket that
`OMACHESS_TEST_CHANNEL` enables (see app/src/TestChannel.h).
"""

from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

STARTUP_TIMEOUT_SECONDS = 30.0
REPLY_TIMEOUT_SECONDS = 15.0


class JourneyError(RuntimeError):
    """The application failed to start, answer, or act."""


@dataclass(frozen=True)
class SquareOnScreen:
    """One board square as it is currently drawn."""

    name: str
    piece: str
    light: bool
    # The Board Theme colour currently painted on this square.
    color: str
    # The marks a player can see: the square a piece was picked up from, a
    # square it may be dropped on, and the squares of the move just played.
    selected: bool
    target: bool
    last_move: bool
    # Whether this square's piece is drawn from loaded Piece Set artwork, and
    # the file it was drawn from.
    artwork_ready: bool
    artwork_source: str
    x: float
    y: float
    size: float
    visible: bool


@dataclass(frozen=True)
class Screen:
    """What the workspace window currently shows."""

    app_id: str
    title: str
    visible: bool
    width: int
    height: int
    device_pixel_ratio: float
    platform: str
    # Chrome and Board Theme colours currently in use, as #rrggbb.
    chrome_background: str
    chrome_foreground: str
    light_square: str
    dark_square: str
    # Where those colours came from: "quattro", "last_valid", or "builtin".
    palette_source: str
    theme_name: str
    board_theme_id: str
    piece_set_id: str
    squares: tuple[SquareOnScreen, ...]
    # The text of every named item that shows any, by item name.
    labels: dict[str, str]

    def square(self, name: str) -> SquareOnScreen:
        for square in self.squares:
            if square.name == name:
                return square
        raise JourneyError(f"no square named {name!r} is on screen")

    def top_left_square(self) -> SquareOnScreen:
        return min(self.squares, key=lambda square: (round(square.y), round(square.x)))

    def bottom_right_square(self) -> SquareOnScreen:
        return max(self.squares, key=lambda square: (round(square.y), round(square.x)))

    def pieces(self) -> dict[str, str]:
        """Occupied squares, by coordinate."""
        return {square.name: square.piece for square in self.squares if square.piece}

    def targets(self) -> set[str]:
        """The squares currently marked as somewhere a piece may be dropped."""
        return {square.name for square in self.squares if square.target}

    def selected(self) -> str | None:
        """The square a piece has been picked up from, if any."""
        for square in self.squares:
            if square.selected:
                return square.name
        return None

    def status(self) -> str:
        """The status line: whose turn it is, or the game's result."""
        return self.labels.get("statusLabel", "")

    def moves(self) -> list[str]:
        """The move list as it reads on screen, in playing order.

        Only the moves currently in view are reported, because only those are
        on screen: a long game's move list scrolls, and a journey that needs to
        read a move has to have it in view first.
        """
        numbered = sorted(
            (int(name.removeprefix("move:")), text)
            for name, text in self.labels.items()
            if name.startswith("move:")
        )
        return [text for _, text in numbered]

    def promotion_choices(self) -> set[str]:
        """The pieces a promoting pawn is currently being offered."""
        return {
            name.removeprefix("promote:")
            for name in self.labels
            if name.startswith("promote:")
        }


# A Quattro Palette fixture with an obvious light/dark square pair: the
# preferred lighter_background / background pairing clears ≥2:1 contrast, so
# a journey can assert those exact colours without re-deriving them.
VALID_PALETTE_A = {
    "mode": "dark",
    "accent": "#7aa2f7",
    "selection": "#292e42",
    "muted": "#414868",
    "background": "#101820",
    "dark_background": "#0a1014",
    "darker_background": "#05080a",
    "lighter_background": "#c0d0e0",
    "foreground": "#e8eef4",
    "red": "#f7768e",
    "yellow": "#e0af68",
    "green": "#9ece6a",
    "orange": "#eb927b",
}

VALID_PALETTE_B = {
    "mode": "light",
    "accent": "#1e66f5",
    "selection": "#ccd0da",
    "muted": "#acb0be",
    "background": "#1a2a1a",
    "dark_background": "#101810",
    "darker_background": "#080c08",
    "lighter_background": "#dce8c8",
    "foreground": "#1e1e2e",
    "red": "#d20f39",
    "yellow": "#df8e1d",
    "green": "#40a02b",
    "orange": "#d84e2b",
}

# Worked expectations for the fixtures above (lighter_background / background).
PALETTE_A_LIGHT_SQUARE = VALID_PALETTE_A["lighter_background"]
PALETTE_A_DARK_SQUARE = VALID_PALETTE_A["background"]
PALETTE_B_LIGHT_SQUARE = VALID_PALETTE_B["lighter_background"]
PALETTE_B_DARK_SQUARE = VALID_PALETTE_B["background"]

# The Built-in Palette's classic Board Theme, independent of Quattro.
BUILTIN_LIGHT_SQUARE = "#ebecd0"
BUILTIN_DARK_SQUARE = "#739552"
BUILTIN_CHROME_BACKGROUND = "#1a1b26"


def _toml_colors(values: dict[str, str]) -> str:
    return "".join(f'{key} = "{value}"\n' for key, value in values.items())


class Workspace:
    """A running Omachess application under test."""

    def __init__(
        self,
        executable: Path,
        *,
        platform: str | None = None,
        data_home: Path | None = None,
        omarchy_version: str | None = "4.0.0.alpha",
        palette: dict[str, str] | None = None,
        theme_name: str = "journey-a",
        install_palette: bool = True,
        malformed_palette: bool = False,
        environment: dict[str, str] | None = None,
    ) -> None:
        self._executable = executable
        self._platform = platform or os.environ.get("OMACHESS_TEST_QPA", "offscreen")
        self._owned_directory = data_home is None
        if data_home is None:
            self._directory = tempfile.TemporaryDirectory(prefix="omachess-journey-")
            self._root = Path(self._directory.name)
        else:
            self._directory = None
            self._root = data_home
            self._root.mkdir(parents=True, exist_ok=True)
        self._socket_path = str(self._root / "control")
        self._omarchy_prefix = self._root / "omarchy-prefix"
        self._state_home = self._root / "xdg_state_home"
        self._omarchy_version = omarchy_version
        self._initial_palette = palette
        self._theme_name = theme_name
        self._install_palette = install_palette
        self._malformed_palette = malformed_palette
        self._extra_environment = environment or {}
        self._process: subprocess.Popen[bytes] | None = None
        self._connection: socket.socket | None = None
        self._buffer = b""

    @property
    def data_home(self) -> Path:
        """The isolated XDG data home this run uses for the Live Store."""
        return self._root / "xdg_data_home"

    def __enter__(self) -> "Workspace":
        self.start()
        return self

    def __exit__(self, *_exception: object) -> None:
        self.stop()

    def start(self) -> None:
        environment = dict(os.environ)
        environment["OMACHESS_TEST_CHANNEL"] = self._socket_path
        environment["QT_QPA_PLATFORM"] = self._platform
        # Isolate the run from the developer's own configuration and state.
        for variable in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME"):
            directory = self._root / variable.lower()
            directory.mkdir(parents=True, exist_ok=True)
            environment[variable] = str(directory)
        environment.update(self._extra_environment)

        # Always point the adapter at an isolated prefix so journeys never read
        # the developer's real /usr/share/omarchy/version.
        self._omarchy_prefix.mkdir(parents=True, exist_ok=True)
        environment["OMACHESS_OMARCHY_PREFIX"] = str(self._omarchy_prefix)
        if self._omarchy_version is not None:
            (self._omarchy_prefix / "version").write_text(
                self._omarchy_version + "\n", encoding="utf-8"
            )

        if self._malformed_palette:
            self.install_theme(None, name=self._theme_name, malformed=True)
        elif self._install_palette:
            self.install_theme(
                self._initial_palette or VALID_PALETTE_A,
                name=self._theme_name,
            )

        # A prior run may have left a stale control socket in a reused data dir.
        try:
            os.unlink(self._socket_path)
        except FileNotFoundError:
            pass

        self._process = subprocess.Popen(
            [str(self._executable)],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self._connection = self._connect()

    def install_theme(
        self,
        palette: dict[str, str] | None,
        *,
        name: str = "journey",
        malformed: bool = False,
    ) -> None:
        """Install (or replace) the active Quattro theme the way Omarchy does.

        Stages a new theme directory, replaces `current/theme` atomically, then
        writes `theme.name`. A missing palette removes colors.toml; a malformed
        one writes structurally incompatible contents.
        """
        current = self._state_home / "omarchy" / "current"
        current.mkdir(parents=True, exist_ok=True)
        staging = self._root / f"theme-staging-{name}"
        if staging.exists():
            shutil.rmtree(staging)
        staging.mkdir(parents=True)

        if malformed:
            (staging / "colors.toml").write_text("this is not a palette\n", encoding="utf-8")
        elif palette is not None:
            (staging / "colors.toml").write_text(_toml_colors(palette), encoding="utf-8")

        target = current / "theme"
        if target.exists() or target.is_symlink():
            shutil.rmtree(target)
        staging.rename(target)
        (current / "theme.name").write_text(name + "\n", encoding="utf-8")

    def restart(self) -> None:
        """Quit and relaunch against the same XDG homes, keeping the Live Store."""
        self.stop(cleanup=False)
        self._buffer = b""
        self.start()

    def stop(self, *, cleanup: bool = True) -> None:
        if self._connection is not None:
            try:
                self._request({"command": "quit"})
            except (JourneyError, OSError, json.JSONDecodeError):
                pass
            self._connection.close()
            self._connection = None
        if self._process is not None:
            try:
                self._process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait(timeout=10)
            if self._process.stdout is not None:
                self._process.stdout.close()
            self._process = None
        if cleanup and self._owned_directory and self._directory is not None:
            self._directory.cleanup()
            self._directory = None

    def _connect(self) -> socket.socket:
        deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self._process is not None and self._process.poll() is not None:
                raise JourneyError(f"the application exited early:\n{self._collect_output()}")
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.settimeout(REPLY_TIMEOUT_SECONDS)
            try:
                connection.connect(self._socket_path)
                return connection
            except OSError:
                connection.close()
                time.sleep(0.05)
        raise JourneyError(
            f"the application never opened its control socket:\n{self._collect_output()}"
        )

    def _collect_output(self) -> str:
        if self._process is None or self._process.stdout is None:
            return ""
        try:
            self._process.stdout.flush()
        except ValueError:
            pass
        self._process.kill()
        return self._process.stdout.read().decode(errors="replace")

    def _request(self, command: dict[str, object]) -> dict:
        if self._connection is None:
            raise JourneyError("the workspace is not running")
        self._connection.sendall(json.dumps(command).encode() + b"\n")

        while b"\n" not in self._buffer:
            chunk = self._connection.recv(65536)
            if not chunk:
                raise JourneyError(
                    f"the application closed the control socket:\n{self._collect_output()}"
                )
            self._buffer += chunk
        line, self._buffer = self._buffer.split(b"\n", 1)

        reply = json.loads(line)
        if not reply.get("ok"):
            raise JourneyError(f"{command} failed: {reply.get('error')}")
        return reply

    # --- Driving the application -------------------------------------------

    def press_key(self, key: str) -> None:
        self._request({"command": "key", "key": key})

    def click(self, target: str) -> None:
        self._request({"command": "click", "target": target})

    def click_square(self, square: str) -> None:
        """Press and release the middle of a board square."""
        self.click(f"square:{square}")

    def play(self, move: str, *, promotion: str | None = None) -> None:
        """Play `move`, given as the two squares it joins ("e2e4").

        This is the pointer journey a player takes: pick the piece up, put it
        down on the destination, and answer the promotion offer when one
        appears.
        """
        self.click_square(move[:2])
        self.click_square(move[2:4])
        if promotion is not None:
            self.click(f"promote:{promotion}")

    def play_all(self, moves: str) -> None:
        """Play a whole scripted game, given as space-separated moves.

        A move that promotes carries the piece as a fifth character, the way
        the engine writes it: "g7g8q".
        """
        for move in moves.split():
            promotions = {"q": "queen", "r": "rook", "b": "bishop", "n": "knight"}
            self.play(move, promotion=promotions.get(move[4:5]))

    def resize(self, width: int, height: int) -> None:
        self._request({"command": "resize", "width": width, "height": height})

    def set_board_theme(self, theme_id: str) -> None:
        """Pin or unpin the Board Theme the way a player does from the chrome."""
        self.click(f"boardTheme:{theme_id}")

    def set_piece_set(self, piece_set_id: str) -> None:
        """Choose a Piece Set from the chrome."""
        self.click(f"pieceSet:{piece_set_id}")

    def replace_theme(
        self,
        palette: dict[str, str] | None,
        *,
        name: str = "journey-swap",
        malformed: bool = False,
    ) -> None:
        """Replace the active Quattro theme while the app is running."""
        self.install_theme(palette, name=name, malformed=malformed)

    # --- Observing the application -----------------------------------------

    def screen(self) -> Screen:
        raw = self._request({"command": "snapshot"})["snapshot"]
        return Screen(
            app_id=raw["appId"],
            title=raw["title"],
            visible=raw["visible"],
            width=raw["width"],
            height=raw["height"],
            device_pixel_ratio=raw["devicePixelRatio"],
            platform=raw["platform"],
            chrome_background=raw["chromeBackground"],
            chrome_foreground=raw["chromeForeground"],
            light_square=raw["lightSquare"],
            dark_square=raw["darkSquare"],
            palette_source=raw["paletteSource"],
            theme_name=raw["themeName"],
            board_theme_id=raw["boardThemeId"],
            piece_set_id=raw["pieceSetId"],
            labels=dict(raw["labels"]),
            squares=tuple(
                SquareOnScreen(
                    name=square["name"],
                    piece=square["piece"],
                    light=square["light"],
                    color=square["color"],
                    selected=square["selected"],
                    target=square["target"],
                    last_move=square["lastMove"],
                    artwork_ready=square["artworkReady"],
                    artwork_source=square["artworkSource"],
                    x=square["x"],
                    y=square["y"],
                    size=square["size"],
                    visible=square["visible"],
                )
                for square in raw["squares"]
            ),
        )

    def screen_when(self, condition, *, timeout: float = 10.0) -> Screen:
        """The screen once `condition(screen)` holds, or a failure."""
        deadline = time.monotonic() + timeout
        screen = self.screen()
        while not condition(screen):
            if time.monotonic() > deadline:
                raise JourneyError(f"the workspace never reached the expected state: {screen}")
            time.sleep(0.05)
            screen = self.screen()
        return screen

    def open_network_sockets(self) -> list[str]:
        """The IP sockets the running application holds open, if any.

        Unix sockets (the Wayland connection, the control channel) are not
        network access and are not reported.
        """
        if self._process is None:
            raise JourneyError("the workspace is not running")

        inodes = set()
        for descriptor in Path(f"/proc/{self._process.pid}/fd").iterdir():
            try:
                target = os.readlink(descriptor)
            except OSError:
                continue
            if target.startswith("socket:["):
                inodes.add(target[len("socket:[") : -1])

        found = []
        for table in ("tcp", "tcp6", "udp", "udp6"):
            listing = Path("/proc/net") / table
            if not listing.exists():
                continue
            for line in listing.read_text().splitlines()[1:]:
                fields = line.split()
                # sl local_address rem_address st ... uid timeout inode
                if len(fields) > 9 and fields[9] in inodes:
                    found.append(f"{table} {fields[1]} -> {fields[2]}")
        return found


def executable_under_test() -> Path:
    """The omachess binary a journey test drives.

    ctest passes it in; a developer running pytest directly can set
    OMACHESS_BINARY instead.
    """
    location = os.environ.get("OMACHESS_BINARY")
    if not location:
        raise JourneyError("set OMACHESS_BINARY to the omachess executable to run journey tests")
    path = Path(location)
    if not path.is_file():
        raise JourneyError(f"OMACHESS_BINARY does not name a file: {path}")
    return path
