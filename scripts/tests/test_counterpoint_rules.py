"""Tests for counterpoint rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, Score, Track
from scripts.bach_analyzer.rules.base import Category, Severity
from scripts.bach_analyzer.rules.counterpoint import (
    AugmentedLeap,
    CrossRelation,
    HiddenPerfect,
    ParallelPerfect,
    VoiceCrossing,
)


def _score(tracks):
    return Score(tracks=tracks)


def _track(name, notes):
    return Track(name=name, notes=notes)


def _n(pitch, tick, dur=480, voice="v"):
    return Note(pitch=pitch, velocity=80, start_tick=tick, duration=dur, voice=voice)


class TestParallelPerfect(unittest.TestCase):
    def test_parallel_fifths(self):
        """Two voices moving in parallel P5."""
        soprano = _track("soprano", [_n(67, 0), _n(69, 480)])  # G4, A4
        alto = _track("alto", [_n(60, 0), _n(62, 480)])  # C4, D4  (both P5)
        result = ParallelPerfect().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)
        self.assertEqual(result.violations[0].severity, Severity.CRITICAL)
        self.assertIn("P5", result.violations[0].description)

    def test_parallel_octaves(self):
        """Two voices moving in parallel P8."""
        soprano = _track("soprano", [_n(72, 0), _n(74, 480)])  # C5, D5
        alto = _track("alto", [_n(60, 0), _n(62, 480)])  # C4, D4  (octaves)
        result = ParallelPerfect().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)
        self.assertIn("P8", result.violations[0].description)

    def test_no_parallel(self):
        """Contrary motion -- no violation."""
        soprano = _track("soprano", [_n(72, 0), _n(71, 480)])  # C5, B4
        alto = _track("alto", [_n(60, 0), _n(62, 480)])  # C4, D4
        result = ParallelPerfect().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_imperfect_parallel_ok(self):
        """Parallel thirds are acceptable."""
        soprano = _track("soprano", [_n(64, 0), _n(66, 480)])  # E4, F#4
        alto = _track("alto", [_n(60, 0), _n(62, 480)])  # C4, D4 (both M3)
        result = ParallelPerfect().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_static_voices_no_violation(self):
        """Both voices at same pitch (no motion) -> no violation."""
        soprano = _track("soprano", [_n(72, 0), _n(72, 480)])
        alto = _track("alto", [_n(60, 0), _n(60, 480)])
        result = ParallelPerfect().check(_score([soprano, alto]))
        self.assertTrue(result.passed)


class TestHiddenPerfect(unittest.TestCase):
    def test_hidden_fifth(self):
        """Both voices leap in same direction arriving at P5 (not from P5)."""
        soprano = _track("soprano", [_n(60, 0), _n(67, 480)])  # C4->G4 (leap up 7)
        alto = _track("alto", [_n(52, 0), _n(60, 480)])  # E3->C4 (leap up 8), arrive at P5
        result = HiddenPerfect().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertEqual(result.violations[0].severity, Severity.WARNING)

    def test_step_no_hidden(self):
        """Upper voice steps -- not hidden perfect."""
        soprano = _track("soprano", [_n(71, 0), _n(72, 480)])  # B4->C5 (step)
        alto = _track("alto", [_n(57, 0), _n(65, 480)])  # A3->F4 (leap)
        result = HiddenPerfect().check(_score([soprano, alto]))
        self.assertTrue(result.passed)


class TestVoiceCrossing(unittest.TestCase):
    def test_crossing(self):
        """Upper voice goes below lower voice."""
        soprano = _track("soprano", [_n(58, 0)])  # below alto
        alto = _track("alto", [_n(65, 0)])
        result = VoiceCrossing().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)
        self.assertEqual(result.violations[0].severity, Severity.ERROR)

    def test_no_crossing(self):
        """Normal: soprano above alto."""
        soprano = _track("soprano", [_n(72, 0)])
        alto = _track("alto", [_n(60, 0)])
        result = VoiceCrossing().check(_score([soprano, alto]))
        self.assertTrue(result.passed)


class TestCrossRelation(unittest.TestCase):
    def test_cross_relation(self):
        """F# in soprano, F natural in alto within 1 beat."""
        soprano = _track("soprano", [_n(66, 0)])  # F#4
        alto = _track("alto", [_n(65, 240)])  # F4
        result = CrossRelation().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertEqual(result.violations[0].severity, Severity.WARNING)

    def test_no_cross_relation_far(self):
        """Same pitches but too far apart."""
        soprano = _track("soprano", [_n(66, 0)])  # F#4
        alto = _track("alto", [_n(65, 1920)])  # F4, 4 beats later
        result = CrossRelation(proximity_ticks=480).check(_score([soprano, alto]))
        self.assertTrue(result.passed)


class TestAugmentedLeap(unittest.TestCase):
    def test_tritone_leap(self):
        """Tritone leap within a voice."""
        soprano = _track("soprano", [_n(60, 0), _n(66, 480)])  # C4->F#4 = 6 semitones
        result = AugmentedLeap().check(_score([soprano]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)
        self.assertIn("tritone", result.violations[0].description)

    def test_no_tritone(self):
        """Perfect fifth leap is okay."""
        soprano = _track("soprano", [_n(60, 0), _n(67, 480)])  # C4->G4 = 7 semitones
        result = AugmentedLeap().check(_score([soprano]))
        self.assertTrue(result.passed)


class TestRuleProtocol(unittest.TestCase):
    def test_all_rules_have_name_and_category(self):
        rules = [ParallelPerfect(), HiddenPerfect(), VoiceCrossing(),
                 CrossRelation(), AugmentedLeap()]
        for rule in rules:
            self.assertIsInstance(rule.name, str)
            self.assertIsInstance(rule.category, Category)
            self.assertEqual(rule.category, Category.COUNTERPOINT)


if __name__ == "__main__":
    unittest.main()
