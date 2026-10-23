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

    def _evaluate(self, notes: list[dict], intents: list[str]) -> dict:
        with tempfile.TemporaryDirectory() as tmp:
            generated = Path(tmp) / "case.generated.json"
            provenance = Path(tmp) / "case.provenance.json"
            generated.write_text(json.dumps({"notes": notes}), encoding="utf-8")
            provenance.write_text(
                json.dumps({"notes": [{"voice_intent": intent} for intent in intents]}),
                encoding="utf-8",
            )
            return report.evaluate(generated, provenance)

    def test_gate_visibility_follows_measured_span_counts(self) -> None:
        notes = [{"voice": 0, "pitch": 72, "start_tick": 0, "duration": 480}]
        result = self._evaluate(notes, ["SubjectCarrier"])
        self.assertFalse(result["p7_gate_visibility"]["passes"])

    def test_subject_window_starts_at_the_first_subject_onset(self) -> None:
        # Prelude figuration fills the first 24 bars; the subject then states
        # two different durations inside its own first four bars.
        prelude = 24 * 1920
        notes = [
            {"voice": 0, "pitch": 72, "start_tick": 0, "duration": 480},
            {"voice": 0, "pitch": 72, "start_tick": prelude, "duration": 480},
            {"voice": 0, "pitch": 74, "start_tick": prelude + 480, "duration": 240},
        ]
        result = self._evaluate(
            notes, ["HarmonicSupport", "SubjectCarrier", "SubjectCarrier"]
        )
        self.assertEqual(result["p5_subject_rhythm"]["unique_subject_durations"], [240, 480])
        self.assertTrue(result["p5_subject_rhythm"]["passes"])

    def test_bass_silence_is_measured_from_the_bass_entry(self) -> None:
        # Voice 2 plays only prelude figuration with a long gap, then enters
        # the theme and sounds continuously; the gap precedes its entry.
        entry = 24 * 1920
        notes = [
            {"voice": 2, "pitch": 48, "start_tick": 0, "duration": 480},
            {"voice": 2, "pitch": 48, "start_tick": entry, "duration": 1920},
            {"voice": 2, "pitch": 50, "start_tick": entry + 1920, "duration": 1920},
        ]
        result = self._evaluate(
            notes, ["HarmonicSupport", "SubjectCarrier", "SubjectCarrier"]
        )
        self.assertEqual(result["p2_bass_continuity"]["v2_silence_ratio"], 0.0)
        self.assertTrue(result["p2_bass_continuity"]["passes"])

    def test_render_markdown_includes_status(self) -> None:
        text = report.render_markdown({"p1": {"passes": False, "value": 3}})
        self.assertIn("`p1`: FAIL", text)
        self.assertIn("`value`", text)


if __name__ == "__main__":
    unittest.main()
