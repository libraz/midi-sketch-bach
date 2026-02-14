"""Tests for dissonance rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, NoteSource, Provenance, Score, Track
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


class TestDiminishedSeventhContext(unittest.TestCase):
    """Diminished 7th chord context should downgrade dissonance severity to INFO."""

    def test_dim7_chord_info(self):
        """B-D-F-Ab (dim7) on beat 1: dissonances within should be INFO."""
        # B3=59, D4=62, F4=65, Ab4=68 -> pitch classes 11,2,5,8 (all 3 apart)
        soprano = _track("soprano", [_n(68, 0, voice="soprano")])  # Ab4
        alto = _track("alto", [_n(65, 0, voice="alto")])          # F4
        tenor = _track("tenor", [_n(62, 0, voice="tenor")])       # D4
        bass = _track("bass", [_n(59, 0, voice="bass")])          # B3
        result = StrongBeatDissonance().check(_score([soprano, alto, tenor, bass]))
        # All dissonances in a dim7 context should be INFO, not WARNING.
        for v in result.violations:
            self.assertEqual(v.severity, Severity.INFO,
                             f"Expected INFO for dim7 context, got {v.severity}: {v.description}")

    def test_non_dim7_stays_warning(self):
        """Non-dim7 dissonance should remain WARNING."""
        soprano = _track("soprano", [_n(61, 0, voice="soprano")])  # C#4
        alto = _track("alto", [_n(60, 0, voice="alto")])          # C4 -> m2 dissonant
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        diss_violations = [v for v in result.violations if v.severity != Severity.INFO]
        self.assertTrue(len(diss_violations) > 0)


class TestDominantSeventhContext(unittest.TestCase):
    """Dominant 7th chord context should downgrade dissonance severity to INFO."""

    def test_v7_chord_info(self):
        """G-B-D-F (V7 in C) on beat 1: dissonances within should be INFO."""
        # G3=55, B3=59, D4=62, F4=65 -> pitch classes 7,11,2,5
        # V7 template from root G: {7, 11, 2, 5} = {0,+4,+7,+10} from 7
        soprano = _track("soprano", [_n(65, 0, voice="soprano")])  # F4
        alto = _track("alto", [_n(62, 0, voice="alto")])          # D4
        tenor = _track("tenor", [_n(59, 0, voice="tenor")])       # B3
        bass = _track("bass", [_n(55, 0, voice="bass")])          # G3
        result = StrongBeatDissonance().check(_score([soprano, alto, tenor, bass]))
        for v in result.violations:
            self.assertEqual(v.severity, Severity.INFO,
                             f"Expected INFO for V7 context, got {v.severity}: {v.description}")

    def test_v7_incomplete_info(self):
        """G-B-F (V7 without 5th) on beat 1: tritone B-F should be INFO."""
        # B3=59, F4=65, G3=55 -> pitch classes 11,5,7 -> subset of V7(G)
        soprano = _track("soprano", [_n(65, 0, voice="soprano")])  # F4
        alto = _track("alto", [_n(59, 0, voice="alto")])          # B3
        bass = _track("bass", [_n(55, 0, voice="bass")])          # G3
        result = StrongBeatDissonance().check(_score([soprano, alto, bass]))
        for v in result.violations:
            self.assertEqual(v.severity, Severity.INFO,
                             f"Expected INFO for incomplete V7 context, got {v.severity}: {v.description}")

    def test_non_v7_stays_warning(self):
        """C-E-Bb is NOT a V7 subset -> dissonance stays WARNING."""
        # C4=60, E4=64, Bb4=70 -> pitch classes 0,4,10
        # Not a subset of any V7: would need {0,4,7,10} but 7 is missing
        # Actually {0,4,10} IS a subset of V7 on C: {0,4,7,10}. Let me pick a non-V7.
        # C4=60, Db4=61 -> pitch classes 0,1 -> not a V7 subset
        soprano = _track("soprano", [_n(61, 0, voice="soprano")])  # Db4
        alto = _track("alto", [_n(60, 0, voice="alto")])          # C4
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        warn_violations = [v for v in result.violations if v.severity == Severity.WARNING]
        self.assertTrue(len(warn_violations) > 0,
                        "Non-V7 dissonance should remain WARNING")


class TestDiatonicSeventhContext(unittest.TestCase):
    """Diatonic 7th chord context (minor 7th, half-dim 7th) should downgrade to INFO."""

    def test_minor_seventh_chord_info(self):
        """D-F-A-C (Dm7, ii7 in C) on beat 1: dissonances should be INFO."""
        # D3=50, F3=53, A3=57, C4=60 -> pitch classes 2,5,9,0 -> minor 7th from D
        soprano = _track("soprano", [_n(60, 0, voice="soprano")])  # C4
        alto = _track("alto", [_n(57, 0, voice="alto")])           # A3
        tenor = _track("tenor", [_n(53, 0, voice="tenor")])        # F3
        bass = _track("bass", [_n(50, 0, voice="bass")])           # D3
        result = StrongBeatDissonance().check(_score([soprano, alto, tenor, bass]))
        for v in result.violations:
            self.assertEqual(v.severity, Severity.INFO,
                             f"Expected INFO for Dm7 context, got {v.severity}: {v.description}")

    def test_half_dim_seventh_chord_info(self):
        """B-D-F-A (Bø7, viiø7 in C) on beat 1: dissonances should be INFO."""
        # B3=59, D4=62, F4=65, A4=69 -> pitch classes 11,2,5,9 -> half-dim from B
        soprano = _track("soprano", [_n(69, 0, voice="soprano")])  # A4
        alto = _track("alto", [_n(65, 0, voice="alto")])           # F4
        tenor = _track("tenor", [_n(62, 0, voice="tenor")])        # D4
        bass = _track("bass", [_n(59, 0, voice="bass")])           # B3
        result = StrongBeatDissonance().check(_score([soprano, alto, tenor, bass]))
        for v in result.violations:
            self.assertEqual(v.severity, Severity.INFO,
                             f"Expected INFO for Bø7 context, got {v.severity}: {v.description}")

    def test_major_seventh_excluded(self):
        """C-E-G-B (Cmaj7) should NOT get seventh_context downgrade (major 7th excluded)."""
        # C4=60, E4=64, G4=67, B4=71 -> pitch classes 0,4,7,11
        # Major 7th template (0,4,7,11) is NOT in _SEVENTH_TEMPLATES.
        # B-C dissonance (m2) should remain WARNING.
        soprano = _track("soprano", [_n(71, 0, voice="soprano")])  # B4
        alto = _track("alto", [_n(67, 0, voice="alto")])           # G4
        tenor = _track("tenor", [_n(64, 0, voice="tenor")])        # E4
        bass = _track("bass", [_n(60, 0, voice="bass")])           # C4
        result = StrongBeatDissonance().check(_score([soprano, alto, tenor, bass]))
        warn_violations = [v for v in result.violations if v.severity == Severity.WARNING]
        self.assertTrue(len(warn_violations) > 0,
                        "Major 7th chord dissonance should remain WARNING (not downgraded)")

    def test_incomplete_minor_seventh_info(self):
        """D-F-C (Dm7 without 5th, 3 voices) should still get INFO."""
        # D3=50, F3=53, C4=60 -> pitch classes 2,5,0 -> subset of minor 7th from D {2,5,9,0}
        soprano = _track("soprano", [_n(60, 0, voice="soprano")])  # C4
        alto = _track("alto", [_n(53, 0, voice="alto")])           # F3
        bass = _track("bass", [_n(50, 0, voice="bass")])           # D3
        result = StrongBeatDissonance().check(_score([soprano, alto, bass]))
        for v in result.violations:
            self.assertEqual(v.severity, Severity.INFO,
                             f"Expected INFO for incomplete Dm7 context, got {v.severity}: {v.description}")

    def test_two_voices_no_seventh_context(self):
        """Only 2 voices should NOT get seventh_context (density guard)."""
        # D3=50, C4=60 -> only 2 notes -> _is_diatonic_seventh_context returns False
        soprano = _track("soprano", [_n(60, 0, voice="soprano")])  # C4
        bass = _track("bass", [_n(50, 0, voice="bass")])           # D3
        result = StrongBeatDissonance().check(_score([soprano, bass]))
        warn_violations = [v for v in result.violations if v.severity == Severity.WARNING]
        self.assertTrue(len(warn_violations) > 0,
                        "2-voice dissonance should remain WARNING (no seventh context)")



class TestGoldbergSuspensionDowngrade(unittest.TestCase):
    """GOLDBERG_SUSPENSION source should downgrade strong-beat dissonance to INFO."""

    def test_goldberg_suspension_info(self):
        """Dissonance where one note is GOLDBERG_SUSPENSION should be INFO."""
        prov = Provenance(source=NoteSource.GOLDBERG_SUSPENSION)
        soprano = _track("soprano", [
            Note(pitch=61, velocity=80, start_tick=0, duration=480,
                 voice="soprano", provenance=prov),
        ])
        alto = _track("alto", [_n(60, 0, voice="alto")])  # m2 dissonance on beat 1
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertEqual(result.violations[0].severity, Severity.INFO)

    def test_non_suspension_source_remains_warning(self):
        """Dissonance without GOLDBERG_SUSPENSION should remain WARNING."""
        prov = Provenance(source=NoteSource.FREE_COUNTERPOINT)
        soprano = _track("soprano", [
            Note(pitch=61, velocity=80, start_tick=0, duration=480,
                 voice="soprano", provenance=prov),
        ])
        alto = _track("alto", [_n(60, 0, voice="alto")])
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertEqual(result.violations[0].severity, Severity.WARNING)

    def test_goldberg_suspension_not_skipped(self):
        """GOLDBERG_SUSPENSION should be downgraded, not skipped (violation still recorded)."""
        prov = Provenance(source=NoteSource.GOLDBERG_SUSPENSION)
        soprano = _track("soprano", [
            Note(pitch=61, velocity=80, start_tick=0, duration=480,
                 voice="soprano", provenance=prov),
        ])
        alto = _track("alto", [_n(60, 0, voice="alto")])
        result = StrongBeatDissonance().check(_score([soprano, alto]))
        # Must have violations (not skipped)
        self.assertTrue(len(result.violations) > 0)
        # But severity is INFO
        self.assertEqual(result.violations[0].severity, Severity.INFO)


if __name__ == "__main__":
    unittest.main()
