"""Tests for batch module."""

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.batch import parse_seed_range


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


if __name__ == "__main__":
    unittest.main()
