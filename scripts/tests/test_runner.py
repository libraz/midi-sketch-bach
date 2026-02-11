"""Tests for the runner module."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.loaders.json_loader import load_json
from scripts.bach_analyzer.runner import (
    get_rules,
    overall_passed,
    validate,
)

FIXTURES = Path(__file__).parent / "fixtures"


class TestRunner(unittest.TestCase):
    def setUp(self):
        self.score = load_json(FIXTURES / "sample_output.json")

    def test_get_all_rules(self):
        rules = get_rules()
        self.assertGreaterEqual(len(rules), 16)

    def test_get_filtered_rules(self):
        rules = get_rules(categories={"counterpoint"})
        self.assertEqual(len(rules), 5)
        for r in rules:
            self.assertEqual(r.category.value, "counterpoint")

    def test_validate_all(self):
        results = validate(self.score)
        self.assertGreaterEqual(len(results), 16)
        for r in results:
            self.assertIsNotNone(r.rule_name)

    def test_validate_with_bar_range(self):
        results = validate(self.score, bar_range=(1, 2))
        for r in results:
            for v in r.violations:
                self.assertGreaterEqual(v.bar, 1)
                self.assertLessEqual(v.bar, 2)

    def test_overall_passed_clean(self):
        results = validate(self.score, categories={"overlap"})
        self.assertTrue(overall_passed(results))


class TestRunnerProvenance(unittest.TestCase):
    def test_provenance_score(self):
        score = load_json(FIXTURES / "sample_with_provenance.json")
        results = validate(score, categories={"structure"})
        self.assertGreater(len(results), 0)


if __name__ == "__main__":
    unittest.main()
