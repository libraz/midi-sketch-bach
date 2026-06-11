"""Tests for per-form texture-gate threshold routing.

Covers the FormThresholds table, the enforced-vs-informational verdict logic,
and per-axis evaluation for the non-fugue uplift forms. The four uplift forms
are enforced (their builders clear every axis); the informational pathway is
still exercised via a synthetic form entry so the verdict machinery keeps
coverage of both branches. Pure-Python: GateCase instances are synthesized
from note dicts, no CLI invocation.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import texture_gate  # noqa: E402


# A synthetic form name that is registered in FORM_THRESHOLDS as informational
# only for the duration of a test, so the informational verdict pathway keeps
# coverage now that every real form is enforced.
SYNTHETIC_INFORMATIONAL_FORM = "synthetic_informational_form"


class _synthetic_informational_form:
    """Context manager registering a synthetic informational form entry.

    Restores FORM_THRESHOLDS on exit so the global table is never mutated
    across tests.
    """

    def __init__(self, thresholds: texture_gate.FormThresholds) -> None:
        self._thresholds = thresholds

    def __enter__(self) -> str:
        texture_gate.FORM_THRESHOLDS[SYNTHETIC_INFORMATIONAL_FORM] = self._thresholds
        return SYNTHETIC_INFORMATIONAL_FORM

    def __exit__(self, *exc) -> None:
        texture_gate.FORM_THRESHOLDS.pop(SYNTHETIC_INFORMATIONAL_FORM, None)


class FormThresholdsTableTest(unittest.TestCase):
    def test_fugue_forms_are_enforced(self) -> None:
        self.assertTrue(texture_gate.thresholds_for("fugue").enforced)
        self.assertTrue(texture_gate.thresholds_for("prelude_and_fugue").enforced)

    def test_uplift_forms_are_enforced(self) -> None:
        for form in (
            "toccata_and_fugue",
            "fantasia_and_fugue",
            "passacaglia",
            "chorale_prelude",
        ):
            self.assertTrue(texture_gate.thresholds_for(form).enforced, form)

    def test_uplift_targets_match_table(self) -> None:
        toccata = texture_gate.thresholds_for("toccata_and_fugue")
        self.assertEqual(toccata.min_avg_active, 2.1)
        self.assertEqual(toccata.max_mono_ratio, 0.25)
        self.assertTrue(toccata.require_v1_v2_occupancy)
        fantasia = texture_gate.thresholds_for("fantasia_and_fugue")
        self.assertEqual(fantasia.min_avg_active, 2.3)
        self.assertEqual(fantasia.max_mono_ratio, 0.10)
        passacaglia = texture_gate.thresholds_for("passacaglia")
        self.assertEqual(passacaglia.min_avg_active, 2.2)
        self.assertEqual(passacaglia.max_mono_ratio, 0.15)
        self.assertEqual(passacaglia.min_final_quarter_avg_active, 2.5)
        chorale = texture_gate.thresholds_for("chorale_prelude")
        self.assertEqual(chorale.min_avg_active, 2.5)
        self.assertEqual(chorale.max_mono_ratio, 0.05)

    def test_unknown_form_defaults_to_enforced(self) -> None:
        self.assertTrue(texture_gate.thresholds_for("brand_new_form").enforced)

    def test_every_form_declares_a_v2_model_floor(self) -> None:
        # All ten shipped forms carry an explicit KL-model floor (20-seed
        # sweep minima minus the seed-noise margin, regression ratchets).
        expected = {
            "fugue": 0.78,
            "prelude_and_fugue": 0.85,
            "toccata_and_fugue": 0.76,
            "fantasia_and_fugue": 0.82,
            "passacaglia": 0.84,
            "chorale_prelude": 0.81,
            "trio_sonata": 0.84,
            "cello_prelude": 0.83,
            "chaconne": 0.89,
            "goldberg_variations": 0.82,
        }
        for form, floor in expected.items():
            self.assertEqual(
                texture_gate.thresholds_for(form).model_score_v2_threshold, floor, form
            )

    def test_unknown_form_falls_back_to_shared_v2_floor(self) -> None:
        self.assertIsNone(
            texture_gate.thresholds_for("brand_new_form").model_score_v2_threshold
        )
        case = texture_gate.GateCase(
            form="brand_new_form",
            seed=1,
            generated=True,
            num_voices=3,
            max_active_voices=3,
            avg_active_voices=2.4,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.6,
            model_score=0.85,
            model_score_v2=texture_gate.MODEL_SCORE_V2_THRESHOLD - 0.01,
        )
        self.assertFalse(case.passes_model_score_v2)


class EnforcedVerdictTest(unittest.TestCase):
    def _failing_uplift_case(self, form: str) -> texture_gate.GateCase:
        # A 3-voice case that fails every uplift axis (thin, mono-heavy).
        return texture_gate.GateCase(
            form=form,
            seed=1,
            generated=True,
            num_voices=3,
            max_active_voices=3,
            avg_active_voices=1.5,
            mono_ratio=0.9,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.4,
            piece_voice_occupancy={0: 0.9, 1: 0.4, 2: 0.4},
            final_quarter_avg_active=1.0,
            parallel_perfect_count=0,
            model_score=0.85,
        )

    def test_enforced_uplift_form_flips_exit_code_on_failure(self) -> None:
        for form in (
            "toccata_and_fugue",
            "fantasia_and_fugue",
            "passacaglia",
            "chorale_prelude",
        ):
            case = self._failing_uplift_case(form)
            self.assertFalse(case.passes_all_axes, form)
            # Now enforced: a failing axis flips the exit-code verdict.
            self.assertFalse(case.passes_texture_gate, form)

    def test_synthetic_informational_form_never_flips_exit_code(self) -> None:
        # The informational pathway is preserved via a synthetic form entry.
        informational = texture_gate.FormThresholds(
            min_avg_active=2.2, max_mono_ratio=0.15, enforced=False
        )
        with _synthetic_informational_form(informational) as form:
            case = self._failing_uplift_case(form)
            self.assertFalse(case.passes_all_axes, form)
            # Axes fail but the exit-code verdict stays True for an
            # informational form.
            self.assertTrue(case.passes_texture_gate, form)

    def test_summarize_excludes_informational_from_exit_code(self) -> None:
        # An informational failing case plus a passing enforced fugue: the
        # summary all_passed must stay True (no enforced failure).
        good_fugue = texture_gate.GateCase(
            form="fugue",
            seed=1,
            generated=True,
            num_voices=3,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
            model_score=0.85,
        )
        informational = texture_gate.FormThresholds(
            min_avg_active=2.2, max_mono_ratio=0.15, enforced=False
        )
        with _synthetic_informational_form(informational) as form:
            bad_informational = self._failing_uplift_case(form)
            summary = texture_gate.summarize([good_fugue, bad_informational])
        self.assertTrue(summary["all_passed"])
        self.assertEqual(summary["failed"], 0)

    def test_summarize_flips_on_enforced_uplift_failure(self) -> None:
        # A failing uplift form is now enforced, so the summary must flip.
        bad_passacaglia = self._failing_uplift_case("passacaglia")
        summary = texture_gate.summarize([bad_passacaglia])
        self.assertFalse(summary["all_passed"])
        self.assertEqual(summary["failed"], 1)

    def test_summarize_still_flips_on_enforced_failure(self) -> None:
        bad_fugue = texture_gate.GateCase(
            form="fugue",
            seed=1,
            generated=True,
            num_voices=3,
            max_active_voices=2,  # thin: fails the voice-count axis
            avg_active_voices=1.5,
            v2_silence_ratio=0.5,
            max_repeated_run=8,
            min_piece_voice_occupancy=0.1,
        )
        summary = texture_gate.summarize([bad_fugue])
        self.assertFalse(summary["all_passed"])
        self.assertEqual(summary["failed"], 1)


class PerAxisRoutingTest(unittest.TestCase):
    def _case(self, form: str, **overrides) -> texture_gate.GateCase:
        base = dict(
            form=form,
            seed=1,
            generated=True,
            num_voices=3,
            max_active_voices=3,
            avg_active_voices=2.6,
            mono_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
            piece_voice_occupancy={0: 0.9, 1: 0.6, 2: 0.6},
            final_quarter_avg_active=2.8,
            parallel_perfect_count=0,
            model_score=0.85,
        )
        base.update(overrides)
        return texture_gate.GateCase(**base)

    def test_mono_axis_replaces_v2_silence_for_uplift_forms(self) -> None:
        axes = self._case("fantasia_and_fugue").axis_results()
        self.assertIn("mono_ratio", axes)
        self.assertNotIn("v2_silence_ratio", axes)

    def test_v2_silence_axis_present_for_fugue_forms(self) -> None:
        axes = self._case("fugue", mono_ratio=0.0, v2_silence_ratio=0.0).axis_results()
        self.assertIn("v2_silence_ratio", axes)
        self.assertNotIn("mono_ratio", axes)

    def test_v2_silence_axis_omitted_below_three_voices(self) -> None:
        # The axis measures the THIRD voice starving; a solo or two-voice form
        # has no voice id 2, so the axis would fail on the dict default (1.0)
        # rather than on anything the form actually played.
        solo = self._case("cello_prelude", num_voices=1, max_active_voices=1,
                          piece_voice_occupancy={0: 1.0}, v2_silence_ratio=1.0)
        duo = self._case("chaconne", num_voices=2, max_active_voices=2,
                         piece_voice_occupancy={0: 1.0, 1: 1.0}, v2_silence_ratio=1.0)
        self.assertNotIn("v2_silence_ratio", solo.axis_results())
        self.assertNotIn("v2_silence_ratio", duo.axis_results())
        trio = self._case("fugue", v2_silence_ratio=0.0)
        self.assertIn("v2_silence_ratio", trio.axis_results())

    def test_goldberg_occupancy_floor_pins_design_value(self) -> None:
        # Goldberg's middle voice is the canon follower only (design value
        # 0.15); the form pins its own floor 0.10 while every other form keeps
        # the fugue-calibrated 0.34.
        ok = self._case("goldberg_variations", min_piece_voice_occupancy=0.15,
                        v2_silence_ratio=0.0)
        gone = self._case("goldberg_variations", min_piece_voice_occupancy=0.05,
                          v2_silence_ratio=0.0)
        self.assertTrue(ok.axis_results()["min_piece_voice_occupancy"])
        self.assertFalse(gone.axis_results()["min_piece_voice_occupancy"])
        fugue_starved = self._case("fugue", min_piece_voice_occupancy=0.2,
                                   v2_silence_ratio=0.0)
        self.assertFalse(fugue_starved.axis_results()["min_piece_voice_occupancy"])

    def test_model_score_v2_axis_present_for_every_form(self) -> None:
        for form in ("fugue", "passacaglia", "trio_sonata", "brand_new_form"):
            self.assertIn("model_score_v2", self._case(form).axis_results(), form)

    def test_model_score_v2_axis_routes_to_form_floor(self) -> None:
        # chaconne floor 0.89: 0.88 fails there but passes the lower trio floor.
        bad_chaconne = self._case("chaconne", model_score_v2=0.88)
        ok_trio = self._case("trio_sonata", model_score_v2=0.88)
        self.assertFalse(bad_chaconne.axis_results()["model_score_v2"])
        self.assertTrue(ok_trio.axis_results()["model_score_v2"])

    def test_mono_ratio_axis_pass_and_fail(self) -> None:
        ok = self._case("chorale_prelude", mono_ratio=0.04)  # ceiling 0.05
        bad = self._case("chorale_prelude", mono_ratio=0.06)
        self.assertTrue(ok.axis_results()["mono_ratio"])
        self.assertFalse(bad.axis_results()["mono_ratio"])

    def test_toccata_v1_v2_occupancy_axis(self) -> None:
        ok = self._case("toccata_and_fugue", piece_voice_occupancy={0: 0.9, 1: 0.6, 2: 0.5})
        bad = self._case("toccata_and_fugue", piece_voice_occupancy={0: 0.9, 1: 0.6, 2: 0.4})
        self.assertTrue(ok.axis_results()["v1_v2_occupancy"])
        self.assertFalse(bad.axis_results()["v1_v2_occupancy"])
        # Forms without the requirement omit the axis entirely.
        self.assertNotIn("v1_v2_occupancy", self._case("fantasia_and_fugue").axis_results())

    def test_passacaglia_final_quarter_axis(self) -> None:
        ok = self._case("passacaglia", final_quarter_avg_active=2.5)
        bad = self._case("passacaglia", final_quarter_avg_active=2.4)
        self.assertTrue(ok.axis_results()["final_quarter_avg_active"])
        self.assertFalse(bad.axis_results()["final_quarter_avg_active"])
        self.assertNotIn(
            "final_quarter_avg_active", self._case("chorale_prelude").axis_results()
        )

    def test_min_avg_active_uses_form_target(self) -> None:
        # chorale target 2.5; 2.4 fails, 2.6 passes.
        self.assertEqual(self._case("chorale_prelude").min_avg_active, 2.5)
        self.assertFalse(
            self._case("chorale_prelude", avg_active_voices=2.4).axis_results()[
                "avg_active_voices"
            ]
        )

    def test_voice_count_floor_applies_when_no_explicit_target(self) -> None:
        # An unknown enforced form has no explicit target; the floor is
        # 0.66 * num_voices = 1.98 for a 3-voice case.
        case = self._case("brand_new_form", avg_active_voices=1.9)
        self.assertAlmostEqual(case.min_avg_active, 0.66 * 3)
        self.assertFalse(case.axis_results()["avg_active_voices"])

    def test_two_voice_form_floor_tracks_voice_count(self) -> None:
        # A 2-voice passacaglia: the voice-count floor is 0.66*2 = 1.32, but the
        # explicit target 2.2 dominates. max_active_voices axis compares to 2.
        case = self._case(
            "passacaglia",
            num_voices=2,
            max_active_voices=2,
            piece_voice_occupancy={0: 0.9, 1: 0.5},
        )
        self.assertEqual(case.min_avg_active, 2.2)
        self.assertTrue(case.axis_results()["max_active_voices"])


class FormSummaryTest(unittest.TestCase):
    def test_summarize_form_labels_enforced_verdict(self) -> None:
        case = texture_gate.GateCase(
            form="passacaglia",
            seed=1,
            generated=True,
            num_voices=2,
            max_active_voices=2,
            avg_active_voices=1.6,
            mono_ratio=0.3,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.4,
            final_quarter_avg_active=1.5,
            model_score=0.86,
        )
        form_summary = texture_gate.summarize_form("passacaglia", [case])
        self.assertEqual(form_summary["verdict"], "enforced")
        self.assertTrue(form_summary["enforced"])
        self.assertEqual(form_summary["num_voices"], 2)
        self.assertEqual(form_summary["target_max_mono_ratio"], 0.15)
        self.assertIn("avg_active_voices", form_summary["axis_pass_counts"])

    def test_summarize_form_labels_informational_verdict(self) -> None:
        informational = texture_gate.FormThresholds(
            min_avg_active=2.2, max_mono_ratio=0.15, enforced=False
        )
        with _synthetic_informational_form(informational) as form:
            case = texture_gate.GateCase(
                form=form,
                seed=1,
                generated=True,
                num_voices=2,
                max_active_voices=2,
                avg_active_voices=1.6,
                mono_ratio=0.3,
                max_repeated_run=1,
                min_piece_voice_occupancy=0.4,
                final_quarter_avg_active=1.5,
                model_score=0.86,
            )
            form_summary = texture_gate.summarize_form(form, [case])
        self.assertEqual(form_summary["verdict"], "informational")
        self.assertFalse(form_summary["enforced"])

    def test_summary_includes_per_form_breakdown(self) -> None:
        fugue = texture_gate.GateCase(
            form="fugue",
            seed=1,
            generated=True,
            num_voices=3,
            max_active_voices=3,
            avg_active_voices=2.2,
            v2_silence_ratio=0.0,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.5,
            model_score=0.85,
        )
        toccata = texture_gate.GateCase(
            form="toccata_and_fugue",
            seed=1,
            generated=True,
            num_voices=3,
            max_active_voices=3,
            avg_active_voices=1.8,
            mono_ratio=0.4,
            max_repeated_run=1,
            min_piece_voice_occupancy=0.4,
            piece_voice_occupancy={0: 0.9, 1: 0.3, 2: 0.3},
            final_quarter_avg_active=1.0,
            model_score=0.82,
        )
        summary = texture_gate.summarize([fugue, toccata])
        self.assertIn("forms", summary)
        self.assertEqual(summary["forms"]["fugue"]["verdict"], "enforced")
        self.assertEqual(
            summary["forms"]["toccata_and_fugue"]["verdict"], "enforced"
        )


if __name__ == "__main__":
    unittest.main()
