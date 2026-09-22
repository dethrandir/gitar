"""Tests for equal-temperament frequency-to-note conversion."""

from __future__ import annotations

import math

import pytest

from gitar_server.tuner import hz_to_note


def test_a4_reference_has_no_deviation() -> None:
    note = hz_to_note(440.0)
    assert note is not None
    assert (note.name, note.octave) == ("A", 4)
    assert note.cents == pytest.approx(0.0)
    assert note.frequency == 440.0


def test_sharp_frequency_reports_positive_cents() -> None:
    note = hz_to_note(442.0)
    assert note is not None
    assert note.name == "A"
    assert note.octave == 4
    assert note.cents > 0.0


def test_low_e_second_octave() -> None:
    note = hz_to_note(82.41)
    assert note is not None
    assert (note.name, note.octave) == ("E", 2)


def test_high_e_maps_to_e4() -> None:
    note = hz_to_note(329.63)
    assert note is not None
    assert (note.name, note.octave) == ("E", 4)
    assert abs(note.cents) < 1.0


def test_invalid_frequencies_return_none() -> None:
    assert hz_to_note(0.0) is None
    assert hz_to_note(-10.0) is None
    assert hz_to_note(math.nan) is None
    assert hz_to_note(math.inf) is None


def test_invalid_reference_frequency_returns_none() -> None:
    assert hz_to_note(440.0, a4=0.0) is None
    assert hz_to_note(440.0, a4=-1.0) is None


def test_note_boundary_rounds_to_nearest() -> None:
    below = 440.0 * 2.0 ** (0.49 / 12.0)
    above = 440.0 * 2.0 ** (0.51 / 12.0)
    lower_note = hz_to_note(below)
    upper_note = hz_to_note(above)
    assert lower_note is not None
    assert upper_note is not None
    assert lower_note.name == "A"
    assert upper_note.name == "A#"


def test_cents_stay_within_half_a_semitone() -> None:
    for step in range(-49, 50):
        hz = 440.0 * 2.0 ** (step / 1200.0)
        note = hz_to_note(hz)
        assert note is not None
        assert -50.0 <= note.cents <= 50.0
