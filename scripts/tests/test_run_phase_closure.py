"""Tests for run_phase_closure.py: C++/Python drift guard, carrier prediction,
and the pure gate-combination decision."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = SCRIPTS_DIR.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import run_phase_closure as rpc  # noqa: E402
from closure_common import fixture_for_seed  # noqa: E402

FIXTURE_CPP = REPO_ROOT / "src" / "composer" / "harness_fixture.cpp"


def _parse_cpp_int_array(name: str) -> tuple[tuple[int, ...], ...]:
    """Parse a ``constexpr ... kName = {{ {...}, {...} }};`` 2D int array.

    @param name C++ array identifier (e.g. "kSubjectPatterns").
    @return Tuple of row tuples of ints, in source order.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    match = re.search(name + r"\s*=\s*\{\{(.*?)\}\};", src, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name} in {FIXTURE_CPP}")
    body = match.group(1)
    rows = re.findall(r"\{([0-9,\s]+)\}", body)
    return tuple(
        tuple(int(tok) for tok in row.replace(" ", "").split(",") if tok != "")
        for row in rows
    )


def _parse_cpp_brace_array(name: str) -> tuple[tuple[int, ...], ...]:
    """Parse a single-brace ``... kName[...] = { {...}, {...} };`` int array.

    Handles the ``kFigures`` / ``kBarPlan`` declarations (single outer brace,
    unlike the ``{{ ... }}`` form ``_parse_cpp_int_array`` expects). Trailing
    ``// ...`` comments after each row are ignored (they sit outside braces).

    @param name C++ array identifier (e.g. "kFigures").
    @return Tuple of row tuples of ints, in source order.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    match = re.search(name + r"\b[^=]*=\s*\{(.*?)\};", src, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name} in {FIXTURE_CPP}")
    rows = re.findall(r"\{([0-9,\s]+)\}", match.group(1))
    return tuple(
        tuple(int(tok) for tok in row.replace(" ", "").split(",") if tok != "")
        for row in rows
    )


class CppDriftGuardTest(unittest.TestCase):
    """Highest-value guard: the Python catalogs must equal the C++ source."""

    def test_phase14_subjects_match_cpp(self) -> None:
        cpp = _parse_cpp_int_array("kPhase14Subjects")
        self.assertEqual(
            cpp,
            rpc.PHASE14_SUBJECTS,
            "PHASE14_SUBJECTS drifted from kPhase14Subjects in harness_fixture.cpp",
        )

    def test_subject_patterns_match_cpp(self) -> None:
        cpp = _parse_cpp_int_array("kSubjectPatterns")
        self.assertEqual(
            cpp,
            rpc.SUBJECT_PATTERNS,
            "SUBJECT_PATTERNS drifted from kSubjectPatterns in harness_fixture.cpp",
        )

    def test_catalogs_are_5x16(self) -> None:
        for catalog in (rpc.SUBJECT_PATTERNS, rpc.PHASE14_SUBJECTS):
            self.assertEqual(len(catalog), 5)
            for row in catalog:
                self.assertEqual(len(row), 16)

    def test_phase15_figures_match_cpp(self) -> None:
        cpp = _parse_cpp_brace_array("kFigures")
        self.assertEqual(
            cpp,
            rpc.PHASE15_FIGURES,
            "PHASE15_FIGURES drifted from kFigures in harness_fixture.cpp",
        )

    def test_phase15_barplan_match_cpp(self) -> None:
        # kBarPlan rows are (root_pc, bass, mid, top); the Python mirror keeps
        # only the (bass, mid, top) triple the structural predictor replays.
        cpp = _parse_cpp_brace_array("kBarPlan")
        self.assertEqual(
            tuple(row[1:] for row in cpp),
            rpc.PHASE15_BARPLAN,
            "PHASE15_BARPLAN drifted from kBarPlan in harness_fixture.cpp",
        )


class ExpectedCarrierSequencesTest(unittest.TestCase):
    """Golden carrier predictions, especially the tonal-answer head mutation."""

    def test_seed0_subject_carrier(self) -> None:
        seq = rpc.expected_carrier_sequences("Phase14", fixture_for_seed(0))
        v0 = seq[(0, "SubjectCarrier")]
        # 16 exposition notes + 16 stretto-leader notes at bar 24.
        self.assertEqual(len(v0), 32)
        self.assertEqual(v0[0], (0, 72))
        self.assertEqual(v0[15], (7200, 72))
        # Stretto leader: subj_idx 0 restated verbatim starting bar 24.
        self.assertEqual(v0[16], (46080, 72))
        self.assertEqual(v0[31], (53280, 72))

    def test_seed0_tonal_answer_head(self) -> None:
        # subj_idx 0 head (72,74,76,77) -> base = pitch-5 = (67,69,71,72).
        # Tonal answer remaps tonic(pc0)->dominant(pc7) and dominant(pc7)->
        # tonic(pc0) for the first 4 notes via closest-octave; non-tonic/dom
        # notes pass through unchanged.
        seq = rpc.expected_carrier_sequences("Phase14", fixture_for_seed(0))
        ans = seq[(1, "AnswerCarrier")]
        self.assertEqual(len(ans), 16)
        # n=0: src pc0 (tonic) -> dom; base 67 already pc7 so delta 0 -> 67.
        self.assertEqual(ans[0], (7680, 67))
        # n=1,2: pcs 2,4 (not tonic/dom) -> base unchanged 69,71.
        self.assertEqual(ans[1], (8160, 69))
        self.assertEqual(ans[2], (8640, 71))
        # n=3: pc5 (not tonic/dom) -> base unchanged 72.
        self.assertEqual(ans[3], (9120, 72))
        # n>=4: plain transposition by -5.
        self.assertEqual(ans[4], (9600, 74))

    def test_seed0_v2_reentry(self) -> None:
        seq = rpc.expected_carrier_sequences("Phase14", fixture_for_seed(0))
        v2 = seq[(2, "SubjectCarrier")]
        self.assertEqual(len(v2), 16)
        # V2 re-entry is the subject down an octave (-12) at bar 16.
        self.assertEqual(v2[0], (15360, 60))
        self.assertEqual(v2[15], (22560, 60))

    def test_seed7_tonal_answer_head(self) -> None:
        # subj_idx 1 (PHASE14_SUBJECTS[1]) head (76,74,72,74) -> base
        # (71,69,67,69). n=2 is pc0 (tonic) -> dominant: base 67 pc7 delta 0
        # -> 67. Others pass through.
        seq = rpc.expected_carrier_sequences("Phase14", fixture_for_seed(7))
        ans = seq[(1, "AnswerCarrier")]
        self.assertEqual(ans[0], (7680, 71))
        self.assertEqual(ans[1], (8160, 69))
        self.assertEqual(ans[2], (8640, 67))
        self.assertEqual(ans[3], (9120, 69))


class ExpectedArpeggioSequenceTest(unittest.TestCase):
    """Golden Phase15 arpeggio prediction (verbatim broken-chord replay)."""

    def test_seed0_arpeggio_head(self) -> None:
        # seed 0 -> harm_idx 0 -> figure (1,0,1,2) = mid-bass-mid-top.
        # Bar 0 tones = (bass 48, mid 55, top 64); first beat replays
        # mid,bass,mid,top at sixteenth (120-tick) spacing.
        seq = rpc.expected_carrier_sequences("Phase15", fixture_for_seed(0))
        arp = seq[(0, "ArpeggioFlow")]
        self.assertEqual(len(arp), 128)  # 8 bars * 4 beats * 4 sixteenths.
        self.assertEqual(arp[0], (0, 55))
        self.assertEqual(arp[1], (120, 48))
        self.assertEqual(arp[2], (240, 55))
        self.assertEqual(arp[3], (360, 64))

    def test_seed3_uses_figure3(self) -> None:
        # seed 3 -> harm_idx 3 -> figure (2,1,0,1) = top-mid-bass-mid.
        seq = rpc.expected_carrier_sequences("Phase15", fixture_for_seed(3))
        arp = seq[(0, "ArpeggioFlow")]
        self.assertEqual(arp[0], (0, 64))  # top.
        self.assertEqual(arp[1], (120, 55))  # mid.
        self.assertEqual(arp[2], (240, 48))  # bass.

    def test_implicit_streams_are_figure_invariant(self) -> None:
        # The per-cell min (bass) / max (top) — the implicit voices the
        # Validator checks — must not depend on the figure ordering.
        for seed in (0, 1, 2, 3):
            arp = rpc.expected_carrier_sequences(
                "Phase15", fixture_for_seed(seed)
            )[(0, "ArpeggioFlow")]
            first_cell = [p for _, p in arp[:4]]
            self.assertEqual(min(first_cell), 48)  # bar 0 bass.
            self.assertEqual(max(first_cell), 64)  # bar 0 top.


class ComputePassedTest(unittest.TestCase):
    """Pure gate-combination decision, incl. evaluator-error and 47-bit gates."""

    BASE = dict(
        seed_count=20,
        composer_ok_count=20,
        model_pass_count=14,
        min_pass=14,
        structural_ok_count=20,
        rule_pass={"ChordTone": True},
        all_bits_pass=True,
        evaluator_error_count=0,
    )

    def test_all_gates_pass(self) -> None:
        self.assertTrue(rpc.compute_passed(**self.BASE))

    def test_evaluator_error_fails_run(self) -> None:
        kwargs = dict(self.BASE)
        kwargs["evaluator_error_count"] = 1
        self.assertFalse(rpc.compute_passed(**kwargs))

    def test_composer_failure_fails_run(self) -> None:
        kwargs = dict(self.BASE)
        kwargs["composer_ok_count"] = 19
        self.assertFalse(rpc.compute_passed(**kwargs))

    def test_insufficient_model_pass_fails(self) -> None:
        kwargs = dict(self.BASE)
        kwargs["model_pass_count"] = 13
        self.assertFalse(rpc.compute_passed(**kwargs))

    def test_structural_mismatch_fails(self) -> None:
        kwargs = dict(self.BASE)
        kwargs["structural_ok_count"] = 19
        self.assertFalse(rpc.compute_passed(**kwargs))

    def test_rule_not_satisfied_fails(self) -> None:
        kwargs = dict(self.BASE)
        kwargs["rule_pass"] = {"ChordTone": True, "PicardyThird": False}
        self.assertFalse(rpc.compute_passed(**kwargs))

    def test_all_bits_gate_fails(self) -> None:
        kwargs = dict(self.BASE)
        kwargs["all_bits_pass"] = False
        self.assertFalse(rpc.compute_passed(**kwargs))


class Phase14BitCountTest(unittest.TestCase):
    """The 47-bit required count must track provenance.h RuleBit max+1."""

    def test_required_bit_count_is_47(self) -> None:
        self.assertEqual(rpc.PHASE14_REQUIRED_BIT_COUNT, 47)

    def test_matches_organ_fugue_bit_boundary(self) -> None:
        # Phase14 covers the Organ-fugue technique RuleBits 0..AffektCurveApplied.
        # The Solo String Flow bits (ArpeggioFlowActive, ImplicitVoiceTracked)
        # added in Phase15 are a SEPARATE system and sit ABOVE that boundary, so
        # the required count tracks AffektCurveApplied+1, NOT the absolute RuleBit
        # max.
        prov = (REPO_ROOT / "src" / "composer" / "provenance.h").read_text(
            encoding="utf-8"
        )
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(
            int(names["AffektCurveApplied"]) + 1, rpc.PHASE14_REQUIRED_BIT_COUNT
        )
        # The two Flow bits are exactly the indices just past that boundary.
        self.assertEqual(int(names["ArpeggioFlowActive"]), 47)
        self.assertEqual(int(names["ImplicitVoiceTracked"]), 48)
        self.assertEqual(rpc.PHASE15_REQUIRED_BITS, (47, 48))


if __name__ == "__main__":
    unittest.main()
