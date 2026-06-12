"""Tests for the offline fugue-subject synthesizer."""

from __future__ import annotations

import random
import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import subject_synth as synth  # noqa: E402


def toy_mode_stats() -> dict:
    """Minimal subject_stats.v1 per-mode payload with a step-leaning chain."""
    return {
        "interval_unigram": {"2": 20, "-2": 18, "1": 8, "-1": 8, "4": 4, "-3": 4, "8": 2, "-5": 2},
        "interval_bigram": {
            "2": {"2": 8, "-2": 6, "1": 2},
            "-2": {"-2": 8, "2": 6, "-1": 2},
            "8": {"-2": 4, "-1": 2},
        },
        "interval_trigram": {
            "2,2": {"-2": 4, "2": 2},
            "-2,-2": {"2": 4, "-2": 2},
        },
        "start_degree": {"0": 3, "7": 2},
        "rhythm_initial": {"eighth": 4, "quarter": 3, "sixteenth": 1},
        "rhythm_bigram": {
            "eighth": {"eighth": 6, "quarter": 3},
            "quarter": {"quarter": 4, "eighth": 3, "half_plus": 1},
            "half_plus": {"quarter": 2},
            "sixteenth": {"sixteenth": 3, "eighth": 2},
        },
        "ryden": {
            "range_semitones": {"quantiles": {"p10": 5.0, "p90": 14.0}},
            "unique_pitch_classes": {"quantiles": {"p10": 4.0, "p90": 8.0}},
            "unique_intervals": {"quantiles": {"p10": 2.0, "p90": 6.0}},
            "max_leap": {"quantiles": {"p10": 2.0, "p90": 12.0}},
        },
    }


class PitchSamplerTest(unittest.TestCase):
    def assert_subject_constraints(self, mode: str, pitches: list[int]) -> None:
        spec = synth.MODE_SPECS[mode]
        self.assertEqual(len(pitches), synth.SUBJECT_POSITIONS)
        self.assertEqual(tuple(pitches[-2:]), synth.LEADING_TONE_TAIL)
        for pitch in pitches[:-2]:
            self.assertIn(pitch % 12, spec.pcs)
            self.assertGreaterEqual(pitch, spec.low)
            self.assertLessEqual(pitch, spec.high)
        intervals = synth.intervals_of(pitches[:-1])  # body + tail approach
        for value in intervals:
            self.assertIn(abs(value), synth.INTERVAL_WHITELIST)
        self.assertIn(abs(pitches[1] - pitches[0]), synth.OPENING_INTERVALS)

    def test_major_subjects_respect_all_constraints(self) -> None:
        stats = toy_mode_stats()
        model = synth.IntervalModel(stats)
        rng = random.Random(7)
        for _ in range(50):
            pitches = synth.sample_pitches(rng, synth.MODE_SPECS["major"], model, stats)
            self.assertIsNotNone(pitches)
            self.assert_subject_constraints("major", pitches)

    def test_minor_subjects_avoid_aug_second_into_tail(self) -> None:
        stats = toy_mode_stats()
        model = synth.IntervalModel(stats)
        rng = random.Random(11)
        for _ in range(50):
            pitches = synth.sample_pitches(rng, synth.MODE_SPECS["minor"], model, stats)
            self.assertIsNotNone(pitches)
            self.assert_subject_constraints("minor", pitches)
            self.assertNotEqual(pitches[-3] % 12, synth.AUG_SECOND_PC)

    def test_no_pendulum_oscillation(self) -> None:
        stats = toy_mode_stats()
        model = synth.IntervalModel(stats)
        rng = random.Random(13)
        for _ in range(60):
            pitches = synth.sample_pitches(rng, synth.MODE_SPECS["major"], model, stats)
            for idx in range(4, len(pitches) - 2):
                window = pitches[idx - 4 : idx + 1]
                self.assertFalse(
                    window[0] == window[2] == window[4] and window[1] == window[3],
                    f"x-y-x-y-x oscillation at {idx}: {pitches}",
                )

    def test_extends_oscillation_detector(self) -> None:
        self.assertTrue(synth.extends_oscillation([72, 70, 72, 70], 72))
        self.assertFalse(synth.extends_oscillation([72, 70, 72, 70], 74))
        self.assertFalse(synth.extends_oscillation([72, 70, 72], 70))  # 2 cycles allowed

    def test_no_repeated_note_runs(self) -> None:
        stats = toy_mode_stats()
        model = synth.IntervalModel(stats)
        rng = random.Random(3)
        for _ in range(30):
            pitches = synth.sample_pitches(rng, synth.MODE_SPECS["major"], model, stats)
            for idx in range(2, len(pitches) - 2):
                self.assertFalse(
                    pitches[idx] == pitches[idx - 1] == pitches[idx - 2],
                    f"3-note repeat at {idx}: {pitches}",
                )


class RhythmSamplerTest(unittest.TestCase):
    def test_rhythm_sums_to_four_bars_with_long_final(self) -> None:
        stats = toy_mode_stats()
        rng = random.Random(5)
        for _ in range(100):
            rhythm = synth.sample_rhythm(rng, stats)
            self.assertEqual(len(rhythm), synth.SUBJECT_POSITIONS)
            self.assertEqual(sum(rhythm), synth.SUBJECT_TICKS)
            self.assertIn(rhythm[-1], (960, 1920))
            for ticks in rhythm[:-1]:
                self.assertIn(ticks, synth.DURATION_TICKS.values())


class ModelTest(unittest.TestCase):
    def test_shape_kl_prefers_corpus_vocabulary(self) -> None:
        stats = toy_mode_stats()
        # Reference with real leap mass (the measured corpus subject windows
        # carry ~40% non-step intervals): an all-step candidate must diverge
        # more than a candidate matching that vocabulary.
        stats["interval_unigram"] = {
            "2": 18, "-2": 18, "1": 6, "-1": 6,
            "4": 6, "-4": 6, "5": 5, "-5": 5, "8": 5, "-8": 5,
        }
        model = synth.IntervalModel(stats)
        all_steps = [2, -2, 2, -2, 2, -2, 2, -2, 2, -2, 2, -2, 2, -2, 2]
        mixed = [2, 2, -2, 8, -2, -2, -1, 4, -2, 2, -4, 2, 2, -5, 1]
        self.assertLess(model.shape_kl(mixed), model.shape_kl(all_steps))

    def test_sequence_nll_is_finite_and_orders(self) -> None:
        stats = toy_mode_stats()
        model = synth.IntervalModel(stats)
        idiomatic = [2, 2, -2, -2, 2, 2, -2]
        alien = [8, 8, -1, 8, 8, -1, 8]  # whitelisted values, unseen chains
        self.assertTrue(model.sequence_nll(idiomatic) < model.sequence_nll(alien))

    def test_interval_edit_distance(self) -> None:
        self.assertEqual(synth.interval_edit_distance([2, 2, -2], [2, 2, -2]), 0)
        self.assertEqual(synth.interval_edit_distance([2, 2, -2], [2, -2, -2]), 1)
        self.assertEqual(synth.interval_edit_distance([], [1, 2]), 2)


class MinorHeaderParserTest(unittest.TestCase):
    def test_parses_rows_from_header_text(self) -> None:
        import tempfile

        text = (
            "inline constexpr std::array<std::array<std::uint8_t, 16>, 5> "
            "kSubjectsMinor = {{\n"
            "    // comment\n"
            "    {72, 74, 75, 77},\n"
            "    {80, 79, 77, 75},\n"
            "}};\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "minor_material.h"
            path.write_text(text)
            rows = synth.load_minor_subjects(path)
        self.assertEqual(rows, [[72, 74, 75, 77], [80, 79, 77, 75]])

    def test_real_header_has_five_16_note_rows_with_tail(self) -> None:
        header = SCRIPTS_DIR.parent / "src" / "composer" / "minor_material.h"
        rows = synth.load_minor_subjects(header)
        self.assertEqual(len(rows), 5)
        for row in rows:
            self.assertEqual(len(row), 16)
            self.assertEqual(tuple(row[-2:]), synth.LEADING_TONE_TAIL)


class PipelineTest(unittest.TestCase):
    def test_synthesize_mode_is_deterministic(self) -> None:
        stats = toy_mode_stats()
        header = SCRIPTS_DIR.parent / "src" / "composer" / "minor_material.h"
        kwargs = dict(
            pool_size=120,
            keep=8,
            nll_margin=2.0,  # generous: the toy chain differs from the anchors
            dedup_distance=3,
            minor_header=header,
        )
        first = synth.synthesize_mode(random.Random(42), "major", stats, **kwargs)
        second = synth.synthesize_mode(random.Random(42), "major", stats, **kwargs)
        self.assertEqual(first, second)
        self.assertGreater(len(first["candidates"]), 0)

    def test_kept_candidates_keep_mutual_distance(self) -> None:
        stats = toy_mode_stats()
        header = SCRIPTS_DIR.parent / "src" / "composer" / "minor_material.h"
        result = synth.synthesize_mode(
            random.Random(9),
            "major",
            stats,
            pool_size=200,
            keep=12,
            nll_margin=2.0,
            dedup_distance=3,
            minor_header=header,
        )
        candidates = result["candidates"]
        sequences = [synth.intervals_of(candidate["pitches"]) for candidate in candidates]
        for left in range(len(sequences)):
            for right in range(left + 1, len(sequences)):
                self.assertGreaterEqual(
                    synth.interval_edit_distance(sequences[left], sequences[right]), 3
                )

    def test_candidates_ranked_by_shape_kl(self) -> None:
        stats = toy_mode_stats()
        header = SCRIPTS_DIR.parent / "src" / "composer" / "minor_material.h"
        result = synth.synthesize_mode(
            random.Random(9),
            "major",
            stats,
            pool_size=200,
            keep=12,
            nll_margin=2.0,
            dedup_distance=3,
            minor_header=header,
        )
        kls = [candidate["shape_kl"] for candidate in result["candidates"]]
        self.assertEqual(kls, sorted(kls))


class SequenceSynthesisTest(unittest.TestCase):
    """The sequence-structured (v2) pipeline: coherence by construction."""

    def test_realize_sequence_body_is_strict_diatonic_sequence(self) -> None:
        stats = toy_mode_stats()
        spec = synth.MODE_SPECS["major"]
        ladder = synth.scale_ladder(spec)
        index_of = {pitch: idx for idx, pitch in enumerate(ladder)}
        rng = random.Random(21)
        realized = 0
        for _ in range(400):
            result = synth.realize_sequence_body(rng, spec, stats)
            if result is None:
                continue
            realized += 1
            body, length = result
            self.assertEqual(len(body), 12)
            self.assertIn(length, synth.MOTIF_LENGTHS)
            indices = [index_of[pitch] for pitch in body]
            # Strict sequence: every statement is the motif shifted by one
            # constant ladder offset, so index deltas repeat with period L.
            shifts = {indices[idx + length] - indices[idx] for idx in range(12 - length)}
            self.assertEqual(len(shifts), 1, f"not a strict sequence: {body}")
            self.assertIn(shifts.pop(), [shift for shift, _ in synth.SEQUENCE_SHIFTS])
            # Body leaps stay within the hard cap (head leap excepted).
            for idx, (prev, nxt) in enumerate(zip(body, body[1:])):
                limit = 7 if idx == 0 else synth.MAX_BODY_LEAP_SEMITONES
                self.assertLessEqual(abs(nxt - prev), limit, f"leap at {idx}: {body}")
        self.assertGreater(realized, 0, "no body ever realized")

    def test_realize_approach_is_stepwise_and_lands_near_tail(self) -> None:
        stats = toy_mode_stats()
        spec = synth.MODE_SPECS["major"]
        rng = random.Random(23)
        checked = 0
        for _ in range(200):
            result = synth.realize_sequence_body(rng, spec, stats)
            if result is None:
                continue
            body, _ = result
            approach = synth.realize_approach(rng, spec, body)
            if approach is None:
                continue
            checked += 1
            self.assertEqual(len(approach), synth.APPROACH_POSITIONS)
            p13 = approach[-1]
            self.assertTrue(synth.tail_approach_ok(spec, p13))
            self.assertLessEqual(abs(synth.LEADING_TONE_TAIL[0] - p13), 4)
            # Each approach move is a ladder step or small skip from the body.
            ladder = synth.scale_ladder(spec)
            index_of = {pitch: idx for idx, pitch in enumerate(ladder)}
            walk = [index_of[body[-1]], index_of[approach[0]], index_of[approach[1]]]
            for prev, nxt in zip(walk, walk[1:]):
                self.assertLessEqual(abs(nxt - prev), 2)
                self.assertNotEqual(nxt, prev)
        self.assertGreater(checked, 0, "no approach ever realized")

    def test_sequence_rhythm_repeats_cell_and_solves_budget_exactly(self) -> None:
        stats = toy_mode_stats()
        rng = random.Random(29)
        for length in synth.MOTIF_LENGTHS:
            for _ in range(50):
                rhythm = synth.sequence_rhythm(rng, stats, length)
                self.assertIsNotNone(rhythm)
                self.assertEqual(len(rhythm), synth.SUBJECT_POSITIONS)
                self.assertEqual(sum(rhythm), synth.SUBJECT_TICKS)
                # Positions 0-11 are the motif cell repeated verbatim.
                cell = rhythm[:length]
                statements = 12 // length
                self.assertEqual(rhythm[:12], cell * statements)
                self.assertIn(rhythm[14], synth.LEADING_TONE_DURATIONS)

    def test_coherence_metrics_orders_directed_lines_over_pendulums(self) -> None:
        descending = [79, 77, 76, 74, 72, 71, 69, 67, 65, 64, 62, 60, 59, 57, 71, 72]
        pendulum = [72, 74, 72, 74, 72, 74, 72, 74, 72, 74, 72, 74, 72, 74, 71, 72]
        run_desc = synth.coherence_metrics(descending)["direction_run_mean"]
        run_pend = synth.coherence_metrics(pendulum)["direction_run_mean"]
        self.assertGreater(run_desc, run_pend)
        self.assertGreaterEqual(run_desc, synth.MIN_DIRECTION_RUN_MEAN)
        leapy = [60, 67, 60, 67, 60, 67, 60, 67, 60, 67, 60, 67, 60, 67, 71, 72]
        self.assertEqual(synth.coherence_metrics(leapy)["body_leaps"], 13)
        self.assertEqual(synth.coherence_metrics(descending)["body_leaps"], 0)

    def synthesize(self, seed: int, keep: int = 8) -> dict:
        stats = toy_mode_stats()
        header = SCRIPTS_DIR.parent / "src" / "composer" / "minor_material.h"
        return synth.synthesize_mode_sequence(
            random.Random(seed),
            "major",
            stats,
            pool_size=600,
            keep=keep,
            nll_margin=2.0,
            dedup_distance=3,
            minor_header=header,
        )

    def test_synthesize_mode_sequence_is_deterministic(self) -> None:
        first = self.synthesize(42)
        second = self.synthesize(42)
        self.assertEqual(first, second)
        self.assertEqual(first["style"], "sequence")
        self.assertGreater(len(first["candidates"]), 0)

    def test_synthesize_mode_sequence_candidates_are_coherent(self) -> None:
        result = self.synthesize(9, keep=12)
        spec = synth.MODE_SPECS["major"]
        for candidate in result["candidates"]:
            pitches = candidate["pitches"]
            self.assertEqual(len(pitches), synth.SUBJECT_POSITIONS)
            self.assertEqual(tuple(pitches[-2:]), synth.LEADING_TONE_TAIL)
            for pitch in pitches[:-2]:
                self.assertIn(pitch % 12, spec.pcs)
            self.assertEqual(len(candidate["rhythm_ticks"]), synth.SUBJECT_POSITIONS)
            self.assertEqual(sum(candidate["rhythm_ticks"]), synth.SUBJECT_TICKS)
            coherence = candidate["coherence"]
            self.assertGreaterEqual(
                coherence["direction_run_mean"], synth.MIN_DIRECTION_RUN_MEAN
            )

    def test_synthesize_mode_sequence_ranks_by_coherence_first(self) -> None:
        result = self.synthesize(9, keep=12)
        keys = [
            (
                candidate["coherence"]["body_leaps"],
                -candidate["coherence"]["direction_run_mean"],
                candidate["trigram_nll"],
            )
            for candidate in result["candidates"]
        ]
        # Dedup may drop intermediate entries, but the kept order must still
        # be non-decreasing in the (leaps, -run, nll) ranking key.
        self.assertEqual(keys, sorted(keys))


if __name__ == "__main__":
    unittest.main()
