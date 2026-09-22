"""Tests for pure WAV level analysis."""

from __future__ import annotations

import array
import math
import wave
from pathlib import Path

import pytest

from gitar_server import levels

FULL_SCALE = 32768
ADVICE_HIGH = "Too high - lower the gain on your audio interface."
ADVICE_LOW = "Too low - raise the gain. If you hear nothing the channel may be wrong."
ADVICE_GOOD = "Good level."


def write_wav(path: Path, samples: list[int], channels: int = 1) -> None:
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(channels)
        handle.setsampwidth(2)
        handle.setframerate(48000)
        handle.writeframes(array.array("h", samples).tobytes())


def test_to_dbfs_full_scale_is_zero() -> None:
    assert levels.to_dbfs(FULL_SCALE) == pytest.approx(0.0)


def test_to_dbfs_half_scale_is_minus_six() -> None:
    assert levels.to_dbfs(FULL_SCALE // 2) == pytest.approx(-6.0206, abs=1e-3)


def test_to_dbfs_zero_uses_floor_of_one() -> None:
    assert levels.to_dbfs(0) == pytest.approx(20 * math.log10(1 / FULL_SCALE))


def test_to_dbfs_respects_custom_full_scale() -> None:
    assert levels.to_dbfs(100, full_scale=100) == pytest.approx(0.0)


@pytest.mark.parametrize("peak", [0, 1, 100, 16384, 32768])
def test_to_dbfs_matches_reference_formula(peak: int) -> None:
    assert levels.to_dbfs(peak) == pytest.approx(20 * math.log10(max(peak, 1) / FULL_SCALE))


def test_dbfs_advice_high_threshold_is_exclusive() -> None:
    assert levels.dbfs_advice(-2.9) == ADVICE_HIGH
    assert levels.dbfs_advice(-3.0) == ADVICE_GOOD


def test_dbfs_advice_low_threshold_is_exclusive() -> None:
    assert levels.dbfs_advice(-24.0) == ADVICE_GOOD
    assert levels.dbfs_advice(-24.1) == ADVICE_LOW


def test_dbfs_advice_good_middle() -> None:
    assert levels.dbfs_advice(-12.0) == ADVICE_GOOD


def test_analyze_wav_single_channel_peak(tmp_path: Path) -> None:
    path = tmp_path / "mono.wav"
    write_wav(path, [0, 1000, -8000, 500])
    reading = levels.analyze_wav(path, 0, 1)
    assert reading.peak_dbfs == pytest.approx(levels.to_dbfs(8000))
    assert reading.advice == ADVICE_GOOD


def test_analyze_wav_stereo_right_channel_only(tmp_path: Path) -> None:
    path = tmp_path / "stereo.wav"
    # interleaved L/R: left silent, right peaks at 3000
    write_wav(path, [0, 0, 0, 3000, 0, -3000], channels=2)
    right = levels.analyze_wav(path, 1, 2)
    left = levels.analyze_wav(path, 0, 2)
    assert right.peak_dbfs == pytest.approx(levels.to_dbfs(3000))
    assert left.peak_dbfs == pytest.approx(levels.to_dbfs(0))


def test_analyze_wav_empty_file_is_silence_floor(tmp_path: Path) -> None:
    path = tmp_path / "empty.wav"
    write_wav(path, [])
    reading = levels.analyze_wav(path, 0, 1)
    assert reading.peak_dbfs == pytest.approx(levels.to_dbfs(0))
    assert reading.advice == ADVICE_LOW


def test_analyze_wav_advice_high_when_near_full_scale(tmp_path: Path) -> None:
    path = tmp_path / "loud.wav"
    write_wav(path, [32767, -32767])
    assert levels.analyze_wav(path, 0, 1).advice == ADVICE_HIGH


def test_analyze_wav_advice_low_when_quiet(tmp_path: Path) -> None:
    path = tmp_path / "quiet.wav"
    write_wav(path, [100, -100])
    assert levels.analyze_wav(path, 0, 1).advice == ADVICE_LOW


def test_analyze_wav_rejects_non_positive_channels(tmp_path: Path) -> None:
    path = tmp_path / "mono.wav"
    write_wav(path, [1])
    with pytest.raises(ValueError):
        levels.analyze_wav(path, 0, 0)
    with pytest.raises(ValueError):
        levels.analyze_wav(path, 0, -1)


def test_analyze_wav_rejects_channel_out_of_range(tmp_path: Path) -> None:
    path = tmp_path / "mono.wav"
    write_wav(path, [1])
    with pytest.raises(ValueError):
        levels.analyze_wav(path, 1, 1)
    with pytest.raises(ValueError):
        levels.analyze_wav(path, -1, 1)
    with pytest.raises(ValueError):
        levels.analyze_wav(path, 2, 2)
