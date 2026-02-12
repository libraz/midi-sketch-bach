"""Tests for dissonance rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, Score, Track
from scripts.bach_analyzer.rules.base import Severity
from scripts.bach_analyzer.rules.dissonance import StrongBeatDissonance, UnresolvedDissonance


def _score(tracks):
    return Score(tracks=tracks)


def _track(name, notes):
    return Track(name=name, notes=notes)


def _n(pitch, tick, dur=480, voice="v"):
    return Note(pitch=pitch, velocity=80, start_tick=tick, duration=dur, voice=voice)


class TestStrongBeatDissonance(unittest.TestCase):
    def test_dissonance_on_beat_1(self):
        """Minor 2nd on beat 1 is a violation."""
        soprano = _track("soprano", [_n(61, 0, voice="soprano")])  # C#4
        alto = _track("alto", [_n(60, 0, voice="alto")])  # C4 -> m2 = dissonant
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        self.assertFalse(result.passed)

    def test_consonance_on_beat_1(self):
        """Perfect 5th on beat 1 is fine."""
        soprano = _track("soprano", [_n(67, 0, voice="soprano")])  # G4
        alto = _track("alto", [_n(60, 0, voice="alto")])  # C4 -> P5
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_dissonance_on_weak_beat_ok(self):
        """Dissonance on beat 2 is not flagged."""
        soprano = _track("soprano", [_n(61, 480, voice="soprano")])  # beat 2
        alto = _track("alto", [_n(60, 480, voice="alto")])
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_p4_consonant_in_3_voices(self):
        """P4 is consonant when 3+ voices are present."""
        soprano = _track("soprano", [_n(65, 0, voice="soprano")])  # F4
        alto = _track("alto", [_n(60, 0, voice="alto")])  # C4 -> P4
        bass = _track("bass", [_n(48, 0, voice="bass")])  # C3
        result = StrongBeatDissonance().check(_score([soprano, alto, bass]))
        self.assertTrue(result.passed)


class TestUnresolvedDissonance(unittest.TestCase):
    def test_resolved_dissonance(self):
        """Minor 2nd resolving by step to P5."""
        soprano = _track("soprano", [
            _n(61, 0, voice="soprano"),   # C#4 (dissonant with C4)
            _n(60, 480, voice="soprano"),  # C4 resolves by step down
        ])
        alto = _track("alto", [
            _n(60, 0, voice="alto"),
            _n(53, 480, voice="alto"),  # F3 -> P5 with C4
        ])
        result = UnresolvedDissonance().check(_score([soprano, alto]))
        self.assertTrue(result.passed)

    def test_unresolved_dissonance(self):
        """Minor 2nd not resolved."""
        soprano = _track("soprano", [
            _n(61, 0, 480, voice="soprano"),  # C#4
            _n(65, 480, 480, voice="soprano"),  # F4 (leap, not step)
        ])
        alto = _track("alto", [
            _n(60, 0, 480, voice="alto"),
            _n(60, 480, 480, voice="alto"),  # Stays C4 -> F4/C4 = P4 (2 voices = dissonant)
        ])
        result = UnresolvedDissonance().check(_score([soprano, alto]))
        self.assertFalse(result.passed)


class TestSuspensionExemption(unittest.TestCase):
    """StrongBeatDissonance should exempt prepared suspensions."""

    def test_prepared_suspension_exempt(self):
        """4-3 suspension: C5 prepared on beat 4, held on beat 1 against E4, resolves to B4."""
        soprano = _track("soprano", [
            _n(72, 1440, 960, voice="soprano"),  # C5 prepared on beat 4, sustains to beat 1
            _n(71, 2400, 480, voice="soprano"),   # B4 resolution (step down)
        ])
        alto = _track("alto", [
            _n(64, 0, 1920, voice="alto"),        # E4 on bar 1
            _n(64, 1920, 480, voice="alto"),       # E4 on bar 2 beat 1 (m2 with C5)
        ])
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        self.assertTrue(result.passed, f"Suspension should be exempt: {result.violations}")

    def test_unprepared_dissonance_not_exempt(self):
        """Unprepared dissonance on beat 1 should still be flagged."""
        soprano = _track("soprano", [
            _n(60, 0, 480, voice="soprano"),       # C4 on beat 1
        ])
        alto = _track("alto", [
            _n(61, 0, 480, voice="alto"),           # C#4 on beat 1 (m2, unprepared)
        ])
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        self.assertFalse(result.passed)


class TestAccentedDissonanceExemption(unittest.TestCase):
    """Accented dissonances (appoggiatura, accented passing/neighbor) should be exempt."""

    def test_accented_passing_tone_exempt(self):
        """Accented passing tone: step approach, step resolution in same direction.
        E4 -> F4 (dissonant with E4 in alto on beat 1) -> G4."""
        soprano = _track("soprano", [
            _n(64, 1440, 480, voice="soprano"),  # E4 on beat 4 (approach)
            _n(65, 1920, 480, voice="soprano"),  # F4 on beat 1 (dissonant)
            _n(67, 2400, 480, voice="soprano"),  # G4 resolution by step
        ])
        alto = _track("alto", [
            _n(64, 0, 1920, voice="alto"),       # E4 bar 1
            _n(64, 1920, 960, voice="alto"),      # E4 bar 2 (m2 with F4)
        ])
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        self.assertTrue(result.passed,
                        f"Accented passing tone should be exempt: {result.violations}")


if __name__ == "__main__":
    unittest.main()
