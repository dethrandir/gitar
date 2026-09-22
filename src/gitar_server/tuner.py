"""Equal-temperament frequency-to-note conversion for the tuner readout.

Kept dependency-free and side-effect-free so it can be unit-tested without the
engine running.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

_NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
_A4_MIDI = 69.0


@dataclass(frozen=True)
class Note:
    """A detected pitch quantised to the nearest equal-tempered note."""

    name: str
    octave: int
    cents: float
    frequency: float


def hz_to_note(hz: float, a4: float = 440.0) -> Note | None:
    """Map a frequency to the nearest note, or ``None`` when it is unusable."""
    if not math.isfinite(hz) or hz <= 0.0:
        return None
    if not math.isfinite(a4) or a4 <= 0.0:
        return None

    midi = _A4_MIDI + 12.0 * math.log2(hz / a4)
    nearest = math.floor(midi + 0.5)
    cents = 100.0 * (midi - nearest)
    return Note(
        name=_NOTE_NAMES[nearest % 12],
        octave=nearest // 12 - 1,
        cents=cents,
        frequency=hz,
    )
