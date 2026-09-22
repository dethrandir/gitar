"""HTTP API for the native engine process and the neural model registry."""

from __future__ import annotations

from typing import Annotated, cast

from fastapi import APIRouter, Depends, HTTPException, Request
from pydantic import BaseModel

from gitar_server.engine_controller import EngineController
from gitar_server.models import ModelInfo, scan_cabs, scan_models

router = APIRouter(prefix="/api", tags=["engine"])


def get_engine(request: Request) -> EngineController:
    state = request.app.state
    if not hasattr(state, "engine"):
        state.engine = EngineController()
    return cast(EngineController, state.engine)


EngineDep = Annotated[EngineController, Depends(get_engine)]


class StartRequest(BaseModel):
    input_device: str | None = None
    output_device: str | None = None
    sample_rate: int | None = None
    period_frames: int | None = None
    channels: int | None = None
    gain: float | None = None
    model_path: str | None = None
    gate_enabled: bool | None = None
    gate_threshold_db: float | None = None
    metronome_enabled: bool | None = None
    metronome_bpm: float | None = None


class ModelRequest(BaseModel):
    path: str = ""


class GainRequest(BaseModel):
    gain: float


class GateRequest(BaseModel):
    enabled: bool | None = None
    threshold_db: float | None = None


class EqRequest(BaseModel):
    low_db: float | None = None
    mid_db: float | None = None
    high_db: float | None = None


class CabRequest(BaseModel):
    path: str = ""


class MetronomeRequest(BaseModel):
    enabled: bool | None = None
    bpm: float | None = None


class RecordRequest(BaseModel):
    path: str = ""


def _model_payload(model: ModelInfo) -> dict[str, object]:
    return {
        "name": model.name,
        "path": str(model.path),
        "architecture": model.architecture,
        "sample_rate": model.sample_rate,
        "size_bytes": model.size_bytes,
    }


@router.get("/models")
def list_models() -> dict[str, list[dict[str, object]]]:
    return {"models": [_model_payload(model) for model in scan_models()]}


@router.get("/cabs")
def list_cabs() -> dict[str, list[dict[str, object]]]:
    return {"cabs": scan_cabs()}


@router.get("/engine/devices")
def engine_devices(engine: EngineDep) -> dict[str, list[dict[str, object]]]:
    if not engine.is_available():
        raise HTTPException(status_code=503, detail="gitar-engine binary not found")
    return {"devices": engine.list_devices()}


@router.get("/engine/status")
def engine_status(engine: EngineDep) -> dict[str, object]:
    if not engine.is_available():
        raise HTTPException(status_code=503, detail="gitar-engine binary not found")
    return engine.status()


@router.post("/engine/start")
def engine_start(request: StartRequest, engine: EngineDep) -> dict[str, object]:
    return engine.start(**request.model_dump(exclude_none=True))


@router.post("/engine/stop")
def engine_stop(engine: EngineDep) -> dict[str, object]:
    return engine.stop()


@router.post("/engine/model")
def engine_model(request: ModelRequest, engine: EngineDep) -> dict[str, object]:
    if request.path:
        return engine.load_model(request.path)
    return engine.clear_model()


@router.post("/engine/gain")
def engine_gain(request: GainRequest, engine: EngineDep) -> dict[str, object]:
    return engine.set_gain(request.gain)


@router.post("/engine/gate")
def engine_gate(request: GateRequest, engine: EngineDep) -> dict[str, object]:
    if request.enabled is None and request.threshold_db is None:
        raise HTTPException(status_code=422, detail="enabled or threshold_db is required")
    return engine.set_gate(enabled=request.enabled, threshold_db=request.threshold_db)


@router.post("/engine/eq")
def engine_eq(request: EqRequest, engine: EngineDep) -> dict[str, object]:
    if request.low_db is None and request.mid_db is None and request.high_db is None:
        raise HTTPException(status_code=422, detail="low_db, mid_db or high_db is required")
    return engine.set_eq(low_db=request.low_db, mid_db=request.mid_db, high_db=request.high_db)


@router.post("/engine/cab")
def engine_cab(request: CabRequest, engine: EngineDep) -> dict[str, object]:
    return engine.load_cab(request.path)


@router.post("/engine/metronome")
def engine_metronome(request: MetronomeRequest, engine: EngineDep) -> dict[str, object]:
    if request.enabled is None and request.bpm is None:
        raise HTTPException(status_code=422, detail="enabled or bpm is required")
    return engine.set_metronome(enabled=request.enabled, bpm=request.bpm)


@router.post("/engine/record")
def engine_record(request: RecordRequest, engine: EngineDep) -> dict[str, object]:
    if not request.path or request.path == "stop":
        return engine.stop_recording()
    return engine.start_recording(request.path)
