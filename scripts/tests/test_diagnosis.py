"""Tests for diagnosis module."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.diagnosis import (
    SOURCE_FILE_MAP,
    group_by_source,
    hotspot_ranking,
    source_file_for,
)
from scripts.bach_analyzer.model import NoteSource
from scripts.bach_analyzer.rules.base import Category, Severity, Violation


class TestSourceFileMap(unittest.TestCase):
    def test_known_source(self):
        self.assertEqual(
            source_file_for(NoteSource.FUGUE_SUBJECT), "src/fugue/subject.cpp"
        )
        self.assertEqual(
            source_file_for(NoteSource.EPISODE_MATERIAL), "src/fugue/episode.cpp"
        )

    def test_none_source(self):
        self.assertIsNone(source_file_for(None))

    def test_unknown_source(self):
        self.assertIsNone(source_file_for(NoteSource.UNKNOWN))


class TestGroupBySource(unittest.TestCase):
    def test_grouping(self):
        violations = [
            Violation(
                rule_name="r1", category=Category.COUNTERPOINT,
                severity=Severity.CRITICAL, source=NoteSource.FUGUE_SUBJECT,
            ),
            Violation(
                rule_name="r2", category=Category.COUNTERPOINT,
                severity=Severity.ERROR, source=NoteSource.FUGUE_SUBJECT,
            ),
            Violation(
                rule_name="r3", category=Category.MELODIC,
                severity=Severity.WARNING, source=NoteSource.EPISODE_MATERIAL,
            ),
            Violation(
                rule_name="r4", category=Category.OVERLAP,
                severity=Severity.CRITICAL, source=None,
            ),
        ]
        groups = group_by_source(violations)
        self.assertEqual(len(groups["src/fugue/subject.cpp"]), 2)
        self.assertEqual(len(groups["src/fugue/episode.cpp"]), 1)
        self.assertEqual(len(groups["unknown"]), 1)


class TestHotspotRanking(unittest.TestCase):
    def test_ranking(self):
        violations = [
            Violation(rule_name="r", category=Category.COUNTERPOINT,
                      severity=Severity.CRITICAL, source=NoteSource.EPISODE_MATERIAL),
            Violation(rule_name="r", category=Category.COUNTERPOINT,
                      severity=Severity.CRITICAL, source=NoteSource.EPISODE_MATERIAL),
            Violation(rule_name="r", category=Category.COUNTERPOINT,
                      severity=Severity.ERROR, source=NoteSource.FUGUE_SUBJECT),
        ]
        ranking = hotspot_ranking(violations)
        self.assertEqual(ranking[0][0], "src/fugue/episode.cpp")
        self.assertEqual(ranking[0][1], 2)
        self.assertAlmostEqual(ranking[0][2], 2 / 3)

    def test_empty(self):
        self.assertEqual(hotspot_ranking([]), [])


if __name__ == "__main__":
    unittest.main()
