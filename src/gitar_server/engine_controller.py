"""Manage the native ``gitar-engine`` process and its control client.

The controller resolves the engine binary, optionally spawns it with its
control server enabled, and forwards high-level operations to a ``EngineClient``.
Public calls connect the client on demand so callers never manage the transport
themselves.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import time
from pathlib import Path

from gitar_server.engine_client import EngineClient, EngineError

_SERVER_ERROR = -32000
_DEFAULT_HOST = "127.0.0.1"
_DEFAULT_PORT = 7344
_DEV_BINARY = Path(__file__).resolve().parents[2] / "engine" / "build" / "gitar-engine"


def _resolve_binary(binary: str | None) -> str | None:
    if binary:
        return binary
    from_env = os.environ.get("GITAR_ENGINE_BIN")
    if from_env:
        return from_env
    found = shutil.which("gitar-engine")
    if found:
        return found
    if _DEV_BINARY.is_file():
        return str(_DEV_BINARY)
    return None


class EngineController:
    def __init__(
        self,
        binary: str | None = None,
        host: str = _DEFAULT_HOST,
        port: int = _DEFAULT_PORT,
        *,
        client: EngineClient | None = None,
        spawn: bool = True,
        start_timeout: float = 5.0,
    ) -> None:
        self._binary = _resolve_binary(binary)
        self._host = host
        self._port = port
        self._spawn = spawn
        self._start_timeout = start_timeout
        self._client = client if client is not None else EngineClient(host, port)
        self._process: subprocess.Popen[str] | None = None

    @property
    def binary(self) -> str | None:
        return self._binary

    def is_available(self) -> bool:
        return self._binary is not None

    @property
    def running(self) -> bool:
        if self._process is not None and self._process.poll() is None:
            return True
        return self._client.connected

    def ensure_process(self) -> None:
        if self._client.connected:
            return
        if not self._spawn:
            self._client.connect()
            return
        if self._binary is None:
            raise EngineError(_SERVER_ERROR, "gitar-engine binary not found")
        self._process = subprocess.Popen(
            [self._binary, "run", "--control-port", str(self._port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        self._wait_until_connectable()

    def _wait_until_connectable(self) -> None:
        deadline = time.monotonic() + self._start_timeout
        while True:
            try:
                self._client.connect()
                return
            except OSError:
                if time.monotonic() >= deadline:
                    break
                time.sleep(0.05)
        output = self._terminate_process()
        raise EngineError(_SERVER_ERROR, f"engine did not become ready: {output}")

    def _terminate_process(self, timeout: float = 2.0) -> str:
        process, self._process = self._process, None
        if process is None:
            return ""
        if process.poll() is None:
            process.terminate()
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate()
        return output or ""

    def shutdown(self) -> None:
        client = self._client
        if client.connected:
            try:
                client.quit()
            except (OSError, EngineError):
                client.close()
        self._terminate_process()

    def _ready_client(self) -> EngineClient:
        self.ensure_process()
        return self._client

    def status(self) -> dict[str, object]:
        return self._ready_client().status()

    def list_devices(self) -> list[dict[str, object]]:
        return self._ready_client().list_devices()

    def start(self, **params: object) -> dict[str, object]:
        return self._ready_client().start(**params)

    def stop(self) -> dict[str, object]:
        return self._ready_client().stop()

    def load_model(self, path: str) -> dict[str, object]:
        return self._ready_client().call("load_model", {"path": path})

    def clear_model(self) -> dict[str, object]:
        return self._ready_client().call("clear_model")

    def set_gain(self, gain: float) -> dict[str, object]:
        return self._ready_client().set_gain(gain)

    def set_gate(
        self, *, enabled: bool | None = None, threshold_db: float | None = None
    ) -> dict[str, object]:
        params: dict[str, object] = {}
        if enabled is not None:
            params["enabled"] = enabled
        if threshold_db is not None:
            params["threshold_db"] = threshold_db
        if not params:
            raise ValueError("set_gate requires enabled or threshold_db")
        return self._ready_client().call("set_gate", params)

    def set_eq(
        self,
        *,
        low_db: float | None = None,
        mid_db: float | None = None,
        high_db: float | None = None,
    ) -> dict[str, object]:
        params: dict[str, object] = {}
        if low_db is not None:
            params["low_db"] = low_db
        if mid_db is not None:
            params["mid_db"] = mid_db
        if high_db is not None:
            params["high_db"] = high_db
        if not params:
            raise ValueError("set_eq requires low_db, mid_db or high_db")
        return self._ready_client().call("set_eq", params)

    def load_cab(self, path: str) -> dict[str, object]:
        return self._ready_client().call("load_cab", {"path": path})
