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


if __name__ == "__main__":
    unittest.main()
