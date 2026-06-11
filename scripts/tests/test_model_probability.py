"""Tests for the bach-mcp score-extraction helpers in bachlib.common."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib.common import (  # noqa: E402
    model_probability,
    model_probability_v2,
    model_probability_v2_length_invariant,
)


class ModelProbabilityTest(unittest.TestCase):
    """Extraction and absent-field sentinels for the three probability axes."""

    def test_v1_probability_extracted(self) -> None:
        score = {"model_score": {"probability": 0.91}}
        self.assertEqual(model_probability(score), 0.91)

    def test_v1_missing_returns_zero(self) -> None:
        self.assertEqual(model_probability({}), 0.0)

    def test_v2_probability_extracted(self) -> None:
        score = {"model_score_v2": {"probability": 0.84}}
        self.assertEqual(model_probability_v2(score), 0.84)

    def test_v2_missing_returns_not_scored(self) -> None:
        # -1.0 marks "not scored" so an older scorer cannot fabricate a fail.
        self.assertEqual(model_probability_v2({}), -1.0)

    def test_length_invariant_extracted(self) -> None:
        score = {
            "model_score_v2": {
                "probability": 0.84,
                "probability_length_invariant": 0.79,
            }
        }
        self.assertEqual(model_probability_v2_length_invariant(score), 0.79)

    def test_length_invariant_missing_field_returns_not_scored(self) -> None:
        # A v2 block without the field (pre-calibration model) is "not scored".
        score = {"model_score_v2": {"probability": 0.84}}
        self.assertEqual(model_probability_v2_length_invariant(score), -1.0)

    def test_length_invariant_missing_block_returns_not_scored(self) -> None:
        self.assertEqual(model_probability_v2_length_invariant({}), -1.0)


if __name__ == "__main__":
    unittest.main()
