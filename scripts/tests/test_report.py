"""Tests for report generation."""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.loaders.json_loader import load_json
from scripts.bach_analyzer.report import format_json, format_text
from scripts.bach_analyzer.runner import validate

FIXTURES = Path(__file__).parent / "fixtures"


class TestTextReport(unittest.TestCase):
    def setUp(self):
        self.score = load_json(FIXTURES / "sample_output.json")
        self.results = validate(self.score)

    def test_text_contains_header(self):
        text = format_text(self.score, self.results)
        self.assertIn("Validation:", text)
        self.assertIn("seed=42", text)
        self.assertIn("fugue", text)

    def test_text_contains_summary(self):
        text = format_text(self.score, self.results)
        self.assertIn("Category Summary:", text)
        self.assertIn("OVERALL:", text)


class TestJsonReport(unittest.TestCase):
    def setUp(self):
        self.score = load_json(FIXTURES / "sample_output.json")
        self.results = validate(self.score)

    def test_json_parseable(self):
        json_str = format_json(self.score, self.results)
        data = json.loads(json_str)
        self.assertIn("metadata", data)
        self.assertIn("results", data)
        self.assertIn("summary", data)

    def test_json_metadata(self):
        data = json.loads(format_json(self.score, self.results))
        self.assertEqual(data["metadata"]["seed"], 42)
        self.assertEqual(data["metadata"]["form"], "fugue")

    def test_json_summary(self):
        data = json.loads(format_json(self.score, self.results))
        self.assertIn("overall_passed", data["summary"])
        self.assertIn("total_violations", data["summary"])


if __name__ == "__main__":
    unittest.main()
