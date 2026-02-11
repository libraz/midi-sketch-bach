"""Tests for JSON loader."""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.loaders.json_loader import load_json
from scripts.bach_analyzer.model import NoteSource, TransformStep

FIXTURES = Path(__file__).parent / "fixtures"


class TestJsonLoaderBasic(unittest.TestCase):
    """Test loading the basic sample_output.json (no provenance)."""

    def setUp(self):
        self.score = load_json(FIXTURES / "sample_output.json")

    def test_tracks(self):
        self.assertEqual(len(self.score.tracks), 3)
        self.assertEqual(self.score.tracks[0].name, "soprano")
        self.assertEqual(self.score.tracks[1].name, "alto")
        self.assertEqual(self.score.tracks[2].name, "bass")

    def test_metadata(self):
        self.assertEqual(self.score.seed, 42)
        self.assertEqual(self.score.form, "fugue")
        self.assertEqual(self.score.key, "C_major")

    def test_notes(self):
        soprano = self.score.tracks[0]
        self.assertEqual(len(soprano.notes), 8)
        self.assertEqual(soprano.notes[0].pitch, 72)
        self.assertEqual(soprano.notes[0].start_tick, 0)
        self.assertEqual(soprano.notes[0].duration, 480)

    def test_no_provenance(self):
        self.assertFalse(self.score.has_provenance)

    def test_total_notes(self):
        self.assertEqual(self.score.total_notes, 24)

    def test_channel(self):
        self.assertEqual(self.score.tracks[0].channel, 0)
        self.assertEqual(self.score.tracks[2].channel, 3)


class TestJsonLoaderProvenance(unittest.TestCase):
    """Test loading sample_with_provenance.json."""

    def setUp(self):
        self.score = load_json(FIXTURES / "sample_with_provenance.json")

    def test_has_provenance(self):
        self.assertTrue(self.score.has_provenance)

    def test_subject_provenance(self):
        note = self.score.tracks[0].notes[0]
        self.assertIsNotNone(note.provenance)
        self.assertEqual(note.provenance.source, NoteSource.FUGUE_SUBJECT)
        self.assertEqual(note.provenance.entry_number, 1)

    def test_answer_provenance(self):
        note = self.score.tracks[1].notes[0]
        self.assertIsNotNone(note.provenance)
        self.assertEqual(note.provenance.source, NoteSource.FUGUE_ANSWER)
        self.assertEqual(note.provenance.entry_number, 2)
        self.assertIn(TransformStep.TONAL_ANSWER, note.provenance.transform_steps)

    def test_episode_provenance(self):
        note = self.score.tracks[0].notes[4]
        self.assertIsNotNone(note.provenance)
        self.assertEqual(note.provenance.source, NoteSource.EPISODE_MATERIAL)
        self.assertIn(TransformStep.SEQUENCE, note.provenance.transform_steps)


class TestJsonLoaderFromDict(unittest.TestCase):
    """Test loading from a pre-parsed dict."""

    def test_load_dict(self):
        data = {
            "seed": 1,
            "tracks": [
                {
                    "name": "test",
                    "channel": 0,
                    "program": 19,
                    "notes": [
                        {"pitch": 60, "velocity": 80, "start_ticks": 0, "duration_ticks": 480}
                    ],
                }
            ],
        }
        score = load_json(data)
        self.assertEqual(score.seed, 1)
        self.assertEqual(len(score.tracks), 1)
        self.assertEqual(score.tracks[0].notes[0].pitch, 60)
        self.assertIsNone(score.source_file)


if __name__ == "__main__":
    unittest.main()
