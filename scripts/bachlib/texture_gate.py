#!/usr/bin/env python3
"""Run the fugue texture gate over generated JSON.

The gate sweeps seed x form generation, computes texture metrics, and prints
a compact pass/fail summary.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from bachlib.common import model_probability, score_generated
from bachlib.phases import normalize_phase  # noqa: F401  (kept import surface aligned)
from bachlib.texture_metrics import compute_texture_metrics


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INDEX_JS = REPO_ROOT.parent / "bach-mcp" / "dist" / "index.js"

# The established gate-3 model-score threshold for fugue work. A generated piece
# clears gate-3 when bach-mcp's corpus model probability is at or above this
# value. Recorded on every case; enforced in passes_texture_gate only when the
# whole fugue sweep already clears it (see ENFORCE_MODEL_SCORE).
MODEL_SCORE_THRESHOLD = 0.80

# Whether a case's model_score participates in passes_texture_gate. The fugue
# sweep (seeds 1-20 x {fugue, prelude_and_fugue}) was measured against the corpus
# model: with the scalar-wave figuration / bass-support construction every case
# clears MODEL_SCORE_THRESHOLD (bare fugue >= 0.82, prelude_and_fugue >= 0.85), so
# the model score is enforced as part of passes_texture_gate. A case that was not
# scored (absent scorer) still passes on the model-score axis so an unavailable
# scorer cannot fabricate a failure (see passes_model_score).
ENFORCE_MODEL_SCORE = True


# Texture bands are calibrated against a corpus of 21 voice-separated Bach
# fugues (duration-weighted boundary sweep). The corpus p25 for the average
# number of simultaneously sounding voices normalises to ~1.97 for a 3-voice
# texture, and the weakest voice sounds for at least ~0.34 of the piece. The
# gate targets those p25 floors so generated fugues stay inside the corpus
# texture envelope rather than thinning to a two-voice texture.
MIN_AVG_ACTIVE_VOICES = 1.95
MIN_PIECE_VOICE_OCCUPANCY = 0.34

# Parallel perfect-fifth/octave ceiling. Measured against the same 21
# voice-separated reference fugues (form=fugue, track_type=voice) under the
# identical union-onset sampling as compute_parallel_counts: per-piece parallel
# counts span 0..12 (median 2), the residual being octave-doublings and
# union-onset sampling edge cases rather than true voice-leading errors. The
# ceiling is the corpus maximum (BWV549), so a generated fugue stays inside the
# corpus envelope on the cardinal parallel-5th/8th prohibition. Hidden perfects
# are recorded only (corpus hidden spans 4..49 and is not a hard prohibition).
MAX_PARALLEL_PERFECT_COUNT = 12


@dataclass
class GateCase:
    form: str
    seed: int
    generated: bool
    max_active_voices: int = 0
    avg_active_voices: float = 0.0
    max_silence_ratio: float = 0.0
    v2_silence_ratio: float = 1.0
    max_repeated_run: int = 0
    compass_violation_count: int = 0
    register_overlap_ratio: float = 0.0
    # Per-voice fraction of the whole piece (sounding_time / piece_total_ticks),
    # keyed by voice id, plus the minimum across voices. This is piece-relative,
    # distinct from the window-relative silence_ratio in texture_metrics.
    piece_voice_occupancy: dict[int, float] | None = None
    min_piece_voice_occupancy: float = 0.0
    middle_entry_bars: list[int] | None = None
    entry_intervals: list[int] | None = None
    entry_plan_nonperiodic: bool = False
    fortspinnung_span_count: int = 0
    stretto_span_count: int = 0
    # Parallel / hidden perfect fifth+octave counts (union-onset sampling). The
    # parallel count is gated against the corpus ceiling; the hidden count is
    # recorded only (informational).
    parallel_perfect_count: int = 0
    hidden_perfect_count: int = 0
    # bach-mcp corpus model probability ("gate-3"). -1.0 marks "not scored"
    # (the scorer was unavailable, e.g. bach-mcp / node missing); such cases do
    # not gate on the model score so an absent scorer cannot mask a texture fail.
    model_score: float = -1.0
    error: str = ""

    @property
    def model_scored(self) -> bool:
        return self.model_score >= 0.0

    @property
    def passes_model_score(self) -> bool:
        """Whether the model score satisfies the gate-3 threshold.

        True when enforcement is off, when the case was not scored (absent
        scorer must not fabricate a failure), or when the recorded probability
        is at or above MODEL_SCORE_THRESHOLD.
        """
        if not ENFORCE_MODEL_SCORE:
            return True
        if not self.model_scored:
            return True
        return self.model_score >= MODEL_SCORE_THRESHOLD

    @property
    def passes_parallel(self) -> bool:
        """Whether the parallel perfect-5th/8th count is within the corpus ceiling."""
        return self.parallel_perfect_count <= MAX_PARALLEL_PERFECT_COUNT

    @property
    def passes_texture_gate(self) -> bool:
        return (
            self.generated
            and self.max_active_voices == 3
            and self.v2_silence_ratio <= 0.25
            and self.max_repeated_run <= 4
            and self.avg_active_voices >= MIN_AVG_ACTIVE_VOICES
            and self.min_piece_voice_occupancy >= MIN_PIECE_VOICE_OCCUPANCY
            and self.passes_parallel
            and self.passes_model_score
        )

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data["passes_texture_gate"] = self.passes_texture_gate
        data["passes_parallel"] = self.passes_parallel
        data["model_scored"] = self.model_scored
        data["passes_model_score"] = self.passes_model_score
        return data


def _read_notes(generated_json: Path) -> list[dict[str, int]]:
    with generated_json.open(encoding="utf-8") as handle:
        data = json.load(handle)
    notes = data.get("notes", [])
    if not isinstance(notes, list):
        raise ValueError(f"{generated_json} does not contain a notes array")
    return notes


def _read_provenance(generated_json: Path) -> list[dict[str, Any]]:
    provenance_json = generated_json.with_name(
        generated_json.name.replace(".generated.json", ".provenance.json")
    )
    if not provenance_json.exists():
        return []
    with provenance_json.open(encoding="utf-8") as handle:
        data = json.load(handle)
    provenance = data.get("notes", [])
    if not isinstance(provenance, list):
        raise ValueError(f"{provenance_json} does not contain a notes array")
    return provenance


def compute_entry_plan_metrics(
    notes: list[dict[str, int]], provenance: list[dict[str, Any]]
) -> tuple[list[int], list[int], bool]:
    by_span: dict[int, int] = {}
    for note, prov in zip(notes, provenance):
        if prov.get("voice_intent") != "MiddleEntryCarrier":
            continue
        span_id = int(prov["span_id"])
        start_tick = int(note["start_tick"])
        by_span[span_id] = min(by_span.get(span_id, start_tick), start_tick)
    bars = sorted({tick // 1920 for tick in by_span.values()})
    intervals = [right - left for left, right in zip(bars, bars[1:])]
    nonperiodic = len(intervals) >= 2 and any(interval != intervals[0] for interval in intervals[1:])
    return bars, intervals, nonperiodic


def _voice_note_table(notes: list[dict[str, int]]) -> dict[int, list[tuple[int, int, int]]]:
    """Group notes into voice -> onset-sorted (start, end, pitch) intervals.

    Operates on the generated.v1 shape (``start_tick`` / ``duration`` /
    ``voice`` / ``pitch``). Pure and order-independent.
    """
    table: dict[int, list[tuple[int, int, int]]] = {}
    for note in notes:
        voice = int(note["voice"])
        start = int(note["start_tick"])
        end = start + int(note["duration"])
        table.setdefault(voice, []).append((start, end, int(note["pitch"])))
    for voice in table:
        table[voice].sort()
    return table


def _sounding_pitch(intervals: list[tuple[int, int, int]], tick: int) -> int | None:
    """Pitch of the latest-onset interval covering ``tick``, or None if silent."""
    found: tuple[int, int, int] | None = None
    for start, end, pitch in intervals:
        if start <= tick < end and (found is None or start > found[0]):
            found = (start, end, pitch)
    return found[2] if found is not None else None


def compute_parallel_counts(
    voice_table: dict[int, list[tuple[int, int, int]]]
) -> tuple[int, int]:
    """Count parallel and hidden perfect fifths/octaves by union-onset sampling.

    For each ordered voice pair, walks the union of all note onsets. At each
    onset where both voices sound, the sounding pitches are sampled. A parallel
    perfect = both voices changed pitch since the previous sampled onset, moved
    in the same direction, and the mod-12 interval was 0 or 7 at both onsets. A
    hidden perfect = a same-direction arrival on interval class 0/7 from a
    different interval where the upper-moving voice leapt (> 2 semitones). This
    mirrors the project's union-onset sampling (see ``_sounding_pitch``), and is
    the cardinal Bach prohibition on parallel fifths/octaves between voices.

    @param voice_table Voice -> onset-sorted (start, end, pitch) intervals.
    @return (parallel_perfect_count, hidden_perfect_count) across all pairs.
    """
    voices = sorted(voice_table.keys())
    onsets = sorted({start for intervals in voice_table.values() for start, _, _ in intervals})
    parallel = 0
    hidden = 0
    for index, lower in enumerate(voices):
        for upper in voices[index + 1 :]:
            previous: tuple[int, int] | None = None
            for tick in onsets:
                pitch_a = _sounding_pitch(voice_table[lower], tick)
                pitch_b = _sounding_pitch(voice_table[upper], tick)
                if pitch_a is None or pitch_b is None:
                    previous = None
                    continue
                if previous is not None:
                    prev_a, prev_b = previous
                    delta_a = pitch_a - prev_a
                    delta_b = pitch_b - prev_b
                    same_dir = (delta_a > 0 and delta_b > 0) or (delta_a < 0 and delta_b < 0)
                    curr_ic = abs(pitch_a - pitch_b) % 12
                    if delta_a != 0 and delta_b != 0 and same_dir and curr_ic in (0, 7):
                        prev_ic = abs(prev_a - prev_b) % 12
                        if prev_ic == curr_ic:
                            parallel += 1
                        else:
                            upper_delta = delta_a if pitch_a >= pitch_b else delta_b
                            if abs(upper_delta) > 2:
                                hidden += 1
                previous = (pitch_a, pitch_b)
    return parallel, hidden


def compute_piece_parallel_counts(notes: list[dict[str, int]]) -> tuple[int, int]:
    """Parallel / hidden perfect counts for a generated.v1 note array."""
    return compute_parallel_counts(_voice_note_table(notes))


def compute_piece_voice_occupancy(notes: list[dict[str, int]]) -> dict[int, float]:
    """Fraction of the whole piece each voice is sounding.

    The piece total is the latest note end tick across all voices, so the value
    is piece-relative (sounding_time / piece_total), not bounded by each voice's
    own first/last note like texture_metrics' silence_ratio.
    """
    if not notes:
        return {}
    piece_total = max(int(note["start_tick"]) + int(note["duration"]) for note in notes)
    if piece_total <= 0:
        return {}
    sounding: dict[int, int] = {}
    for note in notes:
        voice = int(note["voice"])
        sounding[voice] = sounding.get(voice, 0) + int(note["duration"])
    return {voice: ticks / piece_total for voice, ticks in sounding.items()}


def count_intent_spans(provenance: list[dict[str, Any]], voice_intent: str) -> int:
    return len(
        {
            int(prov["span_id"])
            for prov in provenance
            if prov.get("voice_intent") == voice_intent and "span_id" in prov
        }
    )


def evaluate_generated_json(form: str, seed: int, generated_json: Path) -> GateCase:
    notes = _read_notes(generated_json)
    provenance = _read_provenance(generated_json)
    metrics = compute_texture_metrics(notes)
    silence_by_voice = {voice.voice: voice.silence_ratio for voice in metrics.voices}
    entry_bars, entry_intervals, entry_nonperiodic = compute_entry_plan_metrics(
        notes, provenance
    )
    piece_voice_occupancy = compute_piece_voice_occupancy(notes)
    min_piece_voice_occupancy = min(piece_voice_occupancy.values(), default=0.0)
    parallel_perfect, hidden_perfect = compute_piece_parallel_counts(notes)
    return GateCase(
        form=form,
        seed=seed,
        generated=True,
        max_active_voices=metrics.max_active_voices,
        avg_active_voices=metrics.avg_active_voices,
        max_silence_ratio=max(silence_by_voice.values(), default=0.0),
        v2_silence_ratio=silence_by_voice.get(2, 1.0),
        max_repeated_run=max((voice.max_repeated_run for voice in metrics.voices), default=0),
        compass_violation_count=metrics.compass_violation_count,
        register_overlap_ratio=metrics.register_overlap_ratio,
        piece_voice_occupancy=piece_voice_occupancy,
        min_piece_voice_occupancy=min_piece_voice_occupancy,
        middle_entry_bars=entry_bars,
        entry_intervals=entry_intervals,
        entry_plan_nonperiodic=entry_nonperiodic,
        fortspinnung_span_count=count_intent_spans(provenance, "FortspinnungSpan"),
        stretto_span_count=count_intent_spans(provenance, "StrettoCarrier"),
        parallel_perfect_count=parallel_perfect,
        hidden_perfect_count=hidden_perfect,
    )


def model_score_for(index_js: Path | None, generated_json: Path) -> float:
    """Score a generated.json with bach-mcp's corpus model ("gate-3").

    Reuses bachlib.common.score_generated / model_probability so the gate and
    the closure harness obtain the model probability through the same path. A
    missing scorer (no index_js, or a node/bach-mcp error) returns -1.0 so the
    case is recorded as "not scored" rather than failing on a fabricated value.

    @param index_js Path to bach-mcp/dist/index.js, or None to skip scoring.
    @param generated_json The generated.v1 JSON to score.
    @return The corpus model probability, or -1.0 when scoring is unavailable.
    """
    if index_js is None or not index_js.exists():
        return -1.0
    try:
        score = score_generated(index_js, generated_json)
    except (RuntimeError, OSError):
        return -1.0
    return model_probability(score)


def run_case(
    cli: Path,
    form: str,
    seed: int,
    work_dir: Path,
    target_bars: int | None,
    index_js: Path | None = None,
) -> GateCase:
    midi_path = work_dir / f"{form}_{seed}.mid"
    cmd = [
        str(cli),
        "--form",
        form,
        "--seed",
        str(seed),
        "--generated-json",
        "-o",
        str(midi_path),
    ]
    if target_bars is not None:
        cmd.extend(["--bars", str(target_bars)])
    completed = subprocess.run(
        cmd,
        cwd=REPO_ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        return GateCase(form=form, seed=seed, generated=False, error=completed.stderr.strip())
    generated_json = midi_path.with_suffix(".generated.json")
    if not generated_json.exists():
        return GateCase(form=form, seed=seed, generated=False, error="generated JSON missing")
    case = evaluate_generated_json(form, seed, generated_json)
    case.model_score = model_score_for(index_js, generated_json)
    return case


def summarize(cases: list[GateCase]) -> dict[str, Any]:
    failing = [case for case in cases if not case.passes_texture_gate]
    scored = [case.model_score for case in cases if case.model_scored]
    return {
        "total": len(cases),
        "passed": len(cases) - len(failing),
        "failed": len(failing),
        "all_passed": not failing,
        "max_v2_silence_ratio": max((case.v2_silence_ratio for case in cases), default=0.0),
        "max_repeated_run": max((case.max_repeated_run for case in cases), default=0),
        "min_avg_active_voices": min((case.avg_active_voices for case in cases), default=0.0),
        "min_piece_voice_occupancy": min(
            (case.min_piece_voice_occupancy for case in cases if case.generated), default=0.0
        ),
        "min_model_score": min(scored, default=0.0),
        "model_score_threshold": MODEL_SCORE_THRESHOLD,
        "model_score_enforced": ENFORCE_MODEL_SCORE,
        "model_scored_cases": len(scored),
        "max_parallel_perfect_count": max(
            (case.parallel_perfect_count for case in cases if case.generated), default=0
        ),
        "max_hidden_perfect_count": max(
            (case.hidden_perfect_count for case in cases if case.generated), default=0
        ),
        "parallel_perfect_threshold": MAX_PARALLEL_PERFECT_COUNT,
        "failures": [case.to_dict() for case in failing],
    }


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    """Register texture-gate CLI arguments on `parser`.

    Shared by register() (subcommand wiring) and main() (standalone parser) so
    the argument surface stays identical between both entry points.
    """
    parser.add_argument("--cli", type=Path, default=REPO_ROOT / "build/bin/bach_cli")
    parser.add_argument("--forms", nargs="+", default=["fugue", "prelude_and_fugue"])
    # seed 0 means auto (random) in bach_cli, so it is excluded from the
    # deterministic sweep; the default runs seeds 1..20.
    parser.add_argument("--seeds", nargs="+", type=int, default=list(range(1, 21)))
    parser.add_argument("--target-bars", type=int)
    parser.add_argument(
        "--index-js",
        type=Path,
        default=DEFAULT_INDEX_JS,
        help="Path to bach-mcp/dist/index.js for gate-3 model scoring.",
    )
    parser.add_argument(
        "--no-model-score",
        action="store_true",
        help="Skip bach-mcp model scoring (record model_score as not scored).",
    )
    parser.add_argument("--out", type=Path)


def register(subparsers) -> None:
    """Attach the `texture-gate` subcommand to `subparsers`."""
    parser = subparsers.add_parser(
        "texture-gate",
        help="sweep seed x form and report a texture pass/fail summary",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Execute the texture-gate sweep described by `args`."""
    index_js = None if args.no_model_score else args.index_js

    cases: list[GateCase] = []
    with tempfile.TemporaryDirectory(prefix="bach-fugue-texture-gate-") as tmp:
        work_dir = Path(tmp)
        for form in args.forms:
            for seed in args.seeds:
                cases.append(
                    run_case(args.cli, form, seed, work_dir, args.target_bars, index_js)
                )

    report = {"summary": summarize(cases), "cases": [case.to_dict() for case in cases]}
    text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0 if report["summary"]["all_passed"] else 1


def main() -> int:
    """Standalone entry point reusing the shared argument surface."""
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
