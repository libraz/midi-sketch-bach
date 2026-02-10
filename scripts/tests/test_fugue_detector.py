"""Tests for fugue structure detection."""
import json
import sys
import unittest
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.fugue_detector import FugueDetector


class TestFugueDetector(unittest.TestCase):
    """Test fugue structure detection."""

    def setUp(self):
        fixture_path = Path(__file__).parent / "fixtures" / "sample_output.json"
        with open(fixture_path) as file_handle:
            self.data = json.load(file_handle)
        self.detector = FugueDetector(self.data)

    def test_detect_subject_entries(self):
        entries = self.detector.detect_subject_entries()
        self.assertIsInstance(entries, list)

    def test_detect_exposition(self):
        expo = self.detector.detect_exposition()
        # May be None if no provenance data in fixture
        if expo is not None:
            self.assertIn("start_tick", expo)
            self.assertIn("voices_entered", expo)

    def test_detect_episodes(self):
        episodes = self.detector.detect_episodes()
        self.assertIsInstance(episodes, list)

    def test_detect_stretto(self):
        strettos = self.detector.detect_stretto()
        self.assertIsInstance(strettos, list)

    def test_full_detection(self):
        result = self.detector.full_detection()
        self.assertIn("subject_entries", result)
        self.assertIn("exposition", result)
        self.assertIn("episodes", result)
        self.assertIn("stretto", result)


if __name__ == "__main__":
    unittest.main()
