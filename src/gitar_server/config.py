"""Configuration handling for the gitar control server."""

import json
import os
import re
import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

from pydantic import BaseModel, ValidationError, field_validator

DEFAULT_LATENCY = "128/48000"

_LATENCY_PATTERN = re.compile(r"^\d+/\d+$")
_LEGACY_KEY_MAP = {
    "GIRIS": "input",
    "KANAL": "channel",
    "CIKIS": "output",
    "GECIKME": "latency",
}


class ConfigError(Exception):
    """Raised when the configuration on disk cannot be read or validated."""


class Config(BaseModel):
    input: str = ""
    channel: str = ""
    output: str = ""
    latency: str = DEFAULT_LATENCY
    backend: str = ""

    @field_validator("latency")
    @classmethod
    def _validate_latency(cls, value: str) -> str:
        if not _LATENCY_PATTERN.match(value):
            raise ValueError(f"latency must look like '<frames>/<rate>', got {value!r}")
        return value


def _resolve_config_dir(
    env: Mapping[str, str],
    platform: str,
    home: Path,
    os_name: str,
) -> Path:
    override = env.get("GITAR_CONFIG_DIR")
    if override:
        return Path(override)
    if os_name == "nt" or platform == "win32":
        appdata = env.get("APPDATA")
        base = Path(appdata) if appdata else home / "AppData" / "Roaming"
        return base / "gitar"
    if platform == "darwin":
        return home / "Library" / "Application Support" / "gitar"
    xdg_config_home = env.get("XDG_CONFIG_HOME")
    if xdg_config_home:
        return Path(xdg_config_home) / "gitar"
    return home / ".config" / "gitar"


def config_dir() -> Path:
    return _resolve_config_dir(os.environ, sys.platform, Path.home(), os.name)


def config_path() -> Path:
    return config_dir() / "config.json"


def legacy_config_path() -> Path:
    return config_dir() / "config"


def _unescape_legacy(value: str) -> str:
    if len(value) >= 2 and value.startswith("'") and value.endswith("'"):
        value = value[1:-1]
    result: list[str] = []
    index = 0
    while index < len(value):
        char = value[index]
        if char == "\\" and index + 1 < len(value):
            result.append(value[index + 1])
            index += 2
        else:
            result.append(char)
            index += 1
    return "".join(result)


def parse_legacy(text: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue
        if stripped.startswith("export "):
            stripped = stripped[len("export ") :].lstrip()
        key, _, raw_value = stripped.partition("=")
        key = key.strip()
        if not key:
            continue
        parsed[key] = _unescape_legacy(raw_value.strip())
    return parsed


def load_config() -> Config:
    path = config_path()
    if not path.exists():
        return Config()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return Config.model_validate(data)
    except (OSError, json.JSONDecodeError, ValidationError) as exc:
        raise ConfigError(f"cannot read config at {path}: {exc}") from exc


def save_config(config: Config) -> Path:
    directory = config_dir()
    directory.mkdir(parents=True, exist_ok=True)
    path = config_path()
    payload = json.dumps(config.model_dump(), indent=2) + "\n"
    handle, temp_name = tempfile.mkstemp(dir=directory, prefix=".config-", suffix=".tmp")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            stream.write(payload)
        os.replace(temp_name, path)
    except BaseException:
        Path(temp_name).unlink(missing_ok=True)
        raise
    return path


def migrate_legacy_if_needed() -> Config | None:
    if config_path().exists():
        return None
    legacy = legacy_config_path()
    if not legacy.exists():
        return None
    values = parse_legacy(legacy.read_text(encoding="utf-8"))
    fields = {name: values.get(key, "") for key, name in _LEGACY_KEY_MAP.items()}
    if not fields["latency"]:
        fields["latency"] = DEFAULT_LATENCY
    config = Config.model_validate(fields)
    save_config(config)
    return config
