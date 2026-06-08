"""Organ Toccata OrganToccata C++/Python drift guard.

The load-bearing guard: the bachlib.mirror ORGAN_TOCCATA mirror constant
(ORGAN_TOCCATA_BAR_ROOT) must match the C++ OrganToccata fixture source (the kBarRoot[4]
initialiser in buildOrganToccataFixture, src/composer/harness_fixture.cpp). A renamed
constant or an altered progression that the Python predictor still claims would
make structural_ok diverge from the CLI output; this drift guard fails instead.
The OrganToccata figuration reuses the OrganPrelude C-major scale walk, so the scalar-wave
sanity asserts exercise organPrelude_scale_up directly.
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


def _parse_organToccata_bar_root() -> tuple[int, ...]:
    """Parse kBarRoot[4] from buildOrganToccataFixture.

    OrganPrelude also declares a kBarRoot[16]; anchor on the OrganToccata builder body and
    on the ``[4]`` extent so the OrganPrelude array is never captured.

    @return Tuple of the four root pitch classes, in source order.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildOrganToccataFixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildOrganToccataFixture in harness_fixture.cpp")
    match = re.search(r"kBarRoot\s*\[\s*4\s*\]\s*=\s*\{([^{}]*?)\}", body[1], re.S)
    if match is None:
        raise AssertionError("could not locate kBarRoot[4] in buildOrganToccataFixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


class OrganToccataCppDriftGuardTest(unittest.TestCase):
    """The Python ORGAN_TOCCATA mirror constant must equal the C++ source."""

    def test_bar_root_matches_cpp(self) -> None:
        cpp = _parse_organToccata_bar_root()
        self.assertEqual(
            cpp,
            rpc.ORGAN_TOCCATA_BAR_ROOT,
            "ORGAN_TOCCATA_BAR_ROOT drifted from kBarRoot[4] in buildOrganToccataFixture",
        )

    def test_bar_root_is_I_IV_V_vi(self) -> None:
        # I IV V vi over C major: roots C F G A.
        self.assertEqual(rpc.ORGAN_TOCCATA_BAR_ROOT, (0, 5, 7, 9))


class OrganToccataRequiredBitsTest(unittest.TestCase):
    """The OrganToccata required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_are_55_56(self) -> None:
        self.assertEqual(rpc.ORGAN_TOCCATA_REQUIRED_BITS, (55, 56))

    def test_matches_provenance_enum(self) -> None:
        prov = (REPO_ROOT / "src" / "composer" / "provenance.h").read_text(
            encoding="utf-8"
        )
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["ToccataArchetypeApplied"]), 55)
        self.assertEqual(int(names["SectionTransition"]), 56)


class OrganToccataScaleWalkTest(unittest.TestCase):
    """The OrganToccata figuration reuses the OrganPrelude C-major scale walk."""

    def test_whole_step_from_c4(self) -> None:
        # C4 (60) + 1 degree -> D4 (62): a whole step (C->D in C major).
        self.assertEqual(rpc.organPrelude_scale_up(60, 1), 62)

    def test_two_degrees_from_c4_is_e4(self) -> None:
        # C4 (60) + 2 degrees -> E4 (64): C->D->E (major third spans 4 semis).
        self.assertEqual(rpc.organPrelude_scale_up(60, 2), 64)


if __name__ == "__main__":
    unittest.main()
