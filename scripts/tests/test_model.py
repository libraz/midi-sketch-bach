"""Tests for the unified data model."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import (
    CONSONANCES,
    DISSONANCES,
    IMPERFECT_CONSONANCES,
    MAJOR_3RD,
    MINOR_3RD,
    Note,
    NoteSource,
    OCTAVE,
    PERFECT_5TH,
    PERFECT_CONSONANCES,
    Provenance,
    Score,
    SOURCE_STRING_MAP,
    TICKS_PER_BAR,
    TICKS_PER_BEAT,
    Track,
    TransformStep,
    UNISON,
    interval_class,
    is_consonant,
    is_dissonant,
    is_perfect_consonance,
    pitch_to_name,
)


class TestConstants(unittest.TestCase):
    def test_ticks(self):
        self.assertEqual(TICKS_PER_BEAT, 480)
        self.assertEqual(TICKS_PER_BAR, 1920)

    def test_interval_sets(self):
        self.assertIn(UNISON, PERFECT_CONSONANCES)
        self.assertIn(PERFECT_5TH, PERFECT_CONSONANCES)
        self.assertIn(OCTAVE, PERFECT_CONSONANCES)
        self.assertIn(MINOR_3RD, IMPERFECT_CONSONANCES)
        self.assertIn(MAJOR_3RD, IMPERFECT_CONSONANCES)
        self.assertEqual(len(CONSONANCES), 7)
        self.assertEqual(len(DISSONANCES), 6)


class TestNoteSource(unittest.TestCase):
    def test_enum_values(self):
        self.assertEqual(NoteSource.UNKNOWN, 0)
        self.assertEqual(NoteSource.FUGUE_SUBJECT, 1)
        self.assertEqual(NoteSource.CODA, 16)

    def test_source_string_map(self):
        self.assertEqual(SOURCE_STRING_MAP["fugue_subject"], NoteSource.FUGUE_SUBJECT)
        self.assertEqual(SOURCE_STRING_MAP["episode_material"], NoteSource.EPISODE_MATERIAL)

    def test_transform_step(self):
        self.assertEqual(TransformStep.TONAL_ANSWER, 1)
        self.assertEqual(TransformStep.KEY_TRANSPOSE, 11)


class TestNote(unittest.TestCase):
    def setUp(self):
        self.note = Note(
            pitch=60, velocity=80, start_tick=1920, duration=480, voice="soprano"
        )

    def test_end_tick(self):
        self.assertEqual(self.note.end_tick, 2400)

    def test_bar(self):
        self.assertEqual(self.note.bar, 2)  # tick 1920 = start of bar 2

    def test_beat(self):
        self.assertEqual(self.note.beat, 1)  # beat 1 of bar 2

    def test_strong_beat(self):
        self.assertTrue(self.note.is_on_strong_beat)  # beat 1
        note_b2 = Note(pitch=60, velocity=80, start_tick=480, duration=480, voice="s")
        self.assertFalse(note_b2.is_on_strong_beat)  # beat 2

    def test_pitch_class(self):
        self.assertEqual(self.note.pitch_class, 0)  # C

    def test_note_name(self):
        self.assertEqual(self.note.note_name, "C")

    def test_octave(self):
        self.assertEqual(self.note.octave, 4)  # C4 = MIDI 60


class TestProvenance(unittest.TestCase):
    def test_source_string(self):
        prov = Provenance(source=NoteSource.FUGUE_SUBJECT)
        self.assertEqual(prov.source_string, "fugue_subject")

    def test_unknown_source_string(self):
        prov = Provenance()
        self.assertEqual(prov.source_string, "unknown")


class TestTrack(unittest.TestCase):
    def test_sorted_notes(self):
        notes = [
            Note(pitch=60, velocity=80, start_tick=960, duration=480, voice="s"),
            Note(pitch=62, velocity=80, start_tick=0, duration=480, voice="s"),
        ]
        track = Track(name="soprano", notes=notes)
        sorted_n = track.sorted_notes
        self.assertEqual(sorted_n[0].start_tick, 0)
        self.assertEqual(sorted_n[1].start_tick, 960)


class TestScore(unittest.TestCase):
    def setUp(self):
        self.score = Score(
            tracks=[
                Track(
                    name="soprano",
                    notes=[
                        Note(pitch=72, velocity=80, start_tick=0, duration=480, voice="soprano"),
                        Note(pitch=74, velocity=80, start_tick=480, duration=480, voice="soprano"),
                    ],
                ),
                Track(
                    name="alto",
                    notes=[
                        Note(pitch=60, velocity=80, start_tick=0, duration=960, voice="alto"),
                    ],
                ),
            ],
            seed=42,
            form="fugue",
        )

    def test_all_notes(self):
        self.assertEqual(len(self.score.all_notes), 3)

    def test_voices_dict(self):
        vd = self.score.voices_dict
        self.assertIn("soprano", vd)
        self.assertIn("alto", vd)
        self.assertEqual(len(vd["soprano"]), 2)

    def test_num_voices(self):
        self.assertEqual(self.score.num_voices, 2)

    def test_total_notes(self):
        self.assertEqual(self.score.total_notes, 3)

    def test_total_duration(self):
        self.assertEqual(self.score.total_duration, 960)

    def test_total_bars(self):
        self.assertEqual(self.score.total_bars, 1)

    def test_has_provenance(self):
        self.assertFalse(self.score.has_provenance)


class TestUtilities(unittest.TestCase):
    def test_interval_class(self):
        self.assertEqual(interval_class(7), 7)
        self.assertEqual(interval_class(19), 7)  # 19 = 12 + 7
        self.assertEqual(interval_class(-7), 7)

    def test_is_consonant(self):
        self.assertTrue(is_consonant(0))
        self.assertTrue(is_consonant(7))
        self.assertTrue(is_consonant(3))
        self.assertFalse(is_consonant(1))

    def test_is_perfect_consonance(self):
        self.assertTrue(is_perfect_consonance(0))
        self.assertTrue(is_perfect_consonance(7))
        self.assertFalse(is_perfect_consonance(3))

    def test_is_dissonant(self):
        self.assertTrue(is_dissonant(1))
        self.assertTrue(is_dissonant(6))
        self.assertFalse(is_dissonant(7))

    def test_pitch_to_name(self):
        self.assertEqual(pitch_to_name(60), "C4")
        self.assertEqual(pitch_to_name(69), "A4")


if __name__ == "__main__":
    unittest.main()
