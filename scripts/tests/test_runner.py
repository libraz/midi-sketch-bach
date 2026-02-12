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
        self.assertEqual(len(rules), 6)
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


class TestRelaxedSourcesDowngrade(unittest.TestCase):
    """Test that relaxed_sources in toccata_and_fugue downgrades violations."""

    def test_free_counterpoint_downgraded(self):
        """free_counterpoint violations in toccata_and_fugue should be downgraded."""
        from scripts.bach_analyzer.model import Note, NoteSource, Provenance, Score, Track
        from scripts.bach_analyzer.rules.base import Severity

        prov_fc = Provenance(source=NoteSource.FREE_COUNTERPOINT)
        # Create parallel P5 with free_counterpoint source.
        soprano = Track(name="soprano", notes=[
            Note(pitch=67, velocity=80, start_tick=0, duration=480,
                 voice="soprano", provenance=prov_fc),
            Note(pitch=69, velocity=80, start_tick=480, duration=480,
                 voice="soprano", provenance=prov_fc),
        ])
        bass = Track(name="bass", notes=[
            Note(pitch=60, velocity=80, start_tick=0, duration=480,
                 voice="bass", provenance=prov_fc),
            Note(pitch=62, velocity=80, start_tick=480, duration=480,
                 voice="bass", provenance=prov_fc),
        ])
        score = Score(
            tracks=[soprano, bass],
            form="toccata_and_fugue",
            seed=1,
        )
        results = validate(score, categories={"counterpoint"})
        # Find parallel_perfect violations.
        for r in results:
            if r.rule_name == "parallel_perfect":
                for v in r.violations:
                    if v.source == NoteSource.FREE_COUNTERPOINT:
                        # Should be downgraded from CRITICAL to ERROR.
                        self.assertNotEqual(v.severity, Severity.CRITICAL,
                                           "free_counterpoint should be downgraded in toccata")


if __name__ == "__main__":
    unittest.main()
