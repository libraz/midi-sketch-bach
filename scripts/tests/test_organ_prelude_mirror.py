"""Organ Prelude (BWV846) OrganPrelude C++/Python drift guard.

The load-bearing guard: the bachlib.mirror ORGAN_PRELUDE mirror constants and
the organPrelude_scale_up walk must match the C++ OrganPrelude fixture source
(buildOrganPreludeFixture in src/composer/harness_fixture.cpp). A renamed constant or
an altered progression / scale that the Python predictor still claims would make
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

    @param name C++ array identifier (e.g. "kBarRoot").
    @return Tuple of the integer initialisers, in source order.
    """
    src = _composer_source()
    # Anchor on the array-declaration form ``name[...] = { ... }`` so an earlier
    # mention of the identifier in a comment does not capture a neighbour array.
    match = re.search(name + r"\s*\[[^\]]*\]\s*=\s*\{([^{}]*?)\}", src, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name} in {FIXTURE_CPP} / {FIGURATION_H}")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


def _parse_cpp_bool_list(name: str) -> tuple[bool, ...]:
    """Parse a C++ ``... kName[...] = { true, false, ... };`` bool initialiser.

    @param name C++ array identifier (e.g. "kBarMinor").
    @return Tuple of the bool initialisers, in source order.
    """
    src = _composer_source()
    match = re.search(name + r"\s*\[[^\]]*\]\s*=\s*\{([^{}]*?)\}", src, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name} in {FIXTURE_CPP} / {FIGURATION_H}")
    return tuple(tok == "true" for tok in re.split(r"[,\s]+", match.group(1)) if tok)


def _parse_organPrelude_scale_pcs() -> tuple[int, ...]:
    """Recover the C-major scale pitch classes from organPreludeInScale().

    The C++ helper enumerates the membership as ``p == 0 || p == 2 || ...``;
    parse those literals so a change to the scale set is caught here.
    """
    src = _composer_source()
    match = re.search(r"organPreludeInScale\(int pc\)\s*\{(.*?)\}", src, re.S)
    if match is None:
        raise AssertionError("could not locate organPreludeInScale in the composer sources")
    pcs = re.findall(r"p\s*==\s*(\d+)", match.group(1))
    return tuple(sorted(int(p) for p in pcs))


class OrganPreludeCppDriftGuardTest(unittest.TestCase):
    """The Python ORGAN_PRELUDE mirror constants must equal the C++ source."""

    def test_bar_root_matches_cpp(self) -> None:
        cpp = _parse_cpp_int_list("kBarRoot")
        self.assertEqual(
            cpp,
            rpc.ORGAN_PRELUDE_BAR_ROOT,
            "ORGAN_PRELUDE_BAR_ROOT drifted from kBarRoot in harness_fixture.cpp",
        )

    def test_bar_minor_matches_cpp(self) -> None:
        cpp = _parse_cpp_bool_list("kBarMinor")
        self.assertEqual(
            cpp,
            rpc.ORGAN_PRELUDE_BAR_MINOR,
            "ORGAN_PRELUDE_BAR_MINOR drifted from kBarMinor in harness_fixture.cpp",
        )

    def test_cmaj_scale_matches_cpp(self) -> None:
        cpp = _parse_organPrelude_scale_pcs()
        self.assertEqual(
            cpp,
            tuple(sorted(rpc.ORGAN_PRELUDE_CMAJ_SCALE)),
            "ORGAN_PRELUDE_CMAJ_SCALE drifted from organPreludeInScale in harness_fixture.cpp",
        )


class OrganPreludeScaleWalkTest(unittest.TestCase):
    """organPrelude_scale_up must walk C-major degrees, matching C++."""

    def test_whole_step_from_c4(self) -> None:
        # C4 (60) + 1 degree -> D4 (62): a whole step (C->D in C major).
        self.assertEqual(rpc.organPrelude_scale_up(60, 1), 62)

    def test_two_degrees_from_c4_is_e4(self) -> None:
        # C4 (60) + 2 degrees -> E4 (64): C->D->E (major third spans 4 semis).
        self.assertEqual(rpc.organPrelude_scale_up(60, 2), 64)

    def test_half_step_from_e4(self) -> None:
        # E4 (64) + 1 degree -> F4 (65): a semitone step (E->F in C major).
        self.assertEqual(rpc.organPrelude_scale_up(64, 1), 65)

    def test_zero_steps_is_identity(self) -> None:
        self.assertEqual(rpc.organPrelude_scale_up(60, 0), 60)


class OrganPreludeRequiredBitsTest(unittest.TestCase):
    """The OrganPrelude required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_are_52_53_54(self) -> None:
        self.assertEqual(rpc.ORGAN_PRELUDE_REQUIRED_BITS, (52, 53, 54))

    def test_matches_provenance_enum(self) -> None:
        prov = (REPO_ROOT / "src" / "composer" / "provenance.h").read_text(
            encoding="utf-8"
        )
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["FigurationCommitted"]), 52)
        self.assertEqual(int(names["CadenzaApplied"]), 53)
        self.assertEqual(int(names["PedalPreparation"]), 54)


if __name__ == "__main__":
    unittest.main()
