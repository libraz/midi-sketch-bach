"""Tests for generated.json texture diagnostics."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import texture_metrics  # noqa: E402

PITCH_UTILS = (SCRIPTS_DIR.parent / "src/core/pitch_utils.h").read_text(encoding="utf-8")


def cpp_organ_range(name: str) -> int:
    """Value of one organ_range constant in src/core/pitch_utils.h."""
    block = PITCH_UTILS[PITCH_UTILS.index("namespace organ_range {") :]
    match = re.search(rf"constexpr uint8_t {name} = (\d+);", block)
    assert match is not None, name
    return int(match.group(1))


class CompassDriftTest(unittest.TestCase):
    def test_compass_equals_the_core_organ_range(self) -> None:
        self.assertEqual(texture_metrics.ORGAN_COMPASS_LOW, cpp_organ_range("kPedalLow"))
        self.assertEqual(texture_metrics.ORGAN_COMPASS_HIGH, cpp_organ_range("kManualHigh"))

    def test_compass_bounds_are_inclusive(self) -> None:
        notes = [
            {"start_tick": 0, "duration": 480, "pitch": pitch, "voice": 0}
            for pitch in (
                texture_metrics.ORGAN_COMPASS_LOW - 1,
                texture_metrics.ORGAN_COMPASS_LOW,
                texture_metrics.ORGAN_COMPASS_HIGH,
                texture_metrics.ORGAN_COMPASS_HIGH + 1,
            )
        ]
        self.assertEqual(texture_metrics.compute_texture_metrics(notes).compass_violation_count, 2)


class TextureMetricsTest(unittest.TestCase):
    def test_matches_validator_fixture_values(self) -> None:
        notes = [
            {"start_tick": 0, "duration": 480, "pitch": 60, "voice": 0},
            {"start_tick": 480, "duration": 480, "pitch": 60, "voice": 0},
            {"start_tick": 960, "duration": 480, "pitch": 62, "voice": 0},
            {"start_tick": 1920, "duration": 480, "pitch": 97, "voice": 0},
            {"start_tick": 0, "duration": 960, "pitch": 48, "voice": 1},
            {"start_tick": 960, "duration": 960, "pitch": 50, "voice": 1},
            {"start_tick": 1920, "duration": 480, "pitch": 52, "voice": 1},
        ]

        metrics = texture_metrics.compute_texture_metrics(notes)
        self.assertEqual(metrics.max_active_voices, 2)
        self.assertAlmostEqual(metrics.avg_active_voices, 1.8)
        # The span is [0, 2400] (480 ticks short of bar 2 onset at 1920+480).
        # Voice 0 has a gap over [1440, 1920] where only voice 1 sounds, so that
        # single 480-tick segment is monophonic; every other segment has both
        # voices. mono_ratio = 480 / 2400 = 0.2.
        self.assertAlmostEqual(metrics.mono_ratio, 0.2)
        self.assertEqual(metrics.compass_violation_count, 1)
        self.assertAlmostEqual(metrics.register_overlap_ratio, 0.0)
        self.assertEqual(len(metrics.voices), 2)
        self.assertEqual(metrics.voices[0].voice, 0)
        self.assertAlmostEqual(metrics.voices[0].silence_ratio, 0.2)
        self.assertEqual(metrics.voices[0].max_repeated_run, 2)
        self.assertEqual(metrics.voices[0].min_pitch, 60)
        self.assertEqual(metrics.voices[0].max_pitch, 97)
        self.assertEqual(metrics.voices[1].voice, 1)
        self.assertAlmostEqual(metrics.voices[1].silence_ratio, 0.0)
        self.assertEqual(metrics.voices[1].max_repeated_run, 1)

    def test_empty_input_returns_zero_metrics(self) -> None:
        metrics = texture_metrics.compute_texture_metrics([])
        self.assertEqual(metrics.to_dict(), {
            "max_active_voices": 0,
            "avg_active_voices": 0.0,
            "mono_ratio": 0.0,
            "compass_violation_count": 0,
            "register_overlap_ratio": 0.0,
            "voices": [],
        })


if __name__ == "__main__":
    unittest.main()
