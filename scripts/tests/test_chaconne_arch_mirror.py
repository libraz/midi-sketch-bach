"""Solo String Arch (BWV1004 Chaconne) Chaconne C++/Python drift guard.

The load-bearing guard: the bachlib.mirror CHACONNE mirror constants and
the chaconne_scale_up walk must match the C++ Chaconne fixture source
(buildChaconneFixture in src/composer/harness_fixture.cpp). A renamed constant or
an altered ground/scale that the Python predictor still claims would make
structural_ok diverge from the CLI output; this drift guard fails instead.
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
# Shared scale helpers (chaconneInScale / organPreludeInScale) were promoted out of
# harness_fixture.cpp into figuration.h; the drift guards search both sources.
FIGURATION_H = REPO_ROOT / "src" / "composer" / "figuration.h"


def _composer_source() -> str:
    """Concatenated C++ source text searched by the drift-guard parsers."""
    return FIXTURE_CPP.read_text(encoding="utf-8") + FIGURATION_H.read_text(encoding="utf-8")


def _parse_cpp_int_list(name: str) -> tuple[int, ...]:
    """Parse a C++ ``... kName[...] = { a, b, c };`` flat int initialiser.

    @param name C++ array identifier (e.g. "kGroundPitch" / "kVarT0").
    @return Tuple of the integer initialisers, in source order.
    """
    src = _composer_source()
    match = re.search(name + r"\b[^=]*=\s*\{([^{}]*?)\}", src, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name} in {FIXTURE_CPP} / {FIGURATION_H}")
    return tuple(
        int(tok)
        for tok in match.group(1).replace(" ", "").split(",")
        if tok != ""
    )


def _parse_chaconne_scale_pcs() -> tuple[int, ...]:
    """Recover the C-minor scale pitch classes from chaconneInScale().

    The C++ helper enumerates the membership as ``p == 0 || p == 2 || ...``;
    parse those literals so a change to the scale set is caught here.
    """
    src = _composer_source()
    match = re.search(r"chaconneInScale\(int pc\)\s*\{(.*?)\}", src, re.S)
    if match is None:
        raise AssertionError("could not locate chaconneInScale in the composer sources")
    pcs = re.findall(r"p\s*==\s*(\d+)", match.group(1))
    return tuple(sorted(int(p) for p in pcs))


class ChaconneCppDriftGuardTest(unittest.TestCase):
    """The Python CHACONNE mirror constants must equal the C++ source."""

    def test_ground_matches_cpp(self) -> None:
        cpp = _parse_cpp_int_list("kGroundPitch")
        self.assertEqual(
            cpp,
            rpc.CHACONNE_GROUND,
            "CHACONNE_GROUND drifted from kGroundPitch in harness_fixture.cpp",
        )

    def test_var_t0_matches_cpp(self) -> None:
        cpp = _parse_cpp_int_list("kVarT0")
        self.assertEqual(
            cpp,
            rpc.CHACONNE_VAR_T0,
            "CHACONNE_VAR_T0 drifted from kVarT0 in harness_fixture.cpp",
        )

    def test_cmin_scale_matches_cpp(self) -> None:
        cpp = _parse_chaconne_scale_pcs()
        self.assertEqual(
            cpp,
            tuple(sorted(rpc.CHACONNE_CMIN_SCALE)),
            "CHACONNE_CMIN_SCALE drifted from chaconneInScale in harness_fixture.cpp",
        )

    def test_block_notes_per_beat_shape(self) -> None:
        # kBlocks density tiers: Ground=quarter(1), Respond=eighth(2),
        # Propel/Assert=sixteenth(4). Mirror must keep that 4-block shape.
        self.assertEqual(rpc.CHACONNE_BLOCK_NOTES_PER_BEAT, (1, 2, 4, 4))


class ChaconneScaleWalkTest(unittest.TestCase):
    """chaconne_scale_up must walk C-natural-minor degrees, matching C++."""

    def test_whole_step_from_c4(self) -> None:
        # C4 (60) + 1 degree -> D4 (62): a whole step (C->D in C minor).
        self.assertEqual(rpc.chaconne_scale_up(60, 1), 62)

    def test_two_degrees_from_c4_is_eb4(self) -> None:
        # C4 (60) + 2 degrees -> Eb4 (63): C->D->Eb (minor third spans 3 semis).
        self.assertEqual(rpc.chaconne_scale_up(60, 2), 63)

    def test_half_step_from_bb3(self) -> None:
        # Bb3 (58) + 1 degree -> C4 (60): a whole step (Bb->C in C minor).
        self.assertEqual(rpc.chaconne_scale_up(58, 1), 60)

    def test_zero_steps_is_identity(self) -> None:
        self.assertEqual(rpc.chaconne_scale_up(55, 0), 55)


class ChaconneRequiredBitsTest(unittest.TestCase):
    """The Chaconne required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_are_49_50_51(self) -> None:
        self.assertEqual(rpc.CHACONNE_REQUIRED_BITS, (49, 50, 51))

    def test_matches_provenance_enum(self) -> None:
        prov = (REPO_ROOT / "src" / "composer" / "provenance.h").read_text(
            encoding="utf-8"
        )
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["GroundBassReplayed"]), 49)
        self.assertEqual(int(names["VariationRoleApplied"]), 50)
        self.assertEqual(int(names["TextureDensityShift"]), 51)


if __name__ == "__main__":
    unittest.main()
