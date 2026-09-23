"""Review capture protects fixed requests and index-parallel note provenance."""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib.output_review import _check_pair, _request, _resolved_request, run  # noqa: E402


class OutputReviewRequestTest(unittest.TestCase):
    def make_args(self, **overrides: object) -> argparse.Namespace:
        values: dict[str, object] = {
            "repeat_of": None,
            "form": "fugue",
            "seed": 42,
            "key": None,
            "character": None,
            "instrument": None,
            "bpm": None,
            "scale": None,
            "bars": None,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_seed_zero_is_rejected_before_capture(self) -> None:
        with self.assertRaisesRegex(ValueError, "positive fixed integer"):
            _request(self.make_args(seed=0))

    def test_repeat_uses_the_saved_request_and_rejects_overrides(self) -> None:
        original = _request(
            self.make_args(key="g_minor", bars=24, character="severe", instrument="organ", bpm=88)
        )
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            manifest.write_text(json.dumps({"request": original}), encoding="utf-8")
            self.assertEqual(_request(self.make_args(repeat_of=manifest, form=None, seed=None)), original)
            with self.assertRaisesRegex(ValueError, "cannot be combined"):
                _request(self.make_args(repeat_of=manifest, seed=43))

    def test_resolved_defaults_are_saved_for_replay(self) -> None:
        submitted = _request(self.make_args())
        resolved = _resolved_request(
            submitted,
            "Form: fugue\nKey: C_major\nBPM: 100\nCharacter: Severe\nInstrument: organ\n",
        )
        self.assertEqual(resolved["key"], "C_major")
        self.assertEqual(resolved["character"], "severe")
        self.assertEqual(resolved["instrument"], "organ")
        self.assertEqual(resolved["bpm"], 100)
        self.assertEqual(submitted["key"], None)
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            manifest.write_text(json.dumps({"request": resolved}), encoding="utf-8")
            self.assertEqual(_request(self.make_args(repeat_of=manifest, form=None, seed=None)), resolved)

    def test_failed_capture_replays_submitted_flags(self) -> None:
        submitted = _request(self.make_args())
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            manifest.write_text(json.dumps({"request": submitted}), encoding="utf-8")
            self.assertEqual(_request(self.make_args(repeat_of=manifest, form=None, seed=None)), submitted)

    def test_malformed_repeat_manifest_is_a_usage_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            manifest.write_text("[]", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not an object"):
                _request(self.make_args(repeat_of=manifest, form=None, seed=None))

    def test_cli_launch_failure_still_writes_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cli = root / "not-executable"
            cli.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            cli.chmod(0o644)
            output_dir = root / "case"
            args = self.make_args()
            args.cli = cli
            args.output_dir = output_dir
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(run(args), 1)
            manifest = json.loads((output_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertIsNone(manifest["cli_exit_code"])
            self.assertFalse(manifest["ok"])
            self.assertIn("Permission denied", manifest["errors"][0])


class OutputReviewPairTest(unittest.TestCase):
    def check_pair(self, generated: dict[str, object], provenance: dict[str, object]) -> int:
        with tempfile.TemporaryDirectory() as directory:
            gen_path = Path(directory) / "piece.generated.json"
            prov_path = Path(directory) / "piece.provenance.json"
            gen_path.write_text(json.dumps(generated), encoding="utf-8")
            prov_path.write_text(json.dumps(provenance), encoding="utf-8")
            return _check_pair(gen_path, prov_path)

    def test_complete_pair_and_missing_origin(self) -> None:
        generated = {"schema_version": "generated.v1", "notes": [{"index": 0}]}
        provenance = {
            "schema_version": "provenance.v1",
            "notes": [{"index": 0, "source": "Material", "span_id": 3, "voice_intent": "SubjectCarrier"}],
        }
        self.assertEqual(self.check_pair(generated, provenance), 1)
        provenance["notes"][0].pop("span_id")  # type: ignore[index]
        with self.assertRaisesRegex(ValueError, "invalid span_id"):
            self.check_pair(generated, provenance)

    def test_rejects_non_integer_index_and_invalid_provenance_types(self) -> None:
        generated = {"schema_version": "generated.v1", "notes": [{"index": 0}]}
        origin = {"index": 0, "source": "Material", "span_id": 3, "voice_intent": "SubjectCarrier"}
        provenance = {"schema_version": "provenance.v1", "notes": [origin]}
        for field, value in (("index", False), ("span_id", "bad"), ("voice_intent", 7)):
            with self.subTest(field=field):
                provenance["notes"] = [{**origin, field: value}]
                with self.assertRaises(ValueError):
                    self.check_pair(generated, provenance)

    def test_shifted_provenance_fails_even_when_counts_match(self) -> None:
        generated = {"schema_version": "generated.v1", "notes": [{"index": 0}, {"index": 1}]}
        origin = {"source": "Material", "span_id": 2, "voice_intent": "SubjectCarrier"}
        provenance = {
            "schema_version": "provenance.v1",
            "notes": [{**origin, "index": 1}, {**origin, "index": 0}],
        }
        with self.assertRaisesRegex(ValueError, "index mismatch at 0"):
            self.check_pair(generated, provenance)


if __name__ == "__main__":
    unittest.main()
