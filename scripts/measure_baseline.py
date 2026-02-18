"""Measure baseline metrics for fugue generation quality.

Generates MIDI from seeds via bach_cli, then computes:
  - 6-dimension Bach reference scores (structure, melody, harmony, counterpoint, rhythm, texture)
  - Repeat note rate, octave+ leap rate
  - N-gram attestation (5-gram against organ fugue reference corpus)
  - Repeat-containing n-gram rate (3-grams with unison)
  - Violation counts by rule
  - Avg active voices, max voice spacing
  - Rhythm JSD, harmony degree JSD

Usage:
  python3 -m scripts.measure_baseline --seeds 42,123,456,789,1000,2024 --form fugue --voices 4
  python3 -m scripts.measure_baseline --seeds 1-20 --form fugue --json baseline.json
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path
from typing import Any, Dict, List, Optional, Set

# Ensure project root is on sys.path for direct invocation.
_PROJECT_ROOT = str(Path(__file__).resolve().parent.parent)
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

# -- Import from the existing bach_analyzer infrastructure --
from scripts.bach_analyzer.batch import _run_bach_cli, parse_seed_range
from scripts.bach_analyzer.model import TICKS_PER_BEAT, TICKS_PER_BAR, Note, Score
from scripts.bach_analyzer.profiles import (
    count_melodic_intervals,
    js_divergence as jsd,
    normalize,
    sample_texture_density,
)
from scripts.bach_analyzer.runner import load_score, validate
from scripts.bach_analyzer.score import (
    compute_score,
    extract_interval_profile,
    extract_rhythm_profile,
    extract_texture_profile,
    _extract_degree_distribution,
)
from scripts.bach_analyzer.form_profile import get_form_profile
from scripts.bach_analyzer.reference import load_category
from scripts.bach_analyzer.rules.base import Severity


# ---------------------------------------------------------------------------
# Reference 5-gram corpus builder
# ---------------------------------------------------------------------------

_REF_DIR = Path(__file__).resolve().parent.parent / "data" / "reference"


def _build_reference_5grams(glob_pattern: str = "BWV*fugue*.json") -> Set[tuple]:
    """Build a set of pitch-interval 5-grams from organ fugue reference files."""
    ngram_set: set = set()
    for path in sorted(_REF_DIR.glob(glob_pattern)):
        with open(path) as fobj:
            data = json.load(fobj)
        for track in data.get("tracks", []):
            pitches = [n["pitch"] for n in track.get("notes", [])]
            if len(pitches) < 6:
                continue
            intervals = [pitches[idx + 1] - pitches[idx] for idx in range(len(pitches) - 1)]
            for idx in range(len(intervals) - 4):
                ngram_set.add(tuple(intervals[idx:idx + 5]))
    return ngram_set


# ---------------------------------------------------------------------------
# Per-file metric computation
# ---------------------------------------------------------------------------


def _compute_metrics(
    score: Score,
    ref_5grams: Set[tuple],
    ref_data: Dict[str, Any],
) -> Dict[str, Any]:
    """Compute all baseline metrics for a single generated score."""
    metrics: Dict[str, Any] = {}

    # -- 6-dimension Bach reference score --
    profile = get_form_profile(score.form)
    results = validate(score)
    bach_score = compute_score(
        score,
        category=profile.reference_category,
        counterpoint_enabled=profile.counterpoint_enabled,
        results=results,
        form_name=profile.form_name,
    )
    metrics["total_score"] = bach_score.total
    metrics["grade"] = bach_score.grade
    metrics["dimensions"] = {
        dim_name: round(dim_score.score, 1)
        for dim_name, dim_score in bach_score.dimensions.items()
        if dim_score.applicable
    }

    # -- Violation counts by rule --
    violation_counts: Dict[str, int] = {}
    total_violations = 0
    for res in results:
        if res.violation_count > 0:
            violation_counts[res.rule_name] = res.violation_count
            total_violations += res.violation_count
    metrics["total_violations"] = total_violations
    metrics["violation_counts"] = violation_counts

    # -- Melodic analysis (per-voice, then aggregate) --
    all_intervals: List[int] = []  # signed semitone diffs
    total_notes = 0
    for track in score.tracks:
        notes = track.sorted_notes
        total_notes += len(notes)
        for idx in range(1, len(notes)):
            diff = notes[idx].pitch - notes[idx - 1].pitch
            all_intervals.append(diff)

    num_intervals = len(all_intervals)

    # Repeat note rate: consecutive same pitch (interval == 0)
    repeat_count = sum(1 for ivl in all_intervals if ivl == 0)
    metrics["repeat_note_rate"] = repeat_count / num_intervals if num_intervals else 0.0

    # Octave+ leap rate: abs(interval) >= 12
    octave_leap_count = sum(1 for ivl in all_intervals if abs(ivl) >= 12)
    metrics["octave_leap_rate"] = octave_leap_count / num_intervals if num_intervals else 0.0

    # -- N-gram attestation (5-gram) --
    gen_5gram_total = 0
    gen_5gram_attested = 0
    for track in score.tracks:
        notes = track.sorted_notes
        pitches = [n.pitch for n in notes]
        if len(pitches) < 6:
            continue
        intervals = [pitches[idx + 1] - pitches[idx] for idx in range(len(pitches) - 1)]
        for idx in range(len(intervals) - 4):
            gram = tuple(intervals[idx:idx + 5])
            gen_5gram_total += 1
            if gram in ref_5grams:
                gen_5gram_attested += 1

    metrics["ngram_5_attestation"] = (
        gen_5gram_attested / gen_5gram_total if gen_5gram_total else 0.0
    )
    metrics["ngram_5_total"] = gen_5gram_total
    metrics["ngram_5_attested"] = gen_5gram_attested

    # -- Repeat-containing 3-gram rate (3-grams containing unison) --
    gen_3gram_total = 0
    gen_3gram_with_repeat = 0
    for track in score.tracks:
        notes = track.sorted_notes
        pitches = [n.pitch for n in notes]
        if len(pitches) < 4:
            continue
        intervals = [pitches[idx + 1] - pitches[idx] for idx in range(len(pitches) - 1)]
        for idx in range(len(intervals) - 2):
            gram = tuple(intervals[idx:idx + 3])
            gen_3gram_total += 1
            if 0 in gram:
                gen_3gram_with_repeat += 1

    metrics["repeat_3gram_rate"] = (
        gen_3gram_with_repeat / gen_3gram_total if gen_3gram_total else 0.0
    )

    # -- Texture: avg active voices, max voice spacing --
    voices_dict = score.voices_dict
    tex_result = sample_texture_density(voices_dict, score.total_duration, TICKS_PER_BEAT)
    total_samples = tex_result["total_samples"]
    total_active = tex_result["total_active"]
    metrics["avg_active_voices"] = total_active / total_samples if total_samples else 0.0

    # Max voice spacing: sample every beat, find max pitch spread across sounding voices
    from scripts.bach_analyzer.model import sounding_note_at

    max_spacing = 0
    voice_names = list(voices_dict.keys())
    for tick in range(0, score.total_duration, TICKS_PER_BEAT):
        sounding_pitches = []
        for vname in voice_names:
            note = sounding_note_at(voices_dict[vname], tick)
            if note is not None:
                sounding_pitches.append(note.pitch)
        if len(sounding_pitches) >= 2:
            spacing = max(sounding_pitches) - min(sounding_pitches)
            if spacing > max_spacing:
                max_spacing = spacing
    metrics["max_voice_spacing"] = max_spacing

    # -- Rhythm JSD --
    rp = extract_rhythm_profile(score)
    ref_rhythm = ref_data.get("distributions", {}).get("rhythm", {})
    if rp["distribution"] and ref_rhythm:
        metrics["rhythm_jsd"] = round(jsd(rp["distribution"], ref_rhythm), 4)
    else:
        metrics["rhythm_jsd"] = None

    # -- Harmony degree JSD --
    gen_deg = _extract_degree_distribution(score)
    ref_deg = ref_data.get("distributions", {}).get("harmony_degrees", {})
    if gen_deg and ref_deg:
        metrics["harmony_degree_jsd"] = round(jsd(gen_deg, ref_deg), 4)
    else:
        metrics["harmony_degree_jsd"] = None

    metrics["total_notes"] = total_notes
    metrics["total_bars"] = score.total_bars

    return metrics


# ---------------------------------------------------------------------------
# Aggregation
# ---------------------------------------------------------------------------


def _aggregate(per_seed: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Compute mean/std across seeds for key metrics."""
    agg: Dict[str, Any] = {}
    keys = [
        "total_score", "repeat_note_rate", "octave_leap_rate",
        "ngram_5_attestation", "repeat_3gram_rate", "avg_active_voices",
        "max_voice_spacing", "rhythm_jsd", "harmony_degree_jsd",
        "total_violations",
    ]
    for key in keys:
        vals = [s[key] for s in per_seed if s.get(key) is not None]
        if vals:
            agg[key] = {
                "mean": round(statistics.mean(vals), 4),
                "std": round(statistics.stdev(vals), 4) if len(vals) > 1 else 0.0,
                "min": round(min(vals), 4),
                "max": round(max(vals), 4),
            }

    # Aggregate dimension scores
    dim_names = ["structure", "melody", "harmony", "counterpoint", "rhythm", "texture"]
    agg["dimensions"] = {}
    for dim in dim_names:
        vals = [s["dimensions"].get(dim) for s in per_seed if dim in s.get("dimensions", {})]
        vals = [v for v in vals if v is not None]
        if vals:
            agg["dimensions"][dim] = {
                "mean": round(statistics.mean(vals), 1),
                "std": round(statistics.stdev(vals), 1) if len(vals) > 1 else 0.0,
            }

    # Aggregate violation counts by rule
    rule_counter: Counter = Counter()
    for seed_data in per_seed:
        for rule, count in seed_data.get("violation_counts", {}).items():
            rule_counter[rule] += count
    agg["violation_totals_by_rule"] = dict(rule_counter.most_common())

    return agg


# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------


def _format_markdown(agg: Dict[str, Any], per_seed: List[Dict[str, Any]], seeds: List[int]) -> str:
    """Format results as a markdown table."""
    lines: List[str] = []
    lines.append(f"## Fugue Baseline Metrics ({len(seeds)} seeds)")
    lines.append("")
    lines.append("| Metric | Mean | Std | Min | Max |")
    lines.append("|--------|------|-----|-----|-----|")

    display_order = [
        ("Total Score", "total_score"),
        ("Repeat Note Rate", "repeat_note_rate"),
        ("Octave+ Leap Rate", "octave_leap_rate"),
        ("5-gram Attestation", "ngram_5_attestation"),
        ("Repeat 3-gram Rate", "repeat_3gram_rate"),
        ("Avg Active Voices", "avg_active_voices"),
        ("Max Voice Spacing", "max_voice_spacing"),
        ("Rhythm JSD", "rhythm_jsd"),
        ("Harmony Degree JSD", "harmony_degree_jsd"),
        ("Total Violations", "total_violations"),
    ]

    for label, key in display_order:
        stats = agg.get(key, {})
        if stats:
            fmt = ".4f" if key not in ("total_score", "total_violations", "max_voice_spacing") else ".1f"
            lines.append(
                f"| {label} | {stats['mean']:{fmt}} | {stats['std']:{fmt}} "
                f"| {stats['min']:{fmt}} | {stats['max']:{fmt}} |"
            )

    # Dimension scores table
    lines.append("")
    lines.append("### Dimension Scores")
    lines.append("")
    lines.append("| Dimension | Mean | Std |")
    lines.append("|-----------|------|-----|")
    for dim in ["structure", "melody", "harmony", "counterpoint", "rhythm", "texture"]:
        stats = agg.get("dimensions", {}).get(dim, {})
        if stats:
            lines.append(f"| {dim.capitalize()} | {stats['mean']:.1f} | {stats['std']:.1f} |")

    # Top violations
    viol = agg.get("violation_totals_by_rule", {})
    if viol:
        lines.append("")
        lines.append("### Top Violations (across all seeds)")
        lines.append("")
        lines.append("| Rule | Total Count |")
        lines.append("|------|-------------|")
        for rule, count in list(viol.items())[:10]:
            lines.append(f"| {rule} | {count} |")

    # Per-seed summary
    lines.append("")
    lines.append("### Per-Seed Scores")
    lines.append("")
    lines.append("| Seed | Score | Grade | Repeat% | 5gram% | Violations |")
    lines.append("|------|-------|-------|---------|--------|------------|")
    for seed_data in per_seed:
        seed = seed_data.get("seed", "?")
        lines.append(
            f"| {seed} | {seed_data['total_score']:.1f} | {seed_data['grade']} "
            f"| {seed_data['repeat_note_rate']:.1%} "
            f"| {seed_data['ngram_5_attestation']:.1%} "
            f"| {seed_data['total_violations']} |"
        )

    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Measure fugue generation baseline metrics",
    )
    parser.add_argument("--seeds", required=True, help="Seeds (e.g. 42,123 or 1-20)")
    parser.add_argument("--form", default="fugue", help="Form type (default: fugue)")
    parser.add_argument("--voices", type=int, default=4, help="Number of voices (default: 4)")
    parser.add_argument("--cli-path", default="./build/bin/bach_cli", help="Path to bach_cli")
    parser.add_argument("--json", dest="json_out", metavar="FILE", help="Write JSON to file")
    parser.add_argument("--files", nargs="*", help="Use existing output.json files instead of generating")
    args = parser.parse_args()

    seeds = parse_seed_range(args.seeds)

    # Build reference 5-gram corpus
    print("Building reference 5-gram corpus from organ fugue data...", file=sys.stderr)
    ref_5grams = _build_reference_5grams()
    print(f"  {len(ref_5grams)} unique 5-grams from reference corpus", file=sys.stderr)

    # Load reference profile for JSD comparisons
    ref_category = "organ_fugue"
    ref_data = load_category(ref_category)

    per_seed: List[Dict[str, Any]] = []

    if args.files:
        # Use provided files
        for idx, filepath in enumerate(args.files):
            seed = seeds[idx] if idx < len(seeds) else idx
            print(f"  [{idx + 1}/{len(args.files)}] {filepath}...", file=sys.stderr, end="", flush=True)
            try:
                score = load_score(filepath)
                metrics = _compute_metrics(score, ref_5grams, ref_data)
                metrics["seed"] = score.seed or seed
                per_seed.append(metrics)
                print(f" score={metrics['total_score']:.1f}", file=sys.stderr)
            except Exception as exc:
                print(f" ERROR: {exc}", file=sys.stderr)
    else:
        # Generate from seeds
        with tempfile.TemporaryDirectory(prefix="bach_baseline_") as tmpdir:
            for idx, seed in enumerate(seeds):
                print(
                    f"  [{idx + 1}/{len(seeds)}] seed={seed}...",
                    file=sys.stderr, end="", flush=True,
                )
                out_path = _run_bach_cli(
                    seed=seed,
                    form=args.form,
                    voices=args.voices,
                    cli_path=args.cli_path,
                    output_dir=tmpdir,
                )
                if out_path is None:
                    print(" FAILED (generation error)", file=sys.stderr)
                    continue
                try:
                    score = load_score(out_path)
                    metrics = _compute_metrics(score, ref_5grams, ref_data)
                    metrics["seed"] = seed
                    per_seed.append(metrics)
                    print(f" score={metrics['total_score']:.1f}", file=sys.stderr)
                except Exception as exc:
                    print(f" ERROR: {exc}", file=sys.stderr)

    if not per_seed:
        print("No results to report.", file=sys.stderr)
        return 1

    # Aggregate
    agg = _aggregate(per_seed)

    # Output markdown to stdout
    md = _format_markdown(agg, per_seed, seeds)
    print(md)

    # Optional JSON output
    if args.json_out:
        output = {
            "seeds": seeds,
            "form": args.form,
            "voices": args.voices,
            "aggregate": agg,
            "per_seed": per_seed,
        }
        Path(args.json_out).write_text(json.dumps(output, indent=2))
        print(f"JSON written to {args.json_out}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
