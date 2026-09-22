"""Tests for the gitar HTTP/WebSocket control API."""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from gitar_server import __version__, devices
from gitar_server.api import create_app
from gitar_server.backends import LevelReading, NullBackend
from gitar_server.config import Config

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
