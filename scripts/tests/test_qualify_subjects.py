"""Tests for the subject qualification harness (pure parts only)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import qualify_subjects as qualify  # noqa: E402

FIGURATION_FIXTURE = """\
inline constexpr std::array<std::array<std::uint8_t, 16>, 5> kFugueCompleteSubjects = {{
    // comment row
    {72, 74, 76, 77},
    {76, 74, 72, 74},
}};

inline constexpr std::array<std::array<Tick, 16>, 5> kFugueCompleteSubjectRhythms = {{
    {kTicksPerBeat, kTicksPerBeat / 2, 2 * kTicksPerBeat, 3 * kTicksPerBeat / 2},
    {kTicksPerBeat / 4, kTicksPerBeat, kTicksPerBeat, 4 * kTicksPerBeat},
}};
"""

MINOR_FIXTURE = """\
inline constexpr std::array<std::array<std::uint8_t, 16>, 5> kSubjectsMinor = {{
    {72, 74, 75, 77},
}};
"""


class TickExpressionTest(unittest.TestCase):
    def test_parses_catalog_expressions(self) -> None:
        self.assertEqual(qualify.parse_tick_expression("kTicksPerBeat"), 480)
        self.assertEqual(qualify.parse_tick_expression("kTicksPerBeat / 2"), 240)
        self.assertEqual(qualify.parse_tick_expression("kTicksPerBeat / 4"), 120)
        self.assertEqual(qualify.parse_tick_expression("2 * kTicksPerBeat"), 960)
        self.assertEqual(qualify.parse_tick_expression("3 * kTicksPerBeat / 2"), 720)
        self.assertEqual(qualify.parse_tick_expression("4 * kTicksPerBeat"), 1920)

    def test_parse_rhythm_rows(self) -> None:
        rows = qualify.parse_rhythm_rows(FIGURATION_FIXTURE)
        self.assertEqual(rows, [[480, 240, 960, 720], [120, 480, 480, 1920]])


class ArrayPatchTest(unittest.TestCase):
    def test_replace_rows_keeps_wrapper(self) -> None:
        patched = qualify.replace_array_rows(
            FIGURATION_FIXTURE, "kFugueCompleteSubjects", [[60, 62], [64, 65]]
        )
        self.assertIn("kFugueCompleteSubjects = {{\n    {60, 62},\n    {64, 65},\n}};", patched)
        # The rhythm array is untouched.
        self.assertIn("kTicksPerBeat / 4", patched)

    def test_missing_array_raises(self) -> None:
        with self.assertRaises(ValueError):
            qualify.replace_array_rows(FIGURATION_FIXTURE, "kNotThere", [[1]])

    def test_patched_sources_major_touches_figuration_only(self) -> None:
        figuration, minor = qualify.patched_sources(
            FIGURATION_FIXTURE, MINOR_FIXTURE, "major", [60] * 4, [480] * 4
        )
        self.assertIn("{60, 60, 60, 60},", figuration)
        self.assertEqual(figuration.count("{60, 60, 60, 60},"), qualify.CATALOG_SLOTS)
        self.assertEqual(minor, MINOR_FIXTURE)
        self.assertEqual(figuration.count("{480, 480, 480, 480},"), qualify.CATALOG_SLOTS)

    def test_patched_sources_minor_touches_minor_and_rhythm(self) -> None:
        figuration, minor = qualify.patched_sources(
            FIGURATION_FIXTURE, MINOR_FIXTURE, "minor", [70] * 4, [240] * 4
        )
        self.assertIn("kFugueCompleteSubjects = {{\n    // comment row", figuration)
        self.assertEqual(minor.count("{70, 70, 70, 70},"), qualify.CATALOG_SLOTS)
        self.assertEqual(figuration.count("{240, 240, 240, 240},"), qualify.CATALOG_SLOTS)


class FakeCase:
    def __init__(self, form: str, seed: int, *, generated: bool = True, axes=None):
        self.form = form
        self.seed = seed
        self.generated = generated
        self.error = None if generated else "boom"
        self._axes = axes if axes is not None else {"generated": True}
        self.model_score = 0.85
        self.model_score_v2 = 0.85
        self.model_score_v2_length_invariant = 0.85

    def axis_results(self):
        return dict(self._axes)


class QualifyCandidateTest(unittest.TestCase):
    def _runner(self, fail_form: str | None = None):
        def runner(cli, form, seed, work_dir, target_bars, index_js=None, key=None):
            axes = {"generated": True, "model_score": form != fail_form}
            return FakeCase(form, seed, axes=axes)

        return runner

    def test_all_axes_pass_qualifies(self) -> None:
        result = qualify.qualify_candidate(
            Path("cli"),
            Path("idx"),
            Path("/tmp"),
            "major",
            [1, 2],
            case_runner=self._runner(),
        )
        self.assertTrue(result["qualified"])
        self.assertEqual(len(result["cases"]), len(qualify.FUGUE_FORMS) * 2)

    def test_fail_fast_stops_at_first_failure(self) -> None:
        result = qualify.qualify_candidate(
            Path("cli"),
            Path("idx"),
            Path("/tmp"),
            "major",
            [1, 2],
            fail_fast=True,
            case_runner=self._runner(fail_form="fugue"),
        )
        self.assertFalse(result["qualified"])
        self.assertEqual(len(result["cases"]), 1)
        self.assertIn("axis:model_score", result["cases"][0]["failures"])

    def test_baseline_exemption_suppresses_known_failure(self) -> None:
        baseline = {
            "major": {
                "cases": [
                    {"form": "fugue", "seed": 1, "failures": ["axis:model_score"]},
                    {"form": "fugue", "seed": 2, "failures": []},
                ]
            }
        }
        exempt = qualify.baseline_exemptions(baseline, "major")
        self.assertEqual(exempt, {("fugue", 1, "model_score")})
        result = qualify.qualify_candidate(
            Path("cli"),
            Path("idx"),
            Path("/tmp"),
            "major",
            [1],
            forms=("fugue",),
            exempt=exempt,
            case_runner=self._runner(fail_form="fugue"),
        )
        # The only failing axis is exempted on (fugue, seed 1).
        self.assertTrue(result["qualified"])

    def test_exemption_is_seed_specific(self) -> None:
        exempt = {("fugue", 1, "model_score")}
        result = qualify.qualify_candidate(
            Path("cli"),
            Path("idx"),
            Path("/tmp"),
            "major",
            [1, 2],
            forms=("fugue",),
            exempt=exempt,
            case_runner=self._runner(fail_form="fugue"),
        )
        self.assertFalse(result["qualified"])  # seed 2 is not exempted.

    def test_v1_floor_miss_forgiven_within_baseline_tolerance(self) -> None:
        def runner(cli, form, seed, work_dir, target_bars, index_js=None, key=None):
            case = FakeCase(form, seed, axes={"generated": True, "model_score": False})
            case.model_score = 0.798
            return case

        baseline_scores = {("fugue", 1): 0.806}  # 0.798 >= 0.806 - 0.01
        result = qualify.qualify_candidate(
            Path("cli"),
            Path("idx"),
            Path("/tmp"),
            "major",
            [1],
            forms=("fugue",),
            baseline_scores=baseline_scores,
            case_runner=runner,
        )
        self.assertTrue(result["qualified"])

    def test_v1_regression_beyond_tolerance_fails(self) -> None:
        def runner(cli, form, seed, work_dir, target_bars, index_js=None, key=None):
            case = FakeCase(form, seed, axes={"generated": True, "model_score": False})
            case.model_score = 0.78
            return case

        baseline_scores = {("fugue", 1): 0.806}
        result = qualify.qualify_candidate(
            Path("cli"),
            Path("idx"),
            Path("/tmp"),
            "major",
            [1],
            forms=("fugue",),
            baseline_scores=baseline_scores,
            case_runner=runner,
        )
        self.assertFalse(result["qualified"])

    def test_v2_floor_miss_is_never_forgiven_by_tolerance(self) -> None:
        def runner(cli, form, seed, work_dir, target_bars, index_js=None, key=None):
            return FakeCase(form, seed, axes={"generated": True, "model_score_v2": False})

        baseline_scores = {("fugue", 1): 0.5}
        result = qualify.qualify_candidate(
            Path("cli"),
            Path("idx"),
            Path("/tmp"),
            "major",
            [1],
            forms=("fugue",),
            baseline_scores=baseline_scores,
            case_runner=runner,
        )
        self.assertFalse(result["qualified"])

    def test_baseline_v1_scores_extraction(self) -> None:
        baseline = {
            "minor": {
                "cases": [
                    {"form": "fugue", "seed": 1, "model_score": 0.83, "failures": []},
                    {"form": "fugue", "seed": 2, "failures": ["generation failed: x"]},
                ]
            }
        }
        scores = qualify.baseline_v1_scores(baseline, "minor")
        self.assertEqual(scores, {("fugue", 1): 0.83})

    def test_minor_mode_passes_c_minor_key(self) -> None:
        seen_keys = []

        def runner(cli, form, seed, work_dir, target_bars, index_js=None, key=None):
            seen_keys.append(key)
            return FakeCase(form, seed, generated=False)

        qualify.qualify_candidate(
            Path("cli"), Path("idx"), Path("/tmp"), "minor", [1], case_runner=runner
        )
        self.assertEqual(seen_keys, ["c_minor"])


class CatalogRenderTest(unittest.TestCase):
    def test_renders_anchors_plus_qualified_picks(self) -> None:
        pool = {
            "modes": {
                "major": {
                    "candidates": [
                        {"pitches": [60] * 16, "rhythm_ticks": [480] * 16},
                        {"pitches": [62] * 16, "rhythm_ticks": [240] * 16},
                    ]
                },
                "minor": {"candidates": []},
            }
        }
        qualified = {
            qualify.candidate_key("major", [62] * 16): {"qualified": True},
            qualify.candidate_key("major", [60] * 16): {"qualified": False},
        }
        minor_header = SCRIPTS_DIR.parent / "src" / "composer" / "minor_material.h"
        figuration = (
            SCRIPTS_DIR.parent / "src" / "composer" / "figuration.h"
        ).read_text(encoding="utf-8")
        catalog = qualify.render_catalog(
            qualified,
            pool,
            figuration,
            minor_header,
            catalog_size=6,
            command="toy command",
        )
        # 5 anchors + the single qualified pick.
        self.assertIn("16>, 6> kSubjectCatalogMajor = {{", catalog)
        self.assertIn("{62, 62", catalog)
        self.assertNotIn("{60, 60", catalog)
        # Anchor row 0 of the real shipped catalog.
        self.assertIn("{72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72},", catalog)
        # Anchor rhythm row 0 resolved to ticks.
        self.assertIn("{480, 240, 240, 480, 480, 240, 240, 480, 480, 480, 240, 240, 480, 480, 480, 1920},", catalog)
        # Minor side renders anchors even with no qualified picks.
        self.assertIn("16>, 5> kSubjectCatalogMinor", catalog)

    def test_candidate_key_stable(self) -> None:
        first = qualify.candidate_key("major", [60, 62])
        second = qualify.candidate_key("major", [60, 62])
        self.assertEqual(first, second)
        self.assertNotEqual(first, qualify.candidate_key("minor", [60, 62]))


if __name__ == "__main__":
    unittest.main()
