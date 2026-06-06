"""Tests for the tick-weighted mono_ratio texture metric.

mono_ratio is the duration-weighted fraction of the piece span where exactly
one voice is sounding, sharing the segmentation of avg_active_voices.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import texture_metrics  # noqa: E402


def _note(voice: int, start: int, duration: int, pitch: int = 60) -> dict[str, int]:
    return {"voice": voice, "start_tick": start, "duration": duration, "pitch": pitch}


class MonoRatioTest(unittest.TestCase):
    def test_pure_monophony_is_one(self) -> None:
        # A single voice playing four contiguous quarter notes: every segment
        # has exactly one active voice, so mono_ratio is 1.0.
        notes = [_note(0, tick, 480) for tick in (0, 480, 960, 1440)]
        metrics = texture_metrics.compute_texture_metrics(notes)
        self.assertAlmostEqual(metrics.mono_ratio, 1.0)
        self.assertAlmostEqual(metrics.avg_active_voices, 1.0)

    def test_constant_two_voice_homophony_is_zero(self) -> None:
        # Two voices sounding in lock-step over the whole span: no segment is
        # ever monophonic, so mono_ratio is 0.0.
        notes = []
        for tick in (0, 480, 960, 1440):
            notes.append(_note(0, tick, 480, 72))
            notes.append(_note(1, tick, 480, 48))
        metrics = texture_metrics.compute_texture_metrics(notes)
        self.assertAlmostEqual(metrics.mono_ratio, 0.0)
        self.assertAlmostEqual(metrics.avg_active_voices, 2.0)

    def test_half_and_half_is_one_half(self) -> None:
        # First half of the span is two-voice homophony; second half is a single
        # voice. Span is [0, 1920]; the monophonic region is [960, 1920] = 960
        # ticks, so mono_ratio = 960 / 1920 = 0.5.
        notes = [
            _note(0, 0, 960, 72),
            _note(1, 0, 960, 48),
            _note(0, 960, 960, 72),
        ]
        metrics = texture_metrics.compute_texture_metrics(notes)
        self.assertAlmostEqual(metrics.mono_ratio, 0.5)

    def test_mono_ratio_is_meter_independent(self) -> None:
        # The same construction scaled to a 3/4 bar of 1440 ticks yields the
        # identical mono_ratio: it is tick-weighted, not bar-counted. Half the
        # span is two-voice, half is single-voice.
        notes = [
            _note(0, 0, 720, 72),
            _note(1, 0, 720, 48),
            _note(0, 720, 720, 72),
        ]
        metrics = texture_metrics.compute_texture_metrics(notes)
        self.assertAlmostEqual(metrics.mono_ratio, 0.5)

    def test_silent_voice_does_not_widen_span(self) -> None:
        # A voice that never sounds before the union span does not change the
        # decomposition. Voice 1 enters only for the last quarter, so the rest
        # is monophonic: span [0, 1920], mono over [0, 1440] = 1440/1920 = 0.75.
        notes = [
            _note(0, 0, 480, 72),
            _note(0, 480, 480, 72),
            _note(0, 960, 480, 72),
            _note(0, 1440, 480, 72),
            _note(1, 1440, 480, 48),
        ]
        metrics = texture_metrics.compute_texture_metrics(notes)
        self.assertAlmostEqual(metrics.mono_ratio, 0.75)

    def test_empty_input_is_zero(self) -> None:
        metrics = texture_metrics.compute_texture_metrics([])
        self.assertAlmostEqual(metrics.mono_ratio, 0.0)

    def test_single_note_is_fully_mono(self) -> None:
        metrics = texture_metrics.compute_texture_metrics([_note(0, 0, 480)])
        self.assertAlmostEqual(metrics.mono_ratio, 1.0)


if __name__ == "__main__":
    unittest.main()
