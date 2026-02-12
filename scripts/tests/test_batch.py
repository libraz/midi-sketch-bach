"""Tests for batch module."""

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.batch import compute_batch_statistics, parse_seed_range


class TestParseSeedRange(unittest.TestCase):
    def test_simple_range(self):
        self.assertEqual(parse_seed_range("1-5"), [1, 2, 3, 4, 5])

    def test_single_values(self):
        self.assertEqual(parse_seed_range("1,5,10"), [1, 5, 10])

    def test_mixed(self):
        self.assertEqual(parse_seed_range("1-3,7,10-12"), [1, 2, 3, 7, 10, 11, 12])


class TestValidateSeed(unittest.TestCase):
    @patch("scripts.bach_analyzer.batch._run_bach_cli")
    def test_generation_failure(self, mock_cli):
        """When bach_cli fails, result has error."""
        mock_cli.return_value = None
        from scripts.bach_analyzer.batch import validate_seed
        result = validate_seed(seed=1, form="fugue")
        self.assertFalse(result["overall_passed"])
        self.assertEqual(result["error"], "generation_failed")

    @patch("scripts.bach_analyzer.batch._run_bach_cli")
    def test_validation_success(self, mock_cli):
        """When bach_cli generates valid JSON, validation runs."""
        # Create a temp fixture file
        import tempfile, json
        data = {
            "seed": 1, "form": "fugue", "key": "C_major",
            "tracks": [
                {"name": "soprano", "channel": 0, "program": 19, "notes": [
                    {"pitch": 72, "velocity": 80, "start_tick": 0, "duration": 480}
                ]}
            ],
        }
        with tempfile.NamedTemporaryFile(suffix=".json", mode="w", delete=False) as f:
            json.dump(data, f)
            tmp_path = Path(f.name)
        mock_cli.return_value = tmp_path

        from scripts.bach_analyzer.batch import validate_seed
        result = validate_seed(seed=1, form="fugue")
        self.assertIsNone(result["error"])
        self.assertIn("total_violations", result)
        tmp_path.unlink()


class TestComputeBatchStatistics(unittest.TestCase):
    """Test batch statistics computation."""

    def test_basic_statistics(self):
        """Compute stats from mock batch results."""
        results = [
            {"seed": 1, "overall_passed": True, "total_violations": 0,
             "violation_counts": {"parallel_perfect": 0, "voice_crossing": 1}},
            {"seed": 2, "overall_passed": False, "total_violations": 3,
             "violation_counts": {"parallel_perfect": 2, "voice_crossing": 1}},
            {"seed": 3, "overall_passed": True, "total_violations": 1,
             "violation_counts": {"parallel_perfect": 0, "voice_crossing": 1}},
        ]
        stats = compute_batch_statistics(results)
        self.assertAlmostEqual(stats["pass_rate"], 2 / 3)
        self.assertEqual(stats["total_seeds"], 3)
        self.assertEqual(stats["passed_seeds"], 2)
        self.assertIn("parallel_perfect", stats["per_rule"])
        self.assertIn("voice_crossing", stats["per_rule"])
        # voice_crossing has violations in all 3 seeds -> systemic.
        self.assertTrue(stats["per_rule"]["voice_crossing"]["systemic"])
        # parallel_perfect has violations in 1/3 seeds -> not systemic.
        self.assertFalse(stats["per_rule"]["parallel_perfect"]["systemic"])
        # Worst seed should be seed 2 (3 violations).
        self.assertEqual(stats["worst_seeds"][0]["seed"], 2)

    def test_empty_results(self):
        """Empty results should return zero stats."""
        stats = compute_batch_statistics([])
        self.assertEqual(stats["pass_rate"], 0.0)
        self.assertEqual(stats["per_rule"], {})

    def test_all_passing(self):
        """All seeds passing."""
        results = [
            {"seed": i, "overall_passed": True, "total_violations": 0,
             "violation_counts": {}}
            for i in range(5)
        ]
        stats = compute_batch_statistics(results)
        self.assertAlmostEqual(stats["pass_rate"], 1.0)
        self.assertEqual(len(stats["systemic_violations"]), 0)


if __name__ == "__main__":
    unittest.main()
