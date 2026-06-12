"""Tests for fugue-subject window statistics extraction."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import subject_stats as stats  # noqa: E402


def note(onset: float, duration: float, pitch: int) -> dict:
    return {"onset": onset, "duration": duration, "pitch": pitch}


def window(pitches: list[int], *, mode: str = "major", truncated: bool = False,
           durations: list[float] | None = None, tonic_pc: int = 0) -> stats.SubjectWindow:
    return stats.SubjectWindow(
        name="toy.json",
        category="wtc1",
        mode=mode,
        source="heuristic",
        truncated=truncated,
        tonic_pc=tonic_pc,
        pitches=pitches,
        durations=durations if durations is not None else [0.5] * len(pitches),
    )


class MonophonicPrefixTest(unittest.TestCase):
    def test_stops_at_sustained_overlap(self) -> None:
        notes = [
            note(0.0, 1.0, 60),
            note(1.0, 1.0, 62),
            note(2.0, 2.0, 64),
            note(3.0, 1.0, 55),  # enters while 64 still sounds: answer entry.
            note(4.0, 1.0, 57),
        ]
        prefix, truncated = stats.monophonic_prefix(notes)
        self.assertEqual([int(n["pitch"]) for n in prefix], [60, 62, 64])
        self.assertFalse(truncated)

    def test_stops_at_chord_onset(self) -> None:
        notes = [
            note(0.0, 1.0, 60),
            note(1.0, 1.0, 62),
            note(2.0, 1.0, 64),
            note(2.0, 1.0, 55),
        ]
        prefix, truncated = stats.monophonic_prefix(notes)
        self.assertEqual([int(n["pitch"]) for n in prefix], [60, 62])
        self.assertFalse(truncated)

    def test_tiny_legato_overlap_is_not_polyphony(self) -> None:
        notes = [
            note(0.0, 1.02, 60),
            note(1.0, 1.0, 62),
            note(2.0, 1.0, 64),
        ]
        prefix, truncated = stats.monophonic_prefix(notes)
        self.assertEqual(len(prefix), 3)
        self.assertFalse(truncated)

    def test_note_cap_marks_truncated(self) -> None:
        notes = [note(float(idx), 1.0, 60 + (idx % 3)) for idx in range(10)]
        prefix, truncated = stats.monophonic_prefix(notes, max_notes=6)
        self.assertEqual(len(prefix), 6)
        self.assertTrue(truncated)

    def test_beat_cap_marks_truncated(self) -> None:
        notes = [note(float(idx), 1.0, 60 + (idx % 3)) for idx in range(10)]
        prefix, truncated = stats.monophonic_prefix(notes, max_beats=4.0)
        self.assertEqual(len(prefix), 4)
        self.assertTrue(truncated)


class DurationClassTest(unittest.TestCase):
    def test_canonical_values(self) -> None:
        self.assertEqual(stats.duration_class(0.25), "sixteenth")
        self.assertEqual(stats.duration_class(0.5), "eighth")
        self.assertEqual(stats.duration_class(0.75), "dotted_eighth")
        self.assertEqual(stats.duration_class(1.0), "quarter")
        self.assertEqual(stats.duration_class(1.5), "dotted_quarter")
        self.assertEqual(stats.duration_class(2.0), "half_plus")
        self.assertEqual(stats.duration_class(4.0), "half_plus")


class ContourArchetypeTest(unittest.TestCase):
    def test_ascent_and_descent(self) -> None:
        self.assertEqual(stats.classify_contour_archetype([60, 62, 64, 65, 67, 69, 71, 72]), "ascent")
        self.assertEqual(stats.classify_contour_archetype([72, 71, 69, 67, 65, 64, 62, 60]), "descent")

    def test_arch_and_inverted_arch(self) -> None:
        self.assertEqual(stats.classify_contour_archetype([60, 64, 67, 72, 71, 67, 64, 60]), "arch")
        self.assertEqual(
            stats.classify_contour_archetype([72, 67, 64, 60, 62, 64, 67, 72]), "inverted_arch"
        )

    def test_wave(self) -> None:
        self.assertEqual(
            stats.classify_contour_archetype([60, 67, 72, 62, 60, 71, 72, 64]), "wave"
        )


class BuildStatsTest(unittest.TestCase):
    def test_interval_chains_and_opening_pair(self) -> None:
        result = stats.build_stats([window([60, 62, 64, 65])])
        major = result["major"]
        self.assertEqual(major.windows, 1)
        self.assertEqual(major.interval_count, 3)
        self.assertEqual(major.interval_unigram[2], 2)
        self.assertEqual(major.interval_unigram[1], 1)
        self.assertEqual(major.interval_bigram[2][2], 1)
        self.assertEqual(major.interval_bigram[2][1], 1)
        self.assertEqual(major.interval_trigram[(2, 2)][1], 1)
        self.assertEqual(major.opening_pair[(2, 2)], 1)
        self.assertEqual(major.start_degree[0], 1)

    def test_truncated_window_skips_shape_stats(self) -> None:
        result = stats.build_stats([window([60, 62, 64, 65], truncated=True)])
        major = result["major"]
        self.assertEqual(major.windows, 1)
        self.assertEqual(major.clean_windows, 0)
        self.assertEqual(major.interval_count, 3)  # transitions still counted.
        self.assertEqual(sum(major.contour.values()), 0)
        self.assertEqual(major.ryden_values["note_count"], [])

    def test_ryden_features_signed_opening(self) -> None:
        features = stats.ryden_features([60, 55, 57, 59, 60])
        self.assertEqual(features["note_count"], 5)
        self.assertEqual(features["range_semitones"], 5)
        self.assertEqual(features["opening_interval"], -5)
        self.assertEqual(features["max_leap"], 5)

    def test_minor_window_routes_to_minor_mode(self) -> None:
        result = stats.build_stats([window([69, 71, 72, 71], mode="minor", tonic_pc=9)])
        self.assertEqual(result["minor"].windows, 1)
        self.assertEqual(result["major"].windows, 0)
        self.assertEqual(result["minor"].start_degree[0], 1)


class SanityReportTest(unittest.TestCase):
    def test_stepwise_corpus_passes(self) -> None:
        result = stats.build_stats(
            [window([60, 62, 64, 65, 67, 65, 64, 62, 60, 62])]
        )
        sanity = stats.sanity_report(result)
        self.assertEqual(sanity["step_is_largest"], 1.0)
        self.assertEqual(sanity["opening_majority_in_range"], 1.0)
        self.assertGreater(sanity["step_share"], 0.9)

    def test_leap_heavy_corpus_fails_step_check(self) -> None:
        result = stats.build_stats([window([60, 67, 60, 67, 60, 67, 60, 67])])
        sanity = stats.sanity_report(result)
        self.assertEqual(sanity["step_is_largest"], 0.0)


class WriteJsonTest(unittest.TestCase):
    def test_document_shape_and_string_keys(self) -> None:
        windows = [window([60, 62, 64, 65, 64, 62, 60, 62])]
        result = stats.build_stats(windows)
        sanity = stats.sanity_report(result)
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "subject_stats.json"
            stats.write_json(result, windows, sanity, out, source={"corpus_dir": "toy"})
            doc = json.loads(out.read_text())
        self.assertEqual(doc["schema"], "subject_stats.v1")
        major = doc["stats"]["major"]
        self.assertEqual(major["windows"], 1)
        self.assertIn("2", major["interval_unigram"])
        self.assertIn("2,2", major["interval_trigram"])
        self.assertIn("eighth", major["rhythm_initial"])
        self.assertEqual(len(doc["windows"]), 1)
        self.assertEqual(doc["windows"][0]["pitches"][0], 60)
        quantile_keys = major["ryden"]["note_count"]["quantiles"].keys()
        self.assertIn("p50", quantile_keys)


if __name__ == "__main__":
    unittest.main()
