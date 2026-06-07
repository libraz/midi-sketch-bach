"""Unit tests for bachlib.ornament_report (pure histogram helpers).

Synthetic generated/provenance pairs only -- no bach_cli invocation. Covers the
index-parallel Ornament selection, the bar histogram / quarter segmentation,
and the cadence-window count.
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
        self.assertEqual(summary["most_common_bar"], 9)
        self.assertEqual(summary["bar_histogram"]["9"], 2)

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


class TicksPerBarTest(unittest.TestCase):
    def test_three_four_forms(self) -> None:
        self.assertEqual(ornament_report.ticks_per_bar_for_form("passacaglia"), 1440)
        self.assertEqual(ornament_report.ticks_per_bar_for_form("chaconne"), 1440)

    def test_default_is_common_time(self) -> None:
        self.assertEqual(ornament_report.ticks_per_bar_for_form("fugue"), 1920)
        self.assertEqual(ornament_report.ticks_per_bar_for_form("toccata_and_fugue"), 1920)


if __name__ == "__main__":
    unittest.main()
