"""Organ Passacaglia Phase20 C++/Python drift guard.

The load-bearing guard: the run_phase_closure.py PHASE20 mirror constants
(PHASE20_GROUND, PHASE20_VAR_T0, PHASE20_BAR_ROOT) must match the C++ Phase20
fixture source (the kGroundPitch[kCycleBars], kVarT0[kCycleBars] and
kRootPc[kCycleBars] initialisers in buildPhase20Fixture,
src/composer/harness_fixture.cpp). A renamed constant or an altered ground /
progression that the Python predictor still claims would make structural_ok
diverge from the CLI output; this drift guard fails instead. The Phase20 variation
figuration reuses the Phase16 C-minor scale walk, so the scalar-wave sanity
asserts exercise phase16_scale_up directly.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = SCRIPTS_DIR.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import run_phase_closure as rpc  # noqa: E402

FIXTURE_CPP = REPO_ROOT / "src" / "composer" / "harness_fixture.cpp"


def _phase20_body() -> str:
    """Return the source body following the buildPhase20Fixture declaration.

    Phase16 also declares kVarT0 / kRootPc and Phase16/19 declare kGroundPitch /
    kBarRoot style arrays; anchoring on the Phase20 builder body ensures only the
    Phase20 arrays are captured.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildPhase20Fixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildPhase20Fixture in harness_fixture.cpp")
    return body[1]


def _parse_array(name: str) -> tuple[int, ...]:
    """Parse a `<name>[kCycleBars] = { ... }` integer initialiser from the Phase20 body.

    @param name The C++ array identifier (e.g. ``kGroundPitch`` / ``kVarT0`` /
        ``kRootPc``).
    @return Tuple of the array's integer elements, in source order.
    """
    body = _phase20_body()
    match = re.search(rf"{name}\s*\[\s*kCycleBars\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[kCycleBars] in buildPhase20Fixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


class Phase20CppDriftGuardTest(unittest.TestCase):
    """The Python PHASE20 mirror constants must equal the C++ source."""

    def test_ground_matches_cpp(self) -> None:
        cpp = _parse_array("kGroundPitch")
        self.assertEqual(
            cpp,
            rpc.PHASE20_GROUND,
            "PHASE20_GROUND drifted from kGroundPitch[kCycleBars] in buildPhase20Fixture",
        )

    def test_var_t0_matches_cpp(self) -> None:
        cpp = _parse_array("kVarT0")
        self.assertEqual(
            cpp,
            rpc.PHASE20_VAR_T0,
            "PHASE20_VAR_T0 drifted from kVarT0[kCycleBars] in buildPhase20Fixture",
        )

    def test_bar_root_matches_cpp(self) -> None:
        cpp = _parse_array("kRootPc")
        self.assertEqual(
            cpp,
            rpc.PHASE20_BAR_ROOT,
            "PHASE20_BAR_ROOT drifted from kRootPc[kCycleBars] in buildPhase20Fixture",
        )

    def test_constants_are_8_bar_cycle(self) -> None:
        self.assertEqual(len(rpc.PHASE20_GROUND), 8)
        self.assertEqual(len(rpc.PHASE20_VAR_T0), 8)
        self.assertEqual(len(rpc.PHASE20_BAR_ROOT), 8)
        self.assertEqual(len(rpc.PHASE20_BAR_MINOR), 8)
        self.assertEqual(len(rpc.PHASE20_BLOCK_NPB), 3)


class Phase20RequiredBitsTest(unittest.TestCase):
    """The Phase20 required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_are_59_60_61(self) -> None:
        self.assertEqual(rpc.PHASE20_REQUIRED_BITS, (59, 60, 61))

    def test_matches_provenance_enum(self) -> None:
        prov = (REPO_ROOT / "src" / "composer" / "provenance.h").read_text(
            encoding="utf-8"
        )
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["PassacagliaGroundReplayed"]), 59)
        self.assertEqual(int(names["VariationApplied"]), 60)
        self.assertEqual(int(names["ClimaxPlaced"]), 61)


class Phase20ScaleWalkTest(unittest.TestCase):
    """The Phase20 variation figuration reuses the Phase16 C-minor scale walk."""

    def test_whole_step_from_c4(self) -> None:
        # C4 (60) + 1 degree -> D4 (62): a whole step (C->D in C natural minor).
        self.assertEqual(rpc.phase16_scale_up(60, 1), 62)

    def test_two_degrees_from_c4_is_eb4(self) -> None:
        # C4 (60) + 2 degrees -> Eb4 (63): C->D->Eb (C natural minor third).
        self.assertEqual(rpc.phase16_scale_up(60, 2), 63)


if __name__ == "__main__":
    unittest.main()
