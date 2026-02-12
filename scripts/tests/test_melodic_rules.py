"""Tests for melodic rules."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, NoteSource, Provenance, Score, Track
from scripts.bach_analyzer.rules.base import Severity
from scripts.bach_analyzer.rules.melodic import (
    ConsecutiveRepeatedNotes,
    ExcessiveLeap,
    LeapResolution,
    MelodicTritoneOutline,
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


class TestConsecutiveRepeatedNotesGap(unittest.TestCase):
    def test_gap_breaks_run(self):
        """Same pitch notes separated by >2 bars gap should not form a run."""
        # 3 notes at C4, then a 3-bar gap, then 3 more C4 notes
        notes = (
            [_n(60, i * 480) for i in range(3)]
            + [_n(60, 3 * 1920 + i * 480) for i in range(3)]
        )
        result = ConsecutiveRepeatedNotes(max_repeats=3).check(_score([_track("s", notes)]))
        # Each group is 3 (at limit), not 6
        self.assertTrue(result.passed)

    def test_no_gap_forms_run(self):
        """Same pitch notes without large gap should still be detected."""
        notes = [_n(60, i * 480) for i in range(5)]  # 5x C4 contiguous
        result = ConsecutiveRepeatedNotes(max_repeats=3).check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)


class TestExcessiveLeap(unittest.TestCase):
    def test_large_leap(self):
        notes = [_n(60, 0), _n(78, 480)]  # 18 semitones (> default 16)
        result = ExcessiveLeap().check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)
        self.assertEqual(result.violations[0].severity, Severity.ERROR)
        self.assertIn("18", result.violations[0].description)

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


class TestConsecutiveRepeatedNotesPedalExempt(unittest.TestCase):
    def test_pedal_point_exempt(self):
        """Repeated notes with pedal_point source should be exempt."""
        prov = Provenance(source=NoteSource.PEDAL_POINT)
        notes = [
            Note(pitch=36, velocity=80, start_tick=i * 480, duration=480,
                 voice="pedal", provenance=prov)
            for i in range(6)
        ]
        result = ConsecutiveRepeatedNotes(max_repeats=3).check(
            _score([_track("pedal", notes)])
        )
        self.assertTrue(result.passed)

    def test_ground_bass_exempt(self):
        """Repeated notes with ground_bass source should be exempt."""
        prov = Provenance(source=NoteSource.GROUND_BASS)
        notes = [
            Note(pitch=48, velocity=80, start_tick=i * 480, duration=480,
                 voice="bass", provenance=prov)
            for i in range(5)
        ]
        result = ConsecutiveRepeatedNotes(max_repeats=3).check(
            _score([_track("bass", notes)])
        )
        self.assertTrue(result.passed)


class TestLeapResolutionArpeggioExempt(unittest.TestCase):
    def test_episode_material_exempt(self):
        """Leaps in episode_material are exempt from resolution check."""
        prov = Provenance(source=NoteSource.EPISODE_MATERIAL)
        notes = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="s", provenance=prov),
            Note(pitch=67, velocity=80, start_tick=480, duration=480, voice="s", provenance=prov),
            Note(pitch=72, velocity=80, start_tick=960, duration=480, voice="s", provenance=prov),
        ]
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)

    def test_arpeggio_flow_exempt(self):
        """Leaps in arpeggio_flow are exempt from resolution check."""
        prov = Provenance(source=NoteSource.ARPEGGIO_FLOW)
        notes = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="s", provenance=prov),
            Note(pitch=67, velocity=80, start_tick=480, duration=480, voice="s", provenance=prov),
            Note(pitch=69, velocity=80, start_tick=960, duration=480, voice="s", provenance=prov),
        ]
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


class TestLeapResolutionN1Source(unittest.TestCase):
    def test_n1_episode_material_exempt(self):
        """Leap where n1 is episode_material should be exempt even if n2 is not."""
        prov_ep = Provenance(source=NoteSource.EPISODE_MATERIAL)
        prov_fc = Provenance(source=NoteSource.FREE_COUNTERPOINT)
        notes = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="s", provenance=prov_ep),
            Note(pitch=67, velocity=80, start_tick=480, duration=480, voice="s", provenance=prov_fc),
            Note(pitch=69, velocity=80, start_tick=960, duration=480, voice="s", provenance=prov_fc),
        ]
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)

    def test_both_non_arpeggio_flagged(self):
        """Leap where neither n1 nor n2 is arpeggio should still be flagged."""
        prov_fc = Provenance(source=NoteSource.FREE_COUNTERPOINT)
        notes = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="s", provenance=prov_fc),
            Note(pitch=67, velocity=80, start_tick=480, duration=480, voice="s", provenance=prov_fc),
            Note(pitch=69, velocity=80, start_tick=960, duration=480, voice="s", provenance=prov_fc),
        ]
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)


class TestLeapResolutionStructuralExempt(unittest.TestCase):
    def test_subject_exempt(self):
        """Leap in fugue_subject should be exempt from resolution check."""
        prov = Provenance(source=NoteSource.FUGUE_SUBJECT)
        notes = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="s", provenance=prov),
            Note(pitch=67, velocity=80, start_tick=480, duration=480, voice="s", provenance=prov),
            Note(pitch=69, velocity=80, start_tick=960, duration=480, voice="s", provenance=prov),
        ]
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)

    def test_answer_exempt(self):
        """Leap in fugue_answer should be exempt from resolution check."""
        prov = Provenance(source=NoteSource.FUGUE_ANSWER)
        notes = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="s", provenance=prov),
            Note(pitch=67, velocity=80, start_tick=480, duration=480, voice="s", provenance=prov),
            Note(pitch=69, velocity=80, start_tick=960, duration=480, voice="s", provenance=prov),
        ]
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)

    def test_countersubject_exempt(self):
        """Leap in countersubject should be exempt from resolution check."""
        prov = Provenance(source=NoteSource.COUNTERSUBJECT)
        notes = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="s", provenance=prov),
            Note(pitch=67, velocity=80, start_tick=480, duration=480, voice="s", provenance=prov),
            Note(pitch=69, velocity=80, start_tick=960, duration=480, voice="s", provenance=prov),
        ]
        result = LeapResolution().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


class TestExcessiveLeapThreshold(unittest.TestCase):
    def test_14_semitones_flagged(self):
        """14 semitones (> new default 13) should be flagged."""
        notes = [_n(60, 0), _n(74, 480)]
        result = ExcessiveLeap().check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)

    def test_13_semitones_ok(self):
        """Exactly 13 semitones (= threshold) should pass."""
        notes = [_n(60, 0), _n(73, 480)]
        result = ExcessiveLeap().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


class TestMelodicTritoneOutline(unittest.TestCase):
    """Test tritone outline detection in melodic contour."""

    def test_tritone_outline_detected(self):
        """D-C-F#-E: trough at C, peak at F#, outline C-F# = 6 semitones (tritone)."""
        # D4(62) -> C4(60) -> F#4(66) -> E4(64)
        # Direction: down(-2), up(+6), down(-2)
        # C4 is trough (down->up), F#4 is peak (up->down)
        # Outline from C4 to F#4 = 6 semitones = tritone
        notes = [_n(62, 0), _n(60, 480), _n(66, 960), _n(64, 1440)]
        result = MelodicTritoneOutline().check(_score([_track("s", notes)]))
        self.assertFalse(result.passed)
        self.assertEqual(len(result.violations), 1)
        self.assertIn("tritone outline", result.violations[0].description)

    def test_no_tritone_outline(self):
        """D-C-G-F: trough at C, peak at G. Outline = P5 (7 st), not tritone."""
        # D4(62) -> C4(60) -> G4(67) -> F4(65)
        # Trough at C4, peak at G4. Outline = 7 (P5, not tritone).
        notes = [_n(62, 0), _n(60, 480), _n(67, 960), _n(65, 1440)]
        result = MelodicTritoneOutline().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)

    def test_arpeggio_flow_exempt(self):
        """Arpeggio flow notes should be exempt from tritone outline check."""
        prov = Provenance(source=NoteSource.ARPEGGIO_FLOW)
        notes = [
            Note(pitch=59, velocity=80, start_tick=0, duration=480, voice="s"),
            Note(pitch=60, velocity=80, start_tick=480, duration=480, voice="s",
                 provenance=prov),
            Note(pitch=66, velocity=80, start_tick=960, duration=480, voice="s",
                 provenance=prov),
            Note(pitch=64, velocity=80, start_tick=1440, duration=480, voice="s"),
        ]
        result = MelodicTritoneOutline().check(_score([_track("s", notes)]))
        self.assertTrue(result.passed)


if __name__ == "__main__":
    unittest.main()
