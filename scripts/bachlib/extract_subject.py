#!/usr/bin/env python3
"""Extract WTC I fugue-subject feature stats from Algomus labels.

Algomus fugue annotations are ODbL/DbCL data. This script reads the external
clone only; it does not vendor the dataset into this repository. Please cite:
Mathieu Giraud, Richard Groult, Emmanuel Leguy, Florence Leve, Computational
Fugue Analysis, Computer Music Journal, 39(2), 77-96, 2015.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path

from bachlib.common import REPO_ROOT


@dataclass
class SubjectFeatures:
    bwv: int
    label_start: float
    label_duration: float
    note_count: int
    range_semitones: int
    unique_pitch_classes: int
    opening_interval: int
    unique_intervals: int
    max_leap: int
    contour: str


def classify_contour(pitches: list[int]) -> str:
    if len(pitches) < 3:
        return "flat"
    first = pitches[0]
    last = pitches[-1]
    peak_index = max(range(len(pitches)), key=lambda idx: pitches[idx])
    trough_index = min(range(len(pitches)), key=lambda idx: pitches[idx])
    if all(pitches[idx] <= pitches[idx + 1] for idx in range(len(pitches) - 1)):
        return "ascending"
    if all(pitches[idx] >= pitches[idx + 1] for idx in range(len(pitches) - 1)):
        return "descending"
    if 0 < peak_index < len(pitches) - 1 and pitches[peak_index] > max(first, last):
        return "arch"
    if 0 < trough_index < len(pitches) - 1 and pitches[trough_index] < min(first, last):
        return "inverted_arch"
    return "mixed"


def compute_features(bwv: int, start: float, duration: float, pitches: list[int]) -> SubjectFeatures:
    intervals = [abs(pitches[idx] - pitches[idx - 1]) for idx in range(1, len(pitches))]
    return SubjectFeatures(
        bwv=bwv,
        label_start=start,
        label_duration=duration,
        note_count=len(pitches),
        range_semitones=max(pitches) - min(pitches) if pitches else 0,
        unique_pitch_classes=len({pitch % 12 for pitch in pitches}),
        opening_interval=intervals[0] if intervals else 0,
        unique_intervals=len(set(intervals)),
        max_leap=max(intervals) if intervals else 0,
        contour=classify_contour(pitches),
    )


def earliest_subject_label(dez_path: Path) -> tuple[float, float] | None:
    with dez_path.open() as handle:
        doc = json.load(handle)
    labels = [
        label
        for label in doc.get("labels", [])
        if label.get("type") == "S" and label.get("tag") == "S"
    ]
    if not labels:
        return None
    first = min(labels, key=lambda label: float(label["start"]))
    return float(first["start"]), float(first["duration"])


def pitches_in_window(reference_json: Path, start: float, duration: float) -> list[int]:
    with reference_json.open() as handle:
        doc = json.load(handle)
    end = start + duration
    pitches: list[int] = []
    for track in doc.get("tracks", []):
        for note in track.get("notes", []):
            onset = float(note["onset"])
            if start <= onset < end:
                pitches.append(int(note["pitch"]))
    return pitches


def iter_wtc_subjects(algomus_dir: Path, corpus_dir: Path) -> list[SubjectFeatures]:
    out: list[SubjectFeatures] = []
    label_dir = algomus_dir / "fugues" / "bach-wtc-i"
    for dez_path in sorted(label_dir.glob("*-bwv*-ref.dez")):
        match = re.search(r"bwv(\d+)-ref\.dez$", dez_path.name)
        if not match:
            continue
        bwv = int(match.group(1))
        label = earliest_subject_label(dez_path)
        if label is None:
            continue
        start, duration = label
        reference_json = corpus_dir / f"BWV{bwv}_fugue.json"
        if not reference_json.exists():
            continue
        pitches = pitches_in_window(reference_json, start, duration)
        if len(pitches) < 2:
            continue
        out.append(compute_features(bwv, start, duration, pitches))
    return out


def write_json(rows: list[SubjectFeatures], output: Path, *, source: dict[str, str]) -> None:
    """Write subject features as a JSON document.

    @param rows Extracted subject feature rows.
    @param output Destination JSON file (parent directories are created).
    @param source Provenance metadata (input directories) for the document.
    """
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "rows": [asdict(row) for row in rows],
        "source": source,
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
        default=Path("build/algomus_wtc_subject_features.json"),
    )


def register(subparsers) -> None:
    """Register the ``extract-subject`` subcommand on a subparser action."""
    parser = subparsers.add_parser(
        "extract-subject",
        help="extract WTC I fugue-subject features from Algomus labels",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Execute the subject feature extraction from parsed CLI args."""
    rows = iter_wtc_subjects(args.algomus_dir, args.corpus_dir)
    if not rows:
        raise SystemExit("no subject rows extracted")
    source = {
        "algomus_dir": str(args.algomus_dir),
        "corpus_dir": str(args.corpus_dir),
    }
    write_json(rows, args.output, source=source)
    print(f"subjects={len(rows)}")
    print(f"output={args.output}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
