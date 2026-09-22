"""Tests for the PipeWire backend, a port of the legacy bash CLI logic.

Every subprocess/socket seam is monkeypatched: these tests never launch
Guitarix, call pactl, or touch a sound card.
"""

from __future__ import annotations

import array
import shutil
import subprocess
import wave
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from gitar_server import devices, levels
from gitar_server.backends import get_backend, pipewire
from gitar_server.backends.base import BackendError, Route
from gitar_server.config import Config

SOURCE = "src:capture_FR"
LEFT = "sink:playback_FL"
RIGHT = "sink:playback_FR"

CLEAN_SET: tuple[object, ...] = (
    "gain.gain",
    4.0,
    "noise_gate.on_off",
    1,
    "noise_gate.threshold",
    0.017,
    "zita_rev1.output.dry_wet_mix",
    -0.5,
)
ARMY_SET: tuple[object, ...] = (
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
)
CRUNCH_SET: tuple[object, ...] = ("noise_gate.on_off", 1)


def make_config(
    source: str = "src",
    channel: str = "capture_FR",
    output: str = "sink",
) -> Config:
    return Config(input=source, channel=channel, output=output)


def capture_ports(device: str) -> list[str]:
    return [f"{device}:capture_FL", f"{device}:capture_FR"]


def capture_fl_only(device: str) -> list[str]:
    return [f"{device}:capture_FL"]


def no_capture_ports(device: str) -> list[str]:
    return []


def playback_ports(device: str) -> list[str]:
    return [f"{device}:playback_FL", f"{device}:playback_FR"]


def no_playback_ports(device: str) -> list[str]:
    return []


def completed(
    args: list[str], stdout: str = "", returncode: int = 0
) -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(args=args, returncode=returncode, stdout=stdout, stderr="")


def exec_returning(
    stdout: str, returncode: int = 0
) -> Callable[[list[str]], subprocess.CompletedProcess[str]]:
    def run(args: list[str]) -> subprocess.CompletedProcess[str]:
        return completed(args, stdout, returncode)

    return run


def running_true() -> bool:
    return True


def running_false() -> bool:
    return False


def volume_70(sink: str) -> str | None:
    return "70%"


def no_volume(sink: str) -> str | None:
    return None


def which_found(name: str) -> str:
    return f"/usr/bin/{name}"


def which_missing(name: str) -> None:
    return None


def which_no_pactl(name: str) -> str | None:
    return None if name == "pactl" else f"/usr/bin/{name}"


class Recorder:
    def __init__(self) -> None:
        self.calls: list[tuple[tuple[Any, ...], dict[str, Any]]] = []

    def __call__(self, *args: Any, **kwargs: Any) -> None:
        self.calls.append((args, kwargs))


def write_wav(path: Path, samples: list[int], channels: int) -> None:
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(channels)
        handle.setsampwidth(2)
        handle.setframerate(48000)
        handle.writeframes(array.array("h", samples).tobytes())


@pytest.mark.parametrize(
    ("channel", "expected"),
    [
        ("capture_FL", 0),
        ("capture_FR", 1),
        ("capture_AUX0", 0),
        ("capture_AUX3", 3),
        ("capture_AUX12", 12),
        ("weird", 0),
    ],
)
def test_channel_index(channel: str, expected: int) -> None:
    assert pipewire.channel_index(channel) == expected


def test_pick_stereo_ports_standard() -> None:
    assert pipewire.pick_stereo_ports([LEFT, RIGHT]) == (LEFT, RIGHT)


def test_pick_stereo_ports_searches_for_fl_and_fr_regardless_of_order() -> None:
    assert pipewire.pick_stereo_ports([RIGHT, "x:playback_1", LEFT]) == (LEFT, RIGHT)


def test_pick_stereo_ports_mono() -> None:
    assert pipewire.pick_stereo_ports(["sink:playback_MONO"]) == (
        "sink:playback_MONO",
        "sink:playback_MONO",
    )


def test_pick_stereo_ports_empty() -> None:
    assert pipewire.pick_stereo_ports([]) == ("", "")


def test_pick_stereo_ports_nonstandard_positions() -> None:
    assert pipewire.pick_stereo_ports(["a:playback_1", "a:playback_2"]) == (
        "a:playback_1",
        "a:playback_2",
    )


def test_pick_stereo_ports_only_left() -> None:
    assert pipewire.pick_stereo_ports([LEFT]) == (LEFT, LEFT)


def test_pick_stereo_ports_falls_back_positionally() -> None:
    assert pipewire.pick_stereo_ports(["a:playback_1", "b:playback_FR"]) == (
        "a:playback_1",
        "b:playback_FR",
    )


def test_rpc_port_default(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITAR_RPC_PORT", raising=False)
    assert pipewire._rpc_port() == pipewire.DEFAULT_RPC_PORT


def test_rpc_port_from_env(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_RPC_PORT", "7400")
    assert pipewire._rpc_port() == 7400


def test_rpc_port_bad_env_falls_back(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_RPC_PORT", "not-a-port")
    assert pipewire._rpc_port() == pipewire.DEFAULT_RPC_PORT


def test_parse_links_groups_targets() -> None:
    text = "src:capture_FR\n  |-> a\n  |-> b\nother\n"
    assert pipewire._parse_links(text) == {"src:capture_FR": ["a", "b"], "other": []}


def test_is_available_true(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "which", which_found)
    assert pipewire.PipeWireBackend().is_available() is True


def test_is_available_false_without_pw_link(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "which", which_missing)
    assert pipewire.PipeWireBackend().is_available() is False


def test_is_available_false_without_pactl(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "which", which_no_pactl)
    assert pipewire.PipeWireBackend().is_available() is False


def test_connect_direct_stops_engine_unlinks_then_links_stereo(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    events: list[tuple[object, ...]] = []

    def stop() -> None:
        events.append(("stop",))

    def unlink(a: str, b: str) -> bool:
        events.append(("unlink", a, b))
        return True

    def link(a: str, b: str) -> bool:
        events.append(("link", a, b))
        return True

    monkeypatch.setattr(pipewire, "_stop_guitarix", stop)
    monkeypatch.setattr(pipewire, "_unlink", unlink)
    monkeypatch.setattr(pipewire, "_link", link)
    monkeypatch.setattr(devices, "capture_ports", capture_ports)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    pipewire.PipeWireBackend().connect_direct(make_config())

    assert events == [
        ("stop",),
        ("unlink", SOURCE, LEFT),
        ("unlink", SOURCE, RIGHT),
        ("link", SOURCE, LEFT),
        ("link", SOURCE, RIGHT),
    ]


def test_connect_direct_rejects_unknown_channel(monkeypatch: pytest.MonkeyPatch) -> None:
    stop = Recorder()
    monkeypatch.setattr(pipewire, "_stop_guitarix", stop)
    monkeypatch.setattr(devices, "capture_ports", capture_fl_only)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    with pytest.raises(BackendError):
        pipewire.PipeWireBackend().connect_direct(make_config())
    assert stop.calls == []


def test_connect_direct_rejects_missing_output(monkeypatch: pytest.MonkeyPatch) -> None:
    stop = Recorder()
    monkeypatch.setattr(pipewire, "_stop_guitarix", stop)
    monkeypatch.setattr(devices, "capture_ports", capture_ports)
    monkeypatch.setattr(devices, "playback_ports", no_playback_ports)

    with pytest.raises(BackendError):
        pipewire.PipeWireBackend().connect_direct(make_config())
    assert stop.calls == []


def test_connect_amp_wires_chain_in_order(monkeypatch: pytest.MonkeyPatch) -> None:
    events: list[tuple[object, ...]] = []

    def unlink(a: str, b: str) -> bool:
        events.append(("unlink", a, b))
        return True

    def link(a: str, b: str) -> bool:
        events.append(("link", a, b))
        return True

    def start(config: Config, *, rpc: bool = False) -> None:
        events.append(("start", f"rpc={rpc}"))

    def wait() -> bool:
        events.append(("wait",))
        return True

    monkeypatch.setattr(pipewire, "_unlink", unlink)
    monkeypatch.setattr(pipewire, "_link", link)
    monkeypatch.setattr(pipewire, "_start_guitarix", start)
    monkeypatch.setattr(pipewire, "_wait_for_ports", wait)
    monkeypatch.setattr(devices, "capture_ports", capture_ports)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    pipewire.PipeWireBackend().connect_amp(make_config())

    assert events == [
        ("unlink", SOURCE, LEFT),
        ("unlink", SOURCE, RIGHT),
        ("start", "rpc=False"),
        ("wait",),
        ("link", SOURCE, "gx_head_amp:in_0"),
        ("link", "gx_head_amp:out_0", "gx_head_fx:in_0"),
        ("link", "gx_head_fx:out_0", LEFT),
        ("link", "gx_head_fx:out_1", RIGHT),
    ]


def test_connect_amp_raises_when_ports_never_appear(monkeypatch: pytest.MonkeyPatch) -> None:
    unlink = Recorder()
    link = Recorder()
    monkeypatch.setattr(pipewire, "_unlink", unlink)
    monkeypatch.setattr(pipewire, "_link", link)
    monkeypatch.setattr(pipewire, "_start_guitarix", Recorder())
    monkeypatch.setattr(pipewire, "_wait_for_ports", running_false)
    monkeypatch.setattr(devices, "capture_ports", capture_ports)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    with pytest.raises(BackendError):
        pipewire.PipeWireBackend().connect_amp(make_config())
    assert link.calls == []


def test_disconnect_tolerates_empty_config(monkeypatch: pytest.MonkeyPatch) -> None:
    stop = Recorder()
    unlink = Recorder()
    monkeypatch.setattr(pipewire, "_stop_guitarix", stop)
    monkeypatch.setattr(pipewire, "_unlink", unlink)

    pipewire.PipeWireBackend().disconnect(Config())

    assert unlink.calls == []
    assert len(stop.calls) == 1


def test_disconnect_unlinks_then_stops(monkeypatch: pytest.MonkeyPatch) -> None:
    events: list[tuple[object, ...]] = []

    def unlink(a: str, b: str) -> bool:
        events.append(("unlink", a, b))
        return True

    def stop() -> None:
        events.append(("stop",))

    monkeypatch.setattr(pipewire, "_unlink", unlink)
    monkeypatch.setattr(pipewire, "_stop_guitarix", stop)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    pipewire.PipeWireBackend().disconnect(make_config())

    assert events == [
        ("unlink", SOURCE, LEFT),
        ("unlink", SOURCE, RIGHT),
        ("stop",),
    ]


@pytest.mark.parametrize("percent", [0, 70, 150])
def test_set_volume_runs_pactl(monkeypatch: pytest.MonkeyPatch, percent: int) -> None:
    recorder = Recorder()
    monkeypatch.setattr(pipewire, "_exec", recorder)

    pipewire.PipeWireBackend().set_volume(make_config(), percent)

    assert recorder.calls == [
        ((["pactl", "set-sink-volume", "sink", f"{percent}%"],), {}),
    ]


@pytest.mark.parametrize("percent", [-1, 151, 1000])
def test_set_volume_rejects_out_of_range(monkeypatch: pytest.MonkeyPatch, percent: int) -> None:
    recorder = Recorder()
    monkeypatch.setattr(pipewire, "_exec", recorder)

    with pytest.raises(BackendError):
        pipewire.PipeWireBackend().set_volume(make_config(), percent)
    assert recorder.calls == []


def test_set_volume_rejects_non_int(monkeypatch: pytest.MonkeyPatch) -> None:
    recorder = Recorder()
    monkeypatch.setattr(pipewire, "_exec", recorder)
    backend = pipewire.PipeWireBackend()

    bad: Any = "70"
    with pytest.raises(BackendError):
        backend.set_volume(make_config(), bad)
    fractional: Any = 12.5
    with pytest.raises(BackendError):
        backend.set_volume(make_config(), fractional)
    with pytest.raises(BackendError):
        backend.set_volume(make_config(), True)
    assert recorder.calls == []


def test_set_volume_requires_output(monkeypatch: pytest.MonkeyPatch) -> None:
    recorder = Recorder()
    monkeypatch.setattr(pipewire, "_exec", recorder)

    with pytest.raises(BackendError):
        pipewire.PipeWireBackend().set_volume(make_config(output=""), 70)
    assert recorder.calls == []


def test_status_reports_amp(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pipewire, "_exec", exec_returning(f"{SOURCE}\n  |-> gx_head_amp:in_0\n"))
    monkeypatch.setattr(pipewire, "_guitarix_running", running_true)
    monkeypatch.setattr(pipewire, "_read_volume", volume_70)

    status = pipewire.PipeWireBackend().status(make_config())

    assert status.route is Route.AMP
    assert status.connected is True
    assert status.engine_running is True
    assert status.details == {
        "input": "src",
        "channel": "capture_FR",
        "output": "sink",
        "latency": "128/48000",
        "volume": "70%",
    }


def test_status_reports_direct(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pipewire, "_exec", exec_returning(f"{SOURCE}\n  |-> {LEFT}\n"))
    monkeypatch.setattr(pipewire, "_guitarix_running", running_false)
    monkeypatch.setattr(pipewire, "_read_volume", volume_70)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    status = pipewire.PipeWireBackend().status(make_config())

    assert status.route is Route.DIRECT
    assert status.connected is True
    assert status.engine_running is False


def test_status_reports_off(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pipewire, "_exec", exec_returning(""))
    monkeypatch.setattr(pipewire, "_guitarix_running", running_false)
    monkeypatch.setattr(pipewire, "_read_volume", volume_70)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    status = pipewire.PipeWireBackend().status(make_config())

    assert status.route is Route.OFF
    assert status.connected is False


def test_status_omits_volume_when_unreadable(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pipewire, "_exec", exec_returning(""))
    monkeypatch.setattr(pipewire, "_guitarix_running", running_false)
    monkeypatch.setattr(pipewire, "_read_volume", no_volume)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    status = pipewire.PipeWireBackend().status(make_config())

    assert "volume" not in status.details


def test_list_tones() -> None:
    assert pipewire.PipeWireBackend().list_tones() == ["army", "clean", "crunch"]


def run_load_tone(
    monkeypatch: pytest.MonkeyPatch, tone: str, *, rpc_reachable: bool = True
) -> list[tuple[object, ...]]:
    monkeypatch.setenv("GITAR_RPC_PORT", "7400")
    events: list[tuple[object, ...]] = []

    def unlink(a: str, b: str) -> bool:
        events.append(("unlink", a, b))
        return True

    def link(a: str, b: str) -> bool:
        events.append(("link", a, b))
        return True

    def stop() -> None:
        events.append(("stop",))

    def start(config: Config, *, rpc: bool = False) -> None:
        events.append(("start", f"rpc={rpc}"))

    def wait_rpc(port: int) -> bool:
        events.append(("wait_rpc", str(port)))
        return rpc_reachable

    def rpc(port: int, method: str, params: list[object]) -> None:
        events.append(("rpc", str(port), method, tuple(params)))

    def sleep(seconds: float) -> None:
        events.append(("sleep", f"{seconds:g}"))

    def wait_ports() -> bool:
        events.append(("wait_ports",))
        return True

    monkeypatch.setattr(pipewire, "_unlink", unlink)
    monkeypatch.setattr(pipewire, "_link", link)
    monkeypatch.setattr(pipewire, "_stop_guitarix", stop)
    monkeypatch.setattr(pipewire, "_start_guitarix", start)
    monkeypatch.setattr(pipewire, "_wait_for_rpc", wait_rpc)
    monkeypatch.setattr(pipewire, "_rpc", rpc)
    monkeypatch.setattr(pipewire, "_sleep", sleep)
    monkeypatch.setattr(pipewire, "_wait_for_ports", wait_ports)
    monkeypatch.setattr(devices, "capture_ports", capture_ports)
    monkeypatch.setattr(devices, "playback_ports", playback_ports)

    pipewire.PipeWireBackend().load_tone(make_config(), tone)
    return events


def test_load_tone_clean_full_sequence(monkeypatch: pytest.MonkeyPatch) -> None:
    events = run_load_tone(monkeypatch, "clean")
    assert events == [
        ("unlink", SOURCE, LEFT),
        ("unlink", SOURCE, RIGHT),
        ("stop",),
        ("start", "rpc=True"),
        ("wait_rpc", "7400"),
        ("rpc", "7400", "setpreset", ("Sonnie_Tele", "clean_clean")),
        ("sleep", "0.5"),
        ("rpc", "7400", "set", CLEAN_SET),
        ("sleep", "1"),
        ("stop",),
        ("start", "rpc=False"),
        ("wait_ports",),
        ("link", SOURCE, "gx_head_amp:in_0"),
        ("link", "gx_head_amp:out_0", "gx_head_fx:in_0"),
        ("link", "gx_head_fx:out_0", LEFT),
        ("link", "gx_head_fx:out_1", RIGHT),
    ]


def test_load_tone_crunch_preset_and_params(monkeypatch: pytest.MonkeyPatch) -> None:
    events = run_load_tone(monkeypatch, "crunch")
    assert ("rpc", "7400", "setpreset", ("Sonnie_Tele", "crunch_vintage_vox")) in events
    assert ("rpc", "7400", "set", CRUNCH_SET) in events


def test_load_tone_army_preset_and_params(monkeypatch: pytest.MonkeyPatch) -> None:
    events = run_load_tone(monkeypatch, "army")
    assert ("rpc", "7400", "setpreset", ("Sonnie_Tele", "brett_jcm")) in events
    assert ("rpc", "7400", "set", ARMY_SET) in events


def test_load_tone_rejects_unknown(monkeypatch: pytest.MonkeyPatch) -> None:
    start = Recorder()
    monkeypatch.setattr(pipewire, "_start_guitarix", start)

    with pytest.raises(BackendError):
        pipewire.PipeWireBackend().load_tone(make_config(), "nope")
    assert start.calls == []


def test_load_tone_raises_when_rpc_unreachable(monkeypatch: pytest.MonkeyPatch) -> None:
    with pytest.raises(BackendError):
        run_load_tone(monkeypatch, "clean", rpc_reachable=False)


def test_measure_level_records_and_analyzes(monkeypatch: pytest.MonkeyPatch) -> None:
    recorded: list[tuple[int, float, Path]] = []

    def record(config: Config, channels: int, seconds: float, path: Path) -> None:
        recorded.append((channels, seconds, path))
        write_wav(path, [0, 16384, 0, 0], channels=2)

    monkeypatch.setattr(pipewire, "_record_wav", record)
    monkeypatch.setattr(devices, "capture_ports", capture_ports)

    reading = pipewire.PipeWireBackend().measure_level(make_config(), seconds=10.0)

    assert len(recorded) == 1
    channels, seconds, path = recorded[0]
    assert channels == 2
    assert seconds == 10.0
    assert path.exists() is False
    assert reading.peak_dbfs == pytest.approx(levels.to_dbfs(16384))
    assert reading.advice == levels.dbfs_advice(reading.peak_dbfs)


def test_measure_level_defaults_to_one_channel(monkeypatch: pytest.MonkeyPatch) -> None:
    recorded: list[int] = []

    def record(config: Config, channels: int, seconds: float, path: Path) -> None:
        recorded.append(channels)
        write_wav(path, [1000], channels=1)

    monkeypatch.setattr(pipewire, "_record_wav", record)
    monkeypatch.setattr(devices, "capture_ports", no_capture_ports)

    reading = pipewire.PipeWireBackend().measure_level(make_config(channel="capture_MONO"))

    assert recorded == [1]
    assert reading.peak_dbfs == pytest.approx(levels.to_dbfs(1000))


def test_factory_returns_pipewire_on_linux() -> None:
    backend = get_backend("linux")
    assert isinstance(backend, pipewire.PipeWireBackend)
    assert backend.name == "pipewire"
