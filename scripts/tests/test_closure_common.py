"""Tests for the shared closure helpers (fixture mapping + phase aliases)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import closure_common  # noqa: E402


class FixtureForSeedTest(unittest.TestCase):
    """Golden values for the seed -> fixture mapping."""

    def test_seed_zero_is_quarter(self) -> None:
        self.assertEqual(
            closure_common.fixture_for_seed(0),
            {"subj_idx": 0, "harm_idx": 0, "subdivision": "quarter"},
        )

    def test_odd_seed_is_eighth(self) -> None:
        self.assertEqual(
            closure_common.fixture_for_seed(1),
            {"subj_idx": 0, "harm_idx": 1, "subdivision": "eighth"},
        )

    def test_seed_seven(self) -> None:
        self.assertEqual(
            closure_common.fixture_for_seed(7),
            {"subj_idx": 1, "harm_idx": 3, "subdivision": "eighth"},
        )

    def test_subj_idx_wraps_at_five_blocks(self) -> None:
        # subj_idx = (seed // 4) % 5 -> seed 20 wraps back to block 0.
        self.assertEqual(closure_common.fixture_for_seed(20)["subj_idx"], 0)
        self.assertEqual(closure_common.fixture_for_seed(16)["subj_idx"], 4)

    def test_subdivision_parity(self) -> None:
        for seed in range(20):
            expected = "eighth" if seed % 2 else "quarter"
            self.assertEqual(
                closure_common.fixture_for_seed(seed)["subdivision"], expected
            )


class NormalizePhaseTest(unittest.TestCase):
    """Alias coverage for normalize_phase, including the merged superset."""

    def test_canonical_passthrough(self) -> None:
        self.assertEqual(closure_common.normalize_phase("Phase14"), "Phase14")

    def test_case_insensitive(self) -> None:
        self.assertEqual(closure_common.normalize_phase("PHASE14"), "Phase14")
        self.assertEqual(closure_common.normalize_phase("PhAsE6"), "Phase6")

    def test_unknown_passthrough(self) -> None:
        self.assertEqual(closure_common.normalize_phase("nonsense"), "nonsense")

    def test_listening_packet_aliases(self) -> None:
        # Keys that build_listening_packet.py historically supported.
        for token, expected in [
            ("3", "Phase3"),
            ("p3", "Phase3"),
            ("3.5", "Phase35"),
            ("p35", "Phase35"),
            ("4", "Phase4"),
            ("5", "Phase5"),
            ("6", "Phase6"),
            ("14", "Phase14"),
            ("p14", "Phase14"),
        ]:
            self.assertEqual(closure_common.normalize_phase(token), expected, token)

    def test_closure_only_aliases(self) -> None:
        # Keys that only run_phase_closure.py historically supported.
        for token, expected in [
            ("4sus", "Phase4Sus"),
            ("p4sus", "Phase4Sus"),
            ("6ep", "Phase6Episode"),
            ("6tonal", "Phase6Tonal"),
            ("p7", "Phase7"),
            ("p10", "Phase10"),
            ("p13", "Phase13"),
        ]:
            self.assertEqual(closure_common.normalize_phase(token), expected, token)


if __name__ == "__main__":
    unittest.main()
