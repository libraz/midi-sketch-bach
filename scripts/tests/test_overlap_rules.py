"""Tests for overlap rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, Score, Track
from scripts.bach_analyzer.rules.base import Severity
from scripts.bach_analyzer.rules.overlap import VoiceSpacing, WithinVoiceOverlap


def _score(tracks):
    return Score(tracks=tracks)


def _track(name, notes):
    return Track(name=name, notes=notes)


def _n(pitch, tick, dur=480):
    return Note(pitch=pitch, velocity=80, start_tick=tick, duration=dur, voice="v")


class TestWithinVoiceOverlap(unittest.TestCase):
    def test_overlap(self):
        notes = [_n(60, 0, 600), _n(62, 480)]  # first ends at 600, second starts at 480
        result = WithinVoiceOverlap().check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)
        self.assertEqual(result.violations[0].severity, Severity.CRITICAL)
        self.assertIn("overlap 120", result.violations[0].description)

    def test_no_overlap(self):
        notes = [_n(60, 0, 480), _n(62, 480)]
        result = WithinVoiceOverlap().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)

    def test_gap_ok(self):
        notes = [_n(60, 0, 240), _n(62, 480)]  # gap between
        result = WithinVoiceOverlap().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


class TestVoiceSpacing(unittest.TestCase):
    def test_wide_spacing(self):
        soprano = _track("soprano", [_n(84, 0)])  # C6
        alto = _track("alto", [_n(60, 0)])  # C4 = 24 semitones apart
        result = VoiceSpacing().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertIn("24", result.violations[0].description)

    def test_normal_spacing(self):
        soprano = _track("soprano", [_n(72, 0)])  # C5
        alto = _track("alto", [_n(64, 0)])  # E4 = 8 semitones apart
        result = VoiceSpacing().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_wide_spacing_during_sustain(self):
        """Sustained note creates spacing violation on later beats."""
        soprano = _track("soprano", [_n(72, 0, 1920)])  # C5 whole note
        alto = _track("alto", [_n(64, 0), _n(48, 480, 1440)])  # E4 then C3
        result = VoiceSpacing().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        # Violations at beats 480, 960, 1440 (24 semitones)
        self.assertTrue(any(v.tick == 480 for v in result.violations))

    def test_no_violation_one_voice_silent(self):
        """No spacing violation when one voice has no sounding note."""
        soprano = _track("soprano", [_n(84, 0, 240)])  # ends before beat 480
        alto = _track("alto", [_n(48, 480)])
        result = VoiceSpacing().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_single_track(self):
        result = VoiceSpacing().check(_score([_track("solo", [_n(60, 0)])]))
        self.assertTrue(result.passed)


class TestVoiceSpacingPedal(unittest.TestCase):
    def test_pedal_wide_spacing_ok(self):
        """20 semitones between manual and pedal voice should pass (< 24 threshold)."""
        tenor = _track("tenor", [_n(60, 0)])   # C4
        pedal_note = Note(pitch=40, velocity=80, start_tick=0, duration=480, voice="pedal")
        pedal = Track(name="pedal", channel=3, notes=[pedal_note])  # E2, gap=20
        result = VoiceSpacing().check(_score([tenor, pedal]))
        self.assertTrue(result.passed)

    def test_pedal_extreme_spacing_flagged(self):
        """25 semitones between manual and pedal should be flagged (> 24 threshold)."""
        tenor = _track("tenor", [_n(72, 0)])   # C5
        pedal_note = Note(pitch=47, velocity=80, start_tick=0, duration=480, voice="pedal")
        pedal = Track(name="pedal", channel=3, notes=[pedal_note])  # B2, gap=25
        result = VoiceSpacing().check(_score([tenor, pedal]))
        self.assertFalse(result.passed)

    def test_non_pedal_still_strict(self):
        """Non-pedal voices still use 12-semitone threshold."""
        soprano = _track("soprano", [_n(77, 0)])  # F5
        alto = _track("alto", [_n(60, 0)])         # C4, gap=17
        result = VoiceSpacing().check(_score([soprano, alto]))
        self.assertFalse(result.passed)


if __name__ == "__main__":
    unittest.main()
