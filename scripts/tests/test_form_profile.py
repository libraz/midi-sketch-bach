"""Tests for form_profile module and FormProfile-based rule filtering."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.form_profile import (
    FormProfile,
    all_form_names,
    get_form_profile,
)
from scripts.bach_analyzer.model import Note, Score, Track
from scripts.bach_analyzer.rules.base import Severity
from scripts.bach_analyzer.runner import validate


class TestFormProfileRegistry(unittest.TestCase):
    """Test FormProfile lookup and defaults."""

    def test_all_known_forms(self):
        expected = [
            "fugue", "prelude_and_fugue", "toccata_and_fugue",
            "fantasia_and_fugue", "trio_sonata", "chorale_prelude",
            "passacaglia", "cello_prelude", "chaconne",
        ]
        names = all_form_names()
        for form in expected:
            self.assertIn(form, names)

    def test_unknown_form_returns_default(self):
        profile = get_form_profile("unknown_form")
        self.assertEqual(profile.form_name, "unknown")
        self.assertTrue(profile.counterpoint_enabled)
        self.assertTrue(profile.independence_enabled)

    def test_none_form_returns_default(self):
        profile = get_form_profile(None)
        self.assertEqual(profile.form_name, "unknown")

    def test_fugue_profile(self):
        profile = get_form_profile("fugue")
        self.assertEqual(profile.style_family, "organ")
        self.assertTrue(profile.counterpoint_enabled)
        self.assertTrue(profile.exposition_required)
        self.assertEqual(profile.expected_voices, (2, 5))

    def test_cello_prelude_profile(self):
        profile = get_form_profile("cello_prelude")
        self.assertEqual(profile.style_family, "solo_string_flow")
        self.assertFalse(profile.counterpoint_enabled)
        self.assertFalse(profile.independence_enabled)
        self.assertEqual(profile.instrument_range, (36, 81))
        self.assertEqual(profile.expected_voices, (1, 1))

    def test_chaconne_profile(self):
        profile = get_form_profile("chaconne")
        self.assertEqual(profile.style_family, "solo_string_arch")
        self.assertFalse(profile.counterpoint_enabled)
        self.assertTrue(profile.ground_bass_expected)
        self.assertEqual(profile.ground_bass_period, 4)

    def test_passacaglia_profile(self):
        profile = get_form_profile("passacaglia")
        self.assertTrue(profile.counterpoint_enabled)
        self.assertTrue(profile.ground_bass_expected)
        self.assertEqual(profile.ground_bass_period, 8)
        self.assertAlmostEqual(profile.min_stepwise_ratio, 0.2)

    def test_trio_sonata_profile(self):
        profile = get_form_profile("trio_sonata")
        self.assertEqual(profile.hidden_perfect_severity, Severity.INFO)
        self.assertEqual(profile.expected_voices, (3, 3))
        self.assertFalse(profile.exposition_required)

    def test_toccata_relaxed_sources(self):
        profile = get_form_profile("toccata_and_fugue")
        self.assertIn("free_counterpoint", profile.relaxed_sources)
        self.assertIn("post_process", profile.relaxed_sources)

    def test_fugue_no_relaxed_sources(self):
        profile = get_form_profile("fugue")
        self.assertEqual(len(profile.relaxed_sources), 0)

    def test_profile_is_frozen(self):
        profile = get_form_profile("fugue")
        with self.assertRaises(AttributeError):
            profile.counterpoint_enabled = False  # type: ignore


class TestFormProfileIntegration(unittest.TestCase):
    """Test that runner correctly skips rules based on form profile."""

    @staticmethod
    def _make_solo_score(form: str) -> Score:
        """Create a minimal single-voice score."""
        notes = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="cello"),
            Note(pitch=64, velocity=80, start_tick=480, duration=480, voice="cello"),
            Note(pitch=67, velocity=80, start_tick=960, duration=480, voice="cello"),
            Note(pitch=72, velocity=80, start_tick=1440, duration=480, voice="cello"),
        ]
        track = Track(name="cello", channel=0, program=42, notes=notes)
        return Score(tracks=[track], form=form, seed=1)

    def test_cello_prelude_skips_counterpoint(self):
        score = self._make_solo_score("cello_prelude")
        results = validate(score, categories={"counterpoint"})
        for r in results:
            self.assertTrue(r.passed)
            self.assertIn("skipped", r.info or "")

    def test_cello_prelude_skips_independence(self):
        score = self._make_solo_score("cello_prelude")
        results = validate(score, categories={"independence"})
        for r in results:
            self.assertTrue(r.passed)

    def test_chaconne_skips_counterpoint(self):
        score = self._make_solo_score("chaconne")
        results = validate(score, categories={"counterpoint"})
        for r in results:
            self.assertTrue(r.passed)
            self.assertIn("skipped", r.info or "")

    def test_fugue_runs_counterpoint(self):
        """Fugue form should NOT skip counterpoint rules."""
        notes_s = [
            Note(pitch=72, velocity=80, start_tick=0, duration=480, voice="soprano"),
            Note(pitch=74, velocity=80, start_tick=480, duration=480, voice="soprano"),
        ]
        notes_a = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="alto"),
            Note(pitch=62, velocity=80, start_tick=480, duration=480, voice="alto"),
        ]
        score = Score(
            tracks=[
                Track(name="soprano", notes=notes_s),
                Track(name="alto", notes=notes_a),
            ],
            form="fugue",
            seed=1,
        )
        results = validate(score, categories={"counterpoint"})
        # At least one result should NOT be skipped.
        has_non_skipped = any("skipped" not in (r.info or "") for r in results)
        self.assertTrue(has_non_skipped)

    def test_unknown_form_runs_all(self):
        """Unknown form should run all rules (conservative default)."""
        notes_s = [
            Note(pitch=72, velocity=80, start_tick=0, duration=480, voice="soprano"),
            Note(pitch=74, velocity=80, start_tick=480, duration=480, voice="soprano"),
        ]
        notes_a = [
            Note(pitch=60, velocity=80, start_tick=0, duration=480, voice="alto"),
            Note(pitch=62, velocity=80, start_tick=480, duration=480, voice="alto"),
        ]
        score = Score(
            tracks=[
                Track(name="soprano", notes=notes_s),
                Track(name="alto", notes=notes_a),
            ],
            form=None,
            seed=1,
        )
        results = validate(score, categories={"counterpoint"})
        has_non_skipped = any("skipped" not in (r.info or "") for r in results)
        self.assertTrue(has_non_skipped)


if __name__ == "__main__":
    unittest.main()
