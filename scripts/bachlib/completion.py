#!/usr/bin/env python3
"""Generate a compact P1-P7 fugue completion diagnostic report."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from bachlib.texture_gate import (
    MIN_AVG_ACTIVE_VOICES,
    MIN_PIECE_VOICE_OCCUPANCY,
    compute_entry_plan_metrics,
    compute_piece_voice_occupancy,
    count_intent_spans,
)
from bachlib.texture_metrics import compute_texture_metrics


REPO_ROOT = Path(__file__).resolve().parents[2]


def run_cli(cli: Path, form: str, seed: int, bars: int, work_dir: Path) -> tuple[Path, Path]:
    midi_path = work_dir / f"{form}_seed{seed}_{bars}.mid"
    cmd = [
        str(cli),
        "--form",
        form,
        "--seed",
        str(seed),
        "--bars",
        str(bars),
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
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or completed.stdout.strip())
    return midi_path.with_suffix(".generated.json"), midi_path.with_suffix(".provenance.json")


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def evaluate(generated_json: Path, provenance_json: Path) -> dict[str, Any]:
    generated = load_json(generated_json)
    provenance = load_json(provenance_json)
    notes = generated.get("notes", [])
    prov_notes = provenance.get("notes", [])
    metrics = compute_texture_metrics(notes)
    entry_bars, entry_intervals, entry_nonperiodic = compute_entry_plan_metrics(notes, prov_notes)
    subject_durations = [
        int(note["duration"])
        for note, prov in zip(notes, prov_notes)
        if prov.get("voice_intent") == "SubjectCarrier" and int(note["start_tick"]) < 4 * 1920
    ]

    voice_silence = {voice.voice: voice.silence_ratio for voice in metrics.voices}
    max_run = max((voice.max_repeated_run for voice in metrics.voices), default=0)
    occupancy = compute_piece_voice_occupancy(notes)
    min_occupancy = min(occupancy.values(), default=0.0)
    return {
        "p1_three_voice_texture": {
            "max_active_voices": metrics.max_active_voices,
            "avg_active_voices": metrics.avg_active_voices,
            "min_piece_voice_occupancy": min_occupancy,
            # Thresholds shared with the texture gate (corpus-derived; see
            # bachlib.texture_gate for the 21-fugue p25 basis).
            "passes": (
                metrics.max_active_voices == 3
                and metrics.avg_active_voices >= MIN_AVG_ACTIVE_VOICES
                and min_occupancy >= MIN_PIECE_VOICE_OCCUPANCY
            ),
        },
        "p2_bass_continuity": {
            "v2_silence_ratio": voice_silence.get(2, 1.0),
            "passes": voice_silence.get(2, 1.0) <= 0.25,
        },
        "p3_repeated_run": {
            "max_repeated_run": max_run,
            "passes": max_run <= 4,
        },
        "p4_compass_register": {
            "compass_violation_count": metrics.compass_violation_count,
            "register_overlap_ratio": metrics.register_overlap_ratio,
            "passes": metrics.compass_violation_count == 0 and metrics.register_overlap_ratio > 0.0,
        },
        "p5_subject_rhythm": {
            "unique_subject_durations": sorted(set(subject_durations)),
            "passes": len(set(subject_durations)) >= 2,
        },
        "p6_entry_plan": {
            "middle_entry_bars": entry_bars,
            "entry_intervals": entry_intervals,
            "nonperiodic": entry_nonperiodic,
            "passes": entry_nonperiodic,
        },
        "p7_gate_visibility": {
            "fortspinnung_span_count": count_intent_spans(prov_notes, "FortspinnungSpan"),
            "stretto_span_count": count_intent_spans(prov_notes, "StrettoCarrier"),
            "passes": True,
        },
    }


def render_markdown(report: dict[str, Any]) -> str:
    lines = ["# Fugue Completion Diagnostic", ""]
    for key, value in report.items():
        status = "PASS" if value.get("passes") else "FAIL"
        lines.append(f"- `{key}`: {status}")
        for metric_key, metric_value in value.items():
            if metric_key == "passes":
                continue
            lines.append(f"  - `{metric_key}`: `{metric_value}`")
    lines.append("")
    return "\n".join(lines)


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    """Register completion-report CLI arguments on `parser`.

    Shared by register() and main() so the argument surface stays identical.
    """
    parser.add_argument("--cli", type=Path, default=REPO_ROOT / "build/bin/bach_cli")
    parser.add_argument("--form", default="fugue")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--bars", type=int, default=84)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument(
        "--out",
        type=Path,
        help="Write the markdown report to this file instead of stdout.",
    )


def register(subparsers) -> None:
    """Attach the `completion` subcommand to `subparsers`."""
    parser = subparsers.add_parser(
        "completion",
        help="fugue completion diagnostic report (texture, entry plan, occupancy)",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Generate and emit the completion diagnostic described by `args`."""
    with tempfile.TemporaryDirectory(prefix="bach-fugue-completion-") as tmp:
        generated_json, provenance_json = run_cli(args.cli, args.form, args.seed, args.bars, Path(tmp))
        report = evaluate(generated_json, provenance_json)

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    markdown = render_markdown(report)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(markdown, encoding="utf-8")
    else:
        print(markdown, end="")
    return 0 if all(item.get("passes") for item in report.values()) else 1


def main() -> int:
    """Standalone entry point reusing the shared argument surface."""
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
