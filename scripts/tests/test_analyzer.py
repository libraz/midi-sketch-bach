"""Tests for counterpoint analyzer."""
import json
import os
import sys
import unittest
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.analyzer import CounterpointAnalyzer, analyze_file


class TestCounterpointAnalyzer(unittest.TestCase):
    """Test counterpoint analysis functions."""

    def setUp(self):
        fixture_path = Path(__file__).parent / "fixtures" / "sample_output.json"
        with open(fixture_path) as file_handle:
            self.data = json.load(file_handle)
        self.analyzer = CounterpointAnalyzer(self.data)

    def test_extract_notes(self):
        self.assertGreater(len(self.analyzer.notes), 0)

    def test_count_parallel_fifths(self):
        count = self.analyzer.count_parallel_fifths()
        self.assertIsInstance(count, int)
        self.assertGreaterEqual(count, 0)

    def test_count_parallel_octaves(self):
        count = self.analyzer.count_parallel_octaves()
        self.assertIsInstance(count, int)
        self.assertGreaterEqual(count, 0)

    def test_count_voice_crossings(self):
        count = self.analyzer.count_voice_crossings()
        self.assertIsInstance(count, int)
        self.assertGreaterEqual(count, 0)

    def test_voice_independence_score(self):
        score = self.analyzer.voice_independence_score()
        self.assertIn("rhythm", score)
        self.assertIn("contour", score)
        self.assertIn("register", score)
        self.assertIn("composite", score)
        for val in score.values():
            self.assertGreaterEqual(val, 0.0)
            self.assertLessEqual(val, 1.0)

    def test_full_analysis(self):
        results = self.analyzer.full_analysis()
        self.assertIn("parallel_fifths", results)
        self.assertIn("parallel_octaves", results)
        self.assertIn("voice_crossings", results)
        self.assertIn("voice_independence", results)
        self.assertIn("num_tracks", results)
        self.assertEqual(results["num_tracks"], 3)

    def test_analyze_file(self):
        fixture_path = str(Path(__file__).parent / "fixtures" / "sample_output.json")
        results = analyze_file(fixture_path)
        self.assertIsInstance(results, dict)

    def test_analyze_file_with_track_filter(self):
        fixture_path = str(Path(__file__).parent / "fixtures" / "sample_output.json")
        results = analyze_file(fixture_path, track_filter="soprano")
        self.assertEqual(results["num_tracks"], 1)


if __name__ == "__main__":
    unittest.main()
