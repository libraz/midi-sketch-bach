"""Tests for texture statistics extraction."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import extract_texture as extract_texture_stats  # noqa: E402


class ExtractTextureStatsTest(unittest.TestCase):
    def test_summarize_and_write_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            generated = root / "generated.json"
            generated.write_text(
                json.dumps(
                    {
                        "notes": [
                            {"start_tick": 0, "duration": 480, "pitch": 60, "voice": 0},
                            {"start_tick": 480, "duration": 480, "pitch": 60, "voice": 0},
                            {"start_tick": 0, "duration": 960, "pitch": 48, "voice": 1},
                        ]
                    }
                ),
                encoding="utf-8",
            )

            summary = extract_texture_stats.summarize([generated])
            self.assertEqual(summary["piece_count"], 1.0)
            self.assertEqual(summary["max_active_voices_p95"], 2)
            self.assertAlmostEqual(summary["avg_active_voices_mean"], 2.0)
            self.assertAlmostEqual(summary["max_repeated_run_p95"], 1.95)

            inc = root / "texture_stats.inc"
            report = root / "texture_stats.md"
            extract_texture_stats.write_inc(summary, inc)
            extract_texture_stats.write_report(summary, report, [generated])
            self.assertIn("kTextureStatsPieceCount = 1", inc.read_text(encoding="utf-8"))
            self.assertIn("avg_active_voices_mean", report.read_text(encoding="utf-8"))


class CorpusModeTest(unittest.TestCase):
    def _write_piece(
        self, root: Path, name: str, voice_count: int, tracks: list[list[dict]]
    ) -> Path:
        path = root / f"{name}.json"
        path.write_text(
            json.dumps(
                {
                    "form": "fugue",
                    "track_type": "voice",
                    "voice_count": voice_count,
                    "tracks": [{"role": "v", "notes": notes} for notes in tracks],
                }
            ),
            encoding="utf-8",
        )
        return path

    def test_skips_non_voice_fugue_files(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            # Voice-separated fugue: kept.
            self._write_piece(
                root,
                "keep",
                2,
                [
                    [{"onset": 0.0, "duration": 4.0, "pitch": 60, "velocity": 90}],
                    [{"onset": 0.0, "duration": 4.0, "pitch": 48, "velocity": 90}],
                ],
            )
            # Single-track manual file: excluded.
            (root / "skip.json").write_text(
                json.dumps(
                    {
                        "form": "fugue",
                        "track_type": "manual",
                        "voice_count": 1,
                        "tracks": [
                            {
                                "role": "v",
                                "notes": [
                                    {
                                        "onset": 0.0,
                                        "duration": 1.0,
                                        "pitch": 60,
                                        "velocity": 90,
                                    }
                                ],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            summary = extract_texture_stats.summarize_corpus(root)
            self.assertEqual(summary["piece_count"], 1)

    def test_corpus_metrics_and_inc(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            # Voice 0 sounds for half the piece; voice 1 sounds throughout.
            self._write_piece(
                root,
                "p1",
                2,
                [
                    [
                        {"onset": 0.0, "duration": 1.0, "pitch": 60, "velocity": 90},
                        {"onset": 1.0, "duration": 1.0, "pitch": 60, "velocity": 90},
                    ],
                    [{"onset": 0.0, "duration": 4.0, "pitch": 48, "velocity": 90}],
                ],
            )

            summary = extract_texture_stats.summarize_corpus(root)
            self.assertEqual(summary["piece_count"], 1)
            self.assertEqual(summary["voice_count_median"], 2)
            # Avg active over [0,4): voice1 always (1.0), voice0 over [0,2)
            # only -> avg active = 1.5, normalized by 2 -> 0.75.
            self.assertAlmostEqual(summary["normalized_avg_active_median"], 0.75)
            # Weakest voice (voice 0) sounds 2 of 4 ticks -> 0.5.
            self.assertAlmostEqual(summary["weakest_voice_sounding_median"], 0.5)
            # Voice 0 has a repeated pitch run of 2.
            self.assertAlmostEqual(summary["max_repeated_run_median"], 1.5)

            inc = root / "texture_stats.inc"
            report = root / "texture_stats.md"
            extract_texture_stats.write_corpus_inc(summary, inc)
            extract_texture_stats.write_corpus_report(summary, report, root)
            inc_text = inc.read_text(encoding="utf-8")
            self.assertIn("kTextureStatsPieceCount = 1", inc_text)
            self.assertIn("kTextureNormalizedAvgActiveMedian", inc_text)
            self.assertIn("track_type=voice", inc_text)
            self.assertIn("Methodology", report.read_text(encoding="utf-8"))

    def test_empty_corpus_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                extract_texture_stats.summarize_corpus(Path(tmp))


if __name__ == "__main__":
    unittest.main()
