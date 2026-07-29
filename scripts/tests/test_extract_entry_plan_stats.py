"""Tests for entry-plan statistics extraction."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import extract_entry_plan as extract_entry_plan_stats  # noqa: E402


class ExtractEntryPlanStatsTest(unittest.TestCase):
    def test_summarize_and_write_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            csv_path = root / "entries.csv"
            csv_path.write_text(
                "\n".join(
                    [
                        "piece,bar,voice,kind,duration",
                        "wtc01,1,0,S,2",
                        "wtc01,5,1,S,2",
                        "wtc01,9,2,S,2",
                        "wtc02,2,0,S,2",
                        "wtc02,8,1,S,2",
                    ]
                ),
                encoding="utf-8",
            )

            rows = extract_entry_plan_stats.load_rows([csv_path])
            summary = extract_entry_plan_stats.summarize(rows)
            self.assertEqual(summary["piece_count"], 2)
            self.assertAlmostEqual(summary["subject_length_mean"], 2.0)
            # Entry interval gaps: wtc01 (4, 4), wtc02 (6).
            self.assertAlmostEqual(summary["entry_interval_mean"], 14 / 3)
            # True episode length subtracts the 2-bar subject duration.
            self.assertAlmostEqual(summary["episode_length_mean"], (2 + 2 + 4) / 3)
            self.assertNotIn("key_areas", summary)
            self.assertAlmostEqual(summary["stretto_rate"], 0.0)

            inc = root / "entry_plan_stats.inc"
            report = root / "entry_plan_stats.md"
            extract_entry_plan_stats.write_inc(summary, inc)
            extract_entry_plan_stats.write_report(summary, report, [csv_path])
            inc_text = inc.read_text(encoding="utf-8")
            self.assertIn("kEntryPlanStatsPieceCount = 2", inc_text)
            self.assertIn("kSubjectLengthMeanBars = 2.000000", inc_text)
            self.assertIn("stretto_rate", report.read_text(encoding="utf-8"))

    def test_episode_length_differs_from_entry_interval(self) -> None:
        rows = [
            {"piece": "p", "bar": "0", "voice": "0", "kind": "S", "duration": "3"},
            {"piece": "p", "bar": "10", "voice": "1", "kind": "S", "duration": "3"},
        ]
        summary = extract_entry_plan_stats.summarize(rows)
        self.assertAlmostEqual(summary["entry_interval_mean"], 10.0)
        self.assertAlmostEqual(summary["episode_length_mean"], 7.0)


class DezIngestionTest(unittest.TestCase):
    def _write_dez(self, root: Path, name: str, labels: list[dict[str, object]]) -> Path:
        path = root / f"{name}-ref.dez"
        path.write_text(json.dumps({"labels": labels}), encoding="utf-8")
        return path

    def test_only_documented_labels_are_consumed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = self._write_dez(
                root,
                "bwv999",
                [
                    {"type": "S", "start": 0, "duration": 4, "staff": "1", "tag": "S"},
                    {"type": "CS1b", "start": 4, "duration": 4, "staff": "1"},
                    {"type": "Pedal", "start": 6, "duration": 2, "staff": "2"},
                    {"type": "Cadence", "start": 8, "duration": 1, "staff": "1"},
                    # Skipped variants.
                    {"type": "S-inc", "start": 9, "duration": 2, "staff": "1"},
                    {"type": "S-inv", "start": 11, "duration": 2, "staff": "1"},
                    {"type": "ignore", "start": 13, "duration": 2, "staff": "1"},
                ],
            )

            rows = extract_entry_plan_stats.load_dez_rows(path)
            kinds = sorted(row["kind"] for row in rows)
            self.assertEqual(kinds, ["CS", "S", "cadence", "pedal"])
            subject_row = next(row for row in rows if row["kind"] == "S")
            self.assertEqual(subject_row["piece"], "bwv999")
            self.assertEqual(subject_row["bar"], "0")
            self.assertEqual(subject_row["duration"], "1")

    def test_overlap_based_stretto_detection(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            # Dezrann values are quarter-note beats. The second subject starts
            # at 0.75 bars, before the first (duration 1.25 bars) finishes.
            stretto = self._write_dez(
                root,
                "bwv_stretto",
                [
                    {"type": "S", "start": 0, "duration": 5, "staff": "1"},
                    {"type": "S", "start": 3, "duration": 5, "staff": "2"},
                ],
            )
            # Non-overlapping entries -> no stretto.
            plain = self._write_dez(
                root,
                "bwv_plain",
                [
                    {"type": "S", "start": 0, "duration": 4, "staff": "1"},
                    {"type": "S", "start": 8, "duration": 4, "staff": "2"},
                ],
            )

            rows = extract_entry_plan_stats.load_rows([stretto, plain])
            summary = extract_entry_plan_stats.summarize(rows)
            self.assertEqual(summary["piece_count"], 2)
            self.assertAlmostEqual(summary["stretto_rate"], 0.5)

    def test_dez_intervals_and_episode_length(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = self._write_dez(
                root,
                "bwv_seq",
                [
                    {"type": "S", "start": 0, "duration": 4, "staff": "1"},
                    {"type": "S", "start": 12, "duration": 4, "staff": "2"},
                    {"type": "S", "start": 28, "duration": 4, "staff": "3"},
                ],
            )
            rows = extract_entry_plan_stats.load_rows([path])
            summary = extract_entry_plan_stats.summarize(rows)
            # Quarter-note gaps 12 and 16 normalize to 3 and 4 bars.
            self.assertEqual(summary["entry_interval_deciles"][4], 3)
            self.assertAlmostEqual(summary["entry_interval_mean"], 3.5)
            self.assertAlmostEqual(summary["subject_length_mean"], 1.0)
            self.assertGreaterEqual(summary["subject_length_mean"], 1.0)
            self.assertLessEqual(summary["subject_length_mean"], 4.0)
            # Episodes: (3 - 1) and (4 - 1).
            self.assertAlmostEqual(summary["episode_length_mean"], 2.5)

    def test_non_numeric_dezrann_time_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = self._write_dez(
                root,
                "bad",
                [{"type": "S", "start": "later", "duration": 4, "staff": "1"}],
            )
            with self.assertRaisesRegex(ValueError, "non-numeric label start"):
                extract_entry_plan_stats.load_dez_rows(path)


if __name__ == "__main__":
    unittest.main()
