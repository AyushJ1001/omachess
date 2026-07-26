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
    squares: tuple[SquareOnScreen, ...]

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


class Workspace:
    """A running Omachess application under test."""

    def __init__(self, executable: Path, *, platform: str | None = None) -> None:
        self._executable = executable
        self._platform = platform or os.environ.get("OMACHESS_TEST_QPA", "offscreen")
        self._directory = tempfile.TemporaryDirectory(prefix="omachess-journey-")
        self._socket_path = str(Path(self._directory.name) / "control")
        self._process: subprocess.Popen[bytes] | None = None
        self._connection: socket.socket | None = None
        self._buffer = b""

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
            directory = Path(self._directory.name) / variable.lower()
            directory.mkdir(parents=True, exist_ok=True)
            environment[variable] = str(directory)

        self._process = subprocess.Popen(
            [str(self._executable)],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self._connection = self._connect()

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

    def resize(self, width: int, height: int) -> None:
        self._request({"command": "resize", "width": width, "height": height})

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
            squares=tuple(
                SquareOnScreen(
                    name=square["name"],
                    piece=square["piece"],
                    light=square["light"],
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

    def stop(self) -> None:
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
        self._directory.cleanup()


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
