"""Tests for the sweep structure metrics.

The corpus-calibrated axes come from bach-mcp and are covered there; what is
pinned here is the arithmetic this module owns, plus the two conventions the
report depends on -- rests never count as literal repetition, and out-of-key is
measured against the internal C tonic rather than the requested key.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import sweep_score  # noqa: E402


def note(start: int, pitch: int, voice: int, duration: int = 480, velocity: int = 80) -> dict:
    return {
        "start_tick": start,
        "duration": duration,
        "pitch": pitch,
        "voice": voice,
        "velocity": velocity,
    }


class OutOfKeyTest(unittest.TestCase):
    def test_counts_pitch_classes_outside_the_tonic_scale(self) -> None:
        notes = [note(0, 60, 0), note(480, 61, 0), note(960, 62, 0), note(1440, 63, 0)]
        # C, Db, D, Eb against C major: Db and Eb are outside.
        self.assertAlmostEqual(sweep_score.out_of_key_ratio(notes, sweep_score.MAJOR_SCALE), 0.5)
        # Against C minor only Db is outside; Eb is the third degree.
        self.assertAlmostEqual(sweep_score.out_of_key_ratio(notes, sweep_score.MINOR_SCALE), 0.25)

    def test_minor_admits_both_sevenths(self) -> None:
        # Bb (natural seventh) and B (raised seventh) both sit inside the minor
        # set, so a descending line does not read as a departure from the key.
        self.assertIn(10, sweep_score.MINOR_SCALE)
        self.assertIn(11, sweep_score.MINOR_SCALE)

    def test_requested_key_does_not_move_the_tonic(self) -> None:
        # generated.v1 is the internal C-based representation, so a piece
        # requested in G minor still spells its tonic as C.
        generated = {"duration_ticks": 1920, "notes": [note(0, 60, 0), note(480, 67, 0)]}
        metrics = sweep_score.structural_metrics(generated, "g_minor")
        self.assertAlmostEqual(metrics["out_of_key"], 0.0)


class DuplicateBarTest(unittest.TestCase):
    def test_identical_bars_count_on_both_sides(self) -> None:
        notes = [note(0, 60, 0), note(1920, 60, 0), note(3840, 64, 0)]
        combined, worst = sweep_score.duplicate_bar_ratios(notes, 5760)
        # Bars 1 and 2 are identical, bar 3 is not: two of three sounding bars.
        self.assertAlmostEqual(worst, 2 / 3)
        self.assertAlmostEqual(combined, 2 / 3)

    def test_rests_are_not_repetition(self) -> None:
        # A voice that sounds in one bar and rests through three must not read
        # as 75% literal repetition.
        notes = [note(0, 60, 0)]
        combined, worst = sweep_score.duplicate_bar_ratios(notes, 7680)
        self.assertAlmostEqual(worst, 0.0)
        self.assertAlmostEqual(combined, 0.0)

    def test_a_shifted_copy_is_not_a_duplicate(self) -> None:
        # Same pitches, different position inside the bar.
        notes = [note(0, 60, 0), note(1920 + 240, 60, 0)]
        _, worst = sweep_score.duplicate_bar_ratios(notes, 3840)
        self.assertAlmostEqual(worst, 0.0)


class BassAndArticulationTest(unittest.TestCase):
    def test_bass_is_chosen_by_register_not_by_index(self) -> None:
        # Voice 0 is the lower line even though voice 1 has the smaller index
        # ordering convention in some builders.
        voices = {
            1: [note(0, 72, 1), note(480, 74, 1)],
            0: [note(0, 48, 0), note(480, 50, 0)],
        }
        self.assertEqual(sweep_score._bass_voice(voices)[0]["pitch"], 48)

    def test_repeated_notes_are_not_steps(self) -> None:
        line = [note(0, 48, 0), note(480, 48, 0), note(960, 50, 0)]
        # One repeat and one step out of two intervals.
        self.assertAlmostEqual(sweep_score.stepwise_ratio(line), 0.5)

    def test_arpeggiated_bass_reads_as_unmelodic(self) -> None:
        # The aria-bass shape: root, third, fifth with no step anywhere.
        line = [note(idx * 240, pitch, 0, 240) for idx, pitch in enumerate([48, 52, 55, 52])]
        self.assertAlmostEqual(sweep_score.stepwise_ratio(line), 0.0)

    def test_full_legato_reports_no_gaps(self) -> None:
        voices = {0: [note(0, 60, 0), note(480, 62, 0), note(960, 64, 0)]}
        self.assertAlmostEqual(sweep_score.note_gap_ratio(voices), 0.0)

    def test_a_detached_note_reports_a_gap(self) -> None:
        voices = {0: [note(0, 60, 0, 240), note(480, 62, 0)]}
        self.assertAlmostEqual(sweep_score.note_gap_ratio(voices), 1.0)


class AggregateTest(unittest.TestCase):
    def test_rejected_cases_are_listed_and_excluded_from_the_median(self) -> None:
        cases = [
            sweep_score.CaseResult("fugue", "severe", "c_major", 1, metrics={"bass_stepwise": 0.4}),
            sweep_score.CaseResult("fugue", "severe", "c_major", 2, metrics={"bass_stepwise": 0.6}),
            sweep_score.CaseResult("fugue", "noble", "c_major", 3, rejected=True, reason="refused"),
        ]
        report = sweep_score.aggregate(cases)
        self.assertEqual(report["accepted"], 2)
        self.assertEqual(report["rejected"], ["fugue/noble/c_major/seed3"])
        self.assertAlmostEqual(report["forms"]["fugue"]["metrics"]["bass_stepwise"], 0.5)


class ReportTest(unittest.TestCase):
    def _report(self, stepwise: float, seed: int) -> dict:
        return sweep_score.aggregate(
            [
                sweep_score.CaseResult(
                    "fugue",
                    "severe",
                    "c_major",
                    seed,
                    metrics={
                        "out_of_key": 0.01,
                        "duplicate_bars": 0.1,
                        "duplicate_bars_worst_voice": 0.3,
                        "bass_stepwise": stepwise,
                        "velocity_levels": 1.0,
                        "note_gaps": 0.0,
                        "prob_li": 0.87,
                        "excess_distance": 0.0,
                        **{f"kl_{name}": 0.0 for name in sweep_score.KL_COMPONENTS},
                    },
                )
            ]
        )

    def test_renders_without_a_baseline(self) -> None:
        text = sweep_score.format_report(self._report(0.4, 1), None)
        self.assertIn("fugue", text)
        self.assertNotIn("(delta)", text)

    def test_renders_a_signed_delta_row_against_a_baseline(self) -> None:
        baseline = self._report(0.3, 1)
        text = sweep_score.format_report(self._report(0.4, 1), baseline)
        self.assertIn("fugue (delta)", text)
        # The bass improved by ten points; the sign has to survive the padding.
        self.assertIn("+10.0", text)


if __name__ == "__main__":
    unittest.main()
