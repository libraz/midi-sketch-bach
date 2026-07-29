"""Unit tests for bachlib.ornament_report (pure histogram + classifier helpers).

Synthetic generated/provenance pairs only -- no bach_cli invocation. Covers the
index-parallel Ornament selection, the bar histogram / quarter segmentation,
the cadence-window count, and the run-shape classifier (trill / mordent / turn
/ appoggiatura / slide, plus false-positive rejection).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import ornament_report  # noqa: E402


def _note(start_tick: int) -> dict:
    return {"start_tick": start_tick, "duration": 480, "pitch": 60, "voice": 0, "velocity": 80}


def _prov(index: int, source: str) -> dict:
    return {"index": index, "source": source}


class OrnamentBarsTest(unittest.TestCase):
    def test_selects_only_ornament_sourced_notes(self) -> None:
        generated = {"notes": [_note(0), _note(1920), _note(3840), _note(5760)]}
        provenance = {
            "notes": [
                _prov(0, "Material"),
                _prov(1, "Ornament"),
                _prov(2, "Compose"),
                _prov(3, "Ornament"),
            ]
        }
        bars = ornament_report.ornament_bars(generated, provenance, 1920)
        self.assertEqual(bars, [1, 3])

    def test_uses_index_field_not_list_position(self) -> None:
        # Provenance order may differ; the index field is authoritative.
        generated = {"notes": [_note(0), _note(1920)]}
        provenance = {"notes": [_prov(1, "Ornament"), _prov(0, "Material")]}
        bars = ornament_report.ornament_bars(generated, provenance, 1920)
        self.assertEqual(bars, [1])

    def test_three_four_bar_length(self) -> None:
        generated = {"notes": [_note(1440), _note(2880)]}
        provenance = {"notes": [_prov(0, "Ornament"), _prov(1, "Ornament")]}
        bars = ornament_report.ornament_bars(generated, provenance, 1440)
        self.assertEqual(bars, [1, 2])


class DistributionSummaryTest(unittest.TestCase):
    def test_quarters_middle_half_and_cadence(self) -> None:
        # 16-bar piece: quarters are bars 0-3 / 4-7 / 8-11 / 12-15; the cadence
        # window is bars 14-15.
        bars = [0, 5, 9, 9, 15]
        summary = ornament_report.distribution_summary(bars, total_bars=16)
        self.assertEqual(summary["ornament_notes"], 5)
        self.assertEqual(summary["quarter_counts"], [1, 1, 2, 1])
        self.assertEqual(summary["middle_half_count"], 3)
        self.assertEqual(summary["cadence_window_count"], 1)
        self.assertEqual(summary["most_common_bar"], 10)
        self.assertEqual(summary["bar_histogram"]["10"], 2)

    def test_empty_input(self) -> None:
        summary = ornament_report.distribution_summary([], total_bars=8)
        self.assertEqual(summary["ornament_notes"], 0)
        self.assertEqual(summary["quarter_counts"], [0, 0, 0, 0])
        self.assertEqual(summary["middle_half_count"], 0)
        self.assertEqual(summary["cadence_window_count"], 0)
        self.assertIsNone(summary["most_common_bar"])

    def test_last_bar_lands_in_final_quarter(self) -> None:
        # bar * 4 // total must clamp to quarter 3 for the final bar even when
        # total_bars is not divisible by 4.
        summary = ornament_report.distribution_summary([6], total_bars=7)
        self.assertEqual(summary["quarter_counts"], [0, 0, 0, 1])


def _run_note(start: int, dur: int, pitch: int, voice: int = 0) -> dict:
    return {"start_tick": start, "duration": dur, "pitch": pitch, "voice": voice, "velocity": 80}


def _chain(specs: list[tuple[int, int]], start: int = 0, voice: int = 0) -> list[dict]:
    """Build a contiguous run from (duration, pitch) pairs."""
    notes = []
    tick = start
    for dur, pitch in specs:
        notes.append(_run_note(tick, dur, pitch, voice))
        tick += dur
    return notes


class ClassifyRunTest(unittest.TestCase):
    def test_appoggiatura_descending_lean(self) -> None:
        run = _chain([(240, 74), (240, 72)])
        self.assertEqual(ornament_report.classify_run(run), "appoggiatura")

    def test_rising_two_note_pair_is_not_appoggiatura(self) -> None:
        run = _chain([(240, 72), (240, 74)])
        self.assertEqual(ornament_report.classify_run(run), "unknown")

    def test_mordent_main_lower_main(self) -> None:
        run = _chain([(60, 72), (60, 71), (360, 72)])
        self.assertEqual(ornament_report.classify_run(run), "mordent")

    def test_slide_two_rising_graces(self) -> None:
        run = _chain([(60, 76), (60, 77), (840, 79)])
        self.assertEqual(ornament_report.classify_run(run), "slide")

    def test_turn_upper_main_lower_main(self) -> None:
        run = _chain([(60, 77), (60, 76), (60, 74), (780, 76)])
        self.assertEqual(ornament_report.classify_run(run), "turn")

    def test_trill_alternation_with_nachschlag(self) -> None:
        run = _chain([(60, 74), (60, 72), (60, 74), (60, 72), (60, 71), (180, 72)])
        self.assertEqual(ornament_report.classify_run(run), "trill")

    def test_appuy_opening_classifies_as_trill(self) -> None:
        # Held upper tone, then alternation: still one trill run.
        run = _chain([(480, 74), (60, 72), (60, 74), (60, 72), (60, 71), (240, 72)])
        self.assertEqual(ornament_report.classify_run(run), "trill")

    def test_von_unten_opening_classifies_as_trill(self) -> None:
        # Lower -> main prefix, then alternation.
        run = _chain([(60, 71), (60, 72), (60, 74), (60, 72), (60, 71), (180, 72)])
        self.assertEqual(ornament_report.classify_run(run), "trill")

    def test_four_sixteenth_slots_are_trill_not_turn(self) -> None:
        # A 4-slot trill paced at sixteenths shares the turn's contour but not
        # its 32nd grace tones.
        run = _chain([(120, 74), (120, 72), (120, 71), (120, 72)])
        self.assertEqual(ornament_report.classify_run(run), "trill")


class OrnamentRunsTest(unittest.TestCase):
    def test_gap_splits_runs_and_voices_are_separate(self) -> None:
        lean = _chain([(240, 74), (240, 72)], start=0, voice=0)
        # Same voice, after a gap: a second run.
        mordent = _chain([(60, 72), (60, 71), (360, 72)], start=1920, voice=0)
        # Another voice at overlapping ticks: its own run.
        other = _chain([(240, 67), (240, 65)], start=0, voice=1)
        notes = lean + mordent + other
        generated = {"notes": notes}
        provenance = {"notes": [_prov(i, "Ornament") for i in range(len(notes))]}
        runs = ornament_report.ornament_runs(generated, provenance)
        self.assertEqual(len(runs), 3)
        kinds = sorted(ornament_report.classify_run(run) for run in runs)
        self.assertEqual(kinds, ["appoggiatura", "appoggiatura", "mordent"])


class SegmentRunTest(unittest.TestCase):
    def test_adjacent_figures_in_one_run_are_split(self) -> None:
        # The pass gates per (bar, voice): two turns on adjacent quarters abut
        # into ONE Ornament run and must parse as two figures.
        turn_a = _chain([(60, 77), (60, 76), (60, 74), (300, 76)], start=0)
        turn_b = _chain([(60, 79), (60, 77), (60, 76), (300, 77)], start=480)
        figures = ornament_report.segment_run(turn_a + turn_b)
        self.assertEqual([kind for kind, _ in figures], ["turn", "turn"])

    def test_lean_abutting_turn_parses_both(self) -> None:
        lean = _chain([(480, 74), (480, 72)], start=0)
        turn = _chain([(60, 77), (60, 76), (60, 74), (780, 76)], start=960)
        figures = ornament_report.segment_run(lean + turn)
        self.assertEqual([kind for kind, _ in figures], ["appoggiatura", "turn"])

    def test_mixed_run_classifies_compound(self) -> None:
        lean = _chain([(480, 74), (480, 72)], start=0)
        turn = _chain([(60, 77), (60, 76), (60, 74), (780, 76)], start=960)
        self.assertEqual(ornament_report.classify_run(lean + turn), "compound")


class KindSummaryTest(unittest.TestCase):
    def test_counts_and_cadence_trill_flag(self) -> None:
        # 8-bar piece (1920-tick bars): trill in bar 6 (cadence window) plus a
        # mid-piece mordent.
        trill = _chain([(60, 74), (60, 72), (60, 74), (60, 72), (60, 71), (180, 72)],
                       start=6 * 1920, voice=0)
        mordent = _chain([(60, 72), (60, 71), (360, 72)], start=2 * 1920, voice=0)
        summary = ornament_report.kind_summary([trill, mordent], total_bars=8, ticks_per_bar=1920)
        self.assertEqual(summary["kind_counts"], {"trill": 1, "mordent": 1})
        self.assertTrue(summary["cadence_window_trill"])

    def test_mid_piece_trill_does_not_set_cadence_flag(self) -> None:
        trill = _chain([(60, 74), (60, 72), (60, 74), (60, 72), (60, 71), (180, 72)],
                       start=2 * 1920, voice=0)
        summary = ornament_report.kind_summary([trill], total_bars=8, ticks_per_bar=1920)
        self.assertEqual(summary["kind_counts"], {"trill": 1})
        self.assertFalse(summary["cadence_window_trill"])


class TicksPerBarTest(unittest.TestCase):
    def test_three_four_forms(self) -> None:
        self.assertEqual(ornament_report.ticks_per_bar_for_form("passacaglia"), 1440)
        self.assertEqual(ornament_report.ticks_per_bar_for_form("chaconne"), 1440)

    def test_default_is_common_time(self) -> None:
        self.assertEqual(ornament_report.ticks_per_bar_for_form("fugue"), 1920)
        self.assertEqual(ornament_report.ticks_per_bar_for_form("toccata_and_fugue"), 1920)


if __name__ == "__main__":
    unittest.main()
