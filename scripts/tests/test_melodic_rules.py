"""Tests for melodic rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, Score, Track
from scripts.bach_analyzer.rules.base import Severity
from scripts.bach_analyzer.rules.melodic import (
    ConsecutiveRepeatedNotes,
    ExcessiveLeap,
    LeapResolution,
    StepwiseMotionRatio,
)


def _score(tracks):
    return Score(tracks=tracks)


def _track(name, notes):
    return Track(name=name, notes=notes)


def _n(pitch, tick, dur=480):
    return Note(pitch=pitch, velocity=80, start_tick=tick, duration=dur, voice="v")


class TestConsecutiveRepeatedNotes(unittest.TestCase):
    def test_too_many_repeats(self):
        notes = [_n(60, i * 480) for i in range(5)]  # 5x C4
        result = ConsecutiveRepeatedNotes(max_repeats=3).check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)
        self.assertIn("5x", result.violations[0].description)

    def test_exactly_at_limit(self):
        notes = [_n(60, i * 480) for i in range(3)]  # 3x C4
        result = ConsecutiveRepeatedNotes(max_repeats=3).check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)

    def test_broken_run(self):
        notes = [_n(60, 0), _n(60, 480), _n(62, 960), _n(60, 1440)]
        result = ConsecutiveRepeatedNotes(max_repeats=3).check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


class TestExcessiveLeap(unittest.TestCase):
    def test_large_leap(self):
        notes = [_n(60, 0), _n(75, 480)]  # 15 semitones
        result = ExcessiveLeap().check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)
        self.assertEqual(result.violations[0].severity, Severity.ERROR)
        self.assertIn("15", result.violations[0].description)

    def test_octave_ok(self):
        notes = [_n(60, 0), _n(72, 480)]  # exactly 12
        result = ExcessiveLeap().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


class TestLeapResolution(unittest.TestCase):
    def test_unresolved_leap(self):
        notes = [_n(60, 0), _n(67, 480), _n(69, 960)]  # up 7, up 2 (same dir)
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)

    def test_resolved_leap(self):
        notes = [_n(60, 0), _n(67, 480), _n(65, 960)]  # up 7, down 2 (resolved)
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)

    def test_small_leap_ignored(self):
        notes = [_n(60, 0), _n(64, 480), _n(67, 960)]  # up 4, up 3 (< threshold 5)
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


class TestStepwiseMotionRatio(unittest.TestCase):
    def test_mostly_leaps(self):
        notes = [_n(60, 0), _n(67, 480), _n(72, 960), _n(60, 1440)]
        result = StepwiseMotionRatio(min_ratio=0.4).check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)

    def test_mostly_steps(self):
        notes = [_n(60, 0), _n(62, 480), _n(64, 960), _n(65, 1440)]
        result = StepwiseMotionRatio(min_ratio=0.4).check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


if __name__ == "__main__":
    unittest.main()
