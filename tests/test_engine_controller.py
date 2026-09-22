"""Tests for the engine process controller.

A scripted in-process TCP server speaks the control protocol so the controller
can be exercised without starting a real audio process.
"""

from __future__ import annotations

import json
import shutil
import socketserver
import subprocess
import threading
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import pytest

from gitar_server import engine_controller
from gitar_server.engine_client import EngineClient, EngineError
from gitar_server.engine_controller import EngineController


@dataclass
class _State:
    requests: list[dict[str, Any]] = field(default_factory=list)

    def result(self, request: dict[str, Any]) -> dict[str, Any]:
        method = request.get("method")
        params = request.get("params") or {}
        if method == "status":
            return {"running": True, "gain": 1.0}
        if method == "list_devices":
            return {
                "devices": [
                    {"name": "guitar", "is_input": True, "is_output": False, "is_default": True},
                    {
                        "name": "headphones",
                        "is_input": False,
                        "is_output": True,
                        "is_default": False,
                    },
                ]
            }
        if method == "stop":
            return {"running": False}
        if method == "start":
            return {"running": True, "params": params}
        if method == "set_gain":
            return {"gain": params.get("gain")}
        if method == "load_model":
            return {"running": True, "model_path": params.get("path")}
        if method == "clear_model":
            return {"running": True, "model_path": ""}
        if method == "set_gate":
            return {"running": True, "gate": params}
        return {"ok": True}


class _Handler(socketserver.StreamRequestHandler):
    server: _Server

    def handle(self) -> None:
        for raw_line in self.rfile:
            request = json.loads(raw_line)
            self.server.state.requests.append(request)
            response = {
                "jsonrpc": "2.0",
                "id": request.get("id"),
                "result": self.server.state.result(request),
            }
            self.wfile.write(json.dumps(response).encode("utf-8") + b"\n")
            self.wfile.flush()
            if request.get("method") == "quit":
                return


class _Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    state: _State

    def __init__(self, state: _State) -> None:
        super().__init__(("127.0.0.1", 0), _Handler)
        self.state = state


@dataclass
class _Engine:
    host: str
    port: int
    state: _State


@pytest.fixture
def engine_server() -> Iterator[_Engine]:
    state = _State()
    server = _Server(state)
    thread = threading.Thread(
        target=server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True
    )
    thread.start()
    address = server.socket.getsockname()
    engine = _Engine(host=str(address[0]), port=int(address[1]), state=state)
    try:
        yield engine
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2.0)


def _controller(engine: _Engine) -> EngineController:
    client = EngineClient(engine.host, engine.port, timeout=2.0)
    return EngineController(host=engine.host, port=engine.port, client=client, spawn=False)


def test_status_connects_and_delegates(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    assert controller.status() == {"running": True, "gain": 1.0}
    assert engine_server.state.requests[-1]["method"] == "status"
    assert controller.running is True

    controller.shutdown()


def test_list_devices_delegates(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    devices = controller.list_devices()

    assert devices == [
        {"name": "guitar", "is_input": True, "is_output": False, "is_default": True},
        {"name": "headphones", "is_input": False, "is_output": True, "is_default": False},
    ]
    assert engine_server.state.requests[-1]["method"] == "list_devices"

    controller.shutdown()


def test_start_forwards_only_provided_params(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    result = controller.start(input_device="guitar", gain=0.5)

    assert result["params"] == {"input_device": "guitar", "gain": 0.5}
    assert engine_server.state.requests[-1]["params"] == {"input_device": "guitar", "gain": 0.5}

    controller.shutdown()


def test_start_delegates_to_engine_client(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    controller.start(output_device="headphones")

    assert engine_server.state.requests[-1]["params"] == {"output_device": "headphones"}

    controller.shutdown()


def test_set_gain_sends_gain(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    assert controller.set_gain(0.25) == {"gain": 0.25}
    assert engine_server.state.requests[-1]["params"] == {"gain": 0.25}

    controller.shutdown()


def test_load_and_clear_model(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    assert controller.load_model("/models/amp.nam")["model_path"] == "/models/amp.nam"
    assert controller.clear_model()["model_path"] == ""

    controller.shutdown()


def test_set_gate_sends_only_provided_keys(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    controller.set_gate(enabled=True)
    assert engine_server.state.requests[-1]["params"] == {"enabled": True}

    controller.set_gate(threshold_db=-40.0)
    assert engine_server.state.requests[-1]["params"] == {"threshold_db": -40.0}

    controller.shutdown()


def test_set_gate_requires_a_field(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    with pytest.raises(ValueError):
        controller.set_gate()

    controller.shutdown()


def test_stop_delegates(engine_server: _Engine) -> None:
    controller = _controller(engine_server)

    assert controller.stop()["running"] is False
    assert engine_server.state.requests[-1]["method"] == "stop"

    controller.shutdown()


def test_ensure_process_without_binary_raises(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITAR_ENGINE_BIN", raising=False)
    monkeypatch.setattr(shutil, "which", lambda name: None)
    monkeypatch.setattr(engine_controller, "_DEV_BINARY", Path("/nonexistent/gitar-engine"))
    controller = EngineController()

    with pytest.raises(EngineError):
        controller.ensure_process()


def test_ensure_process_spawns_and_connects(
    monkeypatch: pytest.MonkeyPatch, engine_server: _Engine
) -> None:
    spawned: list[list[str]] = []

    class _Process:
        def __init__(self, args: list[str], **kwargs: object) -> None:
            spawned.append(list(args))
            self.returncode: int | None = None

        def poll(self) -> int | None:
            return self.returncode

        def terminate(self) -> None:
            self.returncode = 0

        def kill(self) -> None:
            self.returncode = -9

        def wait(self, timeout: float | None = None) -> int | None:
            return self.returncode

        def communicate(self, timeout: float | None = None) -> tuple[str, str]:
            return ("", "")

    monkeypatch.setattr(subprocess, "Popen", _Process)
    controller = EngineController(
        binary="/fake/gitar-engine",
        host=engine_server.host,
        port=engine_server.port,
        start_timeout=2.0,
    )

    controller.ensure_process()

    assert spawned == [["/fake/gitar-engine", "run", "--control-port", str(engine_server.port)]]
    assert controller.running is True

    controller.shutdown()


def test_shutdown_is_idempotent(engine_server: _Engine) -> None:
    controller = _controller(engine_server)
    controller.status()

    controller.shutdown()
    controller.shutdown()

    assert controller.running is False


def test_binary_resolution_prefers_explicit_argument(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_ENGINE_BIN", "/env/gitar-engine")

    controller = EngineController(binary="/explicit/gitar-engine")

    assert controller.binary == "/explicit/gitar-engine"
    assert controller.is_available()


def test_binary_resolution_uses_env_override(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_ENGINE_BIN", "/env/gitar-engine")

    assert EngineController().binary == "/env/gitar-engine"


def test_binary_resolution_uses_path_lookup(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITAR_ENGINE_BIN", raising=False)
    monkeypatch.setattr(shutil, "which", lambda name: "/path/gitar-engine")

    assert EngineController().binary == "/path/gitar-engine"


def test_binary_resolution_falls_back_to_dev_build(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv("GITAR_ENGINE_BIN", raising=False)
    monkeypatch.setattr(shutil, "which", lambda name: None)
    dev = tmp_path / "gitar-engine"
    dev.write_text("", encoding="utf-8")
    monkeypatch.setattr(engine_controller, "_DEV_BINARY", dev)

    assert EngineController().binary == str(dev)


def test_is_available_false_without_binary(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITAR_ENGINE_BIN", raising=False)
    monkeypatch.setattr(shutil, "which", lambda name: None)
    monkeypatch.setattr(engine_controller, "_DEV_BINARY", Path("/nonexistent/gitar-engine"))

    controller = EngineController()

    assert controller.binary is None
    assert controller.is_available() is False
