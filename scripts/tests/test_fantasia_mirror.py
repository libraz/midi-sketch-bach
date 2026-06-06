"""Organ Fantasia Phase22 C++/Python drift guard.

The load-bearing guard: the bachlib.mirror PHASE22 mirror constants
(PHASE22_BAR_ROOT, PHASE22_BAR_MINOR, the four-section kSpecs windows /
densities / register bases) must match the C++ Phase22 fixture source (the
kBarRoot[4] / kBarMinor[4] initialisers and the kSpecs[4] section table in
buildPhase22Fixture, src/composer/harness_fixture.cpp), the validator's
section_contrast_required margins (2 notes/bar, 5 semitones) must match
validator.cpp, and the FantasiaSectionContrast RuleBit number (63) must match
provenance.h. A renamed constant or an altered progression / section table that
the Python predictor still claims would make structural_ok diverge from the CLI
output; this drift guard fails instead. The Phase22 figuration reuses the Phase17
C-major scale walk, so the scalar-wave sanity asserts exercise phase17_scale_up
directly.
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


def _phase22_body() -> str:
    """Return the source body following the buildPhase22Fixture declaration.

    Earlier builders (Phase17..21) also declare kBarRoot / kBarMinor style
    arrays; anchoring on the Phase22 builder body ensures only the Phase22
    arrays / kSpecs are captured.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildPhase22Fixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildPhase22Fixture in harness_fixture.cpp")
    return body[1]


def _parse_int_array(name: str) -> tuple[int, ...]:
    """Parse a `<name>[N] = { ... }` integer initialiser from the Phase22 body."""
    body = _phase22_body()
    match = re.search(rf"{name}\s*\[\s*\d*\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[...] in buildPhase22Fixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


def _parse_bool_array(name: str) -> tuple[bool, ...]:
    """Parse a `<name>[N] = { ... }` bool initialiser from the Phase22 body."""
    body = _phase22_body()
    match = re.search(rf"{name}\s*\[\s*\d*\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[...] in buildPhase22Fixture")
    return tuple(tok == "true" for tok in re.split(r"[,\s]+", match.group(1)) if tok)


class Phase22CppDriftGuardTest(unittest.TestCase):
    """The Python PHASE22 mirror constants must equal the C++ source."""

    def test_bar_root_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_int_array("kBarRoot"),
            rpc.PHASE22_BAR_ROOT,
            "PHASE22_BAR_ROOT drifted from kBarRoot[4] in buildPhase22Fixture",
        )

    def test_bar_minor_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_bool_array("kBarMinor"),
            rpc.PHASE22_BAR_MINOR,
            "PHASE22_BAR_MINOR drifted from kBarMinor[4] in buildPhase22Fixture",
        )

    def test_sections_match_cpp(self) -> None:
        """The kSpecs[4] section table (first/last bar, density, base, kind)."""
        body = _phase22_body()
        # Capture the kSpecs[4] = { ... } initialiser block.
        match = re.search(r"kSpecs\s*\[\s*\d*\s*\]\s*=\s*\{(.*?)\};", body, re.S)
        self.assertIsNotNone(match, "could not locate kSpecs[4] in buildPhase22Fixture")
        block = match.group(1)
        # Each row: {first, last, FantasiaStyle::X, density, base, kind}.
        rows = re.findall(
            r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*FantasiaStyle::\w+\s*,"
            r"\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
            block,
        )
        parsed = tuple(
            (int(first), int(last), int(density), int(base), int(kind))
            for first, last, density, base, kind in rows
        )
        self.assertEqual(
            parsed,
            rpc.PHASE22_SECTIONS,
            "PHASE22_SECTIONS drifted from kSpecs[4] in buildPhase22Fixture",
        )

    def test_constants_shape(self) -> None:
        self.assertEqual(len(rpc.PHASE22_BAR_ROOT), 4)
        self.assertEqual(len(rpc.PHASE22_BAR_MINOR), 4)
        self.assertEqual(len(rpc.PHASE22_SECTIONS), 4)
        # The four sections tile bars 0..15 contiguously.
        self.assertEqual(
            tuple((f, l) for f, l, _d, _b, _k in rpc.PHASE22_SECTIONS),
            ((0, 3), (4, 7), (8, 11), (12, 15)),
        )
        # Densities 4 / 8 / 16 / 2 as documented.
        self.assertEqual(
            tuple(d for _f, _l, d, _b, _k in rpc.PHASE22_SECTIONS),
            (4, 8, 16, 2),
        )
        # Register bases C3 / C4 / C5 / C4.
        self.assertEqual(
            tuple(b for _f, _l, _d, b, _k in rpc.PHASE22_SECTIONS),
            (48, 60, 72, 60),
        )


class Phase22RequiredBitsTest(unittest.TestCase):
    """The Phase22 required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_is_63(self) -> None:
        self.assertEqual(rpc.PHASE22_REQUIRED_BITS, (63,))

    def test_matches_provenance_enum(self) -> None:
        prov = PROVENANCE_H.read_text(encoding="utf-8")
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["FantasiaSectionContrast"]), 63)


class Phase22ValidatorMarginsTest(unittest.TestCase):
    """The section_contrast_required margins must stay at 2 notes/bar, 5 st."""

    def test_margins_match_cpp(self) -> None:
        src = VALIDATOR_CPP.read_text(encoding="utf-8")
        parts = src.split("section_contrast_required")
        self.assertGreaterEqual(len(parts), 2)
        body = parts[1]
        self.assertIn(
            f"kMinDensityMargin = {rpc.PHASE22_MIN_DENSITY_MARGIN}",
            body,
            "section_contrast_required density margin drifted from 2 in validator.cpp",
        )
        self.assertIn(
            f"kMinRegisterMargin = {rpc.PHASE22_MIN_REGISTER_MARGIN}",
            body,
            "section_contrast_required register margin drifted from 5 in validator.cpp",
        )


class Phase22ScaleWalkTest(unittest.TestCase):
    """The Phase22 figuration reuses the Phase17 C-major scale walk."""

    def test_whole_step_from_c5(self) -> None:
        # C5 (72) + 1 degree -> D5 (74): a whole step (C->D in C major).
        self.assertEqual(rpc.phase17_scale_up(72, 1), 74)

    def test_two_degrees_from_c5_is_e5(self) -> None:
        # C5 (72) + 2 degrees -> E5 (76): C->D->E (C major third).
        self.assertEqual(rpc.phase17_scale_up(72, 2), 76)


class Phase22SequenceShapeTest(unittest.TestCase):
    """The fantasia predictor emits 120 notes in range 48..88 for every seed."""

    def test_total_note_count(self) -> None:
        for seed in range(20):
            fixture = {"harm_idx": seed % 4, "seed": seed}
            seqs = rpc.expected_fantasia_sequence(fixture)
            self.assertEqual(list(seqs.keys()), [(0, "FantasiaCarrier")])
            seq = seqs[(0, "FantasiaCarrier")]
            # 16 (A quarters) + 32 (B eighths) + 64 (C sixteenths) + 8 (D halves).
            self.assertEqual(len(seq), 120, f"seed {seed} note count")
            # The base register starts at C3 (48); the scalar waves never dip
            # below it, and the offset only shifts the peak upward.
            pitches = [p for _t, p in seq]
            self.assertGreaterEqual(min(pitches), 48, f"seed {seed} low pitch")

    def test_seed0_pitch_range(self) -> None:
        # Seed 0 (offset 0) is the documented reference range: C3 (48) .. 88.
        seq = rpc.expected_fantasia_sequence({"harm_idx": 0, "seed": 0})[(0, "FantasiaCarrier")]
        pitches = [p for _t, p in seq]
        self.assertEqual(min(pitches), 48)
        self.assertEqual(max(pitches), 88)


if __name__ == "__main__":
    unittest.main()
