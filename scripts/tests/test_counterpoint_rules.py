"""Tests for counterpoint rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, NoteSource, Provenance, Score, Track
from scripts.bach_analyzer.rules.base import Category, Severity
from scripts.bach_analyzer.model import TICKS_PER_BAR
from scripts.bach_analyzer.rules.counterpoint import (
    AugmentedLeap,
    CrossRelation,
    VoiceInterleaving,
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


class TestParallelPerfectSustained(unittest.TestCase):
    def test_async_attack_parallel_fifths(self):
        """Voice A attacks at 0,960; Voice B attacks at 480,960.
        At beat 480, A is sustained at pitch 67, B attacks 60 -> P5.
        At beat 960, A attacks 69, B attacks 62 -> P5.
        This is parallel P5 (480->960) and should be detected."""
        soprano = _track("soprano", [_n(67, 0, 960), _n(69, 960)])  # G4 sustained, A4
        alto = _track("alto", [_n(60, 480), _n(62, 960)])  # C4, D4 (both P5)
        result = ParallelPerfect().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertTrue(any("P5" in v.description for v in result.violations))

    def test_oblique_motion_no_violation(self):
        """One voice sustains (same pitch) while other moves -> oblique, not parallel."""
        soprano = _track("soprano", [_n(67, 0, 1920)])  # G4 sustained 2 bars
        alto = _track("alto", [_n(60, 0), _n(62, 480)])  # C4->D4 (P5 -> P5 but oblique)
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


class TestVoiceCrossingInvertible(unittest.TestCase):
    def test_subject_countersubject_crossing_info(self):
        """Subject crossing with countersubject -> INFO (invertible counterpoint)."""
        prov_s = Provenance(source=NoteSource.FUGUE_SUBJECT)
        prov_cs = Provenance(source=NoteSource.COUNTERSUBJECT)
        soprano = _track("soprano", [
            Note(pitch=58, velocity=80, start_tick=0, duration=480,
                 voice="soprano", provenance=prov_s),
        ])
        alto = _track("alto", [
            Note(pitch=65, velocity=80, start_tick=0, duration=480,
                 voice="alto", provenance=prov_cs),
        ])
        result = VoiceCrossing().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertEqual(result.violations[0].severity, Severity.INFO)

    def test_free_counterpoint_crossing_not_info(self):
        """Free counterpoint crossing should remain ERROR/WARNING."""
        prov_fc = Provenance(source=NoteSource.FREE_COUNTERPOINT)
        soprano = _track("soprano", [
            Note(pitch=55, velocity=80, start_tick=0, duration=480,
                 voice="soprano", provenance=prov_fc),
        ])
        alto = _track("alto", [
            Note(pitch=65, velocity=80, start_tick=0, duration=480,
                 voice="alto", provenance=prov_fc),
        ])
        result = VoiceCrossing().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        # Same source -> not invertible -> ERROR
        self.assertNotEqual(result.violations[0].severity, Severity.INFO)


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
        """Tritone leap within a voice (long notes)."""
        soprano = _track("soprano", [_n(60, 0, 960), _n(66, 960, 960)])  # C4->F#4 = 6 st
        result = AugmentedLeap().check(_score([soprano]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)
        self.assertIn("tritone", result.violations[0].description)

    def test_no_tritone(self):
        """Perfect fifth leap is okay."""
        soprano = _track("soprano", [_n(60, 0), _n(67, 480)])  # C4->G4 = 7 semitones
        result = AugmentedLeap().check(_score([soprano]))
        self.assertTrue(result.passed)

    def test_both_short_exempt(self):
        """Both notes short (<= eighth) -> exempt from tritone detection."""
        soprano = _track("soprano", [_n(60, 0, 240), _n(66, 240, 240)])  # eighth->eighth
        result = AugmentedLeap().check(_score([soprano]))
        self.assertTrue(result.passed)

    def test_quarter_notes_not_exempt(self):
        """Quarter note pairs (480 ticks) should be caught (> eighth note threshold)."""
        soprano = _track("soprano", [_n(60, 0, 480), _n(66, 480, 480)])
        result = AugmentedLeap().check(_score([soprano]))
        self.assertFalse(result.passed)

    def test_long_to_short_not_exempt(self):
        """Long note -> short note tritone should still be flagged."""
        soprano = _track("soprano", [_n(60, 0, 960), _n(66, 960, 240)])  # long->short
        result = AugmentedLeap().check(_score([soprano]))
        self.assertFalse(result.passed)


class TestVoiceCrossingSustained(unittest.TestCase):
    def test_crossing_during_sustain(self):
        """Upper voice sustained low while lower voice starts high."""
        soprano = _track("soprano", [_n(72, 0, 1920)])  # C5 whole note
        alto = _track("alto", [_n(60, 0), _n(74, 480)])  # C4 then D5
        result = VoiceCrossing().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        # Crossing at beat 480 (soprano 72 < alto 74)
        self.assertTrue(any(v.tick == 480 for v in result.violations))

    def test_no_crossing_with_gap(self):
        """No note sounding in one voice -> no crossing."""
        soprano = _track("soprano", [_n(58, 0, 240)])  # ends before beat 480
        alto = _track("alto", [_n(65, 480)])
        result = VoiceCrossing().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_temporary_crossing_skipped(self):
        """Crossing that resolves in 1 beat is skipped (lookahead)."""
        soprano = _track("soprano", [_n(58, 0), _n(72, 480)])
        alto = _track("alto", [_n(65, 0), _n(60, 480)])
        result = VoiceCrossing().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_single_track(self):
        result = VoiceCrossing().check(_score([_track("solo", [_n(60, 0)])]))
        self.assertTrue(result.passed)


class TestVoiceInterleaving(unittest.TestCase):
    def test_inverted_3_bars(self):
        """3 consecutive bars of register inversion -> violation."""
        TPB = TICKS_PER_BAR
        s = _track("soprano", [_n(48, i * TPB, TPB) for i in range(3)])
        a = _track("alto", [_n(72, i * TPB, TPB) for i in range(3)])
        result = VoiceInterleaving(min_bars=3).check(_score([s, a]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)

    def test_2_bars_not_flagged(self):
        """2 bars of inversion (< min_bars=3) -> no violation."""
        TPB = TICKS_PER_BAR
        s = _track("soprano", [_n(48, i * TPB, TPB) for i in range(2)])
        a = _track("alto", [_n(72, i * TPB, TPB) for i in range(2)])
        result = VoiceInterleaving(min_bars=3).check(_score([s, a]))
        self.assertTrue(result.passed)

    def test_normal_order_ok(self):
        """Soprano higher than alto -> no violation."""
        TPB = TICKS_PER_BAR
        s = _track("soprano", [_n(72, i * TPB, TPB) for i in range(5)])
        a = _track("alto", [_n(48, i * TPB, TPB) for i in range(5)])
        result = VoiceInterleaving().check(_score([s, a]))
        self.assertTrue(result.passed)

    def test_sustained_note_spans_bars(self):
        """A sustained note spanning 3 bars should be counted in each bar."""
        TPB = TICKS_PER_BAR
        s = _track("soprano", [_n(48, 0, 3 * TPB)])  # 3 bars sustained
        a = _track("alto", [_n(72, i * TPB, TPB) for i in range(3)])
        result = VoiceInterleaving(min_bars=3).check(_score([s, a]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)

    def test_single_track(self):
        result = VoiceInterleaving().check(_score([_track("solo", [_n(60, 0)])]))
        self.assertTrue(result.passed)


class TestRuleProtocol(unittest.TestCase):
    def test_all_rules_have_name_and_category(self):
        rules = [ParallelPerfect(), HiddenPerfect(), VoiceCrossing(),
                 CrossRelation(), AugmentedLeap(), VoiceInterleaving()]
        for rule in rules:
            self.assertIsInstance(rule.name, str)
            self.assertIsInstance(rule.category, Category)
            self.assertEqual(rule.category, Category.COUNTERPOINT)


if __name__ == "__main__":
    unittest.main()
