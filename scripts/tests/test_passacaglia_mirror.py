"""Organ Passacaglia Passacaglia C++/Python drift guard.

The load-bearing guard: the bachlib.mirror PASSACAGLIA mirror constants
(PASSACAGLIA_GROUND, PASSACAGLIA_VAR_T0, PASSACAGLIA_BAR_ROOT) must match the C++ Passacaglia
fixture source (the kGroundPitch[kCycleBars], kVarT0[kCycleBars] and
kRootPc[kCycleBars] initialisers in buildPassacagliaFixture,
src/composer/harness_fixture.cpp). A renamed constant or an altered ground /
progression that the Python predictor still claims would make structural_ok
diverge from the CLI output; this drift guard fails instead. The Passacaglia variation
figuration reuses the Chaconne C-minor scale walk, so the scalar-wave sanity
asserts exercise chaconne_scale_up directly.
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

import bachlib as rpc  # noqa: E402

FIXTURE_CPP = REPO_ROOT / "src" / "composer" / "harness_fixture.cpp"


def _passacaglia_body() -> str:
    """Return the source body following the buildPassacagliaFixture declaration.

    Chaconne also declares kVarT0 / kRootPc and Chaconne/19 declare kGroundPitch /
    kBarRoot style arrays; anchoring on the Passacaglia builder body ensures only the
    Passacaglia arrays are captured.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildPassacagliaFixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildPassacagliaFixture in harness_fixture.cpp")
    return body[1]


def _parse_array(name: str) -> tuple[int, ...]:
    """Parse a `<name>[kCycleBars] = { ... }` integer initialiser from the Passacaglia body.

    @param name The C++ array identifier (e.g. ``kGroundPitch`` / ``kVarT0`` /
        ``kRootPc``).
    @return Tuple of the array's integer elements, in source order.
    """
    body = _passacaglia_body()
    match = re.search(rf"{name}\s*\[\s*kCycleBars\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[kCycleBars] in buildPassacagliaFixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


class PassacagliaCppDriftGuardTest(unittest.TestCase):
    """The Python PASSACAGLIA mirror constants must equal the C++ source."""

    def test_ground_matches_cpp(self) -> None:
        cpp = _parse_array("kGroundPitch")
        self.assertEqual(
            cpp,
            rpc.PASSACAGLIA_GROUND,
            "PASSACAGLIA_GROUND drifted from kGroundPitch[kCycleBars] in buildPassacagliaFixture",
        )

    def test_var_t0_matches_cpp(self) -> None:
        cpp = _parse_array("kVarT0")
        self.assertEqual(
            cpp,
            rpc.PASSACAGLIA_VAR_T0,
            "PASSACAGLIA_VAR_T0 drifted from kVarT0[kCycleBars] in buildPassacagliaFixture",
        )

    def test_bar_root_matches_cpp(self) -> None:
        cpp = _parse_array("kRootPc")
        self.assertEqual(
            cpp,
            rpc.PASSACAGLIA_BAR_ROOT,
            "PASSACAGLIA_BAR_ROOT drifted from kRootPc[kCycleBars] in buildPassacagliaFixture",
        )

    def test_constants_are_8_bar_cycle(self) -> None:
        self.assertEqual(len(rpc.PASSACAGLIA_GROUND), 8)
        self.assertEqual(len(rpc.PASSACAGLIA_VAR_T0), 8)
        self.assertEqual(len(rpc.PASSACAGLIA_BAR_ROOT), 8)
        self.assertEqual(len(rpc.PASSACAGLIA_BAR_MINOR), 8)
        self.assertEqual(len(rpc.PASSACAGLIA_BLOCK_NPB), 3)


class PassacagliaRequiredBitsTest(unittest.TestCase):
    """The Passacaglia required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_are_59_60_61(self) -> None:
        self.assertEqual(rpc.PASSACAGLIA_REQUIRED_BITS, (59, 60, 61))

    def test_matches_provenance_enum(self) -> None:
        prov = (REPO_ROOT / "src" / "composer" / "provenance.h").read_text(
            encoding="utf-8"
        )
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["PassacagliaGroundReplayed"]), 59)
        self.assertEqual(int(names["VariationApplied"]), 60)
        self.assertEqual(int(names["ClimaxPlaced"]), 61)


class PassacagliaScaleWalkTest(unittest.TestCase):
    """The Passacaglia variation figuration reuses the Chaconne C-minor scale walk."""

    def test_whole_step_from_c4(self) -> None:
        # C4 (60) + 1 degree -> D4 (62): a whole step (C->D in C natural minor).
        self.assertEqual(rpc.chaconne_scale_up(60, 1), 62)

    def test_two_degrees_from_c4_is_eb4(self) -> None:
        # C4 (60) + 2 degrees -> Eb4 (63): C->D->Eb (C natural minor third).
        self.assertEqual(rpc.chaconne_scale_up(60, 2), 63)


if __name__ == "__main__":
    unittest.main()
