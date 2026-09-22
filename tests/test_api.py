"""Tests for the gitar HTTP/WebSocket control API."""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path
from typing import cast

import pytest
from fastapi.testclient import TestClient

from gitar_server import __version__, devices
from gitar_server.api import create_app
from gitar_server.api import engine as engine_module
from gitar_server.backends import LevelReading, NullBackend
from gitar_server.config import Config
from gitar_server.engine_client import EngineError
from gitar_server.engine_controller import EngineController
from gitar_server.models import ModelInfo

TONES = ["army", "clean", "crunch"]


@pytest.fixture()
def client(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Iterator[TestClient]:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    monkeypatch.setenv("GITAR_WS_INTERVAL", "0.01")
    app = create_app(NullBackend())
    with TestClient(app) as test_client:
        yield test_client


def test_health_reports_version_and_backend(client: TestClient) -> None:
    response = client.get("/api/health")
    assert response.status_code == 200
    body = response.json()
    assert body == {
        "name": "gitar",
        "version": __version__,
        "backend": "null",
        "backend_available": True,
    }


def test_config_get_returns_defaults(client: TestClient) -> None:
    response = client.get("/api/config")
    assert response.status_code == 200
    assert response.json() == Config().model_dump()


def test_config_put_persists(client: TestClient) -> None:
    payload = Config(
        input="Mic",
        channel="2",
        output="Headphones",
        latency="256/48000",
        backend="null",
    ).model_dump()
    response = client.put("/api/config", json=payload)
    assert response.status_code == 200
    assert response.json() == payload
    assert client.get("/api/config").json() == payload


def test_config_put_rejects_invalid_latency(client: TestClient) -> None:
    payload = Config().model_dump() | {"latency": "fast"}
    response = client.put("/api/config", json=payload)
    assert response.status_code == 422


def test_devices_sources(client: TestClient, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(devices, "list_sources", lambda: [devices.Device("in1", "Mic", "source")])
    monkeypatch.setattr(devices, "default_sink", lambda: "speakers")
    response = client.get("/api/devices")
    assert response.status_code == 200
    assert response.json() == {
        "devices": [{"name": "in1", "description": "Mic", "kind": "source"}],
        "default_sink": "speakers",
    }


def test_devices_sinks(client: TestClient, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(devices, "list_sinks", lambda: [devices.Device("out1", "HP", "sink")])
    monkeypatch.setattr(devices, "default_sink", lambda: "speakers")
    response = client.get("/api/devices?kind=sink")
    assert response.status_code == 200
    assert response.json() == {
        "devices": [{"name": "out1", "description": "HP", "kind": "sink"}],
        "default_sink": "speakers",
    }


def test_devices_rejects_unknown_kind(client: TestClient) -> None:
    response = client.get("/api/devices?kind=bogus")
    assert response.status_code == 422


def test_ports_source_strips_device_prefix(
    client: TestClient, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        devices,
        "capture_ports",
        lambda device: [f"{device}:capture_AUX0", f"{device}:capture_AUX1"],
    )
    response = client.get("/api/ports?device=in1")
    assert response.status_code == 200
    assert response.json() == {"ports": ["capture_AUX0", "capture_AUX1"]}


def test_ports_sink_uses_playback_ports(
    client: TestClient, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        devices,
        "playback_ports",
        lambda device: [f"{device}:playback_FL", f"{device}:playback_FR"],
    )
    response = client.get("/api/ports?device=out1&kind=sink")
    assert response.status_code == 200
    assert response.json() == {"ports": ["playback_FL", "playback_FR"]}


def test_ports_requires_device(client: TestClient) -> None:
    response = client.get("/api/ports")
    assert response.status_code == 422


def test_ports_rejects_unknown_kind(client: TestClient) -> None:
    response = client.get("/api/ports?device=in1&kind=bogus")
    assert response.status_code == 422


def test_connect_direct_updates_status(client: TestClient) -> None:
    response = client.post("/api/connect", json={"mode": "direct"})
    assert response.status_code == 200
    assert response.json()["route"] == "direct"
    status = client.get("/api/status").json()
    assert status["route"] == "direct"
    assert status["connected"] is True
    assert status["engine_running"] is False


def test_connect_amp_updates_status(client: TestClient) -> None:
    response = client.post("/api/connect", json={"mode": "amp"})
    assert response.status_code == 200
    assert response.json()["route"] == "amp"
    status = client.get("/api/status").json()
    assert status["route"] == "amp"
    assert status["engine_running"] is True


def test_connect_rejects_unknown_mode(client: TestClient) -> None:
    response = client.post("/api/connect", json={"mode": "bogus"})
    assert response.status_code == 422


def test_disconnect_resets_to_off(client: TestClient) -> None:
    client.post("/api/connect", json={"mode": "amp"})
    response = client.post("/api/disconnect")
    assert response.status_code == 200
    assert response.json()["route"] == "off"
    assert client.get("/api/status").json()["route"] == "off"


def test_volume_ok(client: TestClient) -> None:
    response = client.post("/api/volume", json={"percent": 42})
    assert response.status_code == 200
    assert response.json() == {"percent": 42}


def test_volume_out_of_range_is_conflict(client: TestClient) -> None:
    response = client.post("/api/volume", json={"percent": 200})
    assert response.status_code == 409
    assert "detail" in response.json()


def test_tones_list(client: TestClient) -> None:
    response = client.get("/api/tones")
    assert response.status_code == 200
    assert response.json() == {"tones": TONES}


def test_tone_load_valid(client: TestClient) -> None:
    response = client.post("/api/tone", json={"tone": "clean"})
    assert response.status_code == 200
    assert response.json() == {"tone": "clean"}


def test_tone_load_unknown_is_conflict(client: TestClient) -> None:
    response = client.post("/api/tone", json={"tone": "nope"})
    assert response.status_code == 409
    assert "detail" in response.json()


class _FixedLevelBackend(NullBackend):
    def measure_level(self, config: Config, seconds: float = 10.0) -> LevelReading:
        return LevelReading(peak_dbfs=-6.0, advice="Good level.")


def test_meter_returns_reading(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    app = create_app(_FixedLevelBackend())
    with TestClient(app) as test_client:
        response = test_client.post("/api/meter", json={"seconds": 1.5})
    assert response.status_code == 200
    assert response.json() == {"peak_dbfs": -6.0, "advice": "Good level."}


def test_meter_rejects_invalid_seconds(client: TestClient) -> None:
    assert client.post("/api/meter", json={"seconds": 0}).status_code == 422
    assert client.post("/api/meter", json={"seconds": 61}).status_code == 422


def test_websocket_sends_status_and_health(client: TestClient) -> None:
    with client.websocket_connect("/api/ws") as websocket:
        message = websocket.receive_json()
    assert message["status"]["route"] == "off"
    assert message["health"]["name"] == "gitar"


def test_create_app_with_missing_web_dir(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    app = create_app(NullBackend(), web_dir=tmp_path / "missing")
    with TestClient(app) as test_client:
        assert test_client.get("/api/health").status_code == 200


def test_create_app_without_web_assets(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    app = create_app(NullBackend())
    with TestClient(app) as test_client:
        assert test_client.get("/api/health").status_code == 200


def test_root_serves_packaged_web_ui(client: TestClient) -> None:
    response = client.get("/")
    assert response.status_code == 200
    assert response.headers["content-type"].startswith("text/html")


class _FakeEngine:
    def __init__(self) -> None:
        self.available = True
        self.error: Exception | None = None
        self.calls: list[tuple[str, object]] = []
        self.status_payload: dict[str, object] = {"running": True, "gain": 1.0}
        self.devices: list[dict[str, object]] = []
        self.shutdown_calls = 0

    def _result(self) -> dict[str, object]:
        if self.error is not None:
            raise self.error
        return dict(self.status_payload)

    def is_available(self) -> bool:
        return self.available

    def status(self) -> dict[str, object]:
        self.calls.append(("status", None))
        return self._result()

    def list_devices(self) -> list[dict[str, object]]:
        self.calls.append(("list_devices", None))
        if self.error is not None:
            raise self.error
        return list(self.devices)

    def start(self, **params: object) -> dict[str, object]:
        self.calls.append(("start", params))
        return self._result()

    def stop(self) -> dict[str, object]:
        self.calls.append(("stop", None))
        return self._result()

    def load_model(self, path: str) -> dict[str, object]:
        self.calls.append(("load_model", path))
        return self._result()

    def clear_model(self) -> dict[str, object]:
        self.calls.append(("clear_model", None))
        return self._result()

    def set_gain(self, gain: float) -> dict[str, object]:
        self.calls.append(("set_gain", gain))
        return self._result()

    def set_gate(
        self, *, enabled: bool | None = None, threshold_db: float | None = None
    ) -> dict[str, object]:
        self.calls.append(("set_gate", (enabled, threshold_db)))
        return self._result()

    def set_eq(
        self,
        *,
        low_db: float | None = None,
        mid_db: float | None = None,
        high_db: float | None = None,
    ) -> dict[str, object]:
        self.calls.append(("set_eq", (low_db, mid_db, high_db)))
        return self._result()

    def load_cab(self, path: str) -> dict[str, object]:
        self.calls.append(("load_cab", path))
        return self._result()

    def set_metronome(
        self, *, enabled: bool | None = None, bpm: float | None = None
    ) -> dict[str, object]:
        self.calls.append(("set_metronome", (enabled, bpm)))
        return self._result()

    def start_recording(self, path: str) -> dict[str, object]:
        self.calls.append(("start_recording", path))
        return self._result()

    def stop_recording(self) -> dict[str, object]:
        self.calls.append(("stop_recording", None))
        return self._result()

    def shutdown(self) -> None:
        self.shutdown_calls += 1


@pytest.fixture()
def engine_app(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> Iterator[tuple[TestClient, _FakeEngine]]:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    fake = _FakeEngine()
    app = create_app(NullBackend(), engine=cast(EngineController, fake))
    with TestClient(app) as test_client:
        yield test_client, fake


def test_models_lists_scanned_models(
    engine_app: tuple[TestClient, _FakeEngine], monkeypatch: pytest.MonkeyPatch
) -> None:
    client, _ = engine_app
    model = ModelInfo(
        name="amp",
        path=Path("/models/amp.nam"),
        architecture="WaveNet",
        sample_rate=48000.0,
        size_bytes=1234,
    )
    monkeypatch.setattr(engine_module, "scan_models", lambda directory=None: [model])

    response = client.get("/api/models")

    assert response.status_code == 200
    assert response.json() == {
        "models": [
            {
                "name": "amp",
                "path": "/models/amp.nam",
                "architecture": "WaveNet",
                "sample_rate": 48000.0,
                "size_bytes": 1234,
            }
        ]
    }


def test_cabs_lists_scanned_cabs(
    engine_app: tuple[TestClient, _FakeEngine], monkeypatch: pytest.MonkeyPatch
) -> None:
    client, _ = engine_app
    monkeypatch.setattr(
        engine_module,
        "scan_cabs",
        lambda directory=None: [
            {"name": "marshal", "path": "/cabs/marshal.wav", "size_bytes": 1024}
        ],
    )

    response = client.get("/api/cabs")

    assert response.status_code == 200
    assert response.json() == {
        "cabs": [{"name": "marshal", "path": "/cabs/marshal.wav", "size_bytes": 1024}]
    }


def test_engine_devices_returns_list(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app
    fake.devices = [
        {"name": "guitar", "is_input": True, "is_output": False, "is_default": True},
        {"name": "headphones", "is_input": False, "is_output": True, "is_default": False},
    ]

    response = client.get("/api/engine/devices")

    assert response.status_code == 200
    assert response.json() == {
        "devices": [
            {"name": "guitar", "is_input": True, "is_output": False, "is_default": True},
            {"name": "headphones", "is_input": False, "is_output": True, "is_default": False},
        ]
    }
    assert fake.calls[-1] == ("list_devices", None)


def test_engine_devices_unavailable_is_503(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app
    fake.available = False

    response = client.get("/api/engine/devices")

    assert response.status_code == 503
    assert response.json()["detail"] == "gitar-engine binary not found"


def test_engine_status_returns_status(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.get("/api/engine/status")

    assert response.status_code == 200
    assert response.json() == {"running": True, "gain": 1.0}
    assert fake.calls[-1] == ("status", None)


def test_engine_status_unavailable_is_503(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app
    fake.available = False

    response = client.get("/api/engine/status")

    assert response.status_code == 503


def test_engine_start_forwards_params(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/start", json={"input_device": "guitar", "gain": 0.5})

    assert response.status_code == 200
    assert fake.calls[-1] == ("start", {"input_device": "guitar", "gain": 0.5})


def test_engine_start_with_empty_body(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/start", json={})

    assert response.status_code == 200
    assert fake.calls[-1] == ("start", {})


def test_engine_stop(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/stop")

    assert response.status_code == 200
    assert fake.calls[-1] == ("stop", None)


def test_engine_model_loads_path(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/model", json={"path": "/models/amp.nam"})

    assert response.status_code == 200
    assert fake.calls[-1] == ("load_model", "/models/amp.nam")


def test_engine_model_empty_path_clears(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/model", json={"path": ""})

    assert response.status_code == 200
    assert fake.calls[-1] == ("clear_model", None)


def test_engine_gain(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/gain", json={"gain": 0.75})

    assert response.status_code == 200
    assert fake.calls[-1] == ("set_gain", 0.75)


def test_engine_gate_requires_a_field(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.post("/api/engine/gate", json={})

    assert response.status_code == 422


def test_engine_gate_forwards_fields(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/gate", json={"enabled": True, "threshold_db": -30.0})

    assert response.status_code == 200
    assert fake.calls[-1] == ("set_gate", (True, -30.0))


def test_engine_eq_requires_a_field(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.post("/api/engine/eq", json={})

    assert response.status_code == 422


def test_engine_eq_forwards_fields(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/eq", json={"low_db": 3.0, "high_db": -4.5})

    assert response.status_code == 200
    assert fake.calls[-1] == ("set_eq", (3.0, None, -4.5))


def test_engine_cab_loads_path(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/cab", json={"path": "/cabs/marshal.wav"})

    assert response.status_code == 200
    assert fake.calls[-1] == ("load_cab", "/cabs/marshal.wav")


def test_engine_cab_empty_path_clears(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/cab", json={"path": ""})

    assert response.status_code == 200
    assert fake.calls[-1] == ("load_cab", "")


def test_engine_metronome_requires_a_field(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.post("/api/engine/metronome", json={})

    assert response.status_code == 422


def test_engine_metronome_forwards_fields(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/metronome", json={"enabled": True, "bpm": 90.0})

    assert response.status_code == 200
    assert fake.calls[-1] == ("set_metronome", (True, 90.0))


def test_engine_metronome_forwards_single_field(
    engine_app: tuple[TestClient, _FakeEngine],
) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/metronome", json={"bpm": 132.0})

    assert response.status_code == 200
    assert fake.calls[-1] == ("set_metronome", (None, 132.0))


def test_engine_record_starts_with_path(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/record", json={"path": "take1.wav"})

    assert response.status_code == 200
    assert fake.calls[-1] == ("start_recording", "take1.wav")


def test_engine_record_empty_path_stops(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/record", json={"path": ""})

    assert response.status_code == 200
    assert fake.calls[-1] == ("stop_recording", None)


def test_engine_record_stop_sentinel_stops(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app

    response = client.post("/api/engine/record", json={"path": "stop"})

    assert response.status_code == 200
    assert fake.calls[-1] == ("stop_recording", None)


def test_engine_start_forwards_metronome_params(
    engine_app: tuple[TestClient, _FakeEngine],
) -> None:
    client, fake = engine_app

    response = client.post(
        "/api/engine/start", json={"metronome_enabled": True, "metronome_bpm": 100.0}
    )

    assert response.status_code == 200
    assert fake.calls[-1] == ("start", {"metronome_enabled": True, "metronome_bpm": 100.0})


def test_engine_eq_error_maps_to_conflict(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app
    fake.error = EngineError(-32000, "boom")

    response = client.post("/api/engine/eq", json={"low_db": 1.0})

    assert response.status_code == 409
    assert response.json()["detail"] == "engine error -32000: boom"


def test_engine_error_maps_to_conflict(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app
    fake.error = EngineError(-32000, "boom")

    response = client.post("/api/engine/gain", json={"gain": 0.5})

    assert response.status_code == 409
    assert response.json()["detail"] == "engine error -32000: boom"


def test_engine_error_maps_to_503_when_binary_missing(
    engine_app: tuple[TestClient, _FakeEngine],
) -> None:
    client, fake = engine_app
    fake.available = False
    fake.error = EngineError(-32000, "gitar-engine binary not found")

    response = client.post("/api/engine/gain", json={"gain": 0.5})

    assert response.status_code == 503


def test_engine_shutdown_called_on_app_close(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    fake = _FakeEngine()
    app = create_app(NullBackend(), engine=cast(EngineController, fake))

    with TestClient(app):
        pass

    assert fake.shutdown_calls == 1


PRESET_BODY = {
    "name": "clean",
    "model_path": "/models/amp.nam",
    "cab_ir_path": "/cabs/marshal.wav",
    "gain": 0.7,
    "gate_enabled": False,
    "gate_threshold_db": -40.0,
    "eq_low_db": 2.0,
    "eq_mid_db": -1.0,
    "eq_high_db": 3.0,
}


def test_presets_list_starts_empty(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.get("/api/presets")

    assert response.status_code == 200
    assert response.json() == {"presets": []}


def test_presets_save_get_list_delete(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    saved = client.post("/api/presets", json=PRESET_BODY)
    assert saved.status_code == 200
    assert saved.json() == PRESET_BODY

    assert client.get("/api/presets").json() == {"presets": ["clean"]}
    assert client.get("/api/presets/clean").json() == PRESET_BODY

    deleted = client.delete("/api/presets/clean")
    assert deleted.status_code == 200
    assert deleted.json() == {"deleted": True}
    assert client.get("/api/presets").json() == {"presets": []}


def test_presets_delete_missing_returns_false(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.delete("/api/presets/nope")

    assert response.status_code == 200
    assert response.json() == {"deleted": False}


def test_presets_apply_forwards_to_engine(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app
    client.post("/api/presets", json=PRESET_BODY)
    fake.calls.clear()

    response = client.post("/api/presets/clean/apply")

    assert response.status_code == 200
    assert response.json() == {"running": True, "gain": 1.0}
    assert fake.calls == [
        ("load_model", "/models/amp.nam"),
        ("load_cab", "/cabs/marshal.wav"),
        ("set_gain", 0.7),
        ("set_gate", (False, -40.0)),
        ("set_eq", (2.0, -1.0, 3.0)),
        ("status", None),
    ]


def test_presets_apply_missing_is_404(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.post("/api/presets/nope/apply")

    assert response.status_code == 404


def test_presets_apply_unavailable_is_503(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, fake = engine_app
    client.post("/api/presets", json=PRESET_BODY)
    fake.available = False

    response = client.post("/api/presets/clean/apply")

    assert response.status_code == 503


def test_presets_get_missing_is_404(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.get("/api/presets/nope")

    assert response.status_code == 404


def test_presets_save_unsafe_name_is_400(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.post("/api/presets", json=PRESET_BODY | {"name": "../escape"})

    assert response.status_code == 400


def test_presets_get_unsafe_name_is_400(engine_app: tuple[TestClient, _FakeEngine]) -> None:
    client, _ = engine_app

    response = client.get("/api/presets/a%5Cb")

    assert response.status_code == 400
