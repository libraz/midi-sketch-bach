#!/usr/bin/env python3
"""Extract fugue-subject window statistics for offline subject synthesis.

Two window sources over the sibling corpus checkouts:

1. Algomus WTC I subject labels (clean, exactly-delimited subject windows).
   ODbL/DbCL data, read from the external clone only. Please cite:
   Mathieu Giraud, Richard Groult, Emmanuel Leguy, Florence Leve,
   Computational Fugue Analysis, Computer Music Journal, 39(2), 77-96, 2015.
2. A monophonic-prefix heuristic over the bach-mcp fugue movements
   (wtc1 / wtc2 / organ_pf fugue files plus the organ_fugue category):
   a fugue opens with the subject alone, so the merged note stream stays
   monophonic until the answer enters. Windows cut short by the note or
   beat cap are marked truncated: they still feed the local transition
   statistics (interval Markov, rhythm bigrams) but are excluded from the
   whole-shape statistics (contour archetypes, Ryden features).

The output is an intermediate JSON consumed by the offline subject
synthesizer. It is not vendored into the repository (default destination is
the local backup/ directory) and nothing here feeds the C++ runtime.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from bachlib.common import REPO_ROOT
from bachlib.extract_melodic import TONIC_TO_PC
from bachlib.extract_subject import earliest_subject_label

INTERVAL_CLAMP = 12
# Two notes overlapping by less than this many beats still count as a
# monophonic legato joint rather than the start of polyphony.
OVERLAP_TOLERANCE = 0.06
MAX_WINDOW_NOTES = 40
MAX_WINDOW_BEATS = 16.0
MIN_WINDOW_NOTES = 8

FUGUE_CATEGORIES = {"wtc1", "wtc2", "organ_pf", "organ_fugue"}

# Duration classes for the rhythm transition statistics, in beats.
# Boundaries sit halfway between adjacent canonical values.
DURATION_CLASSES = (
    ("sixteenth", 0.375),
    ("eighth", 0.625),
    ("dotted_eighth", 0.875),
    ("quarter", 1.25),
    ("dotted_quarter", 1.75),
)
DURATION_CLASS_OPEN = "half_plus"

CONTOUR_ARCHETYPES = ("ascent", "descent", "arch", "inverted_arch", "wave", "flat")
# Segment means closer than this many semitones read as level motion.
CONTOUR_LEVEL_TOLERANCE = 0.5

RYDEN_FEATURES = (
    "note_count",
    "range_semitones",
    "unique_pitch_classes",
    "opening_interval",
    "unique_intervals",
    "max_leap",
)
QUANTILE_POINTS = (0.0, 0.10, 0.25, 0.50, 0.75, 0.90, 1.0)


@dataclass
class SubjectWindow:
    """One extracted subject window in corpus pitch space."""

    name: str
    category: str
    mode: str
    source: str  # "algomus" | "heuristic"
    truncated: bool
    tonic_pc: int
    pitches: list[int]
    durations: list[float]


@dataclass
class ModeStats:
    """Accumulated per-mode statistics (raw counts; smoothing is the sampler's job)."""

    windows: int = 0
    clean_windows: int = 0
    interval_count: int = 0
    interval_unigram: Counter = field(default_factory=Counter)
    interval_bigram: dict = field(default_factory=lambda: defaultdict(Counter))
    interval_trigram: dict = field(default_factory=lambda: defaultdict(Counter))
    opening_pair: Counter = field(default_factory=Counter)
    start_degree: Counter = field(default_factory=Counter)
    rhythm_initial: Counter = field(default_factory=Counter)
    rhythm_bigram: dict = field(default_factory=lambda: defaultdict(Counter))
    contour: Counter = field(default_factory=Counter)
    ryden_values: dict = field(default_factory=lambda: {name: [] for name in RYDEN_FEATURES})


def clamp_interval(value: int) -> int:
    return max(-INTERVAL_CLAMP, min(INTERVAL_CLAMP, value))


def duration_class(beats: float) -> str:
    for name, upper in DURATION_CLASSES:
        if beats <= upper:
            return name
    return DURATION_CLASS_OPEN


def monophonic_prefix(
    notes: list[dict],
    *,
    overlap_tolerance: float = OVERLAP_TOLERANCE,
    max_notes: int = MAX_WINDOW_NOTES,
    max_beats: float = MAX_WINDOW_BEATS,
) -> tuple[list[dict], bool]:
    """Scan the merged note stream and return its monophonic opening.

    The prefix ends at the first chord onset or sustained overlap (the answer
    entry in a fugue exposition) or at the note/beat cap, whichever comes
    first. Returns the prefix and whether the cap (not polyphony) ended it.

    @param notes Note dicts with onset/duration/pitch (any track order).
    @param overlap_tolerance Overlap in beats still treated as legato.
    @param max_notes Hard cap on prefix length.
    @param max_beats Hard cap on prefix span from its first onset.
    @return (prefix_notes, truncated_by_cap).
    """
    ordered = sorted(notes, key=lambda note: (float(note["onset"]), int(note["pitch"])))
    prefix: list[dict] = []
    max_offset: float | None = None
    first_onset: float | None = None
    for idx, note in enumerate(ordered):
        onset = float(note["onset"])
        duration = float(note["duration"])
        next_is_chord = (
            idx + 1 < len(ordered) and abs(float(ordered[idx + 1]["onset"]) - onset) < 1e-9
        )
        if next_is_chord:
            return prefix, False  # chord onset: polyphony begins.
        if max_offset is not None and onset < max_offset - overlap_tolerance:
            return prefix, False  # sustained overlap: a second voice entered.
        if first_onset is None:
            first_onset = onset
        if len(prefix) >= max_notes or onset - first_onset >= max_beats:
            return prefix, True
        prefix.append(note)
        offset = onset + duration
        max_offset = offset if max_offset is None else max(max_offset, offset)
    return prefix, False


def classify_contour_archetype(pitches: list[int]) -> str:
    """Classify a melody into an archetype via its 4-segment mean contour."""
    if len(pitches) < 4:
        return "flat"
    quarter = len(pitches) / 4.0
    means: list[float] = []
    for seg in range(4):
        lo = int(round(seg * quarter))
        hi = max(lo + 1, int(round((seg + 1) * quarter)))
        chunk = pitches[lo:hi]
        means.append(sum(chunk) / len(chunk))
    signs: list[int] = []
    for idx in range(1, 4):
        delta = means[idx] - means[idx - 1]
        if abs(delta) <= CONTOUR_LEVEL_TOLERANCE:
            signs.append(0)
        else:
            signs.append(1 if delta > 0 else -1)
    nonzero = [sign for sign in signs if sign != 0]
    if not nonzero:
        return "flat"
    changes = sum(
        1 for idx in range(1, len(nonzero)) if nonzero[idx] != nonzero[idx - 1]
    )
    if changes == 0:
        return "ascent" if nonzero[0] > 0 else "descent"
    if changes == 1:
        return "arch" if nonzero[0] > 0 else "inverted_arch"
    return "wave"


def ryden_features(pitches: list[int]) -> dict[str, int]:
    """Compute the Ryden 6-feature vector (opening interval kept signed)."""
    intervals = [pitches[idx] - pitches[idx - 1] for idx in range(1, len(pitches))]
    return {
        "note_count": len(pitches),
        "range_semitones": max(pitches) - min(pitches),
        "unique_pitch_classes": len({pitch % 12 for pitch in pitches}),
        "opening_interval": intervals[0] if intervals else 0,
        "unique_intervals": len({abs(value) for value in intervals}),
        "max_leap": max((abs(value) for value in intervals), default=0),
    }


def quantiles(values: list[float]) -> dict[str, float]:
    """Linear-interpolation quantiles at QUANTILE_POINTS for a value list."""
    ordered = sorted(values)
    out: dict[str, float] = {}
    for point in QUANTILE_POINTS:
        position = point * (len(ordered) - 1)
        lower = int(position)
        upper = min(lower + 1, len(ordered) - 1)
        fraction = position - lower
        value = ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction
        out[f"p{int(round(point * 100)):02d}"] = value
    return out


def add_window(stats: ModeStats, window: SubjectWindow) -> None:
    """Accumulate one window into the per-mode statistics."""
    stats.windows += 1
    stats.start_degree[(window.pitches[0] - window.tonic_pc) % 12] += 1

    intervals = [
        clamp_interval(window.pitches[idx] - window.pitches[idx - 1])
        for idx in range(1, len(window.pitches))
    ]
    stats.interval_count += len(intervals)
    for value in intervals:
        stats.interval_unigram[value] += 1
    for idx in range(1, len(intervals)):
        stats.interval_bigram[intervals[idx - 1]][intervals[idx]] += 1
    for idx in range(2, len(intervals)):
        stats.interval_trigram[(intervals[idx - 2], intervals[idx - 1])][intervals[idx]] += 1
    if len(intervals) >= 2:
        stats.opening_pair[(intervals[0], intervals[1])] += 1

    classes = [duration_class(value) for value in window.durations]
    stats.rhythm_initial[classes[0]] += 1
    for idx in range(1, len(classes)):
        stats.rhythm_bigram[classes[idx - 1]][classes[idx]] += 1

    if not window.truncated:
        stats.clean_windows += 1
        stats.contour[classify_contour_archetype(window.pitches)] += 1
        for name, value in ryden_features(window.pitches).items():
            stats.ryden_values[name].append(value)


def build_stats(windows: list[SubjectWindow]) -> dict[str, ModeStats]:
    """Build per-mode statistics from extracted subject windows."""
    stats = {"major": ModeStats(), "minor": ModeStats()}
    for window in windows:
        add_window(stats[window.mode], window)
    return stats


def sanity_report(stats: dict[str, ModeStats]) -> dict[str, float]:
    """Corpus-shape sanity: stepwise motion dominates, openings stay singable.

    step_share counts |interval| in {1,2}; the openings check counts
    |opening interval| in [0,7] or exactly 12 (the Ryden/Algomus majority
    range used as a hard synthesis constraint downstream).
    """
    unigram: Counter = Counter()
    openings: list[int] = []
    for mode_stats in stats.values():
        unigram.update(mode_stats.interval_unigram)
        openings.extend(
            abs(value)
            for value in mode_stats.ryden_values["opening_interval"]
        )
    total = sum(unigram.values())
    step = sum(count for value, count in unigram.items() if abs(value) in (1, 2))
    third = sum(count for value, count in unigram.items() if abs(value) in (3, 4))
    fourth_fifth = sum(count for value, count in unigram.items() if abs(value) in (5, 6, 7))
    sixth_plus = sum(count for value, count in unigram.items() if abs(value) >= 8)
    opening_ok = sum(1 for value in openings if value <= 7 or value == 12)
    return {
        "interval_total": float(total),
        "step_share": step / total if total else 0.0,
        "third_share": third / total if total else 0.0,
        "fourth_fifth_share": fourth_fifth / total if total else 0.0,
        "sixth_plus_share": sixth_plus / total if total else 0.0,
        "step_is_largest": 1.0 if step >= max(third, fourth_fifth, sixth_plus) else 0.0,
        "opening_in_range_share": opening_ok / len(openings) if openings else 0.0,
        "opening_majority_in_range": 1.0 if openings and opening_ok * 2 > len(openings) else 0.0,
    }


def notes_in_window(doc: dict, start: float, end: float) -> list[dict]:
    """Collect notes from all tracks with onset in [start, end)."""
    out: list[dict] = []
    for track in doc.get("tracks", []):
        for note in track.get("notes", []):
            if start <= float(note["onset"]) < end:
                out.append(note)
    return out


def merged_notes(doc: dict) -> list[dict]:
    return [note for track in doc.get("tracks", []) for note in track.get("notes", [])]


def window_from_notes(
    name: str,
    doc: dict,
    notes: list[dict],
    truncated: bool,
    source: str,
) -> SubjectWindow | None:
    if len(notes) < MIN_WINDOW_NOTES:
        return None
    mode = str(doc.get("mode", "")).lower()
    if mode not in ("major", "minor"):
        return None
    ordered = sorted(notes, key=lambda note: (float(note["onset"]), int(note["pitch"])))
    return SubjectWindow(
        name=name,
        category=str(doc.get("category", "")),
        mode=mode,
        source=source,
        truncated=truncated,
        tonic_pc=TONIC_TO_PC[str(doc.get("tonic", "C"))],
        pitches=[int(note["pitch"]) for note in ordered],
        durations=[float(note["duration"]) for note in ordered],
    )


def iter_subject_windows(corpus_dir: Path, algomus_dir: Path) -> list[SubjectWindow]:
    """Extract subject windows: Algomus-labelled WTC I first, heuristic for the rest."""
    windows: list[SubjectWindow] = []
    labelled_bwv: set[int] = set()

    label_dir = algomus_dir / "fugues" / "bach-wtc-i"
    for dez_path in sorted(label_dir.glob("*-bwv*-ref.dez")):
        match = re.search(r"bwv(\d+)-ref\.dez$", dez_path.name)
        if not match:
            continue
        bwv = int(match.group(1))
        label = earliest_subject_label(dez_path)
        reference = corpus_dir / f"BWV{bwv}_fugue.json"
        if label is None or not reference.exists():
            continue
        with reference.open() as handle:
            doc = json.load(handle)
        start, duration = label
        notes = notes_in_window(doc, start, start + duration)
        # The label delimits one subject statement, but guard against stray
        # accompaniment notes with the same monophonic scan.
        prefix, truncated = monophonic_prefix(notes)
        window = window_from_notes(reference.name, doc, prefix, truncated, "algomus")
        if window is not None:
            windows.append(window)
            labelled_bwv.add(bwv)

    for path in sorted(corpus_dir.glob("*.json")):
        with path.open() as handle:
            doc = json.load(handle)
        category = str(doc.get("category", ""))
        if category not in FUGUE_CATEGORIES:
            continue
        if category != "organ_fugue" and not path.name.endswith("_fugue.json"):
            continue
        match = re.search(r"BWV(\d+)", path.name)
        if match and int(match.group(1)) in labelled_bwv:
            continue
        prefix, truncated = monophonic_prefix(merged_notes(doc))
        window = window_from_notes(path.name, doc, prefix, truncated, "heuristic")
        if window is not None:
            windows.append(window)
    return windows


def _counter_to_json(counter: Counter) -> dict[str, float]:
    return {str(key): value for key, value in sorted(counter.items(), key=lambda kv: str(kv[0]))}


def _pair_counter_to_json(table: dict) -> dict[str, dict[str, float]]:
    out: dict[str, dict[str, float]] = {}
    for key, counter in table.items():
        name = ",".join(str(part) for part in key) if isinstance(key, tuple) else str(key)
        out[name] = _counter_to_json(counter)
    return dict(sorted(out.items()))


def stats_to_json(stats: dict[str, ModeStats]) -> dict:
    out: dict = {}
    for mode, mode_stats in stats.items():
        ryden: dict = {}
        for name in RYDEN_FEATURES:
            values = mode_stats.ryden_values[name]
            ryden[name] = {
                "histogram": _counter_to_json(Counter(values)),
                "quantiles": quantiles([float(v) for v in values]) if values else {},
            }
        out[mode] = {
            "windows": mode_stats.windows,
            "clean_windows": mode_stats.clean_windows,
            "interval_count": mode_stats.interval_count,
            "interval_unigram": _counter_to_json(mode_stats.interval_unigram),
            "interval_bigram": _pair_counter_to_json(mode_stats.interval_bigram),
            "interval_trigram": _pair_counter_to_json(mode_stats.interval_trigram),
            "opening_pair": {
                ",".join(str(part) for part in key): count
                for key, count in sorted(mode_stats.opening_pair.items())
            },
            "start_degree": _counter_to_json(mode_stats.start_degree),
            "rhythm_initial": _counter_to_json(mode_stats.rhythm_initial),
            "rhythm_bigram": _pair_counter_to_json(mode_stats.rhythm_bigram),
            "contour": _counter_to_json(mode_stats.contour),
            "ryden": ryden,
        }
    return out


def write_json(
    stats: dict[str, ModeStats],
    windows: list[SubjectWindow],
    sanity: dict[str, float],
    output: Path,
    *,
    source: dict[str, str],
) -> None:
    """Write the statistics document (parent directories are created)."""
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": "subject_stats.v1",
        "parameters": {
            "interval_clamp": INTERVAL_CLAMP,
            "overlap_tolerance": OVERLAP_TOLERANCE,
            "max_window_notes": MAX_WINDOW_NOTES,
            "max_window_beats": MAX_WINDOW_BEATS,
            "min_window_notes": MIN_WINDOW_NOTES,
        },
        "source": source,
        "sanity": sanity,
        "stats": stats_to_json(stats),
        "windows": [
            {
                "name": window.name,
                "category": window.category,
                "mode": window.mode,
                "source": window.source,
                "truncated": window.truncated,
                "tonic_pc": window.tonic_pc,
                "pitches": window.pitches,
                "durations": window.durations,
            }
            for window in windows
        ],
    }
    with output.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)
        handle.write("\n")


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--algomus-dir",
        type=Path,
        default=REPO_ROOT.parent / "algomus-data",
    )
    parser.add_argument(
        "--corpus-dir",
        type=Path,
        default=REPO_ROOT.parent / "bach-mcp" / "data" / "reference",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=REPO_ROOT / "backup" / "subject_stats.json",
        help="intermediate JSON destination (kept out of the repository)",
    )


def register(subparsers) -> None:
    """Register the ``extract-subject-stats`` subcommand on a subparser action."""
    parser = subparsers.add_parser(
        "extract-subject-stats",
        help="extract fugue-subject window statistics for offline subject synthesis",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Execute the subject statistics extraction from parsed CLI args."""
    windows = iter_subject_windows(args.corpus_dir, args.algomus_dir)
    if not windows:
        raise SystemExit("no subject windows extracted")
    stats = build_stats(windows)
    sanity = sanity_report(stats)
    source = {
        "algomus_dir": str(args.algomus_dir),
        "corpus_dir": str(args.corpus_dir),
    }
    write_json(stats, windows, sanity, args.output, source=source)
    for mode in ("major", "minor"):
        mode_stats = stats[mode]
        print(
            f"{mode}: windows={mode_stats.windows} clean={mode_stats.clean_windows} "
            f"intervals={mode_stats.interval_count}"
        )
    print(f"step_share={sanity['step_share']:.6f}")
    print(f"opening_in_range_share={sanity['opening_in_range_share']:.6f}")
    print(f"sanity_step_is_largest={'pass' if sanity['step_is_largest'] else 'fail'}")
    print(
        "sanity_opening_majority_in_range="
        f"{'pass' if sanity['opening_majority_in_range'] else 'fail'}"
    )
    print(f"output={args.output}")
    if not sanity["step_is_largest"] or not sanity["opening_majority_in_range"]:
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
