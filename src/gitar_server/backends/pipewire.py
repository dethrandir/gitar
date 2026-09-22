"""PipeWire/PulseAudio backend.

Ports the proven behaviour of the legacy ``gitar`` bash CLI into Python. Every
subprocess and socket call goes through a small module-level seam so the routing
logic can be tested without touching Guitarix, ``pactl``, or a sound card.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from gitar_server import devices
from gitar_server.config import Config
from gitar_server.levels import analyze_wav

from .base import AudioBackend, BackendError, BackendStatus, LevelReading, Route

DEFAULT_RPC_PORT = 7342

GX_AMP_IN = "gx_head_amp:in_0"
GX_AMP_OUT = "gx_head_amp:out_0"
GX_FX_IN = "gx_head_fx:in_0"
GX_FX_OUT_0 = "gx_head_fx:out_0"
GX_FX_OUT_1 = "gx_head_fx:out_1"

MAX_VOLUME = 150
GUITARIX_PORT_TIMEOUT = 30.0
GUITARIX_PORT_INTERVAL = 0.5
GUITARIX_STOP_TIMEOUT = 12.0
GUITARIX_STOP_INTERVAL = 0.3
RPC_WAIT_TIMEOUT = 12.0
RPC_WAIT_INTERVAL = 0.3

_TONE_PRESETS: dict[str, str] = {
    "clean": "clean_clean",
    "crunch": "crunch_vintage_vox",
    "army": "brett_jcm",
}
_TONE_PARAMS: dict[str, list[object]] = {
    "clean": [
        "gain.gain",
        4.0,
        "noise_gate.on_off",
        1,
        "noise_gate.threshold",
        0.017,
        "zita_rev1.output.dry_wet_mix",
        -0.5,
    ],
    "army": [
        "gain.gain",
        6.0,
        "noise_gate.on_off",
        1,
        "noise_gate.threshold",
        0.08,
        "zita_rev1.on_off",
        0,
        "zita_rev1.output.dry_wet_mix",
        -1.0,
        "amp.tonestack.Treble",
        0.35,
        "amp.tonestack.Middle",
        0.65,
        "amp.tonestack.Bass",
        0.55,
        "cab.treble",
        0.0,
        "cab.bass",
        0.0,
    ],
    "crunch": ["noise_gate.on_off", 1],
}


def _rpc_port() -> int:
    raw = os.environ.get("GITAR_RPC_PORT")
    if raw is None:
        return DEFAULT_RPC_PORT
    try:
        return int(raw)
    except ValueError:
        return DEFAULT_RPC_PORT


def channel_index(channel: str) -> int:
    """Map a channel name to its index in a multi-channel capture."""
    if channel.endswith("_FR"):
        return 1
    match = re.search(r"_AUX(\d+)$", channel)
    if match is not None:
        return int(match.group(1))
    return 0


def pick_stereo_ports(ports: list[str]) -> tuple[str, str]:
    """Pick left/right output ports, falling back to position then mono."""
    if not ports:
        return ("", "")
    left = next((port for port in ports if port.endswith("_FL")), "")
    right = next((port for port in ports if port.endswith("_FR")), "")
    if not left:
        left = ports[0]
    if not right and len(ports) > 1:
        right = ports[1]
    if not right:
        right = left
    return (left, right)


def _exec(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, capture_output=True, text=True, check=False)


def _link(source: str, target: str) -> bool:
    return _exec(["pw-link", source, target]).returncode == 0


def _unlink(source: str, target: str) -> bool:
    return _exec(["pw-link", "-d", source, target]).returncode == 0


def _guitarix_running() -> bool:
    return _exec(["pgrep", "-x", "guitarix"]).returncode == 0


def _run_dir() -> Path:
    return Path(os.environ.get("XDG_RUNTIME_DIR") or tempfile.gettempdir())


def _start_guitarix(config: Config, *, rpc: bool = False) -> None:
    if _guitarix_running():
        return
    args: list[str] = []
    if shutil.which("pw-jack") is not None:
        args.append("pw-jack")
    args += ["guitarix", "-J"]
    if rpc:
        args += ["-p", str(_rpc_port())]
    env = {**os.environ, "PIPEWIRE_LATENCY": config.latency}
    log_path = _run_dir() / "gitar-guitarix.log"
    log = log_path.open("ab")
    try:
        subprocess.Popen(
            args,
            stdout=log,
            stderr=subprocess.STDOUT,
            env=env,
            start_new_session=True,
        )
    finally:
        log.close()


def _stop_guitarix() -> None:
    if not _guitarix_running():
        return
    _exec(["pkill", "-x", "guitarix"])
    attempts = max(1, int(GUITARIX_STOP_TIMEOUT / GUITARIX_STOP_INTERVAL))
    for _ in range(attempts):
        if not _guitarix_running():
            return
        _sleep(GUITARIX_STOP_INTERVAL)


def _has_port(listing: str, port: str) -> bool:
    return any(line.strip() == port for line in listing.splitlines())


def _wait_for_ports(
    timeout: float = GUITARIX_PORT_TIMEOUT, interval: float = GUITARIX_PORT_INTERVAL
) -> bool:
    attempts = max(1, int(timeout / interval))
    for _ in range(attempts):
        inputs = devices._run(["pw-link", "-i"])
        outputs = devices._run(["pw-link", "-o"])
        if _has_port(inputs, GX_AMP_IN) and _has_port(outputs, GX_FX_OUT_1):
            return True
        _sleep(interval)
    return False


def _rpc_open(port: int) -> bool:
    try:
        connection = socket.create_connection(("127.0.0.1", port), timeout=0.5)
    except OSError:
        return False
    connection.close()
    return True


def _wait_for_rpc(
    port: int, timeout: float = RPC_WAIT_TIMEOUT, interval: float = RPC_WAIT_INTERVAL
) -> bool:
    attempts = max(1, int(timeout / interval))
    for _ in range(attempts):
        if _rpc_open(port):
            return True
        _sleep(interval)
    return False


def _rpc(port: int, method: str, params: list[object]) -> None:
    payload = json.dumps({"jsonrpc": "2.0", "method": method, "params": params}) + "\n"
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.sendall(payload.encode())


def _sleep(seconds: float) -> None:
    time.sleep(seconds)


def _parse_links(text: str) -> dict[str, list[str]]:
    links: dict[str, list[str]] = {}
    current: str | None = None
    for raw_line in text.splitlines():
        if raw_line[:1].isspace():
            if current is not None and "|->" in raw_line:
                target = raw_line.split("|->", 1)[1].strip()
                if target:
                    links[current].append(target)
            continue
        if not raw_line.strip():
            continue
        current = raw_line.strip()
        links.setdefault(current, [])
    return links


def _read_volume(sink: str) -> str | None:
    if not sink:
        return None
    match = re.search(r"(\d+)%", _exec(["pactl", "get-sink-volume", sink]).stdout)
    if match is None:
        return None
    return f"{match.group(1)}%"


def _record_wav(config: Config, channels: int, seconds: float, path: Path) -> None:
    _exec(
        [
            "timeout",
            f"{seconds:g}",
            "pw-record",
            "--target",
            config.input,
            "--channels",
            str(channels),
            "--format",
            "s16",
            "--rate",
            "48000",
            str(path),
        ]
    )


def _validate_route(config: Config) -> tuple[list[str], list[str]]:
    source = f"{config.input}:{config.channel}"
    capture = devices.capture_ports(config.input)
    if source not in capture:
        raise BackendError(f"guitar input not found: {source}")
    playback = devices.playback_ports(config.output)
    if not playback:
        raise BackendError(f"playback output not found: {config.output}")
    return capture, playback


def _unlink_direct(config: Config) -> None:
    if not config.input or not config.output:
        return
    source = f"{config.input}:{config.channel}"
    for port in devices.playback_ports(config.output):
        _unlink(source, port)


def _wire_amp(config: Config, playback: list[str]) -> None:
    source = f"{config.input}:{config.channel}"
    left, right = pick_stereo_ports(playback)
    _link(source, GX_AMP_IN)
    _link(GX_AMP_OUT, GX_FX_IN)
    _link(GX_FX_OUT_0, left)
    _link(GX_FX_OUT_1, right)


class PipeWireBackend(AudioBackend):
    name = "pipewire"

    def is_available(self) -> bool:
        return shutil.which("pw-link") is not None and shutil.which("pactl") is not None

    def connect_direct(self, config: Config) -> None:
        _, playback = _validate_route(config)
        source = f"{config.input}:{config.channel}"
        _stop_guitarix()
        _unlink_direct(config)
        for port in sorted({*pick_stereo_ports(playback)}):
            _link(source, port)

    def connect_amp(self, config: Config) -> None:
        _, playback = _validate_route(config)
        _unlink_direct(config)
        _start_guitarix(config, rpc=False)
        if not _wait_for_ports():
            raise BackendError("Guitarix did not expose its ports in time")
        _wire_amp(config, playback)

    def disconnect(self, config: Config) -> None:
        _unlink_direct(config)
        _stop_guitarix()

    def set_volume(self, config: Config, percent: int) -> None:
        if isinstance(percent, bool) or not isinstance(percent, int):
            raise BackendError(f"volume must be an int, got {type(percent).__name__}")
        if percent < 0 or percent > MAX_VOLUME:
            raise BackendError(f"volume must be 0..{MAX_VOLUME}, got {percent}")
        if not config.output:
            raise BackendError("no output device configured")
        _exec(["pactl", "set-sink-volume", config.output, f"{percent}%"])

    def status(self, config: Config) -> BackendStatus:
        source = f"{config.input}:{config.channel}" if config.input else ""
        links = _parse_links(_exec(["pw-link", "-l"]).stdout)
        targets = links.get(source, []) if source else []
        engine_running = _guitarix_running()

        route = Route.OFF
        if source and engine_running and any(t.startswith("gx_head_") for t in targets):
            route = Route.AMP
        elif source and any(t in set(devices.playback_ports(config.output)) for t in targets):
            route = Route.DIRECT

        details = {
            "input": config.input,
            "channel": config.channel,
            "output": config.output,
            "latency": config.latency,
        }
        volume = _read_volume(config.output)
        if volume is not None:
            details["volume"] = volume

        return BackendStatus(
            route=route,
            connected=route is not Route.OFF,
            engine_running=engine_running,
            details=details,
        )

    def measure_level(self, config: Config, seconds: float = 10.0) -> LevelReading:
        channels = max(1, len(devices.capture_ports(config.input)))
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as handle:
            path = Path(handle.name)
        try:
            _record_wav(config, channels, seconds, path)
            return analyze_wav(path, channel_index(config.channel), channels)
        finally:
            path.unlink(missing_ok=True)

    def list_tones(self) -> list[str]:
        return ["army", "clean", "crunch"]

    def load_tone(self, config: Config, tone: str) -> None:
        preset = _TONE_PRESETS.get(tone)
        if preset is None:
            raise BackendError(f"unknown tone {tone!r}")
        _, playback = _validate_route(config)
        port = _rpc_port()

        _unlink_direct(config)
        _stop_guitarix()
        _start_guitarix(config, rpc=True)
        if not _wait_for_rpc(port):
            raise BackendError(f"Guitarix RPC port {port} did not open")
        _rpc(port, "setpreset", ["Sonnie_Tele", preset])
        _sleep(0.5)
        _rpc(port, "set", _TONE_PARAMS[tone])
        _sleep(1.0)
        _stop_guitarix()
        _start_guitarix(config, rpc=False)
        if not _wait_for_ports():
            raise BackendError("Guitarix did not expose its ports in time")
        _wire_amp(config, playback)
