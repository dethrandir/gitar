"""Tests for PipeWire/PulseAudio device discovery helpers."""

import os
import subprocess

import pytest

from gitar_server import devices

SOURCES_FIXTURE = """\
Source #50
\tState: SUSPENDED
\tName: alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
\tDescription: Monitor of Built-in Audio Analog Stereo
\tDriver: PipeWire
\tVolume: front-left: 65536 / 100% / 0.00 dB
\tPorts:
\t\tanalog-input-mic: Microphone (type: Mic)
\tActive Port: analog-input-mic

Source #51
\tState: RUNNING
\tName: alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo
\tVolume: front-left: 32768 / 50%
\tDescription: Focusrite Scarlett Solo USB Analog Stereo
\tFlags: HARDWARE

Source #52
\tState: SUSPENDED
\tName: alsa_input.usb-Mystery_Device
\tVolume: front-left: 65536 / 100%
"""

SINKS_FIXTURE = """\
Sink #60
\tState: RUNNING
\tName: alsa_output.pci-0000_00_1f.3.analog-stereo
\tDescription: Built-in Audio Analog Stereo
\tDriver: PipeWire

Sink #61
\tState: SUSPENDED
\tName: bluez_output.AC_12_2F_00_00_00.a2dp-sink
\tDescription: WH-1000XM4
"""

PW_LINK_OUTPUT_FIXTURE = """\
alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo:capture_AUX0
alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo:capture_AUX1
alsa_output.pci-0000_00_1f.3.analog-stereo:playback_FL
some-other:capture_XX
"""

PW_LINK_INPUT_FIXTURE = """\
alsa_output.pci-0000_00_1f.3.analog-stereo:playback_FL
alsa_output.pci-0000_00_1f.3.analog-stereo:playback_FR
alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo:capture_AUX0
unrelated:playback_ZZ
"""


class FakeRun:
    def __init__(self, stdout: str = "", returncode: int = 0) -> None:
        self.stdout = stdout
        self.returncode = returncode
        self.calls: list[tuple[list[str], dict[str, object]]] = []

    def __call__(self, args: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        self.calls.append((args, kwargs))
        return subprocess.CompletedProcess(
            args="", returncode=self.returncode, stdout=self.stdout, stderr=""
        )


def test_parse_pactl_nodes_pairs_name_and_description() -> None:
    result = devices.parse_pactl_nodes(SOURCES_FIXTURE, "source")
    assert result == [
        devices.Device(
            name="alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo",
            description="Focusrite Scarlett Solo USB Analog Stereo",
            kind="source",
        )
    ]


def test_parse_pactl_nodes_excludes_monitor_sources() -> None:
    names = [device.name for device in devices.parse_pactl_nodes(SOURCES_FIXTURE, "source")]
    assert "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor" not in names


def test_parse_pactl_nodes_skips_name_without_description() -> None:
    names = [device.name for device in devices.parse_pactl_nodes(SOURCES_FIXTURE, "source")]
    assert "alsa_input.usb-Mystery_Device" not in names


def test_parse_pactl_nodes_marks_kind() -> None:
    result = devices.parse_pactl_nodes(SINKS_FIXTURE, "sink")
    assert [device.kind for device in result] == ["sink", "sink"]
    assert result[1].name == "bluez_output.AC_12_2F_00_00_00.a2dp-sink"
    assert result[1].description == "WH-1000XM4"


def test_parse_pactl_nodes_handles_crlf() -> None:
    crlf = SOURCES_FIXTURE.replace("\n", "\r\n")
    assert devices.parse_pactl_nodes(crlf, "source") == devices.parse_pactl_nodes(
        SOURCES_FIXTURE, "source"
    )


def test_parse_pactl_nodes_ignores_description_before_name() -> None:
    text = "Description: orphan\nName: real\nDescription: kept\n"
    assert devices.parse_pactl_nodes(text, "source") == [
        devices.Device(name="real", description="kept", kind="source")
    ]


def test_parse_pactl_nodes_empty_text() -> None:
    assert devices.parse_pactl_nodes("", "source") == []


def test_parse_ports_filters_by_device_and_prefix() -> None:
    ports = devices.parse_ports(
        PW_LINK_OUTPUT_FIXTURE,
        "alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo",
        "capture_",
    )
    assert ports == [
        "alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo:capture_AUX0",
        "alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo:capture_AUX1",
    ]


def test_parse_ports_ignores_blank_lines() -> None:
    text = "\nalsa_output.pci:playback_FL\n\n  \n"
    assert devices.parse_ports(text, "alsa_output.pci", "playback_") == [
        "alsa_output.pci:playback_FL"
    ]


def test_list_devices_runs_pactl_with_c_locale(monkeypatch: pytest.MonkeyPatch) -> None:
    fake = FakeRun(stdout=SOURCES_FIXTURE)
    monkeypatch.setattr("gitar_server.devices.subprocess.run", fake)
    result = devices.list_devices("source")
    assert result == devices.parse_pactl_nodes(SOURCES_FIXTURE, "source")
    args, kwargs = fake.calls[0]
    assert args == ["pactl", "list", "sources"]
    assert kwargs["env"] == {**os.environ, "LC_ALL": "C"}
    assert kwargs["capture_output"] is True
    assert kwargs["text"] is True
    assert kwargs["check"] is False


def test_list_devices_returns_empty_on_missing_binary(monkeypatch: pytest.MonkeyPatch) -> None:
    def raise_fnf(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
        raise FileNotFoundError

    monkeypatch.setattr("gitar_server.devices.subprocess.run", raise_fnf)
    assert devices.list_devices("sink") == []


def test_list_devices_returns_empty_on_nonzero_exit(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        "gitar_server.devices.subprocess.run", FakeRun(stdout=SINKS_FIXTURE, returncode=1)
    )
    assert devices.list_devices("sink") == []


def test_list_sources_and_sinks_use_correct_kind(monkeypatch: pytest.MonkeyPatch) -> None:
    fake = FakeRun(stdout=SOURCES_FIXTURE)
    monkeypatch.setattr("gitar_server.devices.subprocess.run", fake)
    assert devices.list_sources() == devices.parse_pactl_nodes(SOURCES_FIXTURE, "source")
    assert fake.calls[0][0] == ["pactl", "list", "sources"]

    fake_sink = FakeRun(stdout=SINKS_FIXTURE)
    monkeypatch.setattr("gitar_server.devices.subprocess.run", fake_sink)
    assert devices.list_sinks() == devices.parse_pactl_nodes(SINKS_FIXTURE, "sink")
    assert fake_sink.calls[0][0] == ["pactl", "list", "sinks"]


def test_default_sink_strips_stdout(monkeypatch: pytest.MonkeyPatch) -> None:
    fake = FakeRun(stdout="alsa_output.pci-0000_00_1f.3.analog-stereo\n")
    monkeypatch.setattr("gitar_server.devices.subprocess.run", fake)
    assert devices.default_sink() == "alsa_output.pci-0000_00_1f.3.analog-stereo"
    assert fake.calls[0][0] == ["pactl", "get-default-sink"]


def test_default_sink_returns_empty_on_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    def raise_fnf(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
        raise FileNotFoundError

    monkeypatch.setattr("gitar_server.devices.subprocess.run", raise_fnf)
    assert devices.default_sink() == ""


def test_capture_and_playback_ports(monkeypatch: pytest.MonkeyPatch) -> None:
    src = "alsa_input.usb-Focusrite_Scarlett_Solo_USB.analog-stereo"
    sink = "alsa_output.pci-0000_00_1f.3.analog-stereo"

    fake_out = FakeRun(stdout=PW_LINK_OUTPUT_FIXTURE)
    monkeypatch.setattr("gitar_server.devices.subprocess.run", fake_out)
    assert devices.capture_ports(src) == devices.parse_ports(
        PW_LINK_OUTPUT_FIXTURE, src, "capture_"
    )
    assert fake_out.calls[0][0] == ["pw-link", "-o"]

    fake_in = FakeRun(stdout=PW_LINK_INPUT_FIXTURE)
    monkeypatch.setattr("gitar_server.devices.subprocess.run", fake_in)
    assert devices.playback_ports(sink) == devices.parse_ports(
        PW_LINK_INPUT_FIXTURE, sink, "playback_"
    )
    assert fake_in.calls[0][0] == ["pw-link", "-i"]
