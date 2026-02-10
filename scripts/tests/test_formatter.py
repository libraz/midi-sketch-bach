"""Tests for output formatter."""
import sys
import unittest
from io import StringIO
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.formatter import print_analysis, print_fugue_structure, format_as_json


class TestFormatter(unittest.TestCase):
    """Test output formatting."""

    def test_print_analysis(self):
        results = {
            "parallel_fifths": 0,
            "parallel_octaves": 0,
            "voice_crossings": 0,
            "voice_independence": {
                "rhythm": 0.8,
                "contour": 0.7,
                "register": 0.9,
                "composite": 0.8,
            },
            "num_tracks": 3,
            "total_notes": 24,
        }
        # Should not raise
        old_stdout = sys.stdout
        sys.stdout = StringIO()
        print_analysis(results)
        output = sys.stdout.getvalue()
        sys.stdout = old_stdout
        self.assertIn("Counterpoint Analysis", output)
        self.assertIn("PASS", output)

    def test_print_analysis_with_violations(self):
        results = {
            "parallel_fifths": 3,
            "parallel_octaves": 1,
            "voice_crossings": 2,
            "voice_independence": {
                "rhythm": 0.4,
                "contour": 0.3,
                "register": 0.5,
                "composite": 0.4,
            },
            "num_tracks": 3,
            "total_notes": 24,
        }
        old_stdout = sys.stdout
        sys.stdout = StringIO()
        print_analysis(results)
        output = sys.stdout.getvalue()
        sys.stdout = old_stdout
        self.assertIn("FAIL", output)
        self.assertIn("NEEDS IMPROVEMENT", output)

    def test_print_fugue_structure(self):
        detection = {
            "subject_entries": [],
            "exposition": None,
            "episodes": [],
            "stretto": [],
        }
        old_stdout = sys.stdout
        sys.stdout = StringIO()
        print_fugue_structure(detection)
        output = sys.stdout.getvalue()
        sys.stdout = old_stdout
        self.assertIn("Fugue Structure", output)
        self.assertIn("NOT DETECTED", output)

    def test_print_fugue_structure_with_data(self):
        detection = {
            "subject_entries": [
                {"tick": 0, "pitch": 60, "voice": "soprano",
                 "source": "fugue_subject", "entry_number": 1},
            ],
            "exposition": {
                "start_tick": 0,
                "end_tick": 1920,
                "voices_entered": 1,
                "entries": [
                    {"tick": 0, "voice": "soprano", "source": "fugue_subject"},
                ],
            },
            "episodes": [
                {"start_tick": 1920, "end_tick": 3840, "duration_ticks": 1920},
            ],
            "stretto": [
                {"first_entry_tick": 5760, "second_entry_tick": 6720,
                 "overlap_ticks": 960, "voices": ["soprano", "alto"]},
            ],
        }
        old_stdout = sys.stdout
        sys.stdout = StringIO()
        print_fugue_structure(detection)
        output = sys.stdout.getvalue()
        sys.stdout = old_stdout
        self.assertIn("Exposition: tick 0-1920", output)
        self.assertIn("soprano", output)
        self.assertIn("Episode 1", output)

    def test_format_as_json(self):
        results = {"key": "value", "number": 42}
        json_str = format_as_json(results)
        self.assertIn('"key"', json_str)
        self.assertIn("42", json_str)


if __name__ == "__main__":
    unittest.main()
