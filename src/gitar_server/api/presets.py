"""HTTP API for rig presets."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException

from gitar_server.api.engine import EngineDep
from gitar_server.presets import (
    Preset,
    PresetError,
    PresetNotFoundError,
    delete_preset,
    list_presets,
    load_preset,
    save_preset,
)

router = APIRouter(prefix="/api/presets", tags=["presets"])


@router.get("")
def list_route() -> dict[str, list[str]]:
    return {"presets": list_presets()}


@router.post("")
def save_route(preset: Preset) -> Preset:
    try:
        save_preset(preset)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return preset


@router.get("/{name}")
def load_route(name: str) -> Preset:
    try:
        return load_preset(name)
    except PresetNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except (PresetError, ValueError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@router.delete("/{name}")
def delete_route(name: str) -> dict[str, bool]:
    try:
        deleted = delete_preset(name)
    except (PresetError, ValueError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return {"deleted": deleted}


@router.post("/{name}/apply")
def apply_route(name: str, engine: EngineDep) -> dict[str, object]:
    try:
        preset = load_preset(name)
    except PresetNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except (PresetError, ValueError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    if not engine.is_available():
        raise HTTPException(status_code=503, detail="gitar-engine binary not found")
    engine.load_model(preset.model_path)
    engine.load_cab(preset.cab_ir_path)
    engine.set_gain(preset.gain)
    engine.set_gate(enabled=preset.gate_enabled, threshold_db=preset.gate_threshold_db)
    engine.set_eq(low_db=preset.eq_low_db, mid_db=preset.eq_mid_db, high_db=preset.eq_high_db)
    return engine.status()
