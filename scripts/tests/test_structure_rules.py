"""Tests for structure rules."""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.loaders.json_loader import load_json
from scripts.bach_analyzer.model import Note, NoteSource, Provenance, Score, Track
from scripts.bach_analyzer.rules.base import Severity
from scripts.bach_analyzer.rules.structure import ExpositionCompleteness

FIXTURES = Path(__file__).parent / "fixtures"


def _score(tracks):
    return Score(tracks=tracks)


def _track(name, notes):
    return Track(name=name, notes=notes)


def _n(pitch, tick, dur=480, voice="v", source=None, entry_number=1):
    prov = None
    if source is not None:
        prov = Provenance(source=source, entry_number=entry_number)
    return Note(pitch=pitch, velocity=80, start_tick=tick, duration=dur,
                voice=voice, provenance=prov)


class TestExpositionCompletenessProvenance(unittest.TestCase):
    def test_all_voices_enter(self):
        """All voices have subject/answer entries."""
        score = load_json(FIXTURES / "sample_with_provenance.json")
        result = ExpositionCompleteness().check(score)
        self.assertTrue(result.passed)

    def test_missing_voice(self):
        """One voice has no subject/answer."""
        soprano = _track("soprano", [_n(72, 0, voice="soprano", source=NoteSource.FUGUE_SUBJECT)])
        alto = _track("alto", [_n(60, 960, voice="alto", source=NoteSource.FREE_COUNTERPOINT)])
        result = ExpositionCompleteness().check(_score([soprano, alto]))
        self.assertFalse(result.passed)
        self.assertIn("alto", result.violations[0].description)


class TestExpositionEntryOrder(unittest.TestCase):
    def test_correct_alternation(self):
        """S-A-S-A order should pass."""
        soprano = _track("soprano", [
            _n(72, 0, voice="soprano", source=NoteSource.FUGUE_SUBJECT),
        ])
        alto = _track("alto", [
            _n(60, 960, voice="alto", source=NoteSource.FUGUE_ANSWER),
        ])
        result = ExpositionCompleteness().check(_score([soprano, alto]))
        self.assertTrue(result.passed)
        # No WARNING for alternation
        self.assertTrue(all(v.severity != Severity.WARNING for v in result.violations))

    def test_consecutive_subjects_warning(self):
        """Two consecutive subjects (no answer between) -> WARNING."""
        soprano = _track("soprano", [
            _n(72, 0, voice="soprano", source=NoteSource.FUGUE_SUBJECT, entry_number=1),
        ])
        alto = _track("alto", [
            _n(60, 960, voice="alto", source=NoteSource.FUGUE_SUBJECT, entry_number=2),
        ])
        result = ExpositionCompleteness().check(_score([soprano, alto]))
        warnings = [v for v in result.violations if v.severity == Severity.WARNING]
        self.assertTrue(len(warnings) >= 1)
        self.assertIn("consecutive subject", warnings[0].description)

    def test_answer_first_then_subject(self):
        """A-S order: still valid alternation, no warning."""
        soprano = _track("soprano", [
            _n(72, 0, voice="soprano", source=NoteSource.FUGUE_ANSWER),
        ])
        alto = _track("alto", [
            _n(60, 960, voice="alto", source=NoteSource.FUGUE_SUBJECT),
        ])
        result = ExpositionCompleteness().check(_score([soprano, alto]))
        self.assertTrue(result.passed)


class TestExpositionCompletenessHeuristic(unittest.TestCase):
    def test_all_voices_present(self):
        """All voices have notes in first bars (no provenance)."""
        score = load_json(FIXTURES / "sample_output.json")
        result = ExpositionCompleteness(max_expo_bars=12).check(score)
        self.assertTrue(result.passed)

    def test_late_entry(self):
        """A voice enters too late -> flagged."""
        soprano = _track("soprano", [_n(72, 0)])
        alto = _track("alto", [_n(60, 50000)])  # Way past exposition
        result = ExpositionCompleteness(max_expo_bars=4).check(_score([soprano, alto]))
        self.assertFalse(result.passed)


class TestFinalBarValidation(unittest.TestCase):
    """Test final bar validation rule."""

    def test_perfect_consonance_final(self):
        """Perfect consonance on final bar -> pass."""
        from scripts.bach_analyzer.rules.structure import FinalBarValidation
        soprano = _track("soprano", [
            _n(72, 0, dur=480, voice="soprano"),
            _n(72, 480, dur=960, voice="soprano"),
        ])
        bass = _track("bass", [
            _n(60, 0, dur=480, voice="bass"),
            _n(60, 480, dur=960, voice="bass"),
        ])
        score = _score([soprano, bass])
        result = FinalBarValidation().check(score)
        # Outer voice interval is P8 (12 semitones) -> consonant.
        consonance_violations = [
            v for v in result.violations if "not perfect consonance" in v.description
        ]
        self.assertEqual(len(consonance_violations), 0)

    def test_final_note_shorter_warning(self):
        """Final note shorter than preceding (short track fallback) -> WARNING."""
        from scripts.bach_analyzer.rules.structure import FinalBarValidation
        soprano = _track("soprano", [
            _n(72, 0, dur=960, voice="soprano"),
            _n(72, 960, dur=480, voice="soprano"),  # Shorter
        ])
        score = _score([soprano])
        result = FinalBarValidation().check(score)
        short_violations = [
            v for v in result.violations if "not longer" in v.description
        ]
        self.assertGreater(len(short_violations), 0)

    def test_relative_comparison_long_track(self):
        """With 6+ notes: last 2 avg vs preceding 4 avg."""
        from scripts.bach_analyzer.rules.structure import FinalBarValidation
        # 6 notes: preceding 4 have duration 480 each (avg=480)
        # last 2 have duration 960 each (avg=960 > 480) -> pass
        soprano = _track("soprano", [
            _n(72, 0, dur=480, voice="soprano"),
            _n(72, 480, dur=480, voice="soprano"),
            _n(72, 960, dur=480, voice="soprano"),
            _n(72, 1440, dur=480, voice="soprano"),
            _n(72, 1920, dur=960, voice="soprano"),
            _n(72, 2880, dur=960, voice="soprano"),
        ])
        score = _score([soprano])
        result = FinalBarValidation().check(score)
        duration_violations = [
            v for v in result.violations if "avg" in v.description or "not longer" in v.description
        ]
        self.assertEqual(len(duration_violations), 0)

    def test_relative_comparison_fails(self):
        """With 6+ notes: last 2 avg shorter than preceding 4 avg -> WARNING."""
        from scripts.bach_analyzer.rules.structure import FinalBarValidation
        soprano = _track("soprano", [
            _n(72, 0, dur=960, voice="soprano"),
            _n(72, 960, dur=960, voice="soprano"),
            _n(72, 1920, dur=960, voice="soprano"),
            _n(72, 2880, dur=960, voice="soprano"),
            _n(72, 3840, dur=240, voice="soprano"),  # Short final
            _n(72, 4080, dur=240, voice="soprano"),  # Short final
        ])
        score = _score([soprano])
        result = FinalBarValidation().check(score)
        duration_violations = [
            v for v in result.violations if "avg" in v.description
        ]
        self.assertGreater(len(duration_violations), 0)

    def test_solo_string_skips_duration(self):
        """Solo string should skip duration check."""
        from scripts.bach_analyzer.rules.structure import FinalBarValidation
        from scripts.bach_analyzer.form_profile import get_form_profile
        cello = _track("cello", [
            _n(60, 0, dur=960, voice="cello"),
            _n(62, 960, dur=480, voice="cello"),  # Shorter final
        ])
        score = Score(tracks=[cello], form="cello_prelude")
        rule = FinalBarValidation()
        profile = get_form_profile("cello_prelude")
        rule.configure(profile)
        result = rule.check(score)
        duration_violations = [
            v for v in result.violations if "not longer" in v.description or "avg" in v.description
        ]
        self.assertEqual(len(duration_violations), 0)


class TestTrioSonataVoiceCount(unittest.TestCase):
    """Test trio sonata voice count rule."""

    def test_three_voices_pass(self):
        from scripts.bach_analyzer.rules.structure import TrioSonataVoiceCount
        score = _score([
            _track("s", [_n(72, 0)]),
            _track("a", [_n(60, 0)]),
            _track("b", [_n(48, 0)]),
        ])
        result = TrioSonataVoiceCount().check(score)
        self.assertTrue(result.passed)

    def test_two_voices_fail(self):
        from scripts.bach_analyzer.rules.structure import TrioSonataVoiceCount
        score = _score([_track("s", [_n(72, 0)]), _track("b", [_n(48, 0)])])
        result = TrioSonataVoiceCount().check(score)
        self.assertFalse(result.passed)


class TestGroundBassRepetition(unittest.TestCase):
    """Test ground bass repetition checking."""

    def test_no_ground_bass_notes(self):
        from scripts.bach_analyzer.rules.structure import GroundBassRepetition
        score = _score([_track("bass", [_n(48, 0)])])
        result = GroundBassRepetition().check(score)
        self.assertTrue(result.passed)

    def test_consistent_repetition(self):
        """Two identical ground bass statements -> pass."""
        from scripts.bach_analyzer.rules.structure import GroundBassRepetition
        # Period=1 bar (1920 ticks). Two statements with same intervals.
        bass = _track("bass", [
            # Statement 1: C-D-E (bar 1)
            _n(48, 0, voice="bass", source=NoteSource.GROUND_BASS),
            _n(50, 480, voice="bass", source=NoteSource.GROUND_BASS),
            _n(52, 960, voice="bass", source=NoteSource.GROUND_BASS),
            # Statement 2: C-D-E (bar 2)
            _n(48, 1920, voice="bass", source=NoteSource.GROUND_BASS),
            _n(50, 2400, voice="bass", source=NoteSource.GROUND_BASS),
            _n(52, 2880, voice="bass", source=NoteSource.GROUND_BASS),
        ])
        score = _score([bass])
        rule = GroundBassRepetition()
        rule._period_bars = 1
        result = rule.check(score)
        self.assertTrue(result.passed)

    def test_inconsistent_repetition(self):
        """Different interval patterns -> ERROR."""
        from scripts.bach_analyzer.rules.structure import GroundBassRepetition
        bass = _track("bass", [
            # Statement 1: C-D-E (+2, +2)
            _n(48, 0, voice="bass", source=NoteSource.GROUND_BASS),
            _n(50, 480, voice="bass", source=NoteSource.GROUND_BASS),
            _n(52, 960, voice="bass", source=NoteSource.GROUND_BASS),
            # Statement 2: C-E-G (+4, +3) - different intervals
            _n(48, 1920, voice="bass", source=NoteSource.GROUND_BASS),
            _n(52, 2400, voice="bass", source=NoteSource.GROUND_BASS),
            _n(55, 2880, voice="bass", source=NoteSource.GROUND_BASS),
        ])
        score = _score([bass])
        rule = GroundBassRepetition()
        rule._period_bars = 1
        result = rule.check(score)
        self.assertFalse(result.passed)


class TestTonalAnswerInterval(unittest.TestCase):
    """Test tonal answer interval verification."""

    def test_correct_tonal_answer(self):
        """Subject P5 mutated to P4 in tonal answer -> pass."""
        from scripts.bach_analyzer.rules.structure import TonalAnswerInterval
        from scripts.bach_analyzer.model import TransformStep
        # Subject: C-G (+7 = P5)
        subject_prov = Provenance(source=NoteSource.FUGUE_SUBJECT, entry_number=1)
        # Answer: G-C (+5 = P4, tonal mutation)
        answer_prov = Provenance(
            source=NoteSource.FUGUE_ANSWER, entry_number=2,
            transform_steps=[TransformStep.TONAL_ANSWER],
        )
        soprano = _track("soprano", [
            _n(60, 0, voice="soprano", source=NoteSource.FUGUE_SUBJECT, entry_number=1),
            _n(67, 480, voice="soprano", source=NoteSource.FUGUE_SUBJECT, entry_number=1),
        ])
        alto = _track("alto", [
            Note(pitch=67, velocity=80, start_tick=960, duration=480,
                 voice="alto", provenance=answer_prov),
            Note(pitch=72, velocity=80, start_tick=1440, duration=480,
                 voice="alto", provenance=answer_prov),
        ])
        score = _score([soprano, alto])
        result = TonalAnswerInterval().check(score)
        self.assertTrue(result.passed)

    def test_no_mutation_info(self):
        """All intervals same (real answer) -> INFO about no P5<->P4 mutation."""
        from scripts.bach_analyzer.rules.structure import TonalAnswerInterval
        from scripts.bach_analyzer.model import TransformStep
        # Subject: C-E (+4)
        # Answer with tonal_answer flag but same interval (+4) -> no mutation
        answer_prov = Provenance(
            source=NoteSource.FUGUE_ANSWER, entry_number=2,
            transform_steps=[TransformStep.TONAL_ANSWER],
        )
        soprano = _track("soprano", [
            _n(60, 0, voice="soprano", source=NoteSource.FUGUE_SUBJECT, entry_number=1),
            _n(64, 480, voice="soprano", source=NoteSource.FUGUE_SUBJECT, entry_number=1),
        ])
        alto = _track("alto", [
            Note(pitch=67, velocity=80, start_tick=960, duration=480,
                 voice="alto", provenance=answer_prov),
            Note(pitch=71, velocity=80, start_tick=1440, duration=480,
                 voice="alto", provenance=answer_prov),
        ])
        score = _score([soprano, alto])
        result = TonalAnswerInterval().check(score)
        self.assertFalse(result.passed)
        self.assertTrue(any("no P5<->P4 mutation" in v.description for v in result.violations))

    def test_no_tonal_answer_pass(self):
        """No tonal answer notes -> pass (nothing to check)."""
        from scripts.bach_analyzer.rules.structure import TonalAnswerInterval
        soprano = _track("soprano", [
            _n(60, 0, voice="soprano", source=NoteSource.FUGUE_SUBJECT, entry_number=1),
        ])
        score = _score([soprano])
        result = TonalAnswerInterval().check(score)
        self.assertTrue(result.passed)


if __name__ == "__main__":
    unittest.main()
