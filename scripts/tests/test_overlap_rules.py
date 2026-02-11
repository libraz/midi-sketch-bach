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


if __name__ == "__main__":
    unittest.main()
