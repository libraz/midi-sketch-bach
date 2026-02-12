"""Tests for independence rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, NoteSource, Provenance, Score, Track
from scripts.bach_analyzer.rules.independence import RhythmDiversity, VoiceIndependence


def _score(tracks):
    return Score(tracks=tracks)


def _track(name, notes):
    return Track(name=name, notes=notes)


def _n(pitch, tick, dur=480, voice="v"):
    return Note(pitch=pitch, velocity=80, start_tick=tick, duration=dur, voice=voice)


class TestVoiceIndependence(unittest.TestCase):
    def test_independent_voices(self):
        """Two voices with different rhythms and contours should score well."""
        soprano = _track("soprano", [
            _n(72, 0, voice="soprano"), _n(74, 480, voice="soprano"),
            _n(76, 960, voice="soprano"), _n(77, 1440, voice="soprano"),
        ])
        alto = _track("alto", [
            _n(60, 240, voice="alto"), _n(57, 720, voice="alto"),
            _n(55, 1200, voice="alto"), _n(53, 1680, voice="alto"),
        ])
        result = VoiceIndependence(min_composite=0.3).check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_single_voice(self):
        """Single voice -> N/A, passes."""
        soprano = _track("soprano", [_n(72, 0)])
        result = VoiceIndependence().check(_score([soprano]))
        self.assertTrue(result.passed)


class TestRhythmDiversity(unittest.TestCase):
    def test_uniform_rhythm(self):
        """All notes same duration -> low diversity."""
        notes = [_n(60 + i, i * 480, 480) for i in range(10)]
        result = RhythmDiversity(min_diversity=0.5).check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)

    def test_diverse_rhythm(self):
        """Mix of durations -> high diversity."""
        notes = [
            _n(60, 0, 240), _n(62, 240, 480), _n(64, 720, 960),
            _n(65, 1680, 120), _n(67, 1800, 360),
        ]
        result = RhythmDiversity(min_diversity=0.3).check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


class TestRhythmDiversityPedal(unittest.TestCase):
    def test_pedal_voice_relaxed_threshold(self):
        """Pedal voice with uniform rhythm should pass with relaxed threshold."""
        prov = Provenance(source=NoteSource.PEDAL_POINT)
        notes = [
            Note(pitch=36, velocity=80, start_tick=i * 480, duration=480,
                 voice="pedal", provenance=prov)
            for i in range(10)
        ]
        result = RhythmDiversity(min_diversity=0.3).check(
            _score([Track(name="pedal", notes=notes)])
        )
        self.assertTrue(result.passed)

    def test_non_pedal_uniform_still_flagged(self):
        """Non-pedal voice with uniform rhythm should still be flagged."""
        notes = [_n(60 + i, i * 480, 480) for i in range(10)]
        result = RhythmDiversity(min_diversity=0.5).check(_score([_track("soprano", notes)]))
        self.assertFalse(result.passed)


if __name__ == "__main__":
    unittest.main()
