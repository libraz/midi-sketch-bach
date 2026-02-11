"""Tests for structure rules."""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.loaders.json_loader import load_json
from scripts.bach_analyzer.model import Note, NoteSource, Provenance, Score, Track
from scripts.bach_analyzer.rules.structure import ExpositionCompleteness

FIXTURES = Path(__file__).parent / "fixtures"


def _score(tracks):
    return Score(tracks=tracks)


def _track(name, notes):
    return Track(name=name, notes=notes)


def _n(pitch, tick, dur=480, voice="v", source=None):
    prov = None
    if source is not None:
        prov = Provenance(source=source, entry_number=1)
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
