"""Tests for the fugue texture gate helpers."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import texture_gate as fugue_texture_gate  # noqa: E402


class FugueTextureGateTest(unittest.TestCase):
    def test_evaluate_generated_json_reports_texture_gate_fields(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "generated.json"
            path.write_text(
                json.dumps(
                    {
                        "notes": [
                            {"voice": 0, "pitch": 72, "start_tick": 0, "duration": 480},
                            {"voice": 1, "pitch": 64, "start_tick": 0, "duration": 480},
                            {"voice": 2, "pitch": 48, "start_tick": 0, "duration": 480},
                            {"voice": 0, "pitch": 74, "start_tick": 480, "duration": 480},
                            {"voice": 1, "pitch": 65, "start_tick": 480, "duration": 480},
                            {"voice": 2, "pitch": 50, "start_tick": 480, "duration": 480},
                        ]
                    }
                ),
                encoding="utf-8",
            )

            case = fugue_texture_gate.evaluate_generated_json("fugue", 3, path)

        self.assertTrue(case.generated)
        self.assertEqual(case.max_active_voices, 3)
        self.assertEqual(case.v2_silence_ratio, 0.0)
        self.assertEqual(case.max_repeated_run, 1)
        self.assertTrue(case.passes_texture_gate)

    def test_compute_entry_plan_metrics_groups_middle_entries_by_span(self) -> None:
        notes = [
            {"voice": 0, "pitch": 72, "start_tick": 12 * 1920, "duration": 480},
            {"voice": 0, "pitch": 74, "start_tick": 12 * 1920 + 480, "duration": 480},
            {"voice": 1, "pitch": 64, "start_tick": 22 * 1920, "duration": 480},
            {"voice": 2, "pitch": 52, "start_tick": 30 * 1920, "duration": 480},
        ]
        provenance = [
            {"span_id": 7, "voice_intent": "MiddleEntryCarrier"},
            {"span_id": 7, "voice_intent": "MiddleEntryCarrier"},
            {"span_id": 8, "voice_intent": "MiddleEntryCarrier"},
            {"span_id": 9, "voice_intent": "MiddleEntryCarrier"},
        ]

        bars, intervals, nonperiodic = fugue_texture_gate.compute_entry_plan_metrics(
            notes, provenance
        )

        self.assertEqual(bars, [12, 22, 30])
        self.assertEqual(intervals, [10, 8])
        self.assertTrue(nonperiodic)

    def test_count_intent_spans_deduplicates_by_span_id(self) -> None:
        provenance = [
            {"span_id": 1, "voice_intent": "FortspinnungSpan"},
            {"span_id": 1, "voice_intent": "FortspinnungSpan"},
            {"span_id": 2, "voice_intent": "FortspinnungSpan"},
            {"span_id": 3, "voice_intent": "StrettoCarrier"},
        ]

        self.assertEqual(
            fugue_texture_gate.count_intent_spans(provenance, "FortspinnungSpan"), 2
        )
        self.assertEqual(fugue_texture_gate.count_intent_spans(provenance, "StrettoCarrier"), 1)

    def test_compute_piece_voice_occupancy_is_piece_relative(self) -> None:
        # Voice 0 sounds for the first half, voice 1 for the whole piece. The
        # occupancy is relative to the latest end tick (the piece total), not to
        # each voice's own first/last note.
        notes = [
            {"voice": 0, "start_tick": 0, "duration": 960},
            {"voice": 1, "start_tick": 0, "duration": 1920},
        ]
        occupancy = fugue_texture_gate.compute_piece_voice_occupancy(notes)
        self.assertAlmostEqual(occupancy[0], 0.5)
        self.assertAlmostEqual(occupancy[1], 1.0)

    def test_gate_rejects_thin_texture_and_silent_voice(self) -> None:
        thin = fugue_texture_gate.GateCase(
            form="fugue",
            seed=1,
            generated=True,
            max_active_voices=3,
            avg_active_voices=1.8,  # below MIN_AVG_ACTIVE_VOICES
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
        )
        starved = fugue_texture_gate.GateCase(
            form="fugue",
            seed=2,
            generated=True,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.2,  # below MIN_PIECE_VOICE_OCCUPANCY
        )
        self.assertFalse(thin.passes_texture_gate)
        self.assertFalse(starved.passes_texture_gate)

    def test_summarize_exposes_failures(self) -> None:
        good = fugue_texture_gate.GateCase(
            form="fugue",
            seed=1,
            generated=True,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
        )
        bad = fugue_texture_gate.GateCase(
            form="fugue",
            seed=2,
            generated=True,
            max_active_voices=2,
            avg_active_voices=1.5,
            v2_silence_ratio=0.5,
            max_repeated_run=8,
            min_piece_voice_occupancy=0.1,
        )

        summary = fugue_texture_gate.summarize([good, bad])

        self.assertFalse(summary["all_passed"])
        self.assertEqual(summary["passed"], 1)
        self.assertEqual(summary["failed"], 1)
        self.assertEqual(summary["failures"][0]["seed"], 2)

    def test_unscored_case_does_not_gate_on_model_score(self) -> None:
        # The default model_score (-1.0) marks "not scored": an absent scorer
        # must never fabricate a model-score failure, so passes_model_score is
        # True regardless of the enforcement flag.
        case = fugue_texture_gate.GateCase(
            form="fugue",
            seed=0,
            generated=True,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
        )
        self.assertFalse(case.model_scored)
        self.assertTrue(case.passes_model_score)
        self.assertTrue(case.passes_texture_gate)

    def test_model_score_is_enforced_by_default(self) -> None:
        # Enforcement is on by default: with the scalar-wave figuration the whole
        # fugue sweep clears 0.80, so a below-threshold score fails the gate.
        self.assertTrue(fugue_texture_gate.ENFORCE_MODEL_SCORE)
        case = fugue_texture_gate.GateCase(
            form="fugue",
            seed=1,
            generated=True,
            max_active_voices=3,
            avg_active_voices=2.4,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.6,
            model_score=0.7139,
        )
        self.assertTrue(case.model_scored)
        self.assertFalse(case.passes_model_score)
        self.assertFalse(case.passes_texture_gate)

    def test_model_score_gates_when_enforcement_enabled(self) -> None:
        # When enforcement is toggled on, a below-threshold model score fails the
        # gate and an at-threshold score passes. Patch the module flag locally so
        # the test is independent of the committed default.
        original = fugue_texture_gate.ENFORCE_MODEL_SCORE
        fugue_texture_gate.ENFORCE_MODEL_SCORE = True
        try:
            base = dict(
                form="fugue",
                seed=0,
                generated=True,
                max_active_voices=3,
                avg_active_voices=2.4,
                v2_silence_ratio=0.0,
                max_repeated_run=1,
                min_piece_voice_occupancy=0.6,
            )
            below = fugue_texture_gate.GateCase(**base, model_score=0.79)
            at = fugue_texture_gate.GateCase(**base, model_score=0.80)
            self.assertFalse(below.passes_model_score)
            self.assertFalse(below.passes_texture_gate)
            self.assertTrue(at.passes_model_score)
            self.assertTrue(at.passes_texture_gate)
        finally:
            fugue_texture_gate.ENFORCE_MODEL_SCORE = original

    def test_summary_records_model_score_fields(self) -> None:
        scored = fugue_texture_gate.GateCase(
            form="fugue",
            seed=1,
            generated=True,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
            model_score=0.83,
        )
        unscored = fugue_texture_gate.GateCase(
            form="prelude_and_fugue",
            seed=1,
            generated=True,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
        )
        summary = fugue_texture_gate.summarize([scored, unscored])
        self.assertAlmostEqual(summary["min_model_score"], 0.83)
        self.assertEqual(summary["model_scored_cases"], 1)
        self.assertEqual(
            summary["model_score_threshold"], fugue_texture_gate.MODEL_SCORE_THRESHOLD
        )
        self.assertEqual(
            summary["model_score_enforced"], fugue_texture_gate.ENFORCE_MODEL_SCORE
        )

    @staticmethod
    def _note(voice: int, start: int, pitch: int, dur: int = 480) -> dict[str, int]:
        return {"voice": voice, "start_tick": start, "duration": dur, "pitch": pitch}

    def test_parallel_octaves_are_counted(self) -> None:
        # Two voices an octave apart, both rising a whole step: a parallel octave
        # on the second onset (interval class 0 at both onsets, same direction).
        notes = [
            self._note(0, 0, 72),
            self._note(0, 480, 74),
            self._note(1, 0, 60),
            self._note(1, 480, 62),
        ]
        parallel, hidden = fugue_texture_gate.compute_piece_parallel_counts(notes)
        self.assertEqual(parallel, 1)
        self.assertEqual(hidden, 0)

    def test_parallel_fifths_are_counted(self) -> None:
        # Two voices a perfect fifth apart, both rising a step: a parallel fifth.
        notes = [
            self._note(0, 0, 67),
            self._note(0, 480, 69),
            self._note(1, 0, 60),
            self._note(1, 480, 62),
        ]
        parallel, hidden = fugue_texture_gate.compute_piece_parallel_counts(notes)
        self.assertEqual(parallel, 1)
        self.assertEqual(hidden, 0)

    def test_contrary_motion_has_no_parallels(self) -> None:
        # Upper voice rises, lower voice falls: contrary motion is never a
        # parallel even when an onset lands on a perfect interval.
        notes = [
            self._note(0, 0, 72),
            self._note(0, 480, 74),
            self._note(1, 0, 60),
            self._note(1, 480, 55),
        ]
        parallel, hidden = fugue_texture_gate.compute_piece_parallel_counts(notes)
        self.assertEqual(parallel, 0)
        self.assertEqual(hidden, 0)

    def test_oblique_motion_has_no_parallels(self) -> None:
        # One voice holds (oblique motion): never a parallel even on octaves.
        notes = [
            self._note(0, 0, 72, dur=960),
            self._note(1, 0, 60),
            self._note(1, 480, 62),
        ]
        parallel, hidden = fugue_texture_gate.compute_piece_parallel_counts(notes)
        self.assertEqual(parallel, 0)

    def test_hidden_octave_is_counted(self) -> None:
        # Same-direction arrival onto an octave from a non-octave interval with
        # the upper voice leaping (> 2 semitones) is a hidden octave.
        notes = [
            self._note(0, 0, 71),  # upper starts a major 7th above the lower.
            self._note(0, 480, 76),  # leaps up a fourth onto the octave.
            self._note(1, 0, 60),
            self._note(1, 480, 64),  # lower steps up.
        ]
        parallel, hidden = fugue_texture_gate.compute_piece_parallel_counts(notes)
        self.assertEqual(parallel, 0)
        self.assertEqual(hidden, 1)

    def test_parallel_count_is_in_gate_case_and_summary(self) -> None:
        # A case over the corpus ceiling fails the gate on the parallel axis.
        over = fugue_texture_gate.GateCase(
            form="fugue",
            seed=1,
            generated=True,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
            parallel_perfect_count=fugue_texture_gate.MAX_PARALLEL_PERFECT_COUNT + 1,
        )
        self.assertFalse(over.passes_parallel)
        self.assertFalse(over.passes_texture_gate)
        ok = fugue_texture_gate.GateCase(
            form="fugue",
            seed=2,
            generated=True,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
            parallel_perfect_count=3,
            hidden_perfect_count=5,
        )
        self.assertTrue(ok.passes_parallel)
        summary = fugue_texture_gate.summarize([over, ok])
        self.assertEqual(summary["max_parallel_perfect_count"], over.parallel_perfect_count)
        self.assertEqual(summary["max_hidden_perfect_count"], 5)
        self.assertEqual(
            summary["parallel_perfect_threshold"],
            fugue_texture_gate.MAX_PARALLEL_PERFECT_COUNT,
        )


if __name__ == "__main__":
    unittest.main()
