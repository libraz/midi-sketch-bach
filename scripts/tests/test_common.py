"""Focused tests for shared closure-tool input and provenance helpers."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib.common import parse_required_rule_bit, provenance_rule_counts  # noqa: E402


class ParseRequiredRuleBitTest(unittest.TestCase):
    def test_accepts_both_provenance_lanes(self) -> None:
        self.assertEqual(parse_required_rule_bit("LowEnd=63"), ("LowEnd", 63))
        self.assertEqual(parse_required_rule_bit("HighStart=64"), ("HighStart", 64))
        self.assertEqual(parse_required_rule_bit("HighEnd=127"), ("HighEnd", 127))

    def test_rejects_values_outside_two_lanes(self) -> None:
        for value in ("Below=-1", "Above=128"):
            with self.subTest(value=value):
                with self.assertRaises(argparse.ArgumentTypeError):
                    parse_required_rule_bit(value)


class ProvenanceRuleCountsTest(unittest.TestCase):
    def counts(self, payload: object, bits: list[tuple[str, int]]) -> dict[str, bool]:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "case.provenance.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            return provenance_rule_counts(path, bits)

    def test_reads_low_and_high_lane_boundaries(self) -> None:
        payload = {
            "notes": [
                {"satisfied_rules": 1 << 63},
                {"satisfied_rules_high": 1},
                {"satisfied_rules_high": 1 << 63},
            ]
        }
        counts = self.counts(payload, [("bit63", 63), ("bit64", 64), ("bit127", 127)])
        self.assertEqual(counts, {"bit63": True, "bit64": True, "bit127": True})

    def test_missing_high_lane_does_not_fall_back_to_low_lane(self) -> None:
        payload = {"notes": [{"satisfied_rules": 1}]}
        self.assertEqual(self.counts(payload, [("bit64", 64)]), {"bit64": False})

    def test_rejects_malformed_lane_values(self) -> None:
        malformed_values: tuple[object, ...] = (True, -1, 1 << 64, "1", 1.0, None)
        for value in malformed_values:
            with self.subTest(value=value):
                payload = {"notes": [{"satisfied_rules_high": value}]}
                self.assertEqual(self.counts(payload, [("bit64", 64)]), {"bit64": False})

    def test_non_list_notes_is_fail_closed(self) -> None:
        payload = {"notes": {"satisfied_rules_high": 1}}
        self.assertEqual(self.counts(payload, [("bit64", 64)]), {"bit64": False})


if __name__ == "__main__":
    unittest.main()
