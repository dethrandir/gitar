"""FastAPI application exposing the gitar control API."""

from __future__ import annotations

import asyncio
import os
from pathlib import Path
from typing import Any, Literal

from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from gitar_server import __version__
from gitar_server import devices as devices_module
from gitar_server.backends import (
    AudioBackend,
    BackendError,
    BackendStatus,
    LevelReading,
    get_backend,
)
from gitar_server.config import Config, ConfigError, load_config, save_config

_PACKAGED_WEB_DIR = Path(__file__).resolve().parent.parent / "web"
_DEFAULT_WS_INTERVAL = 1.0


class ConnectRequest(BaseModel):
    mode: Literal["direct", "amp"]


class VolumeRequest(BaseModel):
    percent: int


class ToneRequest(BaseModel):
    tone: str


class MeterRequest(BaseModel):
    seconds: float = Field(default=10.0, gt=0, le=60)


def _health_payload(backend: AudioBackend) -> dict[str, Any]:
    return {
        "name": "gitar",
        "version": __version__,
        "backend": backend.name,
        "backend_available": backend.is_available(),
    }


def _status_payload(status: BackendStatus) -> dict[str, Any]:
    return {
        "route": status.route.value,
        "connected": status.connected,
        "engine_running": status.engine_running,
        "details": status.details,
    }


def _reading_payload(reading: LevelReading) -> dict[str, Any]:
    return {"peak_dbfs": reading.peak_dbfs, "advice": reading.advice}


def _ws_interval() -> float:
    raw = os.environ.get("GITAR_WS_INTERVAL")
    if raw is None:
        return _DEFAULT_WS_INTERVAL
    try:
        interval = float(raw)
    except ValueError:
        return _DEFAULT_WS_INTERVAL
    return interval if interval > 0 else _DEFAULT_WS_INTERVAL


def create_app(backend: AudioBackend | None = None, *, web_dir: Path | None = None) -> FastAPI:
    """Build the control API application.

    ``backend`` defaults to the platform backend; ``web_dir`` defaults to the
    packaged ``web`` directory when present. Missing web assets are tolerated.
    """
    app = FastAPI(title="gitar", version=__version__)
    audio = backend if backend is not None else get_backend()

    @app.exception_handler(BackendError)
    async def _handle_backend_error(_: Request, exc: BackendError) -> JSONResponse:
        return JSONResponse(status_code=409, content={"detail": str(exc)})

    @app.exception_handler(ConfigError)
    async def _handle_config_error(_: Request, exc: ConfigError) -> JSONResponse:
        return JSONResponse(status_code=400, content={"detail": str(exc)})

    @app.get("/api/health")
    def health() -> dict[str, Any]:
        return _health_payload(audio)

    @app.get("/api/config")
    def get_config() -> Config:
        return load_config()

    @app.put("/api/config")
    def put_config(config: Config) -> Config:
        save_config(config)
        return config

    @app.get("/api/devices")
    def list_devices(kind: Literal["source", "sink"] = "source") -> dict[str, Any]:
        found = devices_module.list_sources() if kind == "source" else devices_module.list_sinks()
        return {
            "devices": [
                {"name": device.name, "description": device.description, "kind": device.kind}
                for device in found
            ],
            "default_sink": devices_module.default_sink(),
        }

    @app.get("/api/status")
    def status() -> dict[str, Any]:
        return _status_payload(audio.status(load_config()))

    @app.post("/api/connect")
    def connect(request: ConnectRequest) -> dict[str, Any]:
        config = load_config()
        if request.mode == "direct":
            audio.connect_direct(config)
        else:
            audio.connect_amp(config)
        return _status_payload(audio.status(config))

    @app.post("/api/disconnect")
    def disconnect() -> dict[str, Any]:
        config = load_config()
        audio.disconnect(config)
        return _status_payload(audio.status(config))

    @app.post("/api/volume")
    def volume(request: VolumeRequest) -> dict[str, int]:
        audio.set_volume(load_config(), request.percent)
        return {"percent": request.percent}

    @app.get("/api/tones")
    def tones() -> dict[str, list[str]]:
        return {"tones": audio.list_tones()}

    @app.post("/api/tone")
    def tone(request: ToneRequest) -> dict[str, str]:
        audio.load_tone(load_config(), request.tone)
        return {"tone": request.tone}

    @app.post("/api/meter")
    def meter(request: MeterRequest) -> dict[str, Any]:
        return _reading_payload(audio.measure_level(load_config(), request.seconds))

    def _snapshot() -> dict[str, Any]:
        config = load_config()
        return {
            "status": _status_payload(audio.status(config)),
            "health": _health_payload(audio),
        }

    @app.websocket("/api/ws")
    async def websocket_status(socket: WebSocket) -> None:
        await socket.accept()
        interval = _ws_interval()
        try:
            while True:
                await socket.send_json(await run_in_threadpool(_snapshot))
                await asyncio.sleep(interval)
        except WebSocketDisconnect:
            return

    static_root = web_dir if web_dir is not None else _PACKAGED_WEB_DIR
    if static_root.is_dir():
        app.mount("/", StaticFiles(directory=str(static_root), html=True), name="web")

    return app
