#!/usr/bin/env python3
"""Offline fugue-subject synthesizer: sample, constrain, rank, deduplicate.

Consumes the subject-window statistics extracted by ``extract-subject-stats``
and produces a candidate-subject pool as an intermediate JSON (default under
backup/, not vendored). All sampling is offline and deterministic
(``random.Random(seed)``); nothing here runs at composition time — the
runtime keeps its deterministic catalog lookup.

Hard constraints mirror the shipped catalogs (figuration.h /
minor_material.h): 16 pitch positions, the 71,72 leading-tone tail (answer
-5 / re-entry -12 / stretto -24 compatibility and the leading-tone
provenance bit), the proven register envelope, diatonic pitch material
(C major / C natural minor with the leading tone reserved for the cadential
tail), adjacent intervals restricted to the verified
{1..5, 7, 8, 12}-semitone set, and a 16-duration rhythm row summing to
exactly four bars with a long final note.

Ranking follows the two-axis split measured on the texture gate: the
subject-domain trigram NLL acts only as a floor (cross-entropy rewards step
concentration, so top-N selection on it would collapse diversity), and the
candidates are ordered by the KL divergence of their interval-category
distribution against the corpus subject windows (the axis that rewards a
corpus-shaped leap vocabulary).
"""

from __future__ import annotations

import argparse
import json
import math
import random
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from bachlib.common import REPO_ROOT
from bachlib.mirror import FUGUE_COMPLETE_SUBJECTS
from bachlib.subject_stats import classify_contour_archetype

SUBJECT_POSITIONS = 16
TICKS_PER_BEAT = 480
SUBJECT_TICKS = 16 * TICKS_PER_BEAT  # four 4/4 bars

LEADING_TONE_TAIL = (71, 72)

# Adjacent-interval whitelist verified note-by-note for the shipped catalogs
# (minor_material.h documents the contract; the major catalog satisfies it).
# Repeats (0) are allowed but never twice in a row.
INTERVAL_WHITELIST = frozenset({0, 1, 2, 3, 4, 5, 7, 8, 12})
OPENING_INTERVALS = frozenset({0, 1, 2, 3, 4, 5, 6, 7, 12})

MAJOR_PCS = frozenset({0, 2, 4, 5, 7, 9, 11})
# C natural minor for the free body; the leading tone (pc 11) appears only in
# the fixed cadential tail, approached from anything but Ab (no aug-2nd).
MINOR_PCS = frozenset({0, 2, 3, 5, 7, 8, 10})
AUG_SECOND_PC = 8

DURATION_TICKS = {
    "sixteenth": 120,
    "eighth": 240,
    "dotted_eighth": 360,
    "quarter": 480,
    "dotted_quarter": 720,
    "half_plus": 960,
}
# Final-note durations and weights follow the shipped rhythm rows (4 of 5 end
# on a whole note, 1 on a half note).
FINAL_DURATIONS = ((1920, 0.8), (960, 0.2))

# Interpolated backoff weights for trigram -> bigram -> unigram -> uniform.
BACKOFF_LAMBDAS = (0.5, 0.3, 0.15, 0.05)

# Degeneracy guards. Two-note oscillations (x y x y x ...) and one dominant
# interval bigram are the measured enemies of the in-context KL axis (top
# bigram concentration), and the category-distribution ranking is blind to
# both, so they are hard constraints rather than ranking terms.
MAX_INTERVAL_BIGRAM_COUNT = 4


INTERVAL_CATEGORIES = (
    "unison",
    "asc_step",
    "desc_step",
    "asc_third",
    "desc_third",
    "asc_fourth_fifth",
    "desc_fourth_fifth",
    "asc_sixth_plus",
    "desc_sixth_plus",
)
CATEGORY_SMOOTHING = 0.5

MAX_SAMPLE_ATTEMPTS = 64


@dataclass
class ModeSpec:
    """Per-mode hard constraints in the internal C-rooted pitch space."""

    name: str
    pcs: frozenset
    low: int
    high: int
    start_pitch_by_degree: dict


MODE_SPECS = {
    "major": ModeSpec(
        name="major",
        pcs=MAJOR_PCS,
        low=71,
        high=81,
        start_pitch_by_degree={0: 72, 2: 74, 4: 76, 7: 79},
    ),
    "minor": ModeSpec(
        name="minor",
        pcs=MINOR_PCS,
        low=70,
        high=82,
        start_pitch_by_degree={0: 72, 3: 75, 7: 79},
    ),
}


@dataclass
class Candidate:
    """One synthesized subject candidate."""

    pitches: list[int]
    rhythm_ticks: list[int]
    trigram_nll: float
    shape_kl: float
    contour: str
    features: dict


def interval_category(value: int) -> str:
    magnitude = abs(value)
    if magnitude == 0:
        return "unison"
    if magnitude <= 2:
        prefix = "asc" if value > 0 else "desc"
        return f"{prefix}_step"
    if magnitude <= 4:
        prefix = "asc" if value > 0 else "desc"
        return f"{prefix}_third"
    if magnitude <= 7:
        prefix = "asc" if value > 0 else "desc"
        return f"{prefix}_fourth_fifth"
    prefix = "asc" if value > 0 else "desc"
    return f"{prefix}_sixth_plus"


def intervals_of(pitches: list[int]) -> list[int]:
    return [pitches[idx] - pitches[idx - 1] for idx in range(1, len(pitches))]


class IntervalModel:
    """Interpolated trigram/bigram/unigram model over the S-1 raw counts."""

    def __init__(self, mode_stats: dict) -> None:
        self.unigram = {int(k): float(v) for k, v in mode_stats["interval_unigram"].items()}
        self.unigram_total = sum(self.unigram.values())
        self.bigram = {
            int(key): {int(k): float(v) for k, v in row.items()}
            for key, row in mode_stats["interval_bigram"].items()
        }
        self.trigram = {}
        for key, row in mode_stats["interval_trigram"].items():
            left, right = key.split(",")
            self.trigram[(int(left), int(right))] = {
                int(k): float(v) for k, v in row.items()
            }
        self.reference_categories = self._category_distribution(self.unigram)

    @staticmethod
    def _category_distribution(unigram: dict) -> dict:
        counts = Counter()
        for value, count in unigram.items():
            counts[interval_category(value)] += count
        total = sum(counts.values()) + CATEGORY_SMOOTHING * len(INTERVAL_CATEGORIES)
        return {
            name: (counts.get(name, 0.0) + CATEGORY_SMOOTHING) / total
            for name in INTERVAL_CATEGORIES
        }

    def probability(self, prev2: int | None, prev1: int | None, value: int) -> float:
        """Interpolated probability of the next interval given up to two predecessors."""
        lambda3, lambda2, lambda1, lambda0 = BACKOFF_LAMBDAS
        prob = lambda0 / len(INTERVAL_WHITELIST)
        uni_total = self.unigram_total
        if uni_total > 0:
            prob += lambda1 * self.unigram.get(value, 0.0) / uni_total
        if prev1 is not None:
            row = self.bigram.get(prev1)
            if row:
                total = sum(row.values())
                prob += lambda2 * row.get(value, 0.0) / total
        if prev2 is not None and prev1 is not None:
            row = self.trigram.get((prev2, prev1))
            if row:
                total = sum(row.values())
                prob += lambda3 * row.get(value, 0.0) / total
        return prob

    def sequence_nll(self, intervals: list[int]) -> float:
        """Mean negative log probability of an interval sequence."""
        total = 0.0
        for idx, value in enumerate(intervals):
            prev1 = intervals[idx - 1] if idx >= 1 else None
            prev2 = intervals[idx - 2] if idx >= 2 else None
            total += -math.log(self.probability(prev2, prev1, value))
        return total / len(intervals)

    def shape_kl(self, intervals: list[int]) -> float:
        """KL of the candidate's interval-category distribution vs the corpus windows."""
        counts = Counter(interval_category(value) for value in intervals)
        total = sum(counts.values()) + CATEGORY_SMOOTHING * len(INTERVAL_CATEGORIES)
        divergence = 0.0
        for name in INTERVAL_CATEGORIES:
            candidate = (counts.get(name, 0.0) + CATEGORY_SMOOTHING) / total
            divergence += candidate * math.log(candidate / self.reference_categories[name])
        return divergence


def extends_oscillation(pitches: list[int], pitch: int) -> bool:
    """True when appending `pitch` makes a third x-y-x-y cycle (x y x y x)."""
    if len(pitches) < 4:
        return False
    return (
        pitch == pitches[-2]
        and pitches[-1] == pitches[-3]
        and pitches[-2] == pitches[-4]
    )


def allowed_next_pitches(
    spec: ModeSpec, pitches: list[int], *, opening: bool, repeat_blocked: bool
) -> list[int]:
    """Enumerate in-envelope diatonic successors honoring the interval whitelist."""
    prev_pitch = pitches[-1]
    out: list[int] = []
    for pitch in range(spec.low, spec.high + 1):
        if pitch % 12 not in spec.pcs:
            continue
        magnitude = abs(pitch - prev_pitch)
        if magnitude not in INTERVAL_WHITELIST:
            continue
        if opening and magnitude not in OPENING_INTERVALS:
            continue
        if magnitude == 0 and repeat_blocked:
            continue
        if extends_oscillation(pitches, pitch):
            continue
        out.append(pitch)
    return out


def tail_approach_ok(spec: ModeSpec, pitch: int) -> bool:
    """Whether `pitch` may precede the fixed 71,72 leading-tone tail."""
    if abs(LEADING_TONE_TAIL[0] - pitch) not in INTERVAL_WHITELIST:
        return False
    if spec.name == "minor" and pitch % 12 == AUG_SECOND_PC:
        return False  # Ab -> B would be the augmented second to avoid.
    return True


def sample_start_pitch(rng: random.Random, spec: ModeSpec, mode_stats: dict) -> int:
    degrees = {int(k): float(v) for k, v in mode_stats["start_degree"].items()}
    choices = [
        (spec.start_pitch_by_degree[degree], weight)
        for degree, weight in degrees.items()
        if degree in spec.start_pitch_by_degree
    ]
    if not choices:
        return spec.start_pitch_by_degree[0]
    pitches, weights = zip(*choices)
    return rng.choices(pitches, weights=weights)[0]


def sample_pitches(
    rng: random.Random, spec: ModeSpec, model: IntervalModel, mode_stats: dict
) -> list[int] | None:
    """Sample one 16-position subject; None when the walk dead-ends."""
    pitches = [sample_start_pitch(rng, spec, mode_stats)]
    free_positions = SUBJECT_POSITIONS - len(LEADING_TONE_TAIL)
    while len(pitches) < free_positions:
        position = len(pitches)
        repeat_blocked = position >= 2 and pitches[-1] == pitches[-2]
        candidates = allowed_next_pitches(
            spec, pitches, opening=(position == 1), repeat_blocked=repeat_blocked
        )
        if position == free_positions - 1:
            candidates = [pitch for pitch in candidates if tail_approach_ok(spec, pitch)]
        if not candidates:
            return None
        prev1 = pitches[-1] - pitches[-2] if len(pitches) >= 2 else None
        prev2 = pitches[-2] - pitches[-3] if len(pitches) >= 3 else None
        weights = [
            model.probability(prev2, prev1, pitch - pitches[-1]) for pitch in candidates
        ]
        pitches.append(rng.choices(candidates, weights=weights)[0])
    pitches.extend(LEADING_TONE_TAIL)
    return pitches


def _reachable_sums(max_count: int) -> list[set]:
    """reachable[k] = set of tick totals composable from exactly k class values."""
    reachable: list[set] = [{0}]
    values = sorted(DURATION_TICKS.values())
    for _ in range(max_count):
        reachable.append({total + value for total in reachable[-1] for value in values})
    return reachable


def sample_rhythm(rng: random.Random, mode_stats: dict) -> list[int]:
    """Sample 16 durations summing to exactly four bars, long final note.

    The duration-class chain follows the S-1 rhythm bigram; at every step the
    choice set is restricted to classes that keep the remaining sum reachable
    (DP feasibility over the min/max tick values), so normalization never
    distorts the sampled classes.
    """
    initial = {k: float(v) for k, v in mode_stats["rhythm_initial"].items()}
    bigram = {
        key: {k: float(v) for k, v in row.items()}
        for key, row in mode_stats["rhythm_bigram"].items()
    }
    final_values, final_weights = zip(*FINAL_DURATIONS)
    final = rng.choices(final_values, weights=final_weights)[0]
    body_positions = SUBJECT_POSITIONS - 1
    budget = SUBJECT_TICKS - final
    reachable = _reachable_sums(body_positions)

    durations: list[int] = []
    previous: str | None = None
    for position in range(body_positions):
        remaining_after = body_positions - position - 1
        feasible: list[str] = []
        weights: list[float] = []
        source = bigram.get(previous, initial) if previous is not None else initial
        for name, ticks in DURATION_TICKS.items():
            if budget - ticks not in reachable[remaining_after]:
                continue
            feasible.append(name)
            weights.append(source.get(name, 0.0) + 0.05)
        if not feasible:
            return []  # unreachable by construction, guarded for safety
        choice = rng.choices(feasible, weights=weights)[0]
        durations.append(DURATION_TICKS[choice])
        budget -= DURATION_TICKS[choice]
        previous = choice
    durations.append(final)
    return durations


def candidate_features(pitches: list[int]) -> dict:
    intervals = intervals_of(pitches)
    return {
        "note_count": len(pitches),
        "range_semitones": max(pitches) - min(pitches),
        "unique_pitch_classes": len({pitch % 12 for pitch in pitches}),
        "opening_interval": intervals[0],
        "unique_intervals": len({abs(value) for value in intervals}),
        "max_leap": max(abs(value) for value in intervals),
    }


def feature_bounds(mode_stats: dict) -> dict:
    """p10..p90 acceptance bands from the S-1 Ryden quantiles (length excluded:
    the catalog format fixes 16 positions)."""
    bounds: dict = {}
    for name in ("range_semitones", "unique_pitch_classes", "unique_intervals", "max_leap"):
        quantile = mode_stats["ryden"][name]["quantiles"]
        if quantile:
            bounds[name] = (quantile["p10"], quantile["p90"])
    return bounds


def within_bounds(features: dict, bounds: dict) -> bool:
    for name, (low, high) in bounds.items():
        if not low <= features[name] <= high:
            return False
    return True


def interval_edit_distance(left: list[int], right: list[int]) -> int:
    """Levenshtein distance between two interval sequences."""
    previous = list(range(len(right) + 1))
    for row, lvalue in enumerate(left, start=1):
        current = [row]
        for col, rvalue in enumerate(right, start=1):
            cost = 0 if lvalue == rvalue else 1
            current.append(
                min(previous[col] + 1, current[col - 1] + 1, previous[col - 1] + cost)
            )
        previous = current
    return previous[-1]


def load_minor_subjects(path: Path) -> list[list[int]]:
    """Parse kSubjectsMinor pitch rows from minor_material.h (read-only)."""
    text = path.read_text(encoding="utf-8")
    match = re.search(r"kSubjectsMinor\s*=\s*\{\{(.*?)\}\};", text, re.DOTALL)
    if not match:
        raise ValueError(f"kSubjectsMinor not found in {path}")
    rows = re.findall(r"\{([0-9,\s]+)\}", match.group(1))
    return [[int(part) for part in row.split(",")] for row in rows]


def existing_subjects(mode: str, minor_header: Path) -> list[list[int]]:
    if mode == "major":
        return [list(row) for row in FUGUE_COMPLETE_SUBJECTS]
    return load_minor_subjects(minor_header)


def synthesize_mode(
    rng: random.Random,
    mode: str,
    mode_stats: dict,
    *,
    pool_size: int,
    keep: int,
    nll_margin: float,
    dedup_distance: int,
    minor_header: Path,
) -> dict:
    """Run the sample -> filter -> rank -> dedup pipeline for one mode."""
    spec = MODE_SPECS[mode]
    model = IntervalModel(mode_stats)
    anchors = existing_subjects(mode, minor_header)
    anchor_intervals = [intervals_of(row) for row in anchors]
    anchor_nll = [model.sequence_nll(seq) for seq in anchor_intervals]
    nll_floor = max(anchor_nll) + nll_margin

    bounds = feature_bounds(mode_stats)
    stage = Counter()
    accepted: list[Candidate] = []
    seen: set[tuple[int, ...]] = set()
    for _ in range(pool_size):
        stage["sampled"] += 1
        pitches = None
        for _ in range(MAX_SAMPLE_ATTEMPTS):
            pitches = sample_pitches(rng, spec, model, mode_stats)
            if pitches is not None:
                break
        if pitches is None:
            stage["dead_end"] += 1
            continue
        key = tuple(pitches)
        if key in seen:
            stage["exact_duplicate"] += 1
            continue
        seen.add(key)
        features = candidate_features(pitches)
        if not within_bounds(features, bounds):
            stage["feature_filtered"] += 1
            continue
        intervals = intervals_of(pitches)
        bigrams = Counter(zip(intervals, intervals[1:]))
        if bigrams and max(bigrams.values()) > MAX_INTERVAL_BIGRAM_COUNT:
            stage["bigram_concentration_cut"] += 1
            continue
        nll = model.sequence_nll(intervals)
        if nll > nll_floor:
            stage["nll_floor_cut"] += 1
            continue
        rhythm = sample_rhythm(rng, mode_stats)
        if len(rhythm) != SUBJECT_POSITIONS or sum(rhythm) != SUBJECT_TICKS:
            stage["rhythm_failed"] += 1
            continue
        accepted.append(
            Candidate(
                pitches=pitches,
                rhythm_ticks=rhythm,
                trigram_nll=nll,
                shape_kl=model.shape_kl(intervals),
                contour=classify_contour_archetype(pitches),
                features=features,
            )
        )
    stage["accepted"] = len(accepted)

    accepted.sort(key=lambda cand: cand.shape_kl)
    kept: list[Candidate] = []
    kept_intervals: list[list[int]] = list(anchor_intervals)
    for candidate in accepted:
        if len(kept) >= keep:
            break
        intervals = intervals_of(candidate.pitches)
        too_close = False
        for other in kept_intervals:
            if interval_edit_distance(intervals, other) < dedup_distance:
                too_close = True
                break
        if too_close:
            stage["dedup_removed"] += 1
            continue
        kept.append(candidate)
        kept_intervals.append(intervals)
    stage["kept"] = len(kept)

    return {
        "spec": {"low": spec.low, "high": spec.high, "tail": list(LEADING_TONE_TAIL)},
        "anchor_nll": anchor_nll,
        "nll_floor": nll_floor,
        "feature_bounds": {name: list(value) for name, value in bounds.items()},
        "stage_counts": dict(stage),
        "candidates": [
            {
                "pitches": candidate.pitches,
                "rhythm_ticks": candidate.rhythm_ticks,
                "trigram_nll": candidate.trigram_nll,
                "shape_kl": candidate.shape_kl,
                "contour": candidate.contour,
                "features": candidate.features,
            }
            for candidate in kept
        ],
    }


def write_pool(document: dict, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=2)
        handle.write("\n")


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--stats",
        type=Path,
        default=REPO_ROOT / "backup" / "subject_stats.json",
        help="subject_stats.v1 JSON produced by extract-subject-stats",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=REPO_ROOT / "backup" / "subject_pool.json",
        help="candidate pool destination (kept out of the repository)",
    )
    parser.add_argument(
        "--minor-header",
        type=Path,
        default=REPO_ROOT / "src" / "composer" / "minor_material.h",
        help="header parsed (read-only) for the minor anchor subjects",
    )
    parser.add_argument("--pool-size", type=int, default=5000)
    parser.add_argument("--keep", type=int, default=200)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument(
        "--nll-margin",
        type=float,
        default=0.15,
        help="floor = worst anchor-subject trigram NLL + this margin",
    )
    parser.add_argument(
        "--dedup-distance",
        type=int,
        default=4,
        help="minimum interval-sequence edit distance between kept candidates",
    )


def register(subparsers) -> None:
    """Register the ``synth-subjects`` subcommand on a subparser action."""
    parser = subparsers.add_parser(
        "synth-subjects",
        help="synthesize a qualified-candidate fugue-subject pool (offline)",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Execute the subject pool synthesis from parsed CLI args."""
    with args.stats.open() as handle:
        stats_doc = json.load(handle)
    if stats_doc.get("schema") != "subject_stats.v1":
        raise SystemExit(f"unexpected stats schema in {args.stats}")

    rng = random.Random(args.seed)
    modes: dict = {}
    for mode in ("major", "minor"):
        modes[mode] = synthesize_mode(
            rng,
            mode,
            stats_doc["stats"][mode],
            pool_size=args.pool_size,
            keep=args.keep,
            nll_margin=args.nll_margin,
            dedup_distance=args.dedup_distance,
            minor_header=args.minor_header,
        )

    document = {
        "schema": "subject_pool.v1",
        "parameters": {
            "pool_size": args.pool_size,
            "keep": args.keep,
            "seed": args.seed,
            "nll_margin": args.nll_margin,
            "dedup_distance": args.dedup_distance,
            "stats": str(args.stats),
        },
        "modes": modes,
    }
    write_pool(document, args.output)

    for mode in ("major", "minor"):
        counts = modes[mode]["stage_counts"]
        kept = modes[mode]["candidates"]
        kls = [candidate["shape_kl"] for candidate in kept]
        contours = Counter(candidate["contour"] for candidate in kept)
        print(
            f"{mode}: sampled={counts.get('sampled', 0)} accepted={counts.get('accepted', 0)} "
            f"kept={counts.get('kept', 0)} (feature_cut={counts.get('feature_filtered', 0)} "
            f"nll_cut={counts.get('nll_floor_cut', 0)} dedup={counts.get('dedup_removed', 0)})"
        )
        if kls:
            print(
                f"{mode}: shape_kl min={min(kls):.4f} max={max(kls):.4f} "
                f"nll_floor={modes[mode]['nll_floor']:.4f} contours={dict(contours)}"
            )
    print(f"output={args.output}")
    minimum_keep = 16
    short = [
        mode
        for mode in ("major", "minor")
        if modes[mode]["stage_counts"].get("kept", 0) < minimum_keep
    ]
    if short:
        print(f"kept below minimum ({minimum_keep}) for: {', '.join(short)}")
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
