#!/usr/bin/env python3
"""Extract corpus melodic probability tables from bach-mcp reference JSON."""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import date
from pathlib import Path
from typing import Iterable

TONIC_TO_PC = {
    "C": 0,
    "B#": 0,
    "C#": 1,
    "Db": 1,
    "D": 2,
    "D#": 3,
    "Eb": 3,
    "E": 4,
    "Fb": 4,
    "E#": 5,
    "F": 5,
    "F#": 6,
    "Gb": 6,
    "G": 7,
    "G#": 8,
    "Ab": 8,
    "A": 9,
    "A#": 10,
    "Bb": 10,
    "B": 11,
    "Cb": 11,
}

MODE_INDEX = {"major": 0, "minor": 1}
INTERVAL_MIN = -12
INTERVAL_MAX = 12
INTERVAL_SIZE = 25
SMOOTHING = 0.5


@dataclass
class MelodySequence:
    pitches: list[int]
    mode: str
    category: str


@dataclass
class ExtractedTables:
    scale_degree_logp: list[list[float]]
    interval_markov_logp: list[list[float]]
    gaussian_fit: dict[str, tuple[float, float]]
    works_count: int
    sanity: dict[str, float]


@dataclass
class Accumulators:
    scale_counts: list[list[float]] = field(
        default_factory=lambda: [[SMOOTHING for _ in range(12)] for _ in range(2)]
    )
    markov_counts: list[list[float]] = field(
        default_factory=lambda: [
            [SMOOTHING for _ in range(INTERVAL_SIZE)] for _ in range(INTERVAL_SIZE)
        ]
    )
    interval_unigram: dict[int, float] = field(
        default_factory=lambda: defaultdict(lambda: SMOOTHING)
    )
    gaussian_samples: dict[str, list[tuple[float, float]]] = field(
        default_factory=lambda: defaultdict(list)
    )


def tonic_pc(name: str) -> int:
    try:
        return TONIC_TO_PC[name]
    except KeyError as exc:
        raise ValueError(f"unsupported tonic: {name!r}") from exc


def clamp_interval(value: int) -> int:
    return max(INTERVAL_MIN, min(INTERVAL_MAX, value))


def interval_index(value: int) -> int:
    return clamp_interval(value) - INTERVAL_MIN


def category_bucket(category: str) -> str:
    if "chorale" in category:
        return "chorale"
    if category.startswith("solo_") or "solo_string" in category:
        return "solo_string"
    if "organ" in category or category in {"orgelbuchlein", "trio_sonata"}:
        return "organ"
    return "organ"


def melody_from_track(notes: list[dict], *, solo_string: bool) -> list[int]:
    if not notes:
        return []
    if solo_string:
        ordered = sorted(notes, key=lambda n: (float(n["onset"]), int(n["pitch"])))
        return [int(n["pitch"]) for n in ordered]

    by_onset: dict[float, int] = {}
    for note in notes:
        onset = float(note["onset"])
        pitch = int(note["pitch"])
        by_onset[onset] = max(pitch, by_onset.get(onset, pitch))
    return [by_onset[onset] for onset in sorted(by_onset)]


def iter_sequences(corpus_dir: Path, categories: set[str] | None = None) -> Iterable[MelodySequence]:
    for path in sorted(corpus_dir.glob("*.json")):
        with path.open() as handle:
            doc = json.load(handle)
        category = str(doc.get("category", ""))
        if categories and category not in categories:
            continue
        mode = str(doc.get("mode", "")).lower()
        if mode not in MODE_INDEX:
            continue
        tonic = tonic_pc(str(doc.get("tonic", "C")))
        transpose = -tonic
        solo_string = str(doc.get("track_type", "")) == "solo_string"
        for track in doc.get("tracks", []):
            pitches = melody_from_track(track.get("notes", []), solo_string=solo_string)
            if len(pitches) < 2:
                continue
            transposed = [pitch + transpose for pitch in pitches]
            yield MelodySequence(transposed, mode, category)


def add_sequence(acc: Accumulators, seq: MelodySequence) -> None:
    mode_idx = MODE_INDEX[seq.mode]
    bucket = category_bucket(seq.category)
    for pitch in seq.pitches:
        acc.scale_counts[mode_idx][pitch % 12] += 1.0

    intervals: list[int] = []
    for i in range(1, len(seq.pitches)):
        current = clamp_interval(seq.pitches[i] - seq.pitches[i - 1])
        intervals.append(current)
        acc.interval_unigram[current] += 1.0
        delta_prev = float(seq.pitches[i] - seq.pitches[i - 1])
        run_start = max(0, i - 8)
        run_mean = sum(seq.pitches[run_start:i]) / float(i - run_start)
        delta_range = float(seq.pitches[i]) - run_mean
        acc.gaussian_samples[bucket].append((delta_prev * delta_prev, delta_range * delta_range))

    for i in range(1, len(intervals)):
        acc.markov_counts[interval_index(intervals[i - 1])][interval_index(intervals[i])] += 1.0


def normalize_logs(rows: list[list[float]]) -> list[list[float]]:
    out: list[list[float]] = []
    for row in rows:
        total = sum(row)
        out.append([math.log(value / total) for value in row])
    return out


def build_tables(sequences: Iterable[MelodySequence]) -> ExtractedTables:
    acc = Accumulators()
    works = 0
    for seq in sequences:
        works += 1
        add_sequence(acc, seq)

    gaussian_fit: dict[str, tuple[float, float]] = {}
    for bucket in ("organ", "solo_string", "chorale"):
        samples = acc.gaussian_samples.get(bucket, [])
        if not samples:
            gaussian_fit[bucket] = (1.0, 1.0)
            continue
        vp = sum(sample[0] for sample in samples) / len(samples)
        vr = sum(sample[1] for sample in samples) / len(samples)
        gaussian_fit[bucket] = (max(vp, 1.0e-6), max(vr, 1.0e-6))

    interval_total = sum(acc.interval_unigram[i] for i in range(INTERVAL_MIN, INTERVAL_MAX + 1))
    whole_prob = (acc.interval_unigram[2] + acc.interval_unigram[-2]) / interval_total
    semi_prob = (acc.interval_unigram[1] + acc.interval_unigram[-1]) / interval_total
    sanity = {
        "whole_step_probability": whole_prob,
        "semitone_probability": semi_prob,
        "whole_gt_semitone": 1.0 if whole_prob > semi_prob else 0.0,
        "essen_2x_reference_check": 1.0 if whole_prob > 2.0 * semi_prob else 0.0,
    }

    return ExtractedTables(
        scale_degree_logp=normalize_logs(acc.scale_counts),
        interval_markov_logp=normalize_logs(acc.markov_counts),
        gaussian_fit=gaussian_fit,
        works_count=works,
        sanity=sanity,
    )


def fmt_float(value: float) -> str:
    return f"{value:.9g}f"


def write_tables(tables: ExtractedTables, output_dir: Path, command: str) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    header = [
        "// Generated by scripts/extract_melodic_tables.py",
        f"// Command: {command}",
        f"// Corpus snapshot date: {date.today().isoformat()}",
        f"// Melody sequences: {tables.works_count}",
        "",
    ]

    scale = output_dir / "scale_degree_0th.inc"
    with scale.open("w") as handle:
        handle.write("\n".join(header))
        handle.write("inline constexpr float kScaleDegreeLogP[2][12] = {\n")
        for row in tables.scale_degree_logp:
            handle.write("  {")
            handle.write(", ".join(fmt_float(value) for value in row))
            handle.write("},\n")
        handle.write("};\n")

    markov = output_dir / "interval_markov.inc"
    with markov.open("w") as handle:
        handle.write("\n".join(header))
        handle.write("inline constexpr float kIntervalLogP[25][25] = {\n")
        for row in tables.interval_markov_logp:
            handle.write("  {")
            handle.write(", ".join(fmt_float(value) for value in row))
            handle.write("},\n")
        handle.write("};\n")

    gaussian = output_dir / "gaussian_fit.inc"
    with gaussian.open("w") as handle:
        handle.write("\n".join(header))
        handle.write("struct GaussianFit { float vp; float vr; };\n")
        handle.write("inline constexpr GaussianFit kGaussianFitOrgan = {")
        handle.write(f"{fmt_float(tables.gaussian_fit['organ'][0])}, ")
        handle.write(f"{fmt_float(tables.gaussian_fit['organ'][1])}}};\n")
        handle.write("inline constexpr GaussianFit kGaussianFitSoloString = {")
        handle.write(f"{fmt_float(tables.gaussian_fit['solo_string'][0])}, ")
        handle.write(f"{fmt_float(tables.gaussian_fit['solo_string'][1])}}};\n")
        handle.write("inline constexpr GaussianFit kGaussianFitChorale = {")
        handle.write(f"{fmt_float(tables.gaussian_fit['chorale'][0])}, ")
        handle.write(f"{fmt_float(tables.gaussian_fit['chorale'][1])}}};\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--corpus-dir",
        type=Path,
        default=Path("~/Projects/bach-mcp/data/reference").expanduser(),
    )
    parser.add_argument("--category", action="append", default=[])
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("src/composer/tables"),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    categories = set(args.category) if args.category else None
    tables = build_tables(iter_sequences(args.corpus_dir, categories))
    if tables.works_count == 0:
        print("no melody sequences extracted", file=sys.stderr)
        return 1

    write_tables(tables, args.output_dir, " ".join(sys.argv))
    whole = tables.sanity["whole_step_probability"]
    semi = tables.sanity["semitone_probability"]
    ok = bool(tables.sanity["whole_gt_semitone"])
    two_x = bool(tables.sanity["essen_2x_reference_check"])
    print(f"sequences={tables.works_count}")
    print(f"whole_step_probability={whole:.6f}")
    print(f"semitone_probability={semi:.6f}")
    print(f"sanity_whole_gt_semitone={'pass' if ok else 'fail'}")
    print(f"diagnostic_essen_2x_reference_check={'pass' if two_x else 'fail'}")
    if not ok:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
