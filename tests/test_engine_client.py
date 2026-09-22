"""Tests for the engine control-protocol client.

A scripted in-process TCP server speaks the newline-delimited JSON protocol so
the client can be exercised without the native engine.
"""

from __future__ import annotations

import json
import socket
import socketserver
import threading
from collections.abc import Iterator
from dataclasses import dataclass, field
from typing import Any

import pytest

from gitar_server.engine_client import EngineClient, EngineError


@dataclass
class HandlerState:
    host: str = "127.0.0.1"
    port: int = 0
    requests: list[dict[str, Any]] = field(default_factory=list)
    error_code: int | None = None
    error_message: str = ""
    response_id: object = 0
    override_response_id: bool = False
    lock: threading.Lock = field(default_factory=threading.Lock)

    def respond(self, request: dict[str, Any]) -> dict[str, Any]:
        request_id = request.get("id")
        with self.lock:
            self.requests.append(request)
            if self.error_code is not None:
                return {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "error": {"code": self.error_code, "message": self.error_message},
                }
            response_id = self.response_id if self.override_response_id else request_id
        return {
            "jsonrpc": "2.0",
            "id": response_id,
            "result": self._result_for(request),
        }

    def _result_for(self, request: dict[str, Any]) -> dict[str, Any]:
        method = request.get("method")
        params = request.get("params") or {}
        if method == "ping":
            return {"pong": True}
        if method == "status":
            return {"running": False}
        if method == "list_devices":
            return {"devices": [{"name": "guitar"}, {"name": "headphones"}]}
        if method == "set_gain":
            return {"gain": params.get("gain")}
        if method == "start":
            return {"params": params}
        return {}


class _EngineHandler(socketserver.StreamRequestHandler):
    server: _EngineServer

    def handle(self) -> None:
        for raw_line in self.rfile:
            request = json.loads(raw_line)
            response = self.server.state.respond(request)
            self.wfile.write(json.dumps(response).encode("utf-8") + b"\n")
            self.wfile.flush()
            if request.get("method") == "quit":
                return


class _EngineServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    state: HandlerState

    def __init__(self, state: HandlerState) -> None:
        super().__init__(("127.0.0.1", 0), _EngineHandler)
        self.state = state
        address = self.socket.getsockname()
        state.host = str(address[0])
        state.port = int(address[1])


@pytest.fixture
def fake_engine() -> Iterator[tuple[EngineClient, HandlerState]]:
    state = HandlerState()
    server = _EngineServer(state)
    thread = threading.Thread(
        target=server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True
    )
    thread.start()
    client = EngineClient(state.host, state.port, timeout=2.0)
    try:
        yield client, state
    finally:
        client.close()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2.0)


def _unused_port() -> int:
    probe = socket.socket()
    try:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])
    finally:
        probe.close()


def test_ping_and_status_round_trip_with_id_echo(
    fake_engine: tuple[EngineClient, HandlerState],
) -> None:
    client, state = fake_engine
    client.connect()

    assert client.ping() == {"pong": True}
    assert client.status() == {"running": False}

    assert [request["id"] for request in state.requests] == [1, 2]
    assert [request["method"] for request in state.requests] == ["ping", "status"]
    assert state.requests[0]["jsonrpc"] == "2.0"
    assert state.requests[0]["params"] == {}


def test_call_raises_engine_error_with_code_and_message(
    fake_engine: tuple[EngineClient, HandlerState],
) -> None:
    client, state = fake_engine
    state.error_code = -32601
    state.error_message = "method not found"
    client.connect()

    with pytest.raises(EngineError) as excinfo:
        client.call("bogus")

    assert excinfo.value.code == -32601
    assert excinfo.value.message == "method not found"
    assert "-32601" in str(excinfo.value)
    assert "method not found" in str(excinfo.value)


def test_list_devices_unwraps_list(fake_engine: tuple[EngineClient, HandlerState]) -> None:
    client, _ = fake_engine
    client.connect()

    assert client.list_devices() == [{"name": "guitar"}, {"name": "headphones"}]


def test_set_gain_sends_gain_and_returns_result(
    fake_engine: tuple[EngineClient, HandlerState],
) -> None:
    client, state = fake_engine
    client.connect()

    assert client.set_gain(0.5) == {"gain": 0.5}
    assert state.requests[-1]["method"] == "set_gain"
    assert state.requests[-1]["params"] == {"gain": 0.5}


def test_start_omits_unset_optional_params(
    fake_engine: tuple[EngineClient, HandlerState],
) -> None:
    client, state = fake_engine
    client.connect()

    client.start(input_device="guitar", output_device=None)
    assert state.requests[-1]["params"] == {"input_device": "guitar"}

    client.start()
    assert state.requests[-1]["params"] == {}


def test_call_before_connect_raises_runtime_error() -> None:
    client = EngineClient()

    with pytest.raises(RuntimeError):
        client.call("ping")


def test_response_with_mismatched_id_raises(
    fake_engine: tuple[EngineClient, HandlerState],
) -> None:
    client, state = fake_engine
    state.override_response_id = True
    state.response_id = 999
    client.connect()

    with pytest.raises(EngineError) as excinfo:
        client.call("ping")

    assert excinfo.value.code == -32600


def test_connect_to_closed_port_raises_oserror() -> None:
    client = EngineClient(host="127.0.0.1", port=_unused_port(), timeout=1.0)

    with pytest.raises(OSError):
        client.connect()


def test_context_manager_connects_and_closes(
    fake_engine: tuple[EngineClient, HandlerState],
) -> None:
    _, state = fake_engine

    with EngineClient(state.host, state.port, timeout=2.0) as client:
        assert client.connected
        assert client.ping() == {"pong": True}
    assert not client.connected


def test_quit_closes_socket(fake_engine: tuple[EngineClient, HandlerState]) -> None:
    client, state = fake_engine
    client.connect()

    client.quit()

    assert not client.connected
    assert state.requests[-1]["method"] == "quit"


def test_close_is_idempotent(fake_engine: tuple[EngineClient, HandlerState]) -> None:
    client, _ = fake_engine
    client.connect()

    client.close()
    client.close()

    assert not client.connected
