"""Synchronous client for the engine's newline-delimited JSON control protocol.

The protocol is one JSON-RPC 2.0 request per line over TCP; the engine replies
with a single line holding either ``result`` or ``error``. The client never
opens the transport implicitly: call ``connect()`` or use it as a context
manager before issuing requests.
"""

from __future__ import annotations

import itertools
import json
import socket
from typing import Any

DEFAULT_ENGINE_HOST = "127.0.0.1"
DEFAULT_ENGINE_PORT = 7344

_JSONRPC_VERSION = "2.0"
_INVALID_REQUEST = -32600


class EngineError(RuntimeError):
    """Raised when the engine answers with a JSON-RPC ``error`` object."""

    def __init__(self, code: int, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message

    def __str__(self) -> str:
        return f"engine error {self.code}: {self.message}"


class EngineClient:
    """Client for the engine control socket.

    ``connect()`` (or entering the client as a context manager) is required
    before ``call()``; issuing a request on a closed client raises
    ``RuntimeError``. A read timeout surfaces as ``TimeoutError``/``OSError``.
    """

    def __init__(
        self,
        host: str = DEFAULT_ENGINE_HOST,
        port: int = DEFAULT_ENGINE_PORT,
        timeout: float = 5.0,
    ) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self._socket: socket.socket | None = None
        self._stream: Any = None
        self._ids = itertools.count(1)

    def connect(self) -> None:
        if self._socket is not None:
            return
        connection = socket.create_connection((self.host, self.port), timeout=self.timeout)
        connection.settimeout(self.timeout)
        self._socket = connection
        self._stream = connection.makefile("rwb")

    def close(self) -> None:
        stream, self._stream = self._stream, None
        connection, self._socket = self._socket, None
        if stream is not None:
            stream.close()
        if connection is not None:
            connection.close()

    @property
    def connected(self) -> bool:
        return self._socket is not None

    def call(self, method: str, params: dict[str, object] | None = None) -> dict[str, object]:
        """Send a single request and return its ``result`` object.

        Raises ``RuntimeError`` when not connected, ``EngineError`` on an error
        response (or a mismatched response id), and ``ConnectionError``/``OSError``
        on transport failure.
        """
        if self._stream is None:
            raise RuntimeError("EngineClient is not connected; call connect() first")

        request_id = next(self._ids)
        payload = {
            "jsonrpc": _JSONRPC_VERSION,
            "id": request_id,
            "method": method,
            "params": {} if params is None else params,
        }
        self._stream.write(json.dumps(payload).encode("utf-8") + b"\n")
        self._stream.flush()

        line = self._stream.readline()
        if not line:
            raise ConnectionError("engine closed the connection")
        response: Any = json.loads(line)

        if response.get("id") != request_id:
            raise EngineError(
                _INVALID_REQUEST,
                f"response id {response.get('id')!r} does not match request id {request_id}",
            )
        error = response.get("error")
        if error is not None:
            raise EngineError(int(error["code"]), str(error["message"]))
        result = response.get("result")
        if not isinstance(result, dict):
            raise EngineError(_INVALID_REQUEST, f"response for {method!r} has no result object")
        return dict(result)

    def ping(self) -> dict[str, object]:
        return self.call("ping")

    def list_devices(self) -> list[dict[str, object]]:
        result = self.call("list_devices")
        devices = result.get("devices")
        if not isinstance(devices, list):
            raise EngineError(_INVALID_REQUEST, "list_devices result has no 'devices' list")
        return [dict(device) for device in devices]

    def start(self, **params: object) -> dict[str, object]:
        return self.call(
            "start", {key: value for key, value in params.items() if value is not None}
        )

    def stop(self) -> dict[str, object]:
        return self.call("stop")

    def status(self) -> dict[str, object]:
        return self.call("status")

    def set_gain(self, gain: float) -> dict[str, object]:
        return self.call("set_gain", {"gain": gain})

    def quit(self) -> None:
        try:
            self.call("quit")
        finally:
            self.close()

    def __enter__(self) -> EngineClient:
        self.connect()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()
