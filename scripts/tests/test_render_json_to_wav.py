"""Tests for the audit WAV renderer."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
import wave
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import audio as render_json_to_wav  # noqa: E402


class RenderJsonToWavTest(unittest.TestCase):
    def test_render_accepts_generated_notes_schema(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            json_path = Path(tmp) / "generated.json"
            wav_path = Path(tmp) / "out.wav"
            json_path.write_text(
                json.dumps(
                    {
                        "ticks_per_beat": 480,
                        "duration_ticks": 960,
                        "notes": [
                            {"voice": 0, "pitch": 72, "start_tick": 0, "duration": 480, "velocity": 80},
                            {"voice": 1, "pitch": 60, "start_tick": 480, "duration": 480, "velocity": 80},
                        ],
                    }
                ),
                encoding="utf-8",
            )

            render_json_to_wav.render(json_path, wav_path, sample_rate=8000)

            with wave.open(str(wav_path), "rb") as wav:
                self.assertEqual(wav.getnchannels(), 2)
                self.assertEqual(wav.getframerate(), 8000)
                self.assertGreater(wav.getnframes(), 0)


class TempoMapTest(unittest.TestCase):
    def test_constant_tempo_from_tempos_list(self) -> None:
        to_sec = render_json_to_wav.tempo_map([{"tick": 0, "bpm": 120}], 480)
        self.assertAlmostEqual(to_sec(960), 1.0)

    def test_tempo_change_applies_from_its_tick(self) -> None:
        to_sec = render_json_to_wav.tempo_map(
            [{"tick": 0, "bpm": 120}, {"tick": 960, "bpm": 60}], 480
        )
        # 960 ticks at 120 bpm = 1 s, then 480 ticks at 60 bpm = 1 s.
        self.assertAlmostEqual(to_sec(1440), 2.0)

    def test_absent_tempos_use_the_default(self) -> None:
        to_sec = render_json_to_wav.tempo_map(None, 480)
        self.assertAlmostEqual(to_sec(480), 60.0 / render_json_to_wav.DEFAULT_BPM)

    def test_render_length_follows_tempos(self) -> None:
        lengths = []
        for bpm in (60, 120):
            with tempfile.TemporaryDirectory() as tmp:
                json_path = Path(tmp) / "g.json"
                wav_path = Path(tmp) / "o.wav"
                json_path.write_text(
                    json.dumps(
                        {
                            "ticks_per_beat": 480,
                            "duration_ticks": 4800,
                            "tempos": [{"tick": 0, "bpm": bpm}],
                            "notes": [
                                {"voice": 0, "pitch": 60, "start_tick": 0, "duration": 4800, "velocity": 80}
                            ],
                        }
                    ),
                    encoding="utf-8",
                )
                render_json_to_wav.render(json_path, wav_path, sample_rate=2000)
                with wave.open(str(wav_path), "rb") as wav:
                    lengths.append(wav.getnframes())
        self.assertGreater(lengths[0], lengths[1])


class ListeningOutputGuardTest(unittest.TestCase):
    def _args(self, out: Path, index_js: Path):
        import argparse

        from bachlib.phases import PHASE_TAGS

        return argparse.Namespace(
            phase=next(iter(PHASE_TAGS)),
            top=1,
            out=out,
            cli=Path(sys.executable),
            bach_mcp_index=index_js,
            keep_json=False,
        )

    def test_repository_directory_and_foreign_directory_are_refused(self) -> None:
        import contextlib
        import io

        from bachlib.common import REPO_ROOT

        with tempfile.TemporaryDirectory() as tmp:
            index_js = Path(tmp) / "index.js"
            index_js.write_text("", encoding="utf-8")
            foreign = Path(tmp) / "foreign"
            foreign.mkdir()
            keep = foreign / "keep.txt"
            keep.write_text("user data", encoding="utf-8")
            for target in (REPO_ROOT / "src", REPO_ROOT, foreign):
                with self.subTest(target=str(target)):
                    with contextlib.redirect_stderr(io.StringIO()):
                        code = render_json_to_wav.run_listening(self._args(target, index_js))
                    self.assertEqual(code, 2)
            self.assertTrue(keep.exists())
            self.assertTrue((REPO_ROOT / "src").is_dir())


if __name__ == "__main__":
    unittest.main()
