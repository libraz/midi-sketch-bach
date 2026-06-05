#!/usr/bin/env python3
"""Report current-score vs shadow-score candidate divergence.

The default target is the composer closure harness because it emits both
generated.json and provenance.json. Default form-mode JSON is homepage events and
does not carry provenance.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from closure_common import normalize_phase

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CLI = REPO_ROOT / "build" / "bin" / "bach_cli"
DEFAULT_OUT = REPO_ROOT / "backup" / "score_divergence_b1_b2_2026-06-05.md"
DEFAULT_PHASES = [
    "Phase7",
    "Phase8",
    "Phase9",
    "Phase10",
    "Phase11",
    "Phase12",
    "Phase14",
    "Phase15",
    "Phase16",
    "Phase17",
    "Phase18",
    "Phase19",
    "Phase20",
    "Phase21",
    "Phase22",
    "Phase23",
    "Phase24",
    "Phase25",
]

CHORD_TONE_BIT = 1 << 0


@dataclass
class DivergenceStats:
    generated: int = 0
    failed_cases: int = 0
    compose_notes: int = 0
    mismatches: int = 0
    markov_changed_shadow_winner: int = 0
    missing_shadow: int = 0
    by_phase: Counter[str] | None = None
    by_voice_intent: Counter[str] | None = None
    by_motion: Counter[str] | None = None
    by_harmony: Counter[str] | None = None
    failures_by_phase: Counter[str] | None = None

    def __post_init__(self) -> None:
        if self.by_phase is None:
            self.by_phase = Counter()
        if self.by_voice_intent is None:
            self.by_voice_intent = Counter()
        if self.by_motion is None:
            self.by_motion = Counter()
        if self.by_harmony is None:
            self.by_harmony = Counter()
        if self.failures_by_phase is None:
            self.failures_by_phase = Counter()


def motion_bucket(prev_pitch: int | None, pitch: int) -> str:
    if prev_pitch is None:
        return "first"
    delta = abs(pitch - prev_pitch)
    if delta == 0:
        return "repeat"
    if delta <= 2:
        return "step"
    return "leap"


def accumulate_case(phase: str, generated: dict[str, Any], provenance: dict[str, Any],
                    stats: DivergenceStats) -> None:
    notes = generated.get("notes", [])
    prov_notes = provenance.get("notes", [])
    stats.generated += 1
    prev_by_voice: dict[int, int] = {}

    for note, prov in zip(notes, prov_notes):
        if prov.get("source") != "Compose":
            voice = int(note.get("voice", 0))
            prev_by_voice[voice] = int(note["pitch"])
            continue
        stats.compose_notes += 1
        selected_pitch = int(note["pitch"])
        shadow_pitch = prov.get("shadow_winning_pitch")
        shadow_pitch_without_markov = prov.get("shadow_winning_pitch_without_markov")
        if shadow_pitch is None:
            stats.missing_shadow += 1
            continue
        if shadow_pitch_without_markov is not None and int(shadow_pitch_without_markov) != int(shadow_pitch):
            stats.markov_changed_shadow_winner += 1
        voice = int(note.get("voice", 0))
        prev_pitch = prev_by_voice.get(voice)
        if int(shadow_pitch) != selected_pitch:
            stats.mismatches += 1
            stats.by_phase[phase] += 1
            stats.by_voice_intent[str(prov.get("voice_intent", "unknown"))] += 1
            stats.by_motion[motion_bucket(prev_pitch, selected_pitch)] += 1
            rules = int(prov.get("satisfied_rules", 0))
            stats.by_harmony["selected_chord_tone" if (rules & CHORD_TONE_BIT) else "selected_nct"] += 1
        prev_by_voice[voice] = selected_pitch


def run_phase(cli: Path, phase: str, seed: int, work_dir: Path) -> tuple[dict[str, Any], dict[str, Any]] | None:
    midi_path = work_dir / f"{phase}_{seed}.mid"
    cmd = [
        str(cli),
        "--composer-phase",
        phase,
        "--seed",
        str(seed),
        "--json",
        "-o",
        str(midi_path),
    ]
    completed = subprocess.run(cmd, cwd=REPO_ROOT, check=False, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    if completed.returncode != 0:
        return None
    json_path = midi_path.with_suffix(".json")
    provenance_path = midi_path.with_suffix(".provenance.json")
    return (
        json.loads(json_path.read_text(encoding="utf-8")),
        json.loads(provenance_path.read_text(encoding="utf-8")),
    )


def render_counter(counter: Counter[str]) -> list[str]:
    if not counter:
        return ["- none"]
    return [f"- {key}: {value}" for key, value in counter.most_common()]


def render_report(stats: DivergenceStats, phases: list[str], seeds: list[int]) -> str:
    rate = 0.0 if stats.compose_notes == 0 else stats.mismatches / stats.compose_notes
    lines = [
        "# Shadow Score Divergence Report B-1/B-2",
        "",
        f"- phases: {', '.join(phases)}",
        f"- seeds: {seeds[0]}..{seeds[-1]}" if seeds else "- seeds: none",
        f"- generated cases: {stats.generated}",
        f"- validation-failed cases skipped: {stats.failed_cases}",
        f"- compose notes: {stats.compose_notes}",
        f"- mismatches: {stats.mismatches}",
        f"- mismatch_rate: {rate:.6f}",
        f"- markov_changed_shadow_winner: {stats.markov_changed_shadow_winner}",
        f"- missing_shadow_winning_pitch: {stats.missing_shadow}",
        "",
        "## By Phase",
        *render_counter(stats.by_phase or Counter()),
        "",
        "## Validation Failures Skipped",
        *render_counter(stats.failures_by_phase or Counter()),
        "",
        "## By Voice Intent",
        *render_counter(stats.by_voice_intent or Counter()),
        "",
        "## Selected Motion Bucket",
        *render_counter(stats.by_motion or Counter()),
        "",
        "## Selected Harmony Bucket",
        *render_counter(stats.by_harmony or Counter()),
        "",
        "Note: this report compares the emitted current-score pitch with the audit-only",
        "`shadow_winning_pitch`; generation still uses the current score.",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--seeds", type=int, default=20)
    parser.add_argument("--phase", action="append", dest="phases")
    args = parser.parse_args()

    phases = [normalize_phase(p) for p in (args.phases or DEFAULT_PHASES)]
    seeds = list(range(args.seeds))
    stats = DivergenceStats()

    with tempfile.TemporaryDirectory(prefix="bach-shadow-divergence-") as tmp:
        work_dir = Path(tmp)
        for phase in phases:
            for seed in seeds:
                case = run_phase(args.cli, phase, seed, work_dir)
                if case is None:
                    stats.failed_cases += 1
                    stats.failures_by_phase[phase] += 1
                    continue
                generated, provenance = case
                accumulate_case(phase, generated, provenance, stats)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(render_report(stats, phases, seeds), encoding="utf-8")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
