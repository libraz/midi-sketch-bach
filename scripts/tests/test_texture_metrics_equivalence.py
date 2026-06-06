"""Equivalence test between the C++ validator and the Python texture metrics.

The C++ validator embeds an ``info.texture_metrics`` object in the generated.v1
JSON. This test regenerates a fugue with ``bach_cli`` and asserts that
``texture_metrics.compute_texture_metrics`` reproduces those values from the
same note array. Integer fields must match exactly; floating-point fields are
compared with a relative tolerance because the JSON writer serializes doubles
with roughly six significant figures, which bounds the achievable agreement.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import texture_metrics  # noqa: E402

REPO_ROOT = SCRIPTS_DIR.parent
BACH_CLI = REPO_ROOT / "build" / "bin" / "bach_cli"

# The generated.v1 JSON writer emits doubles with about six significant
# figures, so the embedded validator values are only that precise.
_FLOAT_REL_TOL = 1e-5


class TextureMetricsEquivalenceTest(unittest.TestCase):
    def setUp(self) -> None:
        if not BACH_CLI.exists():
            self.skipTest(f"bach_cli not built at {BACH_CLI}")

    def _generate(self, tmp: Path) -> dict:
        midi_path = tmp / "eq.mid"
        result = subprocess.run(
            [
                str(BACH_CLI),
                "--form",
                "fugue",
                "--seed",
                "7",
                "--bars",
                "24",
                "--generated-json",
                "-o",
                str(midi_path),
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            self.skipTest(f"bach_cli failed: {result.stderr.strip()}")
        json_path = tmp / "eq.generated.json"
        if not json_path.exists():
            self.skipTest(f"generated JSON not produced at {json_path}")
        with json_path.open("r", encoding="utf-8") as handle:
            return json.load(handle)

    def _assert_close(self, expected: float, actual: float, label: str) -> None:
        self.assertAlmostEqual(
            actual,
            expected,
            delta=max(abs(expected), 1.0) * _FLOAT_REL_TOL,
            msg=f"{label}: python {actual} vs C++ {expected}",
        )

    def test_python_matches_embedded_validator_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            payload = self._generate(Path(tmp))

        embedded = payload.get("info", {}).get("texture_metrics")
        self.assertTrue(embedded, "missing info.texture_metrics")
        cpp = embedded[0]

        notes = payload["notes"]
        computed = texture_metrics.compute_texture_metrics(notes)

        self.assertEqual(computed.max_active_voices, cpp["max_active_voices"])
        self.assertEqual(
            computed.compass_violation_count, cpp["compass_violation_count"]
        )
        self._assert_close(
            cpp["avg_active_voices"], computed.avg_active_voices, "avg_active_voices"
        )
        self._assert_close(
            cpp["register_overlap_ratio"],
            computed.register_overlap_ratio,
            "register_overlap_ratio",
        )

        self.assertEqual(len(computed.voices), len(cpp["voices"]))
        for py_voice, cpp_voice in zip(computed.voices, cpp["voices"]):
            self.assertEqual(py_voice.voice, cpp_voice["voice"])
            self.assertEqual(
                py_voice.max_repeated_run, cpp_voice["max_repeated_run"]
            )
            self.assertEqual(py_voice.min_pitch, cpp_voice["min_pitch"])
            self.assertEqual(py_voice.max_pitch, cpp_voice["max_pitch"])
            self._assert_close(
                cpp_voice["silence_ratio"],
                py_voice.silence_ratio,
                f"voice {py_voice.voice} silence_ratio",
            )


if __name__ == "__main__":
    unittest.main()
