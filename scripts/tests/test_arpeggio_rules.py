"""Tests for arpeggio rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, Score, Track
from scripts.bach_analyzer.rules.arpeggio import (
    ArpeggioPatternConsistency,
    ImpliedPolyphony,
    SoloStringLeapFeasibility,
)


class TestSoloStringLeapFeasibility(unittest.TestCase):
    """Test consecutive large leap detection."""

    def _make_score(self, pitches, form="chaconne"):
        """Create a single-voice score from pitch list."""
        notes = [
            Note(pitch=p, velocity=80, start_tick=i * 240, duration=240, voice="violin")
            for i, p in enumerate(pitches)
        ]
        return Score(
            tracks=[Track(name="violin", notes=notes)],
            form=form,
            seed=1,
        )

    def test_no_large_leaps(self):
        """Stepwise motion should pass."""
        score = self._make_score([60, 62, 64, 65, 67, 69, 71, 72])
        result = SoloStringLeapFeasibility().check(score)
        self.assertTrue(result.passed)

    def test_three_consecutive_large_leaps(self):
        """3+ consecutive leaps > 12st should trigger WARNING."""
        # Each jump is 14 semitones.
        score = self._make_score([55, 69, 55, 69, 55])
        result = SoloStringLeapFeasibility().check(score)
        self.assertFalse(result.passed)
        self.assertGreater(len(result.violations), 0)

    def test_two_large_leaps_ok(self):
        """Only 2 consecutive large leaps should pass."""
        score = self._make_score([55, 69, 55, 60])
        result = SoloStringLeapFeasibility().check(score)
        self.assertTrue(result.passed)

    def test_small_leaps_mixed(self):
        """Mixed small and large leaps should pass if not 3+ consecutive."""
        score = self._make_score([55, 69, 60, 74, 65])
        result = SoloStringLeapFeasibility().check(score)
        self.assertTrue(result.passed)


class TestArpeggioPatternConsistency(unittest.TestCase):
    """Test arpeggio pattern range consistency."""

    def test_no_arpeggio_notes(self):
        """No arpeggio_flow notes -> pass."""
        notes = [Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="v")]
        score = Score(tracks=[Track(name="v", notes=notes)], form="cello_prelude")
        result = ArpeggioPatternConsistency().check(score)
        self.assertTrue(result.passed)


class TestImpliedPolyphony(unittest.TestCase):
    """Test implied voice count estimation."""

    def _make_score(self, pitches, form="chaconne"):
        notes = [
            Note(pitch=p, velocity=80, start_tick=i * 240, duration=240, voice="violin")
            for i, p in enumerate(pitches)
        ]
        return Score(
            tracks=[Track(name="violin", notes=notes)],
            form=form,
            seed=1,
        )

    def test_alternating_octave_implies_two_voices(self):
        """Alternating octave jumps should imply ~2 voices."""
        # Alternate between low and high register (>P5 apart).
        pitches = [55, 72, 57, 74, 55, 72, 57, 74, 55, 72]
        score = self._make_score(pitches)
        result = ImpliedPolyphony(min_implied=2.0, max_implied=3.0).check(score)
        self.assertTrue(result.passed)

    def test_stepwise_implies_one_voice(self):
        """Sequential stepwise motion should imply 1 voice -> INFO."""
        pitches = [60, 62, 64, 65, 67, 69, 71, 72, 74, 76]
        score = self._make_score(pitches)
        result = ImpliedPolyphony(min_implied=2.0, max_implied=3.0).check(score)
        # Single strand -> implied count ~1.0 which is below min -> INFO
        self.assertFalse(result.passed)

    def test_short_score_passes(self):
        """Score with too few notes should pass."""
        pitches = [60, 72]
        score = self._make_score(pitches)
        result = ImpliedPolyphony().check(score)
        self.assertTrue(result.passed)


if __name__ == "__main__":
    unittest.main()
