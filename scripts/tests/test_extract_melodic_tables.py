"""Tests for corpus melodic table extraction."""

from __future__ import annotations

import math
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import extract_melodic_tables as tables  # noqa: E402


class ExtractMelodicTablesTest(unittest.TestCase):
    def test_toy_sequence_scale_and_markov_logs(self) -> None:
        seq = tables.MelodySequence([60, 62, 64], "major", "organ_pf")
        result = tables.build_tables([seq])

        self.assertEqual(result.works_count, 1)
        major_total = 12 * tables.SMOOTHING + 3
        expected_pc0 = math.log((tables.SMOOTHING + 1) / major_total)
        expected_pc1 = math.log(tables.SMOOTHING / major_total)
        self.assertAlmostEqual(result.scale_degree_logp[0][0], expected_pc0)
        self.assertAlmostEqual(result.scale_degree_logp[0][1], expected_pc1)
        self.assertAlmostEqual(result.scale_degree_logp[0][2], expected_pc0)
        self.assertAlmostEqual(result.scale_degree_logp[0][4], expected_pc0)
        self.assertEqual(result.sanity["whole_gt_semitone"], 1.0)
        self.assertEqual(result.sanity["essen_2x_reference_check"], 1.0)

        row_total = tables.INTERVAL_SIZE * tables.SMOOTHING + 1
        expected_markov = math.log((tables.SMOOTHING + 1) / row_total)
        expected_other = math.log(tables.SMOOTHING / row_total)
        row = tables.interval_index(2)
        self.assertAlmostEqual(result.interval_markov_logp[row][row], expected_markov)
        self.assertAlmostEqual(
            result.interval_markov_logp[row][tables.interval_index(1)], expected_other
        )

    def test_toy_sequence_gaussian_fit(self) -> None:
        seq = tables.MelodySequence([60, 62, 64], "major", "organ_pf")
        result = tables.build_tables([seq])
        vp, vr = result.gaussian_fit["organ"]
        self.assertAlmostEqual(vp, 4.0)
        self.assertAlmostEqual(vr, 6.5)

    def test_write_tables_emits_three_inc_files(self) -> None:
        seq = tables.MelodySequence([60, 62, 64], "major", "organ_pf")
        result = tables.build_tables([seq])
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            tables.write_tables(result, out, "toy command")
            self.assertIn("kScaleDegreeLogP", (out / "scale_degree_0th.inc").read_text())
            self.assertIn("kIntervalLogP", (out / "interval_markov.inc").read_text())
            self.assertIn("kGaussianFitOrgan", (out / "gaussian_fit.inc").read_text())


if __name__ == "__main__":
    unittest.main()
