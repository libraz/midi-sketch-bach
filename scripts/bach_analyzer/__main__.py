"""CLI entry point: python -m bach_analyzer validate/batch/analyze."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Optional, Set

from .batch import parse_seed_range, run_batch
from .diagnosis import compute_dissonance_diagnostics
from .report import (format_batch_text, format_diagnostics_text, format_json,
                     format_score_json, format_score_text, format_text)
from .runner import load_score, overall_passed, validate
from .form_profile import get_form_profile
from .score import compute_score
from .stats import compute_stats, format_stats_json, format_stats_text


def _parse_categories(cats_str: Optional[str]) -> Optional[Set[str]]:
    if not cats_str:
        return None
    return set(c.strip() for c in cats_str.split(","))


def _parse_bar_range(br_str: Optional[str]) -> Optional[tuple]:
    if not br_str:
        return None
    parts = br_str.split("-")
    if len(parts) == 2:
        return (int(parts[0]), int(parts[1]))
    return None


def cmd_validate(args: argparse.Namespace) -> int:
    """Run single-file validation."""
    score = load_score(args.input)
    categories = _parse_categories(args.rules)
    bar_range = _parse_bar_range(args.bar_range)
    results = validate(score, categories=categories, bar_range=bar_range)

    if args.json:
        output = format_json(score, results)
    else:
        output = format_text(score, results)

    if args.output:
        Path(args.output).write_text(output)
    else:
        print(output)

    return 0 if overall_passed(results) else 1


def cmd_batch(args: argparse.Namespace) -> int:
    """Run batch validation."""
    seeds = parse_seed_range(args.seeds)
    keys = args.keys.split(",") if args.keys else None
    categories = _parse_categories(args.rules)

    def progress(seed, idx, total):
        if not args.json:
            print(f"\r  [{idx + 1}/{total}] seed={seed}...", end="", flush=True)

    diagnostics = getattr(args, "diagnostics", False)

    results = run_batch(
        seeds=seeds,
        form=args.form,
        voices=args.voices,
        keys=keys,
        cli_path=args.cli_path,
        categories=categories,
        on_progress=progress,
        diagnostics=diagnostics,
    )

    if not args.json:
        print()  # Clear progress line

    # Compute diagnostics if requested.
    diag = None
    if diagnostics:
        diag = [
            compute_dissonance_diagnostics(results, "strong_beat_dissonance"),
            compute_dissonance_diagnostics(results, "unresolved_dissonance"),
        ]

    if args.json:
        if diag:
            output_data = {"results": results, "diagnostics": diag}
            output = json.dumps(output_data, indent=2)
        else:
            output = json.dumps(results, indent=2)
    else:
        output = format_batch_text(
            results,
            form=args.form,
            seed_range=args.seeds,
            voices=args.voices,
        )
        if diag:
            output += "\n" + format_diagnostics_text(diag)

    if args.output:
        Path(args.output).write_text(output)
    else:
        print(output)

    passed = sum(1 for r in results if r.get("overall_passed", False))
    return 0 if passed == len(results) else 1


def cmd_stats(args: argparse.Namespace) -> int:
    """Show per-track/per-bar statistics."""
    score = load_score(args.input)
    data = compute_stats(score)

    if args.json:
        output = format_stats_json(data)
    else:
        output = format_stats_text(data)

    if args.output:
        Path(args.output).write_text(output)
    else:
        print(output)

    return 0


def cmd_analyze(args: argparse.Namespace) -> int:
    """Legacy-compatible analyze command (alias for validate)."""
    args.rules = None
    args.bar_range = None
    args.json = getattr(args, "json", False)
    args.output = getattr(args, "output", None)
    return cmd_validate(args)


def cmd_score(args: argparse.Namespace) -> int:
    """Compute Bach reference score."""
    score = load_score(args.input)
    form = args.form or score.form
    profile = get_form_profile(form)

    if not profile.reference_category:
        print(f"Error: No reference category for form '{form}'", file=sys.stderr)
        return 1

    # Run validation for penalty integration
    results = validate(score)

    bach_score = compute_score(
        score,
        category=profile.reference_category,
        counterpoint_enabled=profile.counterpoint_enabled,
        results=results,
    )

    if args.json:
        output = format_score_json(bach_score)
    else:
        output = format_score_text(bach_score)

    if args.output:
        Path(args.output).write_text(output)
    else:
        print(output)

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="bach_analyzer",
        description="Bach MIDI Generator - Comprehensive Validation Tool",
    )
    subparsers = parser.add_subparsers(dest="command", help="Command")

    # validate
    p_val = subparsers.add_parser("validate", help="Validate a single file")
    p_val.add_argument("input", help="Path to output.json or .mid file")
    p_val.add_argument("--rules", help="Comma-separated rule categories")
    p_val.add_argument("--bar-range", help="Bar range (e.g. 10-20)")
    p_val.add_argument("--json", action="store_true", help="JSON output")
    p_val.add_argument("-o", "--output", help="Output file path")

    # batch
    p_batch = subparsers.add_parser("batch", help="Batch validation across seeds")
    p_batch.add_argument("--seeds", required=True, help="Seed range (e.g. 1-50)")
    p_batch.add_argument("--form", default="fugue", help="Form type")
    p_batch.add_argument("--voices", type=int, help="Number of voices")
    p_batch.add_argument("--keys", help="Comma-separated keys")
    p_batch.add_argument("--rules", help="Comma-separated rule categories")
    p_batch.add_argument("--cli-path", default="./build/bin/bach_cli", help="Path to bach_cli")
    p_batch.add_argument("--json", action="store_true", help="JSON output")
    p_batch.add_argument("--diagnostics", action="store_true",
                         help="Include source/interval breakdown for dissonance violations")
    p_batch.add_argument("-o", "--output", help="Output file path")

    # stats
    p_stats = subparsers.add_parser("stats", help="Show per-track/per-bar statistics")
    p_stats.add_argument("input", help="Path to output.json or .mid file")
    p_stats.add_argument("--json", action="store_true", help="JSON output")
    p_stats.add_argument("-o", "--output", help="Output file path")

    # analyze (legacy compat)
    p_ana = subparsers.add_parser("analyze", help="Analyze (legacy, alias for validate)")
    p_ana.add_argument("input", help="Path to output.json")
    p_ana.add_argument("--json", action="store_true", help="JSON output")
    p_ana.add_argument("-o", "--output", help="Output file path")

    # score
    p_score = subparsers.add_parser("score", help="Compute Bach reference score")
    p_score.add_argument("input", help="Path to output.json or .mid file")
    p_score.add_argument("--form", help="Override form type")
    p_score.add_argument("--json", action="store_true", help="JSON output")
    p_score.add_argument("-o", "--output", help="Output file path")

    args = parser.parse_args()

    if args.command == "validate":
        return cmd_validate(args)
    elif args.command == "batch":
        return cmd_batch(args)
    elif args.command == "stats":
        return cmd_stats(args)
    elif args.command == "analyze":
        return cmd_analyze(args)
    elif args.command == "score":
        return cmd_score(args)
    else:
        parser.print_help()
        return 0


if __name__ == "__main__":
    sys.exit(main())
