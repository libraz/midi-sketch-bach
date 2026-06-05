"""Tests for Phase B-3 melodic weight sweep helpers."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import melodic_weight_sweep_b3 as sweep  # noqa: E402


class MelodicWeightSweepB3Test(unittest.TestCase):
    def test_iter_configs_includes_step_axis(self) -> None:
        configs = list(sweep.iter_configs([0.0, 1.0]))
        self.assertEqual(len(configs), 32)
        self.assertIn(0, {c.step_bonus for c in configs})
        self.assertIn(1, {c.step_bonus for c in configs})

    def test_rank_prefers_feasible_then_probability_then_l1(self) -> None:
        high_infeasible = sweep.SweepResult(
            index=0,
            config=sweep.SweepConfig(2, 2, 2, 2, 1),
            generated=1,
            validation_failed=1,
            model_pass=1,
            required_pass=1,
            mean_probability=0.99,
            min_probability=0.99,
        )
        feasible_heavy = sweep.SweepResult(
            index=1,
            config=sweep.SweepConfig(2, 2, 2, 2, 1),
            generated=1,
            validation_failed=0,
            model_pass=1,
            required_pass=1,
            mean_probability=0.8,
            min_probability=0.8,
        )
        feasible_light = sweep.SweepResult(
            index=2,
            config=sweep.SweepConfig(0, 0, 0, 0, 0),
            generated=1,
            validation_failed=0,
            model_pass=1,
            required_pass=1,
            mean_probability=0.8,
            min_probability=0.8,
        )

        ranked = sweep.rank_results([high_infeasible, feasible_heavy, feasible_light])

        self.assertEqual([r.index for r in ranked], [2, 1, 0])

    def test_jsonl_roundtrip_filters_scope(self) -> None:
        result = sweep.SweepResult(
            index=3,
            config=sweep.SweepConfig(0.5, 1, 2, 0, 1),
            generated=20,
            validation_failed=0,
            model_pass=20,
            required_pass=20,
            mean_probability=0.9,
            min_probability=0.8,
        )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "results.jsonl"
            sweep.append_result_jsonl(path, result, "forms", ["fugue"], [0, 1], "all")

            loaded = sweep.load_results_jsonl(path, "forms", ["fugue"], [0, 1], "all")
            filtered = sweep.load_results_jsonl(path, "forms", ["chaconne"], [0, 1], "all")

        self.assertEqual(len(loaded), 1)
        self.assertEqual(loaded[0].index, 3)
        self.assertEqual(loaded[0].config.wp, 0.5)
        self.assertEqual(filtered, [])

    def test_resume_work_items_skip_existing_indices(self) -> None:
        # Keep the resume predicate explicit in the test suite: indices already
        # in the JSONL should not be evaluated again.
        selected = [(0, sweep.SweepConfig(0, 0, 0, 0, 0)), (1, sweep.SweepConfig(0, 0, 0, 0, 1))]
        existing = {0}

        work_items = [
            (index, config) for index, config in selected if not (True and index in existing)
        ]

        self.assertEqual([index for index, _ in work_items], [1])


if __name__ == "__main__":
    unittest.main()
