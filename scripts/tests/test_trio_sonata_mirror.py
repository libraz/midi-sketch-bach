"""Organ Trio Sonata TrioSonata C++/Python drift guard.

The load-bearing guard: the bachlib.mirror TRIO_SONATA mirror constants
(TRIO_SONATA_BAR_ROOT, TRIO_SONATA_BAR_MINOR, the per-voice register bases and
densities) must match the C++ TrioSonata fixture source (the kBarRoot[4] /
kBarMinor[4] initialisers and the V0/V1/V2 base / notes_per_beat literals in
buildTrioSonataFixture, src/composer/harness_fixture.cpp), the validator's
voice_independence_threshold soft threshold (0.6) must match validator.cpp, and
the TrioVoiceIndependent RuleBit number (62) must match provenance.h. A renamed
constant or an altered progression / register that the Python predictor still
claims would make structural_ok diverge from the CLI output; this drift guard
fails instead. The TrioSonata figuration reuses the OrganPrelude C-major scale walk, so
the scalar-wave sanity asserts exercise organPrelude_scale_up directly.
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
VALIDATOR_CPP = REPO_ROOT / "src" / "composer" / "validator.cpp"
PROVENANCE_H = REPO_ROOT / "src" / "composer" / "provenance.h"


def _trioSonata_body() -> str:
    """Return the source body following the buildTrioSonataFixture declaration.

    Earlier builders (OrganPrelude/18/19/20) also declare kBarRoot / kBarMinor style
    arrays; anchoring on the TrioSonata builder body ensures only the TrioSonata
    arrays are captured.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildTrioSonataFixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildTrioSonataFixture in harness_fixture.cpp")
    return body[1]


def _parse_int_array(name: str) -> tuple[int, ...]:
    """Parse a `<name>[N] = { ... }` integer initialiser from the TrioSonata body."""
    body = _trioSonata_body()
    match = re.search(rf"{name}\s*\[\s*\d*\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[...] in buildTrioSonataFixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


def _parse_bool_array(name: str) -> tuple[bool, ...]:
    """Parse a `<name>[N] = { ... }` bool initialiser from the TrioSonata body."""
    body = _trioSonata_body()
    match = re.search(rf"{name}\s*\[\s*\d*\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[...] in buildTrioSonataFixture")
    return tuple(tok == "true" for tok in re.split(r"[,\s]+", match.group(1)) if tok)


class TrioSonataCppDriftGuardTest(unittest.TestCase):
    """The Python TRIO_SONATA mirror constants must equal the C++ source."""

    def test_bar_root_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_int_array("kBarRoot"),
            rpc.TRIO_SONATA_BAR_ROOT,
            "TRIO_SONATA_BAR_ROOT drifted from kBarRoot[4] in buildTrioSonataFixture",
        )

    def test_bar_minor_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_bool_array("kBarMinor"),
            rpc.TRIO_SONATA_BAR_MINOR,
            "TRIO_SONATA_BAR_MINOR drifted from kBarMinor[4] in buildTrioSonataFixture",
        )

    def test_register_bases_match_cpp(self) -> None:
        body = _trioSonata_body()
        # V0 (RH / Great) appends from 72 + (kBarRoot[bar % 4] % 12).
        self.assertIn(
            f"{rpc.TRIO_SONATA_V0_BASE} + (kBarRoot[bar % 4] % 12)",
            body,
            "TRIO_SONATA_V0_BASE drifted from the V0 (Great) register base in buildTrioSonataFixture",
        )
        # V1 (LH / Swell) appends from 60 + (kBarRoot[bar % 4] % 12).
        self.assertIn(
            f"{rpc.TRIO_SONATA_V1_BASE} + (kBarRoot[bar % 4] % 12)",
            body,
            "TRIO_SONATA_V1_BASE drifted from the V1 (Swell) register base in buildTrioSonataFixture",
        )
        # V2 (Pedal) root_midi = 40 + root_pc.
        self.assertIn(
            f"{rpc.TRIO_SONATA_PEDAL_BASE} + root_pc",
            body,
            "TRIO_SONATA_PEDAL_BASE drifted from the V2 (Pedal) register base in buildTrioSonataFixture",
        )

    def test_voice_densities_match_cpp(self) -> None:
        body = _trioSonata_body()
        # V0 calls appendScalarBar with notes_per_beat = 4 (sixteenths).
        self.assertIn(
            f"/*notes_per_beat=*/{rpc.TRIO_SONATA_V0_NPB}",
            body,
            "TRIO_SONATA_V0_NPB drifted from the V0 notes_per_beat in buildTrioSonataFixture",
        )
        # V1 calls appendScalarBar with notes_per_beat = 2 (eighths).
        self.assertIn(
            f"/*notes_per_beat=*/{rpc.TRIO_SONATA_V1_NPB}",
            body,
            "TRIO_SONATA_V1_NPB drifted from the V1 notes_per_beat in buildTrioSonataFixture",
        )

    def test_constants_shape(self) -> None:
        self.assertEqual(len(rpc.TRIO_SONATA_BAR_ROOT), 4)
        self.assertEqual(len(rpc.TRIO_SONATA_BAR_MINOR), 4)


class TrioSonataRequiredBitsTest(unittest.TestCase):
    """The TrioSonata required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_is_62(self) -> None:
        self.assertEqual(rpc.TRIO_SONATA_REQUIRED_BITS, (62,))

    def test_matches_provenance_enum(self) -> None:
        prov = PROVENANCE_H.read_text(encoding="utf-8")
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["TrioVoiceIndependent"]), 62)


class TrioSonataValidatorThresholdTest(unittest.TestCase):
    """The voice_independence_threshold soft cut-off must stay at 0.6."""

    def test_threshold_is_0_6(self) -> None:
        src = VALIDATOR_CPP.read_text(encoding="utf-8")
        # Anchor on the rule body: the section runs from the rule's comment
        # header through to the failure.rule_id assignment. The < 0.6 soft
        # cut-off must appear inside that window.
        parts = src.split("voice_independence_threshold")
        # [0] = source before the comment header; [1] = the rule body (ends just
        # before the failure.rule_id = "voice_independence_threshold" literal).
        self.assertGreaterEqual(len(parts), 2)
        body = parts[1]
        self.assertIn(
            "mean_independence < 0.6",
            body,
            "voice_independence_threshold soft cut-off drifted from 0.6 in validator.cpp",
        )


class TrioSonataScaleWalkTest(unittest.TestCase):
    """The TrioSonata figuration reuses the OrganPrelude C-major scale walk."""

    def test_whole_step_from_c5(self) -> None:
        # C5 (72) + 1 degree -> D5 (74): a whole step (C->D in C major).
        self.assertEqual(rpc.organPrelude_scale_up(72, 1), 74)

    def test_two_degrees_from_c5_is_e5(self) -> None:
        # C5 (72) + 2 degrees -> E5 (76): C->D->E (C major third).
        self.assertEqual(rpc.organPrelude_scale_up(72, 2), 76)


if __name__ == "__main__":
    unittest.main()
