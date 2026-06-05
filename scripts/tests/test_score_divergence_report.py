"""Tests for shadow-score divergence reporting."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import score_divergence_report as report  # noqa: E402


class ScoreDivergenceReportTest(unittest.TestCase):
    def test_accumulate_case_counts_only_compose_mismatches(self) -> None:
        generated = {
            "notes": [
                {"index": 0, "voice": 0, "pitch": 60},
                {"index": 1, "voice": 0, "pitch": 62},
                {"index": 2, "voice": 0, "pitch": 65},
            ]
        }
        provenance = {
            "notes": [
                {"source": "Material", "voice_intent": "SubjectCarrier"},
                {
                    "source": "Compose",
                    "voice_intent": "SequentialCounterline",
                    "shadow_winning_pitch": 62,
                    "shadow_winning_pitch_without_markov": 62,
                    "satisfied_rules": 1,
                },
                {
                    "source": "Compose",
                    "voice_intent": "SequentialCounterline",
                    "shadow_winning_pitch": 64,
                    "shadow_winning_pitch_without_markov": 65,
                    "satisfied_rules": 0,
                },
            ]
        }
        stats = report.DivergenceStats()

        report.accumulate_case("Phase7", generated, provenance, stats)

        self.assertEqual(stats.generated, 1)
        self.assertEqual(stats.compose_notes, 2)
        self.assertEqual(stats.mismatches, 1)
        self.assertEqual(stats.markov_changed_shadow_winner, 1)
        self.assertEqual(stats.by_phase["Phase7"], 1)
        self.assertEqual(stats.by_voice_intent["SequentialCounterline"], 1)
        self.assertEqual(stats.by_motion["leap"], 1)
        self.assertEqual(stats.by_harmony["selected_nct"], 1)


if __name__ == "__main__":
    unittest.main()
