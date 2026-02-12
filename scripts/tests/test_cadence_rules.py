"""Tests for cadence rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, Score, Track, TICKS_PER_BAR, TICKS_PER_BEAT
from scripts.bach_analyzer.rules.cadence import (
    CadenceDensity,
    CadenceType,
    FinalCadence,
    _detect_cadence_at,
)


class TestFinalCadence(unittest.TestCase):
    """Test final cadence detection."""

    def _make_score(self, soprano_notes, bass_notes, form="fugue"):
        """Create a two-voice score."""
        return Score(
            tracks=[
                Track(name="soprano", notes=soprano_notes),
                Track(name="bass", notes=bass_notes),
            ],
            form=form,
            seed=1,
        )

    def test_valid_cadence_v_to_i(self):
        """V->I cadence in final bars should pass."""
        # Bar 3 beat 1: bass G->C (P5 descent), soprano E->C (P8)
        soprano = [
            Note(pitch=76, velocity=80, start_tick=0, duration=1920, voice="soprano"),
            Note(pitch=76, velocity=80, start_tick=1920, duration=1920, voice="soprano"),
            Note(pitch=76, velocity=80, start_tick=3840, duration=1920, voice="soprano"),
            Note(pitch=72, velocity=80, start_tick=5760, duration=1920, voice="soprano"),
        ]
        bass = [
            Note(pitch=60, velocity=80, start_tick=0, duration=1920, voice="bass"),
            Note(pitch=60, velocity=80, start_tick=1920, duration=1920, voice="bass"),
            Note(pitch=67, velocity=80, start_tick=3840, duration=1920, voice="bass"),
            Note(pitch=60, velocity=80, start_tick=5760, duration=1920, voice="bass"),
        ]
        score = self._make_score(soprano, bass)
        result = FinalCadence().check(score)
        self.assertTrue(result.passed)

    def test_no_cadence_dissonant_final(self):
        """No cadence: outer voices dissonant at end."""
        soprano = [
            Note(pitch=72, velocity=80, start_tick=0, duration=1920, voice="soprano"),
            Note(pitch=73, velocity=80, start_tick=1920, duration=1920, voice="soprano"),
        ]
        bass = [
            Note(pitch=60, velocity=80, start_tick=0, duration=1920, voice="bass"),
            Note(pitch=60, velocity=80, start_tick=1920, duration=1920, voice="bass"),
        ]
        score = self._make_score(soprano, bass)
        result = FinalCadence().check(score)
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)

    def test_single_voice_skipped(self):
        """Single voice -> no cadence check needed."""
        notes = [Note(pitch=60, velocity=80, start_tick=0, duration=1920, voice="solo")]
        score = Score(tracks=[Track(name="solo", notes=notes)], form="fugue")
        result = FinalCadence().check(score)
        self.assertTrue(result.passed)


class TestCadenceDensity(unittest.TestCase):
    """Test cadence density detection."""

    def test_short_score_passes(self):
        """Score shorter than bars_per_cadence passes automatically."""
        notes_s = [Note(pitch=72, velocity=80, start_tick=0, duration=480, voice="s")]
        notes_b = [Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="b")]
        score = Score(
            tracks=[Track(name="s", notes=notes_s), Track(name="b", notes=notes_b)],
            form="fugue",
        )
        result = CadenceDensity().check(score)
        self.assertTrue(result.passed)


class TestCadenceTypeDetection(unittest.TestCase):
    """Test CadenceType enum and extended cadence detection."""

    def _make_score(self, soprano_notes, bass_notes):
        return Score(
            tracks=[
                Track(name="soprano", notes=soprano_notes),
                Track(name="bass", notes=bass_notes),
            ],
            form="fugue",
        )

    def test_perfect_cadence_type(self):
        """V->I cadence should return PERFECT type."""
        # Bar 1: bass G, soprano E -> Bar 2: bass C, soprano C (P8)
        bass = [
            Note(pitch=67, velocity=80, start_tick=0, duration=1920, voice="bass"),
            Note(pitch=60, velocity=80, start_tick=1920, duration=1920, voice="bass"),
        ]
        soprano = [
            Note(pitch=76, velocity=80, start_tick=0, duration=1920, voice="soprano"),
            Note(pitch=72, velocity=80, start_tick=1920, duration=1920, voice="soprano"),
        ]
        score = self._make_score(soprano, bass)
        ctype = _detect_cadence_at(bass, soprano, 1920, score)
        self.assertEqual(ctype, CadenceType.PERFECT)

    def test_half_cadence_type(self):
        """Bass ascending P5 to dominant with consonant outer voices -> HALF."""
        # Bar 1: bass C, soprano E -> Bar 2: bass G, soprano D (P5 from bass = consonant)
        bass = [
            Note(pitch=48, velocity=80, start_tick=0, duration=1920, voice="bass"),
            Note(pitch=55, velocity=80, start_tick=1920, duration=1920, voice="bass"),
        ]
        soprano = [
            Note(pitch=64, velocity=80, start_tick=0, duration=1920, voice="soprano"),
            Note(pitch=62, velocity=80, start_tick=1920, duration=1920, voice="soprano"),
        ]
        score = self._make_score(soprano, bass)
        ctype = _detect_cadence_at(bass, soprano, 1920, score)
        self.assertEqual(ctype, CadenceType.HALF)

    def test_deceptive_cadence_type(self):
        """V->vi: bass P5 descent but outer voices form imperfect consonance -> DECEPTIVE."""
        # Bar 1: bass G, soprano B -> Bar 2: bass C descended by P5,
        # but soprano A makes M6 (imperfect consonance)
        bass = [
            Note(pitch=67, velocity=80, start_tick=0, duration=1920, voice="bass"),
            Note(pitch=60, velocity=80, start_tick=1920, duration=1920, voice="bass"),
        ]
        soprano = [
            Note(pitch=83, velocity=80, start_tick=0, duration=1920, voice="soprano"),
            Note(pitch=69, velocity=80, start_tick=1920, duration=1920, voice="soprano"),
        ]
        score = self._make_score(soprano, bass)
        ctype = _detect_cadence_at(bass, soprano, 1920, score)
        # C-A = M6 (9 semitones) is imperfect consonance, bass descended P5 -> PERFECT
        # Actually 69-60 = 9 -> M6 in CONSONANCES but not in PERFECT_CONSONANCES
        # Bass 67->60 = -7 = P5 descent, outer iv = 9 (M6) -> not perfect consonance
        # So this should be DECEPTIVE
        self.assertEqual(ctype, CadenceType.DECEPTIVE)

    def test_beat_3_cadence_detected(self):
        """Cadence on beat 3 should also be detected."""
        # Cadence at beat 3 of bar 2 (tick = 1920 + 960 = 2880)
        bass = [
            Note(pitch=67, velocity=80, start_tick=960, duration=1920, voice="bass"),
            Note(pitch=60, velocity=80, start_tick=2880, duration=960, voice="bass"),
        ]
        soprano = [
            Note(pitch=76, velocity=80, start_tick=960, duration=1920, voice="soprano"),
            Note(pitch=72, velocity=80, start_tick=2880, duration=960, voice="soprano"),
        ]
        score = self._make_score(soprano, bass)
        ctype = _detect_cadence_at(bass, soprano, 2880, score)
        self.assertEqual(ctype, CadenceType.PERFECT)

    def test_no_cadence_on_beat_2(self):
        """Beat 2 should not detect a cadence."""
        bass = [
            Note(pitch=67, velocity=80, start_tick=0, duration=480, voice="bass"),
            Note(pitch=60, velocity=80, start_tick=480, duration=480, voice="bass"),
        ]
        soprano = [
            Note(pitch=76, velocity=80, start_tick=0, duration=480, voice="soprano"),
            Note(pitch=72, velocity=80, start_tick=480, duration=480, voice="soprano"),
        ]
        score = self._make_score(soprano, bass)
        # Beat 2 = tick 480
        ctype = _detect_cadence_at(bass, soprano, 480, score)
        self.assertIsNone(ctype)


if __name__ == "__main__":
    unittest.main()
