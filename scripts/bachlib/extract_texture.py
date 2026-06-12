#!/usr/bin/env python3
"""Extract texture design statistics for fugue voice layout.

Two input modes are supported:

* ``--corpus DIR`` reads reference fugue JSON files (one per piece) and derives
  duration-weighted texture bands across the corpus. Only files whose top-level
  ``form`` is ``"fugue"`` and ``track_type`` is ``"voice"`` are used; single
  track corpus files are skipped. Corpus notes carry ``onset``, ``duration``,
  ``pitch`` and ``velocity`` keys with the track index acting as the voice
  index. Corpus files are external and are never redistributed; only the derived
  constants and the generated report are stored.
* Positional ``inputs`` read generated.json note arrays via ``texture_metrics``
  and report the legacy generated-output summary. This path is retained for
  ad-hoc inspection only.

The default ``--inc`` target is regenerated from corpus mode.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import mean, median
from typing import Iterable

from bachlib.texture_metrics import compute_texture_metrics


def _percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    pos = (len(ordered) - 1) * q
    lo = int(pos)
    hi = min(lo + 1, len(ordered) - 1)
    frac = pos - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def load_notes(path: Path) -> list[dict[str, int]]:
    with path.open("r", encoding="utf-8") as f:
        payload = json.load(f)
    notes = payload.get("notes")
    if not isinstance(notes, list):
        raise ValueError(f"{path}: missing notes array")
    return notes


def summarize(paths: Iterable[Path]) -> dict[str, float]:
    metrics = [compute_texture_metrics(load_notes(path)) for path in paths]
    if not metrics:
        raise ValueError("no input files")

    silence = [
        voice.silence_ratio
        for metric in metrics
        for voice in metric.voices
    ]
    runs = [
        float(voice.max_repeated_run)
        for metric in metrics
        for voice in metric.voices
    ]
    return {
        "piece_count": float(len(metrics)),
        "max_active_voices_p95": _percentile([m.max_active_voices for m in metrics], 0.95),
        "avg_active_voices_mean": mean(m.avg_active_voices for m in metrics),
        "silence_ratio_p75": _percentile(silence, 0.75),
        "max_repeated_run_p95": _percentile(runs, 0.95),
        "register_overlap_ratio_mean": mean(m.register_overlap_ratio for m in metrics),
        "compass_violation_count_total": float(sum(m.compass_violation_count for m in metrics)),
    }


def _is_voice_separated_fugue(payload: dict) -> bool:
    return payload.get("form") == "fugue" and payload.get("track_type") == "voice"


def load_corpus_piece(path: Path) -> dict | None:
    """Load a corpus piece if it is a voice-separated fugue.

    @param path Reference JSON file.
    @return Parsed payload, or None when the file is not a voice fugue.
    """
    with path.open("r", encoding="utf-8") as f:
        payload = json.load(f)
    if not _is_voice_separated_fugue(payload):
        return None
    if not isinstance(payload.get("tracks"), list):
        return None
    return payload


def _piece_texture(payload: dict) -> dict[str, object]:
    """Compute per-piece texture evidence from a voice-separated fugue.

    Tracks map one-to-one onto voices. A duration-weighted boundary sweep over
    every note on/off boundary measures the active-voice density; per-voice
    sounding fraction, repeated-pitch runs, pitch compass and adjacent-voice
    register overlap are derived directly from the note intervals.

    @param payload Parsed corpus fugue payload.
    @return Per-piece statistics dictionary.
    """
    voice_count = int(payload["voice_count"])
    tracks = payload["tracks"]

    voice_intervals: list[list[tuple[float, float]]] = []
    voice_pitch_order: list[list[int]] = []
    boundaries: set[float] = set()
    for track in tracks:
        notes = track.get("notes", []) if isinstance(track, dict) else []
        ordered = sorted(notes, key=lambda note: float(note["onset"]))
        intervals = [
            (float(note["onset"]), float(note["onset"]) + float(note["duration"]))
            for note in ordered
        ]
        voice_intervals.append(intervals)
        voice_pitch_order.append([int(note["pitch"]) for note in ordered])
        for begin, end in intervals:
            boundaries.add(begin)
            boundaries.add(end)

    bounds = sorted(boundaries)
    piece_lo = bounds[0]
    piece_hi = bounds[-1]
    piece_total = piece_hi - piece_lo

    active_time = 0.0
    total_time = 0.0
    for begin, end in zip(bounds, bounds[1:]):
        span = end - begin
        if span <= 0:
            continue
        mid = (begin + end) / 2.0
        active = sum(
            1
            for intervals in voice_intervals
            if any(start <= mid < stop for start, stop in intervals)
        )
        active_time += active * span
        total_time += span
    avg_active = active_time / total_time if total_time > 0 else 0.0

    sounding_fracs: list[float] = []
    for intervals in voice_intervals:
        sounding = sum(stop - start for start, stop in intervals)
        sounding_fracs.append(sounding / piece_total if piece_total > 0 else 0.0)
    weakest_sounding = min(sounding_fracs) if sounding_fracs else 0.0

    max_runs: list[int] = []
    for pitches in voice_pitch_order:
        max_run = 1 if pitches else 0
        current = 0
        previous: int | None = None
        for pitch in pitches:
            current = current + 1 if pitch == previous else 1
            max_run = max(max_run, current)
            previous = pitch
        max_runs.append(max_run)

    pitch_ranges: list[tuple[int, int]] = []
    for pitches in voice_pitch_order:
        if pitches:
            pitch_ranges.append((min(pitches), max(pitches)))

    overlap_ratios: list[float] = []
    ranges_by_register = sorted(pitch_ranges, key=lambda span: (span[0] + span[1]) / 2.0)
    for (lo_a, hi_a), (lo_b, hi_b) in zip(ranges_by_register, ranges_by_register[1:]):
        overlap_lo = max(lo_a, lo_b)
        overlap_hi = min(hi_a, hi_b)
        union_lo = min(lo_a, lo_b)
        union_hi = max(hi_a, hi_b)
        overlap = max(0, overlap_hi - overlap_lo + 1)
        union = max(1, union_hi - union_lo + 1)
        overlap_ratios.append(overlap / union)

    return {
        "voice_count": voice_count,
        "avg_active_voices": avg_active,
        "normalized_avg_active": avg_active / voice_count if voice_count > 0 else 0.0,
        "weakest_voice_sounding": weakest_sounding,
        "max_repeated_runs": max_runs,
        "register_overlaps": overlap_ratios,
        "pitch_ranges": pitch_ranges,
    }


def summarize_corpus(directory: Path) -> dict[str, object]:
    """Aggregate texture bands across the voice-separated fugue corpus.

    @param directory Reference corpus directory containing per-piece JSON.
    @return Aggregate statistics with p25 / median / p75 bands.
    """
    pieces: list[dict[str, object]] = []
    for path in sorted(directory.glob("*.json")):
        payload = load_corpus_piece(path)
        if payload is None:
            continue
        pieces.append(_piece_texture(payload))
    if not pieces:
        raise ValueError(f"{directory}: no voice-separated fugue pieces found")

    norm_avg = [float(piece["normalized_avg_active"]) for piece in pieces]
    weakest = [float(piece["weakest_voice_sounding"]) for piece in pieces]
    runs = [float(run) for piece in pieces for run in piece["max_repeated_runs"]]
    overlaps = [float(ov) for piece in pieces for ov in piece["register_overlaps"]]
    pitch_lo = [span[0] for piece in pieces for span in piece["pitch_ranges"]]
    pitch_hi = [span[1] for piece in pieces for span in piece["pitch_ranges"]]

    return {
        "piece_count": len(pieces),
        "voice_count_median": median(int(piece["voice_count"]) for piece in pieces),
        "normalized_avg_active_p25": _percentile(norm_avg, 0.25),
        "normalized_avg_active_median": median(norm_avg),
        "normalized_avg_active_p75": _percentile(norm_avg, 0.75),
        "weakest_voice_sounding_p25": _percentile(weakest, 0.25),
        "weakest_voice_sounding_median": median(weakest),
        "weakest_voice_sounding_p75": _percentile(weakest, 0.75),
        "max_repeated_run_p25": _percentile(runs, 0.25),
        "max_repeated_run_median": median(runs),
        "max_repeated_run_p75": _percentile(runs, 0.75),
        "register_overlap_p25": _percentile(overlaps, 0.25),
        "register_overlap_median": median(overlaps),
        "register_overlap_p75": _percentile(overlaps, 0.75),
        "compass_min_pitch": min(pitch_lo) if pitch_lo else 0,
        "compass_max_pitch": max(pitch_hi) if pitch_hi else 0,
    }


def write_corpus_inc(summary: dict[str, object], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("// Generated by `python3 scripts/bach_tools.py extract-texture` (corpus mode).\n")
        f.write(
            "// Source: bach-mcp reference corpus, voice-separated fugues "
            "(form=fugue, track_type=voice).\n"
        )
        f.write(
            "// Derived statistics only; corpus files are external and not "
            "redistributed.\n"
        )
        f.write(
            f"constexpr int kTextureStatsPieceCount = {int(summary['piece_count'])};\n"
        )
        f.write(
            f"constexpr int kTextureVoiceCountMedian = {int(summary['voice_count_median'])};\n"
        )
        f.write(
            "constexpr double kTextureNormalizedAvgActiveP25 = "
            f"{float(summary['normalized_avg_active_p25']):.6f};\n"
        )
        f.write(
            "constexpr double kTextureNormalizedAvgActiveMedian = "
            f"{float(summary['normalized_avg_active_median']):.6f};\n"
        )
        f.write(
            "constexpr double kTextureNormalizedAvgActiveP75 = "
            f"{float(summary['normalized_avg_active_p75']):.6f};\n"
        )
        f.write(
            "constexpr double kTextureWeakestVoiceSoundingP25 = "
            f"{float(summary['weakest_voice_sounding_p25']):.6f};\n"
        )
        f.write(
            "constexpr double kTextureWeakestVoiceSoundingMedian = "
            f"{float(summary['weakest_voice_sounding_median']):.6f};\n"
        )
        f.write(
            "constexpr double kTextureWeakestVoiceSoundingP75 = "
            f"{float(summary['weakest_voice_sounding_p75']):.6f};\n"
        )
        f.write(
            "constexpr int kTextureMaxRepeatedRunP25 = "
            f"{int(round(float(summary['max_repeated_run_p25'])))};\n"
        )
        f.write(
            "constexpr int kTextureMaxRepeatedRunMedian = "
            f"{int(round(float(summary['max_repeated_run_median'])))};\n"
        )
        f.write(
            "constexpr int kTextureMaxRepeatedRunP75 = "
            f"{int(round(float(summary['max_repeated_run_p75'])))};\n"
        )
        f.write(
            "constexpr double kTextureRegisterOverlapP25 = "
            f"{float(summary['register_overlap_p25']):.6f};\n"
        )
        f.write(
            "constexpr double kTextureRegisterOverlapMedian = "
            f"{float(summary['register_overlap_median']):.6f};\n"
        )
        f.write(
            "constexpr double kTextureRegisterOverlapP75 = "
            f"{float(summary['register_overlap_p75']):.6f};\n"
        )
        f.write(
            f"constexpr int kTextureCompassMinPitch = {int(summary['compass_min_pitch'])};\n"
        )
        f.write(
            f"constexpr int kTextureCompassMaxPitch = {int(summary['compass_max_pitch'])};\n"
        )


def render_corpus_report(summary: dict[str, object], directory: Path) -> str:
    """Render the corpus texture report markdown shared by file and stdout.

    @param summary Aggregated corpus statistics from ``summarize_corpus``.
    @param directory External corpus directory cited in the report.
    @return The full markdown report as a single string.
    """
    lines: list[str] = []
    lines.append("# Texture Stats Report (corpus)\n\n")
    lines.append(
        "Texture bands derived from the bach-mcp reference corpus, restricted "
        "to voice-separated fugues (`form == \"fugue\"` and "
        "`track_type == \"voice\"`). Single-track corpus files are excluded. "
        "Corpus audio/score files are external and are not redistributed; "
        "only the derived constants below and this report are stored.\n\n"
    )
    lines.append("## Methodology\n\n")
    lines.append(
        "- Each track is a voice; the top-level `voice_count` is the divisor "
        "for normalization.\n"
        "- Active-voice density is a duration-weighted sweep over every note "
        "on/off boundary; `avg_active_voices` is divided by `voice_count`.\n"
        "- Per-voice sounding fraction is `sounding_time / piece_total`; the "
        "per-piece minimum captures the weakest (most resting) voice.\n"
        "- Max repeated-pitch run counts consecutive equal pitches in onset "
        "order, per voice.\n"
        "- Register overlap is the inclusive-semitone range intersection over "
        "union between adjacent voices ordered by register.\n"
        "- Extraction command: `python3 scripts/bach_tools.py extract-texture --corpus`.\n\n"
    )
    lines.append(f"Corpus directory (external): `{directory}`\n\n")
    lines.append(f"Pieces: {int(summary['piece_count'])}\n\n")
    lines.append("## Bands\n\n")
    lines.append("| Metric | p25 | median | p75 |\n")
    lines.append("| --- | --- | --- | --- |\n")
    lines.append(
        "| normalized avg active voices | "
        f"{float(summary['normalized_avg_active_p25']):.6f} | "
        f"{float(summary['normalized_avg_active_median']):.6f} | "
        f"{float(summary['normalized_avg_active_p75']):.6f} |\n"
    )
    lines.append(
        "| weakest-voice sounding fraction | "
        f"{float(summary['weakest_voice_sounding_p25']):.6f} | "
        f"{float(summary['weakest_voice_sounding_median']):.6f} | "
        f"{float(summary['weakest_voice_sounding_p75']):.6f} |\n"
    )
    lines.append(
        "| max repeated-pitch run | "
        f"{float(summary['max_repeated_run_p25']):.6f} | "
        f"{float(summary['max_repeated_run_median']):.6f} | "
        f"{float(summary['max_repeated_run_p75']):.6f} |\n"
    )
    lines.append(
        "| adjacent register overlap | "
        f"{float(summary['register_overlap_p25']):.6f} | "
        f"{float(summary['register_overlap_median']):.6f} | "
        f"{float(summary['register_overlap_p75']):.6f} |\n"
    )
    lines.append("\n## Compass\n\n")
    lines.append(
        f"- min pitch: {int(summary['compass_min_pitch'])}\n"
        f"- max pitch: {int(summary['compass_max_pitch'])}\n"
        f"- median voice count: {int(summary['voice_count_median'])}\n"
    )
    return "".join(lines)


def write_corpus_report(summary: dict[str, object], path: Path, directory: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write(render_corpus_report(summary, directory))


def write_inc(summary: dict[str, float], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("// Generated by `python3 scripts/bach_tools.py extract-texture`.\n")
        f.write("// Derived statistics only; corpus files are not redistributed.\n")
        f.write(f"constexpr int kTextureStatsPieceCount = {int(summary['piece_count'])};\n")
        f.write(
            "constexpr double kTextureAvgActiveVoicesMean = "
            f"{summary['avg_active_voices_mean']:.6f};\n"
        )
        f.write(
            "constexpr double kTextureSilenceRatioP75 = "
            f"{summary['silence_ratio_p75']:.6f};\n"
        )
        f.write(
            "constexpr int kTextureMaxRepeatedRunP95 = "
            f"{int(round(summary['max_repeated_run_p95']))};\n"
        )
        f.write(
            "constexpr double kTextureRegisterOverlapRatioMean = "
            f"{summary['register_overlap_ratio_mean']:.6f};\n"
        )
        f.write(
            "constexpr int kTextureCompassViolationCountTotal = "
            f"{int(summary['compass_violation_count_total'])};\n"
        )


def render_report(summary: dict[str, float], inputs: list[Path]) -> str:
    """Render the generated-output texture report markdown.

    @param summary Aggregated statistics from ``summarize``.
    @param inputs generated.json input paths cited in the report.
    @return The full markdown report as a single string.
    """
    lines: list[str] = []
    lines.append("# Texture Stats Report\n\n")
    lines.append("Derived from generated.json note arrays.\n\n")
    lines.append("## Inputs\n\n")
    for input_path in inputs:
        lines.append(f"- `{input_path}`\n")
    lines.append("\n## Summary\n\n")
    for key, value in summary.items():
        lines.append(f"- `{key}`: {value:.6f}\n")
    return "".join(lines)


def write_report(summary: dict[str, float], path: Path, inputs: list[Path]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write(render_report(summary, inputs))


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("inputs", nargs="*", type=Path)
    parser.add_argument(
        "--corpus",
        type=Path,
        default=None,
        help="reference corpus directory of voice-separated fugue JSON files",
    )
    parser.add_argument(
        "--inc",
        type=Path,
        default=Path("src/composer/tables/texture_stats.inc"),
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="optional report file; prints to stdout when omitted",
    )


def register(subparsers) -> None:
    """Register the ``extract-texture`` subcommand on a subparser action."""
    parser = subparsers.add_parser(
        "extract-texture",
        help="extract texture design statistics for fugue voice layout",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Execute the texture extraction from parsed CLI args."""
    if args.corpus is not None:
        summary = summarize_corpus(args.corpus)
        write_corpus_inc(summary, args.inc)
        if args.out is not None:
            write_corpus_report(summary, args.out, args.corpus)
        else:
            print(render_corpus_report(summary, args.corpus), end="")
        return 0

    if not args.inputs:
        raise SystemExit("provide generated.json inputs or --corpus DIR")
    summary = summarize(args.inputs)
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
