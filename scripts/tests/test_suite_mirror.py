"""Keyboard suite Phase23 C++/Python drift guard.

The load-bearing guard: the bachlib.mirror PHASE23 mirror constants
(PHASE23_BAR_ROOT, PHASE23_BAR_MINOR, PHASE23_GROUND, PHASE23_GROUND_PERIOD, and
the kMovements[5] window / carrier / kind / base_midi table) must match the C++
Phase23 fixture source (the kBarRoot[4] / kBarMinor[4] / kGroundPitch[4]
initialisers and the kMovements[5] movement table in buildPhase23Fixture,
src/composer/harness_fixture.cpp), and the three reused RuleBit numbers
(FigurationCommitted=52, FantasiaSectionContrast=63, GroundBassReplayed=49) must
match provenance.h. A renamed constant or an altered progression / movement /
ground table that the Python predictor still claims would make structural_ok
diverge from the CLI output; this drift guard fails instead. The Phase23 fixture
is a reuse-only assembly (no new VoiceIntent / RuleBit / validator rule), so the
bit guard asserts the three numbers stay pinned to their existing provenance.h
values. The suite figuration reuses the Phase17 C-major scale walk, so the
scalar-wave sanity asserts exercise phase17_scale_up directly.
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
PROVENANCE_H = REPO_ROOT / "src" / "composer" / "provenance.h"


def _phase23_body() -> str:
    """Return the source body following the buildPhase23Fixture declaration.

    Earlier builders (Phase17..22) also declare kBarRoot / kBarMinor /
    kGroundPitch style arrays; anchoring on the Phase23 builder body ensures only
    the Phase23 arrays / kMovements are captured.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildPhase23Fixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildPhase23Fixture in harness_fixture.cpp")
    return body[1]


def _parse_int_array(name: str) -> tuple[int, ...]:
    """Parse a `<name>[N] = { ... }` integer initialiser from the Phase23 body."""
    body = _phase23_body()
    match = re.search(rf"{name}\s*\[[^\]]*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[...] in buildPhase23Fixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


def _parse_bool_array(name: str) -> tuple[bool, ...]:
    """Parse a `<name>[N] = { ... }` bool initialiser from the Phase23 body."""
    body = _phase23_body()
    match = re.search(rf"{name}\s*\[[^\]]*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[...] in buildPhase23Fixture")
    return tuple(tok == "true" for tok in re.split(r"[,\s]+", match.group(1)) if tok)


class Phase23CppDriftGuardTest(unittest.TestCase):
    """The Python PHASE23 mirror constants must equal the C++ source."""

    def test_bar_root_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_int_array("kBarRoot"),
            rpc.PHASE23_BAR_ROOT,
            "PHASE23_BAR_ROOT drifted from kBarRoot[4] in buildPhase23Fixture",
        )

    def test_bar_minor_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_bool_array("kBarMinor"),
            rpc.PHASE23_BAR_MINOR,
            "PHASE23_BAR_MINOR drifted from kBarMinor[4] in buildPhase23Fixture",
        )

    def test_ground_pitch_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_int_array("kGroundPitch"),
            rpc.PHASE23_GROUND,
            "PHASE23_GROUND drifted from kGroundPitch[4] in buildPhase23Fixture",
        )

    def test_ground_period_matches_cpp(self) -> None:
        # ground_bass_period = kCycleBars (4) * kTicksPerBar (1920) = 7680.
        self.assertEqual(rpc.PHASE23_GROUND_PERIOD, 4 * 1920)
        body = _phase23_body()
        self.assertIn(
            "ground_bass_period = static_cast<Tick>(kCycleBars) * kTicksPerBar",
            body,
            "PHASE23_GROUND_PERIOD construction drifted in buildPhase23Fixture",
        )

    def test_movements_match_cpp(self) -> None:
        """The kMovements[5] table (first/last bar, carrier, kind, base_midi)."""
        body = _phase23_body()
        match = re.search(r"kMovements\s*\[\s*\d*\s*\]\s*=\s*\{(.*?)\};", body, re.S)
        self.assertIsNotNone(match, "could not locate kMovements[5] in buildPhase23Fixture")
        block = match.group(1)
        # Each row: {first, last, carrier, kind, base_midi, FantasiaStyle::X, density}.
        rows = re.findall(
            r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,"
            r"\s*FantasiaStyle::\w+\s*,\s*\d+\s*\}",
            block,
        )
        parsed = tuple(
            (int(first), int(last), int(carrier), int(kind), int(base))
            for first, last, carrier, kind, base in rows
        )
        self.assertEqual(
            parsed,
            rpc.PHASE23_MOVEMENTS,
            "PHASE23_MOVEMENTS drifted from kMovements[5] in buildPhase23Fixture",
        )

    def test_constants_shape(self) -> None:
        self.assertEqual(len(rpc.PHASE23_BAR_ROOT), 4)
        self.assertEqual(len(rpc.PHASE23_BAR_MINOR), 4)
        self.assertEqual(len(rpc.PHASE23_GROUND), 4)
        self.assertEqual(len(rpc.PHASE23_MOVEMENTS), 5)
        # The five movements tile bars 0..19 contiguously.
        self.assertEqual(
            tuple((f, l) for f, l, _c, _k, _b in rpc.PHASE23_MOVEMENTS),
            ((0, 3), (4, 7), (8, 11), (12, 15), (16, 19)),
        )
        # Carriers: Figuration / Fantasia / Fantasia / Figuration / Fantasia.
        self.assertEqual(
            tuple(c for _f, _l, c, _k, _b in rpc.PHASE23_MOVEMENTS),
            (0, 1, 1, 0, 1),
        )
        # Kinds: sixteenths / eighths / halves / eighths / sixteenths.
        self.assertEqual(
            tuple(k for _f, _l, _c, k, _b in rpc.PHASE23_MOVEMENTS),
            (1, 0, 2, 0, 1),
        )
        # Register bases C5 / C5 / C5 / E5 / E5.
        self.assertEqual(
            tuple(b for _f, _l, _c, _k, b in rpc.PHASE23_MOVEMENTS),
            (72, 72, 72, 76, 76),
        )


class Phase23RequiredBitsTest(unittest.TestCase):
    """The Phase23 required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_set(self) -> None:
        # Reuse-only assembly: FigurationCommitted / FantasiaSectionContrast /
        # GroundBassReplayed. No new bit is introduced.
        self.assertEqual(rpc.PHASE23_REQUIRED_BITS, (52, 63, 49))

    def test_matches_provenance_enum(self) -> None:
        prov = PROVENANCE_H.read_text(encoding="utf-8")
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["FigurationCommitted"]), 52)
        self.assertEqual(int(names["FantasiaSectionContrast"]), 63)
        self.assertEqual(int(names["GroundBassReplayed"]), 49)


class Phase23ScaleWalkTest(unittest.TestCase):
    """The Phase23 figuration reuses the Phase17 C-major scale walk."""

    def test_whole_step_from_c5(self) -> None:
        # C5 (72) + 1 degree -> D5 (74): a whole step (C->D in C major).
        self.assertEqual(rpc.phase17_scale_up(72, 1), 74)

    def test_two_degrees_from_e5_is_g5(self) -> None:
        # E5 (76) + 2 degrees -> G5 (79): E->F->G in C major.
        self.assertEqual(rpc.phase17_scale_up(76, 2), 79)


class Phase23SequenceShapeTest(unittest.TestCase):
    """The suite predictor emits 220 notes across three carrier groups."""

    def test_total_note_count(self) -> None:
        for seed in range(20):
            fixture = {"harm_idx": seed % 4, "seed": seed}
            seqs = rpc.expected_suite_sequence(fixture)
            self.assertEqual(
                set(seqs.keys()),
                {(0, "FigurationCarrier"), (0, "FantasiaCarrier"), (1, "GroundCarrier")},
            )
            fig = seqs[(0, "FigurationCarrier")]
            fan = seqs[(0, "FantasiaCarrier")]
            gr = seqs[(1, "GroundCarrier")]
            # Figuration = Prelude 64 + Courante 32 = 96.
            self.assertEqual(len(fig), 96, f"seed {seed} figuration note count")
            # Fantasia = Allemande 32 + Sarabande 8 + Gigue 64 = 104.
            self.assertEqual(len(fan), 104, f"seed {seed} fantasia note count")
            # Ground = 4-bar figure tiled 5x = 20.
            self.assertEqual(len(gr), 20, f"seed {seed} ground note count")
            self.assertEqual(len(fig) + len(fan) + len(gr), 220, f"seed {seed} total")

    def test_ground_register_below_dance(self) -> None:
        # The ground (40..52) sits strictly below the V0 dance line (>= 72), so
        # no voice crossing occurs.
        seqs = rpc.expected_suite_sequence({"harm_idx": 0, "seed": 0})
        gr = [p for _t, p in seqs[(1, "GroundCarrier")]]
        self.assertEqual(set(gr), set(rpc.PHASE23_GROUND))
        self.assertLessEqual(max(gr), 52)
        dance = [p for _t, p in seqs[(0, "FigurationCarrier")]]
        dance += [p for _t, p in seqs[(0, "FantasiaCarrier")]]
        self.assertGreaterEqual(min(dance), 72)

    def test_seed0_dance_register(self) -> None:
        # Seed 0 (offset 0) dance lines start in the C5 / E5 region and never dip
        # below C5 (72); the offset only shifts the peak upward.
        seqs = rpc.expected_suite_sequence({"harm_idx": 0, "seed": 0})
        dance = [p for _t, p in seqs[(0, "FigurationCarrier")]]
        dance += [p for _t, p in seqs[(0, "FantasiaCarrier")]]
        self.assertGreaterEqual(min(dance), 72)


if __name__ == "__main__":
    unittest.main()
