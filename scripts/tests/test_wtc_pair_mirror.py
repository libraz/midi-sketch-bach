"""WTC Prelude+Fugue pair Phase24 C++/Python drift guard.

The load-bearing guard: the bachlib.mirror PHASE24 mirror constants
(PHASE24_BAR_ROOT, PHASE24_BAR_MINOR, the PHASE24_PRELUDE_SECTIONS section table
-- including is_pedal_prep on the SECOND V0 section -- and the
PHASE24_FUGUE_ENTRIES add_subject window / transposition scheme, leader bar 20)
must match the C++ Phase24 fixture source (the kBarRoot[4] / kBarMinor[4]
initialisers, the appendFigurationBar calls, and the add_subject / answer loops
in buildPhase24Fixture, src/composer/harness_fixture.cpp), and the two reused
prelude RuleBit numbers (FigurationCommitted=52, PedalPreparation=54) must match
provenance.h. A renamed constant or an altered progression / section / subject
scheme that the Python predictor still claims would make structural_ok diverge
from the CLI output; this drift guard fails instead. The Phase24 fixture is a
reuse-only assembly (no new VoiceIntent / RuleBit / validator rule), so the bit
guard asserts the two numbers stay pinned to their existing provenance.h values.
The fugue carriers have NO identity bit, so the fugue half is asserted
structurally; the predictor must reproduce all 256 notes byte-for-byte, including
the dual-window V0 SubjectCarrier merge (bars 8-11 + the bar-20 stretto leader).
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


def _phase24_body() -> str:
    """Return the source body following the buildPhase24Fixture declaration.

    Earlier builders (Phase17..23) also declare kBarRoot / kBarMinor style
    arrays; anchoring on the Phase24 builder body ensures only the Phase24
    arrays / sections / subject loops are captured.
    """
    src = FIXTURE_CPP.read_text(encoding="utf-8")
    body = src.split("buildPhase24Fixture", 1)
    if len(body) < 2:
        raise AssertionError("could not locate buildPhase24Fixture in harness_fixture.cpp")
    # Stop at the next builder / phaseSpec so trailing functions are excluded.
    return body[1].split("}  // namespace", 1)[0]


def _parse_int_array(name: str) -> tuple[int, ...]:
    """Parse a `<name>[N] = { ... }` integer initialiser from the Phase24 body."""
    body = _phase24_body()
    match = re.search(rf"{name}\s*\[[^\]]*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[...] in buildPhase24Fixture")
    return tuple(int(tok) for tok in re.split(r"[,\s]+", match.group(1)) if tok)


def _parse_bool_array(name: str) -> tuple[bool, ...]:
    """Parse a `<name>[N] = { ... }` bool initialiser from the Phase24 body."""
    body = _phase24_body()
    match = re.search(rf"{name}\s*\[[^\]]*\]\s*=\s*\{{([^{{}}]*?)\}}", body, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name}[...] in buildPhase24Fixture")
    return tuple(tok == "true" for tok in re.split(r"[,\s]+", match.group(1)) if tok)


class Phase24CppDriftGuardTest(unittest.TestCase):
    """The Python PHASE24 mirror constants must equal the C++ source."""

    def test_bar_root_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_int_array("kBarRoot"),
            rpc.PHASE24_BAR_ROOT,
            "PHASE24_BAR_ROOT drifted from kBarRoot[4] in buildPhase24Fixture",
        )

    def test_bar_minor_matches_cpp(self) -> None:
        self.assertEqual(
            _parse_bool_array("kBarMinor"),
            rpc.PHASE24_BAR_MINOR,
            "PHASE24_BAR_MINOR drifted from kBarMinor[4] in buildPhase24Fixture",
        )

    def test_layout_bars_match_cpp(self) -> None:
        body = _phase24_body()
        self.assertIn("constexpr int kBars = 24;", body)
        self.assertIn("constexpr int kPreludeBars = 8;", body)
        self.assertIn("constexpr int kCycleBars = 4;", body)

    def test_prelude_sections_match_cpp(self) -> None:
        """The appendFigurationBar calls drive base_midi / notes_per_beat, and the
        section windows + is_pedal_prep flag drive the merge / bit-54 mask."""
        body = _phase24_body()
        # V0 sec0 bars 0-3, base 60, npb 4, NOT pedal-prep.
        self.assertRegex(
            body,
            r"sec0\.start_tick\s*=\s*bar_tick\(0\);",
        )
        self.assertRegex(body, r"sec0\.end_tick\s*=\s*bar_tick\(4\);")
        self.assertNotRegex(body, r"sec0\.is_pedal_prep")
        # V0 sec1 bars 4-7, base 60, npb 4, IS pedal-prep.
        self.assertRegex(body, r"sec1\.start_tick\s*=\s*bar_tick\(4\);")
        self.assertRegex(body, r"sec1\.end_tick\s*=\s*bar_tick\(kPreludeBars\);")
        self.assertRegex(body, r"sec1\.is_pedal_prep\s*=\s*true;")
        # V1 bass bars 0-7, base 55, npb 2.
        self.assertRegex(body, r"bass\.voice\s*=\s*1;")
        # Section append calls (base_midi / notes_per_beat).
        self.assertRegex(
            body, r"appendFigurationBar\(sec0\.notes, bar, /\*base_midi=\*/60, /\*notes_per_beat=\*/4\)"
        )
        self.assertRegex(
            body, r"appendFigurationBar\(sec1\.notes, bar, /\*base_midi=\*/60, /\*notes_per_beat=\*/4\)"
        )
        self.assertRegex(
            body, r"appendFigurationBar\(bass\.notes, bar, /\*base_midi=\*/55, /\*notes_per_beat=\*/2\)"
        )
        # Mirror table agrees with the parsed C++ section layout.
        self.assertEqual(
            rpc.PHASE24_PRELUDE_SECTIONS,
            (
                (0, 0, 3, 60, 4, False),
                (0, 4, 7, 60, 4, True),
                (1, 0, 7, 55, 2, False),
            ),
        )

    def test_fugue_entries_match_cpp(self) -> None:
        """add_subject windows / transpositions + the answer (-5) loop + leader."""
        body = _phase24_body()
        # V0 subject bars 8-11, +0.
        self.assertRegex(body, r"add_subject\(/\*first_bar=\*/8, /\*semis=\*/0\)")
        # V2 re-entry bars 16-19, -12.
        self.assertRegex(body, r"add_subject\(/\*first_bar=\*/16, /\*semis=\*/-12\)")
        # V0 stretto leader bars 20-23, +0 (leader bar 20).
        self.assertRegex(body, r"add_subject\(/\*first_bar=\*/20, /\*semis=\*/0\)")
        # V1 answer: subject - 5 over bars 12-15.
        self.assertRegex(body, r"const int bar = 12 \+ n / 4;")
        self.assertRegex(body, r"subj_pat\[n\] - 5")
        # add_subject pitch is subj_pat[n] + semis.
        self.assertRegex(body, r"subj_pat\[n\] \+ semis")
        # subj_a / offset derivation.
        self.assertIn("const int offset = seed % 4;", body)
        self.assertIn("const int subj_a = (seed / 4) % 5;", body)
        self.assertIn("kPhase14Subjects[subj_a]", body)
        # Span intents.
        self.assertRegex(body, r"push_subj_span\(0, 8, 11, VoiceIntent::SubjectCarrier\)")
        self.assertRegex(body, r"push_subj_span\(1, 12, 15, VoiceIntent::AnswerCarrier\)")
        self.assertRegex(body, r"push_subj_span\(2, 16, 19, VoiceIntent::SubjectCarrier\)")
        self.assertRegex(body, r"push_subj_span\(0, 20, 23, VoiceIntent::SubjectCarrier\)")
        # Mirror table agrees.
        self.assertEqual(
            rpc.PHASE24_FUGUE_ENTRIES,
            (
                (0, 8, "SubjectCarrier", 0),
                (1, 12, "AnswerCarrier", -5),
                (2, 16, "SubjectCarrier", -12),
                (0, 20, "SubjectCarrier", 0),
            ),
        )

    def test_subject_catalog_is_phase14(self) -> None:
        # Phase24 reuses the kPhase14Subjects catalog; slot 0 sanity.
        self.assertEqual(
            rpc.PHASE14_SUBJECTS[0],
            (72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72),
        )


class Phase24RequiredBitsTest(unittest.TestCase):
    """The Phase24 required-bit set must track provenance.h RuleBit values."""

    def test_required_bits_set(self) -> None:
        # Reuse-only assembly: FigurationCommitted (52) + PedalPreparation (54).
        # The fugue carriers have NO identity bit (asserted structurally).
        self.assertEqual(rpc.PHASE24_REQUIRED_BITS, (52, 54))

    def test_matches_provenance_enum(self) -> None:
        prov = PROVENANCE_H.read_text(encoding="utf-8")
        enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
        names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
        self.assertEqual(int(names["FigurationCommitted"]), 52)
        self.assertEqual(int(names["PedalPreparation"]), 54)


class Phase24SequenceShapeTest(unittest.TestCase):
    """The WTC-pair predictor emits 256 notes across five carrier groups."""

    def test_total_note_count(self) -> None:
        for seed in range(20):
            fixture = {"harm_idx": seed % 4, "subj_idx": (seed // 4) % 5, "seed": seed}
            seqs = rpc.expected_wtc_pair_sequence(fixture)
            self.assertEqual(
                set(seqs.keys()),
                {
                    (0, "FigurationCarrier"),
                    (1, "FigurationCarrier"),
                    (0, "SubjectCarrier"),
                    (1, "AnswerCarrier"),
                    (2, "SubjectCarrier"),
                },
            )
            # Prelude: V0 two sections merged = 64 + 64 = 128; V1 bass = 8*8 = 64.
            self.assertEqual(len(seqs[(0, "FigurationCarrier")]), 128, f"seed {seed} V0 prelude")
            self.assertEqual(len(seqs[(1, "FigurationCarrier")]), 64, f"seed {seed} V1 bass")
            # Fugue: V0 subject (bars 8-11 + 20-23) merged = 32; answer 16; V2 16.
            self.assertEqual(len(seqs[(0, "SubjectCarrier")]), 32, f"seed {seed} V0 subject")
            self.assertEqual(len(seqs[(1, "AnswerCarrier")]), 16, f"seed {seed} answer")
            self.assertEqual(len(seqs[(2, "SubjectCarrier")]), 16, f"seed {seed} V2 re-entry")
            total = sum(len(v) for v in seqs.values())
            self.assertEqual(total, 256, f"seed {seed} total")

    def test_v0_subject_dual_window(self) -> None:
        # The V0 SubjectCarrier group must span BOTH bars 8-11 (tick 15360..) and
        # the bar-20 stretto leader (tick 38400..); the first and last ticks bound
        # the two windows.
        seqs = rpc.expected_wtc_pair_sequence({"harm_idx": 0, "subj_idx": 0, "seed": 0})
        v0_subj = seqs[(0, "SubjectCarrier")]
        ticks = [t for t, _p in v0_subj]
        self.assertEqual(min(ticks), 8 * 1920)
        self.assertEqual(max(ticks), 23 * 1920 + 3 * 480)
        # The two windows are identical pitch content (both verbatim subject).
        first_window = [p for t, p in v0_subj if t < 16 * 1920]
        second_window = [p for t, p in v0_subj if t >= 20 * 1920]
        self.assertEqual(first_window, second_window)

    def test_answer_is_minus_five(self) -> None:
        # The real answer is the subject transposed down a perfect fourth.
        seqs = rpc.expected_wtc_pair_sequence({"harm_idx": 0, "subj_idx": 0, "seed": 0})
        subj = [p for _t, p in seqs[(0, "SubjectCarrier")] if _t < 16 * 1920]
        ans = [p for _t, p in seqs[(1, "AnswerCarrier")]]
        self.assertEqual(ans, [p - 5 for p in subj])

    def test_prelude_per_beat_anchor_repeats(self) -> None:
        # Every beat of a prelude bar restarts from the same anchor, so the first
        # note of each beat in a bar is identical (the 4-fold up-run sawtooth).
        seqs = rpc.expected_wtc_pair_sequence({"harm_idx": 1, "subj_idx": 0, "seed": 1})
        v0 = seqs[(0, "FigurationCarrier")]
        # Bar 0 spans tick 0..1920; the four beat-start notes (ticks 0/480/960/1440).
        beat_starts = {t: p for t, p in v0 if t < 1920 and t % 480 == 0}
        self.assertEqual(len(beat_starts), 4)
        self.assertEqual(len(set(beat_starts.values())), 1, "beat starts share one anchor")


if __name__ == "__main__":
    unittest.main()
