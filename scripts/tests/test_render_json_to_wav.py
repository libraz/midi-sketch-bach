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


if __name__ == "__main__":
    unittest.main()
