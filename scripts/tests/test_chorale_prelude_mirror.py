"""Organ Chorale Prelude ChoralePrelude C++/Python drift guard.

The load-bearing guard: the bachlib.mirror CHORALE_PRELUDE mirror constants
(CHORALE_PRELUDE_CF_SKELETON, CHORALE_PRELUDE_BAR_ROOT) must match the C++ ChoralePrelude fixture
source (the kCfSkeleton[kBars] and kBarRoot[kBars] initialisers in
buildChoralePreludeFixture, src/composer/harness_fixture.cpp). A renamed constant or an
altered chorale tune / progression that the Python predictor still claims would
make structural_ok diverge from the CLI output; this drift guard fails instead.
The ChoralePrelude figuration reuses the OrganPrelude C-major scale walk, and the embellishment
step uses the same scale-membership test, so the stepToward sanity asserts
exercise choralePrelude_cf_embellished / organPrelude_scale_up directly.
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


def _choralePrelude_body() -> str:
    """Return the source body following the buildChoralePreludeFixture declaration."""
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildChoralePreludeFixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildChoralePreludeFixture in harness_fixture.cpp")
    return body[1]


def _parse_array(name: str) -> tuple[int, ...]:
    """Parse a `<name>[kBars] = { ... }` integer initialiser from the ChoralePrelude body.

    @param name The C++ array identifier (e.g. ``kCfSkeleton`` / ``kBarRoot``).
    @return Tuple of the array's integer elements, in source order.
    """
    body = _choralePrelude_body()
    match = re.search(rf"{name}\s*\[\s*kBars\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[kBars] in buildChoralePreludeFixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


class ChoralePreludeCppDriftGuardTest(unittest.TestCase):
    """The Python CHORALE_PRELUDE mirror constants must equal the C++ source."""

    def test_cf_skeleton_matches_cpp(self) -> None:
        cpp = _parse_array("kCfSkeleton")
        self.assertEqual(
            cpp,
            rpc.CHORALE_PRELUDE_CF_SKELETON,
            "CHORALE_PRELUDE_CF_SKELETON drifted from kCfSkeleton[kBars] in buildChoralePreludeFixture",
        )

    def test_bar_root_matches_cpp(self) -> None:
        cpp = _parse_array("kBarRoot")
        self.assertEqual(
            cpp,
            rpc.CHORALE_PRELUDE_BAR_ROOT,
            "CHORALE_PRELUDE_BAR_ROOT drifted from kBarRoot[kBars] in buildChoralePreludeFixture",
        )

    def test_skeleton_and_root_are_16_bars(self) -> None:
        self.assertEqual(len(rpc.CHORALE_PRELUDE_CF_SKELETON), 16)
        self.assertEqual(len(rpc.CHORALE_PRELUDE_BAR_ROOT), 16)


class ChoralePreludeRequiredBitsTest(unittest.TestCase):
    """The ChoralePrelude required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_are_57_58(self) -> None:
        self.assertEqual(rpc.CHORALE_PRELUDE_REQUIRED_BITS, (57, 58))

    def test_matches_provenance_enum(self) -> None:
        prov = (REPO_ROOT / "src" / "composer" / "provenance.h").read_text(
            encoding="utf-8"
        )
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["CantusFirmusReplayed"]), 57)
        self.assertEqual(int(names["CFEmbellishmentApplied"]), 58)


class ChoralePreludeEmbellishmentWalkTest(unittest.TestCase):
    """The embellishment / scale walk mirrors the C++ stepToward construction."""

    def test_cf_has_48_notes(self) -> None:
        # 16 bars x (1 downbeat half + 2 passing quarters) = 48 notes.
        self.assertEqual(len(rpc.choralePrelude_cf_embellished()), 48)

    def test_downbeats_equal_skeleton(self) -> None:
        emb = rpc.choralePrelude_cf_embellished()
        for bar, tone in enumerate(rpc.CHORALE_PRELUDE_CF_SKELETON):
            downbeats = [p for (t, p) in emb if t == bar * 1920]
            self.assertEqual(downbeats, [tone], f"bar {bar} downbeat != skeleton tone")

    def test_step_toward_c_to_d_takes_two_semitones(self) -> None:
        # stepToward(48, 50): C#3 (49) is not in C major, so the step is +2 -> D3.
        emb = rpc.choralePrelude_cf_embellished()
        # bar 0: skeleton C3 (48) -> next D3 (50); first passing tone at tick 960.
        first_passing = next(p for (t, p) in emb if t == 960)
        self.assertEqual(first_passing, 50)


if __name__ == "__main__":
    unittest.main()
