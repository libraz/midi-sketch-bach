#!/usr/bin/env python3
"""Extract fugue entry-plan statistics from external annotation files.

Two input formats are accepted; raw annotations are not vendored here, only the
derived constants and the generated report are stored.

CSV format columns:
  piece,bar,voice,kind[,duration]

`.dez` format (Dezrann fugue annotations) is read from the top-level ``labels``
array. Dezrann ``start`` and ``duration`` values are quarter-note beats; they
are converted at ingestion to 4/4-equivalent bars, matching the generated
fugue's internal meter. Only these label ``type`` values are consumed:

  * ``S``      -> subject entry (its ``duration`` is the subject length)
  * ``CS*``    -> countersubject (recorded but not used in the summary)
  * ``Pedal``  -> pedal point (recorded but not used in the summary)
  * ``Cadence``-> cadence (recorded but not used in the summary)

All other label types (``S-inc``, ``S-inv``, ``S2``, ``ignore``, ``Structure``
etc.) are skipped. Subject-derived variants are intentionally excluded so the
entry interval reflects principal subject entries only.

Derived statistics:

  * ``entry_interval`` is the bar gap between consecutive subject entry starts
    within a piece. Its mean and deciles drive the C++ entry plan.
  * ``episode_length`` is the true inter-entry rest: the entry interval minus
    the preceding subject's duration (clamped at zero). This requires the S row
    ``duration``; without it the value collapses toward the entry interval.
  * ``stretto_rate`` is overlap-based: a piece counts as having stretto when a
    subject entry begins before the previous subject finishes. There is no
    explicit stretto label in the source data, so this is the only signal.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from statistics import mean

# .dez label types consumed by the entry-plan extraction.
_SUBJECT_LABEL = "S"
_COUNTERSUBJECT_PREFIX = "CS"
_PEDAL_LABEL = "Pedal"
_CADENCE_LABEL = "Cadence"
_DEZRANN_QUARTER_BEATS_PER_BAR = 4.0


def _dez_beats_to_bars(value: object, *, path: Path, field: str) -> str:
    """Convert a Dezrann quarter-note offset to 4/4-equivalent bars."""
    try:
        beats = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{path}: non-numeric label {field}: {value!r}") from exc
    return f"{beats / _DEZRANN_QUARTER_BEATS_PER_BAR:g}"


def load_rows(paths: list[Path]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        if path.suffix == ".dez":
            rows.extend(load_dez_rows(path))
            continue
        with path.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            required = {"piece", "bar", "voice", "kind"}
            if reader.fieldnames is None or not required.issubset(set(reader.fieldnames)):
                missing = sorted(required - set(reader.fieldnames or []))
                raise ValueError(f"{path}: missing columns {missing}")
            rows.extend(reader)
    return rows


def load_dez_rows(path: Path) -> list[dict[str, str]]:
    """Read Dezrann fugue labels into normalized entry rows.

    Only the subject (``S``), countersubject (``CS*``), pedal (``Pedal``) and
    cadence (``Cadence``) label types are consumed; every other type is skipped.

    @param path Path to a ``.dez`` annotation file.
    Dezrann's quarter-note beat offsets are normalized to 4/4-equivalent bars
    here, so every downstream field carrying a ``...Bars`` name has one unit.

    @return List of row dictionaries with piece, bar, voice, kind and duration.
    """
    with path.open("r", encoding="utf-8") as f:
        payload = json.load(f)
    labels = payload.get("labels", [])
    if not isinstance(labels, list):
        raise ValueError(f"{path}: missing labels array")

    rows: list[dict[str, str]] = []
    piece = path.stem.replace("-ref", "")
    for label in labels:
        if not isinstance(label, dict):
            continue
        label_type = str(label.get("type", "")).strip()
        if label_type == _SUBJECT_LABEL:
            kind = "S"
        elif label_type.startswith(_COUNTERSUBJECT_PREFIX):
            kind = "CS"
        elif label_type == _PEDAL_LABEL:
            kind = "pedal"
        elif label_type == _CADENCE_LABEL:
            kind = "cadence"
        else:
            continue
        rows.append(
            {
                "piece": piece,
                "bar": _dez_beats_to_bars(
                    label.get("start", 0), path=path, field="start"
                ),
                "voice": str(label.get("staff", "*")),
                "kind": kind,
                "duration": _dez_beats_to_bars(
                    label.get("duration", 0), path=path, field="duration"
                ),
            }
        )
    return rows


def summarize(rows: list[dict[str, str]]) -> dict[str, object]:
    """Aggregate entry-plan statistics across annotated pieces.

    @param rows Normalized annotation rows (see ``load_rows``).
    @return Summary dict with entry interval, true episode length and stretto.
    """
    subjects_by_piece: dict[str, list[tuple[float, float]]] = defaultdict(list)
    subject_lengths: list[float] = []
    stretto_pieces: set[str] = set()

    for row in rows:
        piece = row["piece"]
        kind = row["kind"].strip().lower()
        if kind == "s":
            bar = float(row["bar"])
            duration = float(row.get("duration", "0") or 0)
            subjects_by_piece[piece].append((bar, duration))
            if duration > 0:
                subject_lengths.append(duration)

    intervals: list[int] = []
    episode_lengths: list[int] = []
    for piece, entries in subjects_by_piece.items():
        ordered = sorted(entries)
        for (start, duration), (next_start, _) in zip(ordered, ordered[1:]):
            interval = round(next_start - start)
            intervals.append(interval)
            # True episode length: rest between the subject end and the next
            # entry. Uses the subject duration from the S row.
            episode_lengths.append(max(0, round(next_start - start - duration)))
            if duration > 0 and next_start < start + duration:
                stretto_pieces.add(piece)

    piece_count = len({row["piece"] for row in rows})
    return {
        "piece_count": piece_count,
        "subject_length_mean": mean(subject_lengths) if subject_lengths else 0.0,
        "entry_interval_mean": mean(intervals) if intervals else 0.0,
        "entry_interval_deciles": deciles(intervals),
        "episode_length_mean": mean(episode_lengths) if episode_lengths else 0.0,
        "stretto_rate": len(stretto_pieces) / piece_count if piece_count else 0.0,
    }


def deciles(values: list[int]) -> list[int]:
    if not values:
        return []
    ordered = sorted(values)
    out: list[int] = []
    for idx in range(1, 10):
        pos = round((len(ordered) - 1) * idx / 10)
        out.append(ordered[pos])
    return out


def write_inc(summary: dict[str, object], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    decile_values = ", ".join(str(v) for v in summary["entry_interval_deciles"])
    with path.open("w", encoding="utf-8") as f:
        f.write("// Generated by `python3 scripts/bach_tools.py extract-entry-plan`.\n")
        f.write("// Derived statistics only; external annotations are not redistributed.\n")
        f.write(f"constexpr int kEntryPlanStatsPieceCount = {summary['piece_count']};\n")
        f.write(
            "constexpr double kSubjectLengthMeanBars = "
            f"{float(summary['subject_length_mean']):.6f};\n"
        )
        f.write(
            "constexpr double kEntryIntervalMeanBars = "
            f"{float(summary['entry_interval_mean']):.6f};\n"
        )
        f.write(f"constexpr int kEntryIntervalDeciles[9] = {{{decile_values}}};\n")
        f.write(
            "constexpr double kEpisodeLengthMeanBars = "
            f"{float(summary['episode_length_mean']):.6f};\n"
        )
        f.write(f"constexpr double kStrettoRate = {float(summary['stretto_rate']):.6f};\n")


def render_report(summary: dict[str, object], inputs: list[Path]) -> str:
    """Render the entry-plan report markdown shared by file and stdout output.

    @param summary Aggregated statistics from ``summarize``.
    @param inputs Annotation input paths cited in the report.
    @return The full markdown report as a single string.
    """
    lines: list[str] = []
    lines.append("# Entry Plan Stats Report\n\n")
    lines.append(
        "Derived from external fugue annotations (Dezrann `.dez` labels or "
        "equivalent CSV). Raw annotations are not redistributed.\n\n"
    )
    lines.append("## Consumed labels\n\n")
    lines.append(
        "- `S` subject entries drive the entry interval, episode length and "
        "stretto detection (the `duration` field is the subject length).\n"
        "- `CS*`, `Pedal` and `Cadence` are recognized but do not contribute "
        "to the summary statistics.\n"
        "- All other label types (`S-inc`, `S-inv`, `S2`, `ignore`, "
        "`Structure`, ...) are skipped.\n\n"
    )
    lines.append("## Definitions\n\n")
    lines.append(
        "- `entry_interval`: bar gap between consecutive subject entry "
        "starts.\n"
        "- `subject_length`: mean duration of principal `S` labels in bars; "
        "Dezrann quarter-note beats are divided by four at ingestion.\n"
        "- `episode_length`: entry interval minus the preceding subject "
        "duration, clamped at zero (the true inter-entry rest).\n"
        "- `stretto_rate`: fraction of pieces where a subject begins before "
        "the previous subject ends (overlap-based; no explicit stretto "
        "label exists in the source).\n\n"
    )
    lines.append("## Inputs\n\n")
    for input_path in inputs:
        lines.append(f"- `{input_path}`\n")
    lines.append("\n## Summary\n\n")
    for key, value in summary.items():
        lines.append(f"- `{key}`: `{value}`\n")
    return "".join(lines)


def write_report(summary: dict[str, object], path: Path, inputs: list[Path]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write(render_report(summary, inputs))


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument(
        "--inc",
        type=Path,
        default=Path("src/composer/tables/entry_plan_stats.inc"),
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="optional report file; prints to stdout when omitted",
    )


def register(subparsers) -> None:
    """Register the ``extract-entry-plan`` subcommand on a subparser action."""
    parser = subparsers.add_parser(
        "extract-entry-plan",
        help="extract fugue entry-plan statistics from annotation files",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Execute the entry-plan extraction from parsed CLI args."""
    rows = load_rows(args.inputs)
    summary = summarize(rows)
    subject_length_mean = float(summary["subject_length_mean"])
    if subject_length_mean > 0.0 and not 1.0 <= subject_length_mean <= 4.0:
        raise ValueError(
            "mean subject length must be within 1..4 bars after unit "
            f"normalization (got {subject_length_mean:.6f})"
        )
    write_inc(summary, args.inc)
    if args.out is not None:
        write_report(summary, args.out, args.inputs)
    else:
        print(render_report(summary, args.inputs), end="")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
