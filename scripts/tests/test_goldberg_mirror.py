"""Goldberg-style immutable-bass variation Phase25 C++/Python drift guard.

The load-bearing guard: the bachlib.mirror PHASE25 mirror constants
(PHASE25_GROUND, PHASE25_BAR_ROOT, PHASE25_BAR_MINOR, the PHASE25_BLOCK_SPEC
table -- including is_climax on block 4 -- and PHASE25_GROUND_PERIOD) must match
the C++ Phase25 fixture source (the kGroundPitch[kCycleBars] / kBarRoot[kCycleBars]
/ kBarMinor[kCycleBars] initialisers and the kBlockSpec[kBlocks] table in
buildPhase25Fixture, src/composer/harness_fixture.cpp), and the three reused
Passacaglia RuleBit numbers (PassacagliaGroundReplayed=59, VariationApplied=60,
ClimaxPlaced=61) must match provenance.h. A renamed constant or an altered ground
/ progression / block table that the Python predictor still claims would make
structural_ok diverge from the CLI output; this drift guard fails instead. The
Phase25 fixture is a reduced-scope reuse-only assembly (no new VoiceIntent /
RuleBit / validator rule), so the bit guard asserts the three numbers stay pinned
to their existing provenance.h values, and the predictor must reproduce all 172
notes byte-for-byte -- including the SPECIAL Aria (block 0) two-half-note layout,
which is NOT the scalar wave the other four blocks use.
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


def _phase25_body() -> str:
    """Return the source body following the buildPhase25Fixture declaration.

    Earlier builders (Phase16..24) also declare kGroundPitch / kBarRoot /
    kBarMinor style arrays; anchoring on the Phase25 builder body ensures only
    the Phase25 arrays / block table are captured.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildPhase25Fixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildPhase25Fixture in harness_fixture.cpp")
    # Stop at the next builder / namespace close so trailing functions are excluded.
    return body[1].split("}  // namespace", 1)[0]


def _parse_int_array(name: str) -> tuple[int, ...]:
    """Parse a `<name>[kCycleBars] = { ... }` integer initialiser from the body."""
    body = _phase25_body()
    match = re.search(rf"{name}\s*\[\s*kCycleBars\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[kCycleBars] in buildPhase25Fixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


def _parse_bool_array(name: str) -> tuple[bool, ...]:
    """Parse a `<name>[kCycleBars] = { ... }` bool initialiser from the body."""
    body = _phase25_body()
    match = re.search(rf"{name}\s*\[\s*kCycleBars\s*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[kCycleBars] in buildPhase25Fixture")
    return tuple(tok == "true" for tok in re.split(r"[,\s]+", match.group(1)) if tok)


def _parse_block_spec() -> tuple[tuple[int, int, int, bool], ...]:
    """Parse the kBlockSpec[kBlocks] = { {d,m,base,clim}, ... } struct table.

    Each row is `{density_level, notes_per_bar, base_midi, is_climax}`.
    """
    body = _phase25_body()
    match = re.search(
        r"kBlockSpec\s*\[\s*kBlocks\s*\]\s*=\s*\{(.*?)\};", body, re.S
    )
    if match is None:
        raise AssertionError("could not locate kBlockSpec[kBlocks] in buildPhase25Fixture")
    rows = re.findall(r"\{([^{}]*?)\}", match.group(1))
    out: list[tuple[int, int, int, bool]] = []
    for row in rows:
        toks = [t for t in re.split(r"[,\s]+", row) if t]
        if len(toks) != 4:
            raise AssertionError(f"unexpected kBlockSpec row: {row!r}")
        out.append((int(toks[0]), int(toks[1]), int(toks[2]), toks[3] == "true"))
    return tuple(out)


class Phase25CppDriftGuardTest(unittest.TestCase):
    """The Python PHASE25 mirror constants must equal the C++ source."""

    def test_ground_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_int_array("kGroundPitch"),
            rpc.PHASE25_GROUND,
            "PHASE25_GROUND drifted from kGroundPitch[kCycleBars] in buildPhase25Fixture",
        )

    def test_bar_root_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_int_array("kBarRoot"),
            rpc.PHASE25_BAR_ROOT,
            "PHASE25_BAR_ROOT drifted from kBarRoot[kCycleBars] in buildPhase25Fixture",
        )

    def test_bar_minor_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_bool_array("kBarMinor"),
            rpc.PHASE25_BAR_MINOR,
            "PHASE25_BAR_MINOR drifted from kBarMinor[kCycleBars] in buildPhase25Fixture",
        )

    def test_block_spec_matches_cpp(self) -> None:
        cpp = _parse_block_spec()
        self.assertEqual(
            cpp,
            rpc.PHASE25_BLOCK_SPEC,
            "PHASE25_BLOCK_SPEC drifted from kBlockSpec[kBlocks] in buildPhase25Fixture",
        )

    def test_block4_is_the_climax(self) -> None:
        cpp = _parse_block_spec()
        # Exactly block 4 (bars 16-19) carries is_climax.
        self.assertEqual([row[3] for row in cpp], [False, False, False, False, True])

    def test_aria_is_two_notes_per_bar(self) -> None:
        # Block 0 (Aria) is m=2; the four others ramp 4/8/8/16.
        cpp = _parse_block_spec()
        self.assertEqual([row[1] for row in cpp], [2, 4, 8, 8, 16])

    def test_ground_period_matches_cpp(self) -> None:
        body = _phase25_body()
        self.assertIn("constexpr int kCycleBars = 4;", body)
        self.assertIn("constexpr int kBlocks = 5;", body)
        self.assertEqual(rpc.PHASE25_GROUND_PERIOD, 7680)
        # The C++ ground period is kCycleBars * kTicksPerBar = 4 * 1920 = 7680.
        self.assertRegex(
            body,
            r"passacaglia_ground_period\s*=\s*static_cast<Tick>\(kCycleBars\)\s*\*\s*kTicksPerBar",
        )

    def test_constants_are_4_bar_cycle(self) -> None:
        self.assertEqual(len(rpc.PHASE25_GROUND), 4)
        self.assertEqual(len(rpc.PHASE25_BAR_ROOT), 4)
        self.assertEqual(len(rpc.PHASE25_BAR_MINOR), 4)
        self.assertEqual(len(rpc.PHASE25_BLOCK_SPEC), 5)


class Phase25RequiredBitsTest(unittest.TestCase):
    """The Phase25 required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_are_59_60_61(self) -> None:
        # Reduced-scope reuse-only assembly: it reuses the Phase20 Passacaglia bits.
        self.assertEqual(rpc.PHASE25_REQUIRED_BITS, (59, 60, 61))

    def test_matches_provenance_enum(self) -> None:
        prov = PROVENANCE_H.read_text(encoding="utf-8")
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["PassacagliaGroundReplayed"]), 59)
        self.assertEqual(int(names["VariationApplied"]), 60)
        self.assertEqual(int(names["ClimaxPlaced"]), 61)


class Phase25SequenceShapeTest(unittest.TestCase):
    """The Goldberg predictor emits 172 notes across two carrier groups."""

    def test_total_note_count(self) -> None:
        for seed in range(20):
            fixture = {"harm_idx": seed % 4, "subj_idx": (seed // 4) % 5, "seed": seed}
            seqs = rpc.expected_goldberg_sequence(fixture)
            self.assertEqual(
                set(seqs.keys()),
                {(1, "PassacagliaGround"), (0, "PassacagliaVariation")},
            )
            # V1 ground: 4-bar bass tiled 5x = 20 notes.
            self.assertEqual(len(seqs[(1, "PassacagliaGround")]), 20, f"seed {seed} ground")
            # V0 variation: Aria 8 + Var1 16 + Var2 32 + Var3 32 + Var4 64 = 152.
            self.assertEqual(
                len(seqs[(0, "PassacagliaVariation")]), 152, f"seed {seed} variation"
            )
            total = sum(len(v) for v in seqs.values())
            self.assertEqual(total, 172, f"seed {seed} total")

    def test_aria_block_is_two_half_notes_per_bar(self) -> None:
        # Block 0 (bars 0-3, ticks 0..7680) must be two half-notes/bar (m=2):
        # eight notes total, on ticks bar*1920 and bar*1920+960.
        seqs = rpc.expected_goldberg_sequence({"harm_idx": 0, "subj_idx": 0, "seed": 0})
        v0 = seqs[(0, "PassacagliaVariation")]
        aria_ticks = sorted(t for t, _p in v0 if t < 4 * 1920)
        self.assertEqual(
            aria_ticks,
            [bar * 1920 + half for bar in range(4) for half in (0, 960)],
        )

    def test_climax_block_is_sixteenths(self) -> None:
        # Block 4 (bars 16-19, ticks 30720..38400) must be 16 sixteenths/bar = 64.
        seqs = rpc.expected_goldberg_sequence({"harm_idx": 0, "subj_idx": 0, "seed": 0})
        v0 = seqs[(0, "PassacagliaVariation")]
        climax = [t for t, _p in v0 if 16 * 1920 <= t < 20 * 1920]
        self.assertEqual(len(climax), 64)

    def test_ground_is_immutable_across_cycles(self) -> None:
        # Every 4-bar cycle replays the same Goldberg bass pitch sequence.
        seqs = rpc.expected_goldberg_sequence({"harm_idx": 2, "subj_idx": 0, "seed": 2})
        ground = seqs[(1, "PassacagliaGround")]
        for cycle in range(5):
            pitches = [p for _t, p in ground[cycle * 4:(cycle + 1) * 4]]
            self.assertEqual(tuple(pitches), rpc.PHASE25_GROUND, f"cycle {cycle}")


class Phase25ScaleWalkTest(unittest.TestCase):
    """The Phase25 variation figuration reuses the Phase17 C-major scale walk."""

    def test_whole_step_from_c5(self) -> None:
        # C5 (72) + 1 degree -> D5 (74): a whole step (C->D in C major).
        self.assertEqual(rpc.phase17_scale_up(72, 1), 74)

    def test_aria_chord_anchor_is_scale_tone(self) -> None:
        # chord_start for the I/IV/V/vi roots (0/5/7/9) over base C5 (72) is
        # 72/77/79/81, all already C-major scale tones (no snap needed).
        for root_pc, expect in zip((0, 5, 7, 9), (72, 77, 79, 81)):
            chord_start = 72 + ((root_pc - (72 % 12) + 12) % 12)
            self.assertEqual(chord_start, expect)
            self.assertEqual(rpc.phase17_scale_up(chord_start, 0), expect)


if __name__ == "__main__":
    unittest.main()
