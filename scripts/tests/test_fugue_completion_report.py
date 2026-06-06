"""Tests for fugue completion diagnostics."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import completion as report  # noqa: E402


class FugueCompletionReportTest(unittest.TestCase):
    def test_evaluate_reports_p1_to_p7_statuses(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            generated = Path(tmp) / "case.generated.json"
            provenance = Path(tmp) / "case.provenance.json"
            notes = [
                {"voice": 0, "pitch": 72, "start_tick": 0, "duration": 480},
                {"voice": 1, "pitch": 64, "start_tick": 0, "duration": 480},
                {"voice": 2, "pitch": 48, "start_tick": 0, "duration": 480},
                {"voice": 0, "pitch": 74, "start_tick": 480, "duration": 240},
                {"voice": 0, "pitch": 76, "start_tick": 720, "duration": 240},
                {"voice": 1, "pitch": 65, "start_tick": 480, "duration": 480},
                {"voice": 2, "pitch": 50, "start_tick": 480, "duration": 480},
                {"voice": 0, "pitch": 77, "start_tick": 12 * 1920, "duration": 480},
                {"voice": 0, "pitch": 79, "start_tick": 22 * 1920, "duration": 480},
                {"voice": 0, "pitch": 81, "start_tick": 30 * 1920, "duration": 480},
            ]
            provenance_notes = [
                {"span_id": 1, "voice_intent": "SubjectCarrier"},
                {"span_id": 2, "voice_intent": "HarmonicSupport"},
                {"span_id": 3, "voice_intent": "HarmonicSupport"},
                {"span_id": 1, "voice_intent": "SubjectCarrier"},
                {"span_id": 1, "voice_intent": "SubjectCarrier"},
                {"span_id": 4, "voice_intent": "FortspinnungSpan"},
                {"span_id": 5, "voice_intent": "StrettoCarrier"},
                {"span_id": 6, "voice_intent": "MiddleEntryCarrier"},
                {"span_id": 7, "voice_intent": "MiddleEntryCarrier"},
                {"span_id": 8, "voice_intent": "MiddleEntryCarrier"},
            ]
            generated.write_text(json.dumps({"notes": notes}), encoding="utf-8")
            provenance.write_text(json.dumps({"notes": provenance_notes}), encoding="utf-8")

            result = report.evaluate(generated, provenance)

        self.assertEqual(set(result), {
            "p1_three_voice_texture",
            "p2_bass_continuity",
            "p3_repeated_run",
            "p4_compass_register",
            "p5_subject_rhythm",
            "p6_entry_plan",
            "p7_gate_visibility",
        })
        self.assertTrue(result["p5_subject_rhythm"]["passes"])
        self.assertTrue(result["p6_entry_plan"]["passes"])
        self.assertEqual(result["p7_gate_visibility"]["fortspinnung_span_count"], 1)
        self.assertEqual(result["p7_gate_visibility"]["stretto_span_count"], 1)

    def test_render_markdown_includes_status(self) -> None:
        text = report.render_markdown({"p1": {"passes": False, "value": 3}})
        self.assertIn("`p1`: FAIL", text)
        self.assertIn("`value`", text)


if __name__ == "__main__":
    unittest.main()
