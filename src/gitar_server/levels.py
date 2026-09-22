"""Pure analysis of 16-bit PCM WAV captures.

Kept free of subprocesses and third-party dependencies so the level math can be
unit-tested independently of PipeWire.
"""

from __future__ import annotations

import array
import math
import wave
from pathlib import Path

from gitar_server.backends.base import LevelReading

FULL_SCALE = 32768


def to_dbfs(peak: int, full_scale: int = FULL_SCALE) -> float:
    """Convert a signed 16-bit peak amplitude to dBFS."""
    return 20.0 * math.log10(max(peak, 1) / full_scale)


def dbfs_advice(db: float) -> str:
    """Human advice for a measured level, mirroring the legacy CLI wording."""
    if db > -3:
        return "Too high - lower the gain on your audio interface."
    if db < -24:
        return "Too low - raise the gain. If you hear nothing the channel may be wrong."
    return "Good level."


def analyze_wav(path: Path, channel: int, channels: int) -> LevelReading:
    """Return the peak level of one channel of a 16-bit PCM WAV file.

    ``channel`` is a zero-based index and ``channels`` the interleave stride.
    """
    if channels <= 0:
        raise ValueError(f"channels must be positive, got {channels}")
    if channel < 0 or channel >= channels:
        raise ValueError(f"channel {channel} out of range for {channels} channels")

    with wave.open(str(path), "rb") as handle:
        frames = handle.getnframes()
        samples: array.array[int] = array.array("h", handle.readframes(frames))

    selected = samples[channel::channels]
    peak = max((abs(value) for value in selected), default=0)
    db = to_dbfs(peak)
    return LevelReading(peak_dbfs=db, advice=dbfs_advice(db))
