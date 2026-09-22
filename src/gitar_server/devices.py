"""PipeWire/PulseAudio device and port discovery.

Discovery shells out to ``pactl`` and ``pw-link``. The control server must keep
running when those tools are missing, so all runners degrade to empty results
instead of raising.
"""

import os
import subprocess
from dataclasses import dataclass
from typing import Literal

DeviceKind = Literal["source", "sink"]


@dataclass(frozen=True)
class Device:
    name: str
    description: str
    kind: DeviceKind


def _run(args: list[str]) -> str:
    try:
        proc = subprocess.run(
            args,
            capture_output=True,
            text=True,
            check=False,
            env={**os.environ, "LC_ALL": "C"},
        )
    except FileNotFoundError:
        return ""
    if proc.returncode != 0:
        return ""
    return proc.stdout


def parse_pactl_nodes(text: str, kind: DeviceKind) -> list[Device]:
    devices: list[Device] = []
    pending: str | None = None
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line.startswith("Name: "):
            pending = line[len("Name: ") :]
        elif line.startswith("Description: ") and pending is not None:
            if kind == "source" and pending.endswith(".monitor"):
                pending = None
                continue
            devices.append(Device(pending, line[len("Description: ") :], kind))
            pending = None
    return devices


def list_devices(kind: DeviceKind) -> list[Device]:
    return parse_pactl_nodes(_run(["pactl", "list", f"{kind}s"]), kind)


def list_sources() -> list[Device]:
    return list_devices("source")


def list_sinks() -> list[Device]:
    return list_devices("sink")


def default_sink() -> str:
    return _run(["pactl", "get-default-sink"]).strip()


def parse_ports(text: str, device: str, prefix: str) -> list[str]:
    marker = f"{device}:{prefix}"
    return [line.strip() for line in text.splitlines() if line.strip().startswith(marker)]


def capture_ports(device: str) -> list[str]:
    return parse_ports(_run(["pw-link", "-o"]), device, "capture_")


def playback_ports(device: str) -> list[str]:
    return parse_ports(_run(["pw-link", "-i"]), device, "playback_")
