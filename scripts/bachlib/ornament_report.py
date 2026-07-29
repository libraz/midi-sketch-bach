#!/usr/bin/env python3
"""Ornament distribution report: where ornament notes land across a piece.

Reads an index-parallel ``generated.v1`` / ``provenance.v1`` JSON pair, picks
the notes whose provenance ``source`` is ``"Ornament"``, and reports their
bar-position distribution: a per-bar histogram, the per-quarter (piece-relative
25% segments) counts, and the cadence-window share. Contiguous ornament runs
are additionally classified by shape into the ornament vocabulary (trill /
mordent / turn / appoggiatura / slide) with per-kind counts and a
cadence-window-trill flag. The sweep mode drives ``bach_cli`` over a form x
character x seed grid so the distribution of a whole family can be checked in
one command. The generated.v1 / provenance.v1 schemas are untouched: the
classification is derived purely from run shapes.
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


THIRTY_SECOND_TICKS = 60

ORNAMENT_KINDS = ("trill", "mordent", "turn", "appoggiatura", "slide", "unknown")


def ornament_runs(generated: dict, provenance: dict) -> list[list[dict]]:
    """Contiguous same-voice ornament runs, in (voice, start_tick) order.

    A run is a maximal chain of Ornament-sourced notes within one voice where
    each note starts exactly where the previous one ends -- the shape the
    ornament pass emits when it expands a single candidate note.
    """
    gnotes = generated["notes"]
    by_voice: dict[int, list[dict]] = {}
    for pnote in provenance["notes"]:
        if pnote.get("source") != "Ornament":
            continue
        note = gnotes[pnote["index"]]
        by_voice.setdefault(note["voice"], []).append(note)
    runs: list[list[dict]] = []
    for voice in sorted(by_voice):
        notes = sorted(by_voice[voice], key=lambda n: n["start_tick"])
        run = [notes[0]]
        for note in notes[1:]:
            if note["start_tick"] == run[-1]["start_tick"] + run[-1]["duration"]:
                run.append(note)
            else:
                runs.append(run)
                run = [note]
        runs.append(run)
    return runs


def _step_down(high: int, low: int) -> bool:
    """True when ``high`` sits a diatonic step (1-2 semitones) above ``low``."""
    return 0 < high - low <= 2


def _match_turn(run: list[dict], i: int) -> int:
    """Turn: upper - main - lower in 32nd graces, then a longer main tone."""
    if i + 3 >= len(run):
        return 0
    a, b, c, d = run[i : i + 4]
    if not (a["duration"] == b["duration"] == c["duration"] == THIRTY_SECOND_TICKS):
        return 0
    if d["duration"] <= a["duration"]:
        return 0  # a 4-slot trill shares the contour but has no held tail.
    if _step_down(a["pitch"], b["pitch"]) and _step_down(b["pitch"], c["pitch"]) and d[
        "pitch"
    ] == b["pitch"]:
        return 4
    return 0


def _match_trill(run: list[dict], i: int) -> int:
    """Trill: optional opening (appuy held upper tone / von-unten lower->main
    prefix), then >= 4 upper/main alternation slots ending in the
    lower-neighbour Nachschlag and the resolved main tone."""
    n = len(run)
    j = i
    if j + 1 < n and run[j]["duration"] > 120 and _step_down(run[j]["pitch"], run[j + 1]["pitch"]):
        # Appuy: a held upper tone; the alternation resumes on the main tone.
        upper, main = run[j]["pitch"], run[j + 1]["pitch"]
        j += 1
        expect = main
    elif (
        j + 2 < n
        and run[j]["duration"] <= 120
        and _step_down(run[j + 1]["pitch"], run[j]["pitch"])
        and _step_down(run[j + 2]["pitch"], run[j + 1]["pitch"])
    ):
        # Von-unten: lower -> main prefix; the alternation starts on the upper.
        main, upper = run[j + 1]["pitch"], run[j + 2]["pitch"]
        j += 2
        expect = upper
    elif j + 1 < n and run[j]["duration"] <= 120 and _step_down(run[j]["pitch"],
                                                                run[j + 1]["pitch"]):
        # Plain upper start.
        upper, main = run[j]["pitch"], run[j + 1]["pitch"]
        expect = upper
    else:
        return 0
    alt_start = j
    while j < n and run[j]["pitch"] == expect:
        expect = main if expect == upper else upper
        j += 1
    # Nachschlag tail: the lower neighbour, then the resolved main tone.
    if j + 1 >= n or not _step_down(main, run[j]["pitch"]) or run[j + 1]["pitch"] != main:
        return 0
    if (j + 2) - alt_start < 4:
        return 0
    return (j + 2) - i


def _match_slide(run: list[dict], i: int) -> int:
    """Slide: two rising 32nd graces, then the held arrival tone."""
    if i + 2 >= len(run):
        return 0
    a, b, c = run[i : i + 3]
    if not (a["duration"] == b["duration"] == THIRTY_SECOND_TICKS):
        return 0
    if c["duration"] <= THIRTY_SECOND_TICKS:
        return 0
    if _step_down(b["pitch"], a["pitch"]) and _step_down(c["pitch"], b["pitch"]):
        return 3
    return 0


def _match_mordent(run: list[dict], i: int) -> int:
    """Mordent: main - lower - main with two equal short tones and a tail."""
    if i + 2 >= len(run):
        return 0
    a, b, c = run[i : i + 3]
    if a["duration"] != b["duration"] or c["duration"] < a["duration"]:
        return 0
    if a["pitch"] == c["pitch"] and _step_down(a["pitch"], b["pitch"]):
        return 3
    return 0


def _match_appoggiatura(run: list[dict], i: int) -> int:
    """Appoggiatura: a long lean a step above, resolving to the main tone.

    The pass emits leans of half (two thirds when dotted) the candidate's
    value, and candidates go down to eighth notes -- so the smallest real lean
    is 120/120; anything at grace (32nd) length is a trill/turn fragment, not
    a lean."""
    if i + 1 >= len(run):
        return 0
    a, b = run[i : i + 2]
    if a["duration"] < 120 or b["duration"] < 2 * THIRTY_SECOND_TICKS:
        return 0
    if _step_down(a["pitch"], b["pitch"]):
        return 2
    return 0


_MATCHERS = (
    ("turn", _match_turn),
    ("trill", _match_trill),
    ("slide", _match_slide),
    ("mordent", _match_mordent),
    ("appoggiatura", _match_appoggiatura),
)


def segment_run(run: list[dict]) -> list[tuple[str, list[dict]]]:
    """Parse one contiguous run into a sequence of ornament figures.

    The ornament pass gates per (bar, voice), so adjacent expansions in one
    bar abut and merge into a single Ornament-sourced run: a run is a SEQUENCE
    of figures, not necessarily one. The parser matches greedily from the
    left (turn before trill so the 4-slot contour with a held tail stays a
    turn); an unmatched note is consumed as a single "unknown" figure.
    """
    figures: list[tuple[str, list[dict]]] = []
    i = 0
    while i < len(run):
        for kind, matcher in _MATCHERS:
            length = matcher(run, i)
            if length:
                figures.append((kind, run[i : i + length]))
                i += length
                break
        else:
            figures.append(("unknown", [run[i]]))
            i += 1
    return figures


def classify_run(run: list[dict]) -> str:
    """Single-shape view of a run: the figure kind when the whole run parses
    to one kind, "compound" when several kinds mix, "unknown" otherwise."""
    figures = segment_run(run)
    kinds = {kind for kind, _ in figures}
    if len(kinds) == 1:
        return figures[0][0]
    return "compound"


def kind_summary(runs: list[list[dict]], total_bars: int, ticks_per_bar: int) -> dict:
    """Per-kind figure counts plus the cadence-window-trill flag.

    ``cadence_window_trill`` is true when at least one trill figure starts
    inside the final two bars (the mandatory cadential trill site).
    """
    figures = [figure for run in runs for figure in segment_run(run)]
    counts = Counter(kind for kind, _ in figures)
    cadence_start_tick = max(0, total_bars - 2) * ticks_per_bar
    cadence_trill = any(
        kind == "trill" and notes[0]["start_tick"] >= cadence_start_tick
        for kind, notes in figures
    )
    return {
        "kind_counts": {kind: counts.get(kind, 0) for kind in ORNAMENT_KINDS if counts.get(kind)},
        "cadence_window_trill": cadence_trill,
    }


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
        "bar_histogram": {str(bar + 1): count for bar, count in sorted(histogram.items())},
        "quarter_counts": quarter_counts,
        "middle_half_count": quarter_counts[1] + quarter_counts[2],
        "cadence_window_count": cadence_count,
        "most_common_bar": histogram.most_common(1)[0][0] + 1 if histogram else None,
    }


def report_for_files(generated_path: Path, provenance_path: Path, ticks_per_bar: int) -> dict:
    """Build a distribution + kind summary for one generated/provenance pair."""
    with generated_path.open() as handle:
        generated = json.load(handle)
    with provenance_path.open() as handle:
        provenance = json.load(handle)
    total_bars = (generated["duration_ticks"] + ticks_per_bar - 1) // ticks_per_bar
    bars = ornament_bars(generated, provenance, ticks_per_bar)
    summary = distribution_summary(bars, total_bars)
    summary.update(kind_summary(ornament_runs(generated, provenance), total_bars, ticks_per_bar))
    return summary


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
    kinds = ",".join(f"{kind}={count}" for kind, count in case["kind_counts"].items()) or "none"
    print(
        f"{label}: ornaments={case['ornament_notes']} bars={case['total_bars']} "
        f"quarters={quarters} middle_half={case['middle_half_count']} "
        f"cadence_window={case['cadence_window_count']} kinds=[{kinds}] "
        f"cadence_trill={'yes' if case['cadence_window_trill'] else 'NO'}"
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
    kind_totals: Counter = Counter()
    for case in ok_cases:
        kind_totals.update(case["kind_counts"])
    report = {
        "cases": cases,
        "summary": {
            "total": len(cases),
            "generated": len(ok_cases),
            "cases_with_middle_half": sum(1 for c in ok_cases if c["middle_half_count"] >= 1),
            "cases_with_ornaments": sum(1 for c in ok_cases if c["ornament_notes"] >= 1),
            "cases_with_cadence_trill": sum(1 for c in ok_cases if c["cadence_window_trill"]),
            "kind_totals": dict(kind_totals),
        },
    }
    print(
        f"summary: generated={report['summary']['generated']}/{report['summary']['total']} "
        f"with_middle_half={report['summary']['cases_with_middle_half']} "
        f"with_ornaments={report['summary']['cases_with_ornaments']} "
        f"with_cadence_trill={report['summary']['cases_with_cadence_trill']} "
        f"kinds={report['summary']['kind_totals']}"
    )
    if args.out:
        args.out.write_text(json.dumps(report, indent=2) + "\n")
    return 0
