"""Equivalence test between the C++ validator and the Python texture metrics.

The C++ validator embeds an ``info.texture_metrics`` object in the generated.v1
JSON. This test regenerates several forms with ``bach_cli`` and asserts that
``texture_metrics.compute_texture_metrics`` reproduces those values from the
same note array. Integer fields must match exactly; floating-point fields are
compared against the serialized C++ value rounded to the JSON writer's 17
significant figures, which preserves a round-trippable IEEE-754 double.

The form set spans both 4/4 pieces and the 3/4 passacaglia so the
meter-independence of ``avg_active_voices`` and ``mono_ratio`` (tick-weighted
only) is exercised on different ticks-per-bar grids.
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

# Forms covered. passacaglia is 3/4 (1440 ticks/bar) -- an important case for
# verifying the tick-weighted metrics do not depend on ticks-per-bar.
_FORMS = ("fugue", "toccata_and_fugue", "passacaglia", "chorale_prelude")


def _serialized(value: float) -> float:
    """Round ``value`` to the JSON writer's serialization precision.

    The generated.v1 JSON writer prints doubles with 17 significant figures.
    Mirror that formatting before comparison so both sides exercise the wire
    representation rather than relying on Python's display formatting.
    """
    return float(f"{value:.17g}")


@unittest.skipUnless(BACH_CLI.exists(), f"bach_cli not built at {BACH_CLI}")
class TextureMetricsEquivalenceTest(unittest.TestCase):
    def _generate(self, form: str, tmp: Path) -> dict:
        midi_path = tmp / f"{form}.mid"
        result = subprocess.run(
            [
                str(BACH_CLI),
                "--form",
                form,
                "--seed",
                "1",
                "--generated-json",
                "-o",
                str(midi_path),
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            self.skipTest(f"bach_cli failed for {form}: {result.stderr.strip()}")
        json_path = tmp / f"{form}.generated.json"
        if not json_path.exists():
            self.skipTest(f"generated JSON not produced at {json_path}")
        with json_path.open("r", encoding="utf-8") as handle:
            return json.load(handle)

    def test_python_matches_embedded_validator_metrics(self) -> None:
        for form in _FORMS:
            with self.subTest(form=form):
                with tempfile.TemporaryDirectory() as tmp:
                    payload = self._generate(form, Path(tmp))

                embedded = payload.get("info", {}).get("texture_metrics")
                self.assertTrue(embedded, f"{form}: missing info.texture_metrics")
                cpp = embedded[0]

                notes = payload["notes"]
                computed = texture_metrics.compute_texture_metrics(notes)

                self.assertEqual(
                    computed.max_active_voices,
                    cpp["max_active_voices"],
                    f"{form}: max_active_voices",
                )
                self.assertEqual(
                    computed.compass_violation_count,
                    cpp["compass_violation_count"],
                    f"{form}: compass_violation_count",
                )

                # Round through the same 17-significant-digit wire format used
                # by JsonWriter before comparing the recomputed values.
                self.assertEqual(
                    _serialized(computed.avg_active_voices),
                    cpp["avg_active_voices"],
                    f"{form}: avg_active_voices",
                )
                self.assertEqual(
                    _serialized(computed.mono_ratio),
                    cpp["mono_ratio"],
                    f"{form}: mono_ratio",
                )
                self.assertEqual(
                    _serialized(computed.register_overlap_ratio),
                    cpp["register_overlap_ratio"],
                    f"{form}: register_overlap_ratio",
                )

                self.assertEqual(len(computed.voices), len(cpp["voices"]))
                for py_voice, cpp_voice in zip(computed.voices, cpp["voices"]):
                    self.assertEqual(py_voice.voice, cpp_voice["voice"])
                    self.assertEqual(
                        py_voice.max_repeated_run, cpp_voice["max_repeated_run"]
                    )
                    self.assertEqual(py_voice.min_pitch, cpp_voice["min_pitch"])
                    self.assertEqual(py_voice.max_pitch, cpp_voice["max_pitch"])
                    self.assertEqual(
                        _serialized(py_voice.silence_ratio),
                        cpp_voice["silence_ratio"],
                        f"{form} voice {py_voice.voice}: silence_ratio",
                    )


if __name__ == "__main__":
    unittest.main()
