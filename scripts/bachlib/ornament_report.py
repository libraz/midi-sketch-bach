#!/usr/bin/env python3
"""Ornament distribution report: where ornament notes land across a piece.

Reads an index-parallel ``generated.v1`` / ``provenance.v1`` JSON pair, picks
the notes whose provenance ``source`` is ``"Ornament"``, and reports their
bar-position distribution: a per-bar histogram, the per-quarter (piece-relative
25% segments) counts, and the cadence-window share. The sweep mode drives
``bach_cli`` over a form x character x seed grid so the distribution of a whole
family can be checked in one command.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import Counter
from pathlib import Path

from bachlib.common import REPO_ROOT

DEFAULT_CLI = REPO_ROOT / "build" / "bin" / "bach_cli"

# 3/4 forms use 1440-tick bars; every other form is 4/4 (1920 ticks).
TICKS_PER_BAR_34 = 1440
TICKS_PER_BAR_44 = 1920
FORM_TICKS_PER_BAR = {
    "passacaglia": TICKS_PER_BAR_34,
    "chaconne": TICKS_PER_BAR_34,
}

DEFAULT_FORMS = (
    "toccata_and_fugue",
    "fantasia_and_fugue",
    "passacaglia",
    "chorale_prelude",
)
DEFAULT_CHARACTERS = ("severe", "playful", "noble", "restless")


def ticks_per_bar_for_form(form: str) -> int:
    """Bar length in ticks for a form (3/4 forms are 1440, the rest 1920)."""
    return FORM_TICKS_PER_BAR.get(form, TICKS_PER_BAR_44)


def ornament_bars(generated: dict, provenance: dict, ticks_per_bar: int) -> list[int]:
    """Bar index of every ornament-sourced note.

    The two documents are index-parallel: ``provenance.notes[i]`` describes
    ``generated.notes[i]``. Provenance carries the source tag; generated
    carries the tick position.
    """
    gnotes = generated["notes"]
    bars: list[int] = []
    for pnote in provenance["notes"]:
        if pnote.get("source") != "Ornament":
            continue
        index = pnote["index"]
        bars.append(gnotes[index]["start_tick"] // ticks_per_bar)
    bars.sort()
    return bars


def distribution_summary(bars: list[int], total_bars: int) -> dict:
    """Summarise an ornament bar list against the piece length.

    Returns the total ornament-note count, a per-bar histogram, the counts per
    piece-relative quarter (0-25%, 25-50%, 50-75%, 75-100% of the bars), the
    count inside the middle 50% (bars in [25%, 75%)), and the count inside the
    final cadence window (the last two bars).
    """
    histogram = Counter(bars)
    quarter_counts = [0, 0, 0, 0]
    if total_bars > 0:
        for bar in bars:
            quarter = min(3, (bar * 4) // total_bars)
            quarter_counts[quarter] += 1
    cadence_start = max(0, total_bars - 2)
    cadence_count = sum(1 for bar in bars if bar >= cadence_start)
    return {
        "ornament_notes": len(bars),
        "total_bars": total_bars,
        "bar_histogram": {str(bar): count for bar, count in sorted(histogram.items())},
        "quarter_counts": quarter_counts,
        "middle_half_count": quarter_counts[1] + quarter_counts[2],
        "cadence_window_count": cadence_count,
        "most_common_bar": histogram.most_common(1)[0][0] if histogram else None,
    }


def report_for_files(generated_path: Path, provenance_path: Path, ticks_per_bar: int) -> dict:
    """Build a distribution summary for one generated/provenance pair."""
    with generated_path.open() as handle:
        generated = json.load(handle)
    with provenance_path.open() as handle:
        provenance = json.load(handle)
    total_bars = (generated["duration_ticks"] + ticks_per_bar - 1) // ticks_per_bar
    bars = ornament_bars(generated, provenance, ticks_per_bar)
    return distribution_summary(bars, total_bars)


def run_case(cli: Path, form: str, character: str, seed: int, work_dir: Path) -> dict:
    """Generate one (form, character, seed) piece and summarise its ornaments."""
    midi_path = work_dir / f"{form}_{character}_{seed}.mid"
    cmd = [
        str(cli),
        "--form",
        form,
        "--character",
        character,
        "--seed",
        str(seed),
        "--generated-json",
        "-o",
        str(midi_path),
    ]
    completed = subprocess.run(
        cmd,
        cwd=REPO_ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    case: dict = {"form": form, "character": character, "seed": seed}
    if completed.returncode != 0:
        case["error"] = completed.stderr.strip()
        return case
    generated_path = midi_path.with_suffix(".generated.json")
    provenance_path = midi_path.with_suffix(".provenance.json")
    case.update(report_for_files(generated_path, provenance_path, ticks_per_bar_for_form(form)))
    return case


def _print_case(case: dict) -> None:
    label = f"{case['form']} {case['character']} seed={case['seed']}"
    if "error" in case:
        print(f"{label}: ERROR {case['error']}")
        return
    quarters = "/".join(str(count) for count in case["quarter_counts"])
    print(
        f"{label}: ornaments={case['ornament_notes']} bars={case['total_bars']} "
        f"quarters={quarters} middle_half={case['middle_half_count']} "
        f"cadence_window={case['cadence_window_count']}"
    )


def register(subparsers) -> None:
    """Attach the `ornament-report` subcommand to `subparsers`."""
    parser = subparsers.add_parser(
        "ornament-report",
        help="ornament bar-distribution report (single pair or form sweep)",
        description=__doc__,
    )
    parser.add_argument("--generated", type=Path, help="generated.v1 JSON path (file mode)")
    parser.add_argument("--provenance", type=Path, help="provenance.v1 JSON path (file mode)")
    parser.add_argument(
        "--ticks-per-bar",
        type=int,
        default=None,
        help="bar length in ticks for file mode (default 1920; 3/4 forms are 1440)",
    )
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--forms", nargs="+", default=list(DEFAULT_FORMS))
    parser.add_argument("--characters", nargs="+", default=list(DEFAULT_CHARACTERS))
    parser.add_argument(
        "--seeds",
        nargs="+",
        type=int,
        default=[1, 2, 3, 4, 5],
        help="sweep seeds (seed 0 is CLI auto; use seeds >= 1)",
    )
    parser.add_argument("--out", type=Path, help="write the full report JSON here")
    parser.set_defaults(func=run)


def run(args) -> int:
    """File mode when --generated/--provenance are given; sweep mode otherwise."""
    if args.generated or args.provenance:
        if not (args.generated and args.provenance):
            print("file mode needs both --generated and --provenance")
            return 2
        ticks_per_bar = args.ticks_per_bar or TICKS_PER_BAR_44
        summary = report_for_files(args.generated, args.provenance, ticks_per_bar)
        print(json.dumps(summary, indent=2))
        if args.out:
            args.out.write_text(json.dumps(summary, indent=2) + "\n")
        return 0

    cases: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="ornament_report_") as tmp:
        work_dir = Path(tmp)
        for form in args.forms:
            for character in args.characters:
                for seed in args.seeds:
                    case = run_case(args.cli, form, character, seed, work_dir)
                    cases.append(case)
                    _print_case(case)

    ok_cases = [case for case in cases if "error" not in case]
    report = {
        "cases": cases,
        "summary": {
            "total": len(cases),
            "generated": len(ok_cases),
            "cases_with_middle_half": sum(1 for c in ok_cases if c["middle_half_count"] >= 1),
            "cases_with_ornaments": sum(1 for c in ok_cases if c["ornament_notes"] >= 1),
        },
    }
    print(
        f"summary: generated={report['summary']['generated']}/{report['summary']['total']} "
        f"with_middle_half={report['summary']['cases_with_middle_half']} "
        f"with_ornaments={report['summary']['cases_with_ornaments']}"
    )
    if args.out:
        args.out.write_text(json.dumps(report, indent=2) + "\n")
    return 0
