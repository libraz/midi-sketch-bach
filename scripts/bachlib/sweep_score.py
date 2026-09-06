#!/usr/bin/env python3
"""Score a form x character x key x seed sweep and report per-form structure metrics.

The counterpoint gates (texture-gate, closure) answer "is this piece legal".
This command answers "is this piece music": how far the tonal plan travels, how
literally material returns, whether the bass walks, and how much performance
information reaches the output. Those axes move slowly and across every form at
once, so they need a report that is reproducible between sessions rather than a
throwaway script.

Two families of numbers are collected per piece:

  - corpus-calibrated, from ``bach-mcp score`` -- the length-invariant model
    probability, the six KL components' excess over length-matched Bach, and
    the counterpoint corpus excess distance
  - structural, computed here from the generated.v1 payload -- out-of-key share,
    exact-duplicate bar share, bass stepwise share, velocity level count and
    note-gap share

Pitches in generated.v1 are the internal C-based representation (``--key`` only
transposes the MIDI file), so the out-of-key share is counted against C.

Aggregation is the median over the accepted configurations of a form, which
keeps one rejected or extreme seed from moving the row. Configurations the
composer refuses are counted and listed rather than silently dropped: a change
in the rejection set is itself a result.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from bachlib.common import (
    DEFAULT_CLI,
    DEFAULT_INDEX_JS,
    REPO_ROOT,
    run as run_process,
    score_generated,
)

TICKS_PER_BAR = 1920

FORMS = (
    "fugue",
    "prelude_and_fugue",
    "trio_sonata",
    "chorale_prelude",
    "toccata_and_fugue",
    "passacaglia",
    "fantasia_and_fugue",
    "cello_prelude",
    "chaconne",
    "goldberg_variations",
)

CHARACTERS = ("severe", "playful", "noble", "restless")

# Only the mode of the key reaches the internal representation; the tonic is
# applied when the MIDI file is written. One key per mode is therefore the whole
# sweep, not a sample of it.
KEYS = ("c_major", "c_minor")

# The seeds the structural baseline was taken on. Spread over three magnitudes
# so a form whose archetype or palette is chosen by a small modulus of the seed
# reaches more than one branch.
DEFAULT_SEEDS = (1, 2, 3, 5, 7, 42, 99, 123)

# Diatonic pitch classes relative to the tonic. The minor set admits both the
# raised seventh of the harmonic minor and the natural seventh, because a
# descending melodic line lowers it as a matter of course and counting that as
# a departure from the key would report every minor piece as chromatic.
MAJOR_SCALE = frozenset({0, 2, 4, 5, 7, 9, 11})
MINOR_SCALE = frozenset({0, 2, 3, 5, 7, 8, 10, 11})

# The KL components bach-mcp's corpus model decomposes into, in the order the
# report prints them.
KL_COMPONENTS = (
    "pitch_class",
    "melodic_interval",
    "melodic_interval_bigram",
    "duration",
    "beat_position",
    "vertical_interval_class",
)


@dataclass
class CaseResult:
    """One generated (form, character, key, seed) configuration.

    @ivar form Form name passed to ``--form``.
    @ivar character Character name passed to ``--character``.
    @ivar key Key name passed to ``--key``.
    @ivar seed Seed passed to ``--seed``.
    @ivar rejected Whether the composer refused the configuration.
    @ivar reason Refusal reason when rejected, else None.
    @ivar metrics Metric name to value for an accepted configuration.
    """

    form: str
    character: str
    key: str
    seed: int
    rejected: bool = False
    reason: str | None = None
    metrics: dict[str, float] = field(default_factory=dict)

    @property
    def label(self) -> str:
        """Configuration identity as it appears in the rejection list."""
        return f"{self.form}/{self.character}/{self.key}/seed{self.seed}"


def _voices(notes: list[dict[str, Any]]) -> dict[int, list[dict[str, Any]]]:
    """Group notes by voice, each list sorted by onset then pitch."""
    out: dict[int, list[dict[str, Any]]] = {}
    for note in notes:
        out.setdefault(int(note["voice"]), []).append(note)
    for voice_notes in out.values():
        voice_notes.sort(key=lambda note: (note["start_tick"], note["pitch"]))
    return out


def out_of_key_ratio(notes: list[dict[str, Any]], scale: frozenset[int]) -> float:
    """Share of notes whose pitch class lies outside the tonic scale."""
    if not notes:
        return 0.0
    outside = sum(1 for note in notes if int(note["pitch"]) % 12 not in scale)
    return outside / len(notes)


def _bar_signature(voice_notes: list[dict[str, Any]], bar: int) -> tuple:
    """Pitch / offset / duration content of one bar of one voice.

    A bar is identified by what sounds in it, so two bars match only when every
    note agrees on pitch, position within the bar and length. Notes are keyed by
    their onset bar; a note sustained across the barline belongs to the bar it
    starts in.
    """
    start = bar * TICKS_PER_BAR
    end = start + TICKS_PER_BAR
    return tuple(
        (int(note["pitch"]), int(note["start_tick"]) - start, int(note["duration"]))
        for note in voice_notes
        if start <= int(note["start_tick"]) < end
    )


def duplicate_bar_ratios(notes: list[dict[str, Any]], duration_ticks: int) -> tuple[float, float]:
    """Share of bars that are byte-identical to another bar.

    @return ``(all_voices, worst_voice)``. The first treats the full texture of
        a bar as the unit; the second is the largest per-voice share, which is
        where a returning ground or a copied countersubject shows up.
    """
    bar_count = max(1, -(-duration_ticks // TICKS_PER_BAR))
    voices = _voices(notes)

    combined: list[tuple] = []
    for bar in range(bar_count):
        # Silent voices are dropped rather than recorded as empty, so a bar in
        # which nothing sounds collapses to the empty signature that
        # _repeat_share excludes.
        sounding = [_bar_signature(voices[voice], bar) for voice in sorted(voices)]
        combined.append(tuple(sorted(signature for signature in sounding if signature)))
    all_voices = _repeat_share(combined)

    worst = 0.0
    for voice in sorted(voices):
        bars = [_bar_signature(voices[voice], bar) for bar in range(bar_count)]
        worst = max(worst, _repeat_share(bars))
    return all_voices, worst


def _repeat_share(bars: list[tuple]) -> float:
    """Share of entries that are equal to at least one other entry.

    Empty bars are excluded: a voice that rests through half the piece would
    otherwise report those rests as literal repetition.
    """
    sounding = [bar for bar in bars if bar]
    if not sounding:
        return 0.0
    seen: dict[tuple, int] = {}
    for bar in sounding:
        seen[bar] = seen.get(bar, 0) + 1
    repeated = sum(count for count in seen.values() if count > 1)
    return repeated / len(sounding)


def _bass_voice(voices: dict[int, list[dict[str, Any]]]) -> list[dict[str, Any]]:
    """The voice with the lowest mean pitch, or an empty list when there is none.

    Chosen by sounding register rather than by voice index, because the index
    ordering is a form's own convention and is not the same in every builder.
    """
    best: list[dict[str, Any]] = []
    best_mean = 1e9
    for voice in sorted(voices):
        voice_notes = voices[voice]
        if not voice_notes:
            continue
        mean = statistics.fmean(int(note["pitch"]) for note in voice_notes)
        if mean < best_mean:
            best_mean = mean
            best = voice_notes
    return best


def stepwise_ratio(voice_notes: list[dict[str, Any]]) -> float:
    """Share of consecutive intervals that move by a step (1 or 2 semitones).

    Repeated notes are counted in the denominator but are not steps, so a line
    that reiterates one pitch reads as unmelodic rather than as neutral.
    """
    if len(voice_notes) < 2:
        return 0.0
    steps = 0
    for prev, curr in zip(voice_notes, voice_notes[1:]):
        if 1 <= abs(int(curr["pitch"]) - int(prev["pitch"])) <= 2:
            steps += 1
    return steps / (len(voice_notes) - 1)


def note_gap_ratio(voices: dict[int, list[dict[str, Any]]]) -> float:
    """Share of consecutive note pairs separated by silence within a voice.

    This reads generated.v1, which carries notated lengths, so the silence it
    finds is a rest the form wrote -- not the release a player takes between two
    joined notes. Touch is applied to the rendered output and is measured there;
    a value near zero here says the voice writes few rests, not that the piece
    is played legato.
    """
    pairs = 0
    gaps = 0
    for voice_notes in voices.values():
        for prev, curr in zip(voice_notes, voice_notes[1:]):
            pairs += 1
            if int(curr["start_tick"]) > int(prev["start_tick"]) + int(prev["duration"]):
                gaps += 1
    return gaps / pairs if pairs else 0.0


def structural_metrics(generated: dict[str, Any], key: str) -> dict[str, float]:
    """Compute the structure axes of one generated.v1 payload."""
    notes = generated.get("notes", [])
    scale = MINOR_SCALE if key.endswith("minor") else MAJOR_SCALE
    voices = _voices(notes)
    all_voices, worst_voice = duplicate_bar_ratios(
        notes, int(generated.get("duration_ticks", 0))
    )
    velocities = {int(note.get("velocity", 0)) for note in notes}
    return {
        "out_of_key": out_of_key_ratio(notes, scale),
        "duplicate_bars": all_voices,
        "duplicate_bars_worst_voice": worst_voice,
        "bass_stepwise": stepwise_ratio(_bass_voice(voices)),
        "velocity_levels": float(len(velocities)),
        "note_gaps": note_gap_ratio(voices),
        "note_count": float(len(notes)),
        "voice_count": float(len(voices)),
    }


def corpus_metrics(score: dict[str, Any]) -> dict[str, float]:
    """Extract the corpus-calibrated axes from a bach-mcp score payload."""
    model = score.get("model_score_v2") or {}
    components = model.get("components") or {}
    out: dict[str, float] = {
        "prob_li": float(model.get("probability_length_invariant", -1.0)),
        "heuristic": float(score.get("score", 0.0)),
        "excess_distance": float(
            ((score.get("counterpoint") or {}).get("corpus") or {}).get("excess_distance", 0.0)
        ),
    }
    for name in KL_COMPONENTS:
        component = components.get(name) or {}
        out[f"kl_{name}"] = float(component.get("kl_excess_vs_length_matched_bach", 0.0))
    return out


def run_case(
    cli: Path,
    index_js: Path,
    work_dir: Path,
    form: str,
    character: str,
    key: str,
    seed: int,
) -> CaseResult:
    """Generate and score one configuration."""
    result = CaseResult(form=form, character=character, key=key, seed=seed)
    case_dir = work_dir / f"{form}_{character}_{key}_{seed}"
    case_dir.mkdir(parents=True, exist_ok=True)
    midi = case_dir / "case.mid"
    cmd = [
        str(cli),
        "--form",
        form,
        "--character",
        character,
        "--key",
        key,
        "--seed",
        str(seed),
        "--generated-json",
        "-o",
        str(midi),
    ]
    proc = run_process(cmd, cwd=REPO_ROOT)
    if proc.returncode != 0:
        result.rejected = True
        result.reason = (proc.stderr.strip() or proc.stdout.strip() or "").splitlines()
        result.reason = result.reason[-1] if result.reason else f"exit {proc.returncode}"
        return result

    generated_json = midi.with_suffix(".generated.json")
    try:
        with generated_json.open(encoding="utf-8") as handle:
            generated = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        result.rejected = True
        result.reason = f"unreadable generated JSON: {exc}"
        return result

    result.metrics.update(structural_metrics(generated, key))
    try:
        result.metrics.update(corpus_metrics(score_generated(index_js, generated_json)))
    except RuntimeError as exc:
        result.rejected = True
        result.reason = f"scorer failed: {exc}"
    return result


def _median(values: list[float]) -> float:
    return statistics.median(values) if values else 0.0


def aggregate(cases: list[CaseResult]) -> dict[str, Any]:
    """Collapse the per-case results into the report payload.

    The per-form row is the median over that form's accepted configurations.
    The out-of-key axis is additionally split by mode, because the two modes
    have different scale sizes and averaging them hides which one is flat.
    """
    forms = sorted({case.form for case in cases})
    metric_names = sorted({name for case in cases for name in case.metrics})
    per_form: dict[str, Any] = {}
    for form in forms:
        accepted = [case for case in cases if case.form == form and not case.rejected]
        rejected = [case for case in cases if case.form == form and case.rejected]
        row: dict[str, Any] = {
            "accepted": len(accepted),
            "rejected": len(rejected),
            "metrics": {
                name: round(_median([case.metrics[name] for case in accepted if name in case.metrics]), 6)
                for name in metric_names
            },
        }
        for mode_key in sorted({case.key for case in cases}):
            subset = [case for case in accepted if case.key == mode_key]
            row.setdefault("out_of_key_by_key", {})[mode_key] = round(
                _median([case.metrics["out_of_key"] for case in subset if "out_of_key" in case.metrics]),
                6,
            )
        per_form[form] = row
    return {
        "schema_version": "bach-sweep-score.v1",
        "case_count": len(cases),
        "accepted": sum(1 for case in cases if not case.rejected),
        "rejected": sorted(case.label for case in cases if case.rejected),
        "forms": per_form,
    }


def _pct(value: float) -> str:
    return f"{value * 100:.1f}"


def _pct_delta(value: float) -> str:
    """Percentage difference, always signed, so a zero row reads as unchanged."""
    return f"{value * 100:+.1f}"


def format_report(report: dict[str, Any], baseline: dict[str, Any] | None) -> str:
    """Render the human-readable report, optionally as a delta against a baseline."""
    lines: list[str] = []
    lines.append(
        f"sweep: {report['case_count']} configurations, "
        f"{report['accepted']} accepted, {len(report['rejected'])} rejected"
    )
    lines.append("")

    keys = sorted(
        {key for row in report["forms"].values() for key in row.get("out_of_key_by_key", {})}
    )
    header = f"{'form':<20} " + " ".join(f"{'ook:' + key.split('_')[-1][:3]:>9}" for key in keys)
    header += f" {'dup%':>7} {'dupV%':>7} {'bass%':>7} {'vel':>4} {'gap%':>7} {'probLI':>8} {'exc':>6}"
    lines.append(header)
    lines.append("-" * len(header))

    base_forms = (baseline or {}).get("forms", {})
    for form in sorted(report["forms"]):
        row = report["forms"][form]
        metrics = row["metrics"]
        base = base_forms.get(form, {}).get("metrics") if baseline else None
        cells = [f"{form:<20}"]
        for key in keys:
            cells.append(f"{_pct(row['out_of_key_by_key'].get(key, 0.0)):>9}")
        cells.append(f"{_pct(metrics['duplicate_bars']):>7}")
        cells.append(f"{_pct(metrics['duplicate_bars_worst_voice']):>7}")
        cells.append(f"{_pct(metrics['bass_stepwise']):>7}")
        cells.append(f"{int(metrics['velocity_levels']):>4}")
        cells.append(f"{_pct(metrics['note_gaps']):>7}")
        cells.append(f"{metrics['prob_li']:>8.3f}")
        cells.append(f"{metrics['excess_distance']:>6.2f}")
        lines.append(" ".join(cells))
        if base:
            deltas = [f"{form + ' (delta)':<20}"]
            for key in keys:
                previous = base_forms[form].get("out_of_key_by_key", {}).get(key, 0.0)
                deltas.append(f"{_pct_delta(row['out_of_key_by_key'].get(key, 0.0) - previous):>9}")
            for name, width in (
                ("duplicate_bars", 7),
                ("duplicate_bars_worst_voice", 7),
                ("bass_stepwise", 7),
            ):
                deltas.append(f"{_pct_delta(metrics[name] - base.get(name, 0.0)):>{width}}")
            deltas.append(f"{int(metrics['velocity_levels'] - base.get('velocity_levels', 0)):>+4}")
            deltas.append(f"{_pct_delta(metrics['note_gaps'] - base.get('note_gaps', 0.0)):>7}")
            deltas.append(f"{metrics['prob_li'] - base.get('prob_li', 0.0):>+8.3f}")
            deltas.append(f"{metrics['excess_distance'] - base.get('excess_distance', 0.0):>+6.2f}")
            lines.append(" ".join(deltas))

    lines.append("")
    lines.append("KL excess over length-matched Bach (positive = worse than the corpus)")
    kl_header = f"{'form':<20} " + " ".join(f"{name[:9]:>10}" for name in KL_COMPONENTS)
    lines.append(kl_header)
    lines.append("-" * len(kl_header))
    for form in sorted(report["forms"]):
        metrics = report["forms"][form]["metrics"]
        cells = [f"{form:<20}"]
        for name in KL_COMPONENTS:
            cells.append(f"{metrics[f'kl_{name}']:>+10.3f}")
        lines.append(" ".join(cells))

    if report["rejected"]:
        lines.append("")
        lines.append(f"rejected configurations ({len(report['rejected'])}):")
        for label in report["rejected"]:
            lines.append(f"  {label}")
        if baseline is not None:
            added = sorted(set(report["rejected"]) - set(baseline.get("rejected", [])))
            removed = sorted(set(baseline.get("rejected", [])) - set(report["rejected"]))
            if added:
                lines.append(f"  newly rejected: {', '.join(added)}")
            if removed:
                lines.append(f"  no longer rejected: {', '.join(removed)}")
    return "\n".join(lines)


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    """Register sweep-score CLI arguments on `parser`.

    Shared by register() and main() so the argument surface stays identical.
    """
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI, help="bach_cli binary")
    parser.add_argument(
        "--index-js", type=Path, default=DEFAULT_INDEX_JS, help="bach-mcp dist/index.js"
    )
    parser.add_argument("--forms", nargs="+", default=list(FORMS), choices=list(FORMS))
    parser.add_argument(
        "--characters", nargs="+", default=list(CHARACTERS), choices=list(CHARACTERS)
    )
    parser.add_argument("--keys", nargs="+", default=list(KEYS))
    parser.add_argument("--seeds", nargs="+", type=int, default=list(DEFAULT_SEEDS))
    parser.add_argument("--jobs", type=int, default=1, help="configurations generated in parallel")
    parser.add_argument("--out", type=Path, default=None, help="write the machine report here")
    parser.add_argument(
        "--baseline", type=Path, default=None, help="report the delta against this earlier report"
    )


def register(subparsers) -> None:
    """Attach the `sweep-score` subcommand to `subparsers`."""
    parser = subparsers.add_parser(
        "sweep-score",
        help="sweep forms x characters x keys x seeds and report structure metrics",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Generate the sweep, score it, and print the report.

    @return 0 on success, 2 when the CLI or the scorer is missing.
    """
    if not args.cli.exists():
        print(f"bach_cli not found: {args.cli}", file=sys.stderr)
        return 2
    if not args.index_js.exists():
        print(f"bach-mcp not found: {args.index_js}", file=sys.stderr)
        return 2

    baseline = None
    if args.baseline is not None:
        with args.baseline.open(encoding="utf-8") as handle:
            baseline = json.load(handle)

    configurations = [
        (form, character, key, seed)
        for form in args.forms
        for character in args.characters
        for key in args.keys
        for seed in args.seeds
    ]

    with tempfile.TemporaryDirectory(prefix="bach_sweep_") as tmp:
        work_dir = Path(tmp)
        if args.jobs > 1:
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                cases = list(
                    pool.map(
                        lambda cfg: run_case(args.cli, args.index_js, work_dir, *cfg),
                        configurations,
                    )
                )
        else:
            cases = [run_case(args.cli, args.index_js, work_dir, *cfg) for cfg in configurations]

    report = aggregate(cases)
    print(format_report(report, baseline))
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open("w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2, sort_keys=True)
            handle.write("\n")
    return 0


def main() -> int:
    """Standalone entry point reusing the shared argument surface."""
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
