#!/usr/bin/env python3
"""Run the fugue texture gate over generated JSON.

The gate sweeps seed x form generation, computes texture metrics, and prints
a compact pass/fail summary.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from bachlib.common import model_probability, model_probability_v2, score_generated
from bachlib.phases import normalize_phase  # noqa: F401  (kept import surface aligned)
from bachlib.texture_metrics import compute_texture_metrics


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INDEX_JS = REPO_ROOT.parent / "bach-mcp" / "dist" / "index.js"

# The established gate-3 model-score threshold for fugue work. A generated piece
# clears gate-3 when bach-mcp's corpus model probability is at or above this
# value. Recorded on every case; enforced in passes_texture_gate only when the
# whole fugue sweep already clears it (see ENFORCE_MODEL_SCORE).
MODEL_SCORE_THRESHOLD = 0.80

# Whether a case's model scores participate in passes_texture_gate. The fugue
# sweep (seeds 1-20 x {fugue, prelude_and_fugue}) was measured against the corpus
# model: with the scalar-wave figuration / bass-support construction every case
# clears MODEL_SCORE_THRESHOLD (bare fugue >= 0.82, prelude_and_fugue >= 0.85), so
# the model score is enforced as part of passes_texture_gate. A case that was not
# scored (absent scorer) still passes on the model-score axes so an unavailable
# scorer cannot fabricate a failure (see passes_model_score). The flag covers
# both the v1 cross-entropy axis and the v2 KL axis.
ENFORCE_MODEL_SCORE = True

# Shared floor for the KL-divergence model probability (bach-mcp
# model_score_v2). The v2 model measures per-component KL divergence against
# the reference corpus with a probability scale anchored to the reference
# works' own p95 distance envelope (a real solo-cello prelude scores ~0.84;
# degenerate interval-spam output scores ~0.01), so unlike the v1
# cross-entropy score it cannot be gamed by over-concentrating on common
# intervals. 0.70 is a conservative fallback for forms without an explicit
# floor; the per-form floors in FORM_THRESHOLDS are the measured 20-seed
# sweep minima minus a ~0.02 seed-noise margin and act as regression
# ratchets, not aspirational targets.
MODEL_SCORE_V2_THRESHOLD = 0.70


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


# Fraction of num_voices that the tick-weighted average active-voice count must
# reach when a form has no explicit min_avg_active. Shared with the fugue gate:
# 0.66 x 3 = 1.98, close to the historical MIN_AVG_ACTIVE_VOICES floor.
AVG_ACTIVE_VOICE_FRACTION = 0.66


@dataclass(frozen=True)
class FormThresholds:
    """Per-form texture targets and whether they gate the exit code.

    A form is enforced when a failure on any of its axes flips the exit code.
    The fugue forms and the four texture-uplift forms (toccata_and_fugue,
    fantasia_and_fugue, passacaglia, chorale_prelude) are enforced; an unknown
    or synthetic form can be left informational so its axes are measured and
    reported but never cause a FAIL verdict or a nonzero exit. Common floors
    (avg-active vs voice count, all-voice piece occupancy, repeated-run,
    parallel, model score) are derived per form in GateCase, so only the
    form-specific axes live here.

    @field min_avg_active Form-specific floor on the tick-weighted average of
        simultaneously sounding voices. None falls back to the voice-count
        floor (0.66 x num_voices) shared with the fugue gate.
    @field max_mono_ratio Ceiling on the duration-weighted fraction of the
        piece that is monophonic. None disables the mono axis.
    @field require_v1_v2_occupancy When True, V1 and V2 piece occupancy must
        each be at least 0.5 (toccata's dramatic-but-not-thin requirement).
    @field min_final_quarter_avg_active When set, the final quarter of the
        piece (by tick span) must average at least this many active voices.
    @field model_score_threshold Form-specific gate-3 model-score floor. None
        falls back to the shared MODEL_SCORE_THRESHOLD (0.80); the sectional
        fantasia uses its established 0.78 closure threshold.
    @field model_score_v2_threshold Form-specific floor on the KL-model
        probability (model_score_v2). None falls back to the shared
        MODEL_SCORE_V2_THRESHOLD; the explicit values are 20-seed sweep
        minima minus a seed-noise margin (regression ratchets).
    @field enforced When True, failures flip the exit code; otherwise the form
        is informational only.
    """

    min_avg_active: float | None = None
    max_mono_ratio: float | None = None
    require_v1_v2_occupancy: bool = False
    min_final_quarter_avg_active: float | None = None
    model_score_threshold: float | None = None
    model_score_v2_threshold: float | None = None
    enforced: bool = True


# Texture targets per form. Fugue forms use the historical enforced gate
# (min_avg_active falls back to the voice-count floor, no mono ceiling). The
# four non-fugue forms carry the texture-uplift targets; their builders now
# clear every axis on seeds 1-20, so they are enforced alongside the fugue
# forms (their threshold values are unchanged from the informational baseline).
FORM_THRESHOLDS: dict[str, FormThresholds] = {
    # Fugue forms keep the historical enforced floor (1.95) exactly; the
    # voice-count floor is not applied to them so their enforcement is
    # byte-identical to the original gate.
    "fugue": FormThresholds(
        min_avg_active=MIN_AVG_ACTIVE_VOICES,
        model_score_v2_threshold=0.78,
        enforced=True,
    ),
    "prelude_and_fugue": FormThresholds(
        min_avg_active=MIN_AVG_ACTIVE_VOICES,
        model_score_v2_threshold=0.85,
        enforced=True,
    ),
    "toccata_and_fugue": FormThresholds(
        min_avg_active=2.1,
        max_mono_ratio=0.25,
        require_v1_v2_occupancy=True,
        model_score_v2_threshold=0.75,
        enforced=True,
    ),
    "fantasia_and_fugue": FormThresholds(
        min_avg_active=2.3,
        max_mono_ratio=0.10,
        model_score_threshold=0.78,
        model_score_v2_threshold=0.77,
        enforced=True,
    ),
    "passacaglia": FormThresholds(
        min_avg_active=2.2,
        max_mono_ratio=0.15,
        min_final_quarter_avg_active=2.5,
        model_score_v2_threshold=0.83,
        enforced=True,
    ),
    "chorale_prelude": FormThresholds(
        min_avg_active=2.5,
        max_mono_ratio=0.05,
        model_score_v2_threshold=0.75,
        enforced=True,
    ),
    # The remaining forms previously fell through to the default (enforced,
    # fugue-style) entry; they are listed explicitly to carry their KL-model
    # floors. All other fields keep the default-entry semantics.
    "trio_sonata": FormThresholds(model_score_v2_threshold=0.76, enforced=True),
    "cello_prelude": FormThresholds(model_score_v2_threshold=0.65, enforced=True),
    "chaconne": FormThresholds(model_score_v2_threshold=0.89, enforced=True),
    "goldberg_variations": FormThresholds(
        model_score_v2_threshold=0.82, enforced=True
    ),
}


def thresholds_for(form: str) -> FormThresholds:
    """Return the texture targets for `form`.

    Unknown forms default to the enforced fugue-style gate (voice-count floor,
    no mono ceiling) so a new form is never silently treated as informational.
    """
    return FORM_THRESHOLDS.get(form, FormThresholds(enforced=True))


@dataclass
class GateCase:
    form: str
    seed: int
    generated: bool
    max_active_voices: int = 0
    avg_active_voices: float = 0.0
    mono_ratio: float = 0.0
    # Number of distinct voices in the generated output. Read from the notes,
    # not hardcoded, so the voice-count floors track 2- vs 3-voice forms.
    num_voices: int = 0
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
    # Tick-weighted average active voices over the final quarter of the span.
    final_quarter_avg_active: float = 0.0
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
    # KL-divergence model probability (bach-mcp model_score_v2). Gated against
    # the form's model_score_v2_threshold (MODEL_SCORE_V2_THRESHOLD fallback);
    # -1.0 marks "not scored" and does not gate, mirroring model_score.
    model_score_v2: float = -1.0
    error: str = ""

    @property
    def thresholds(self) -> FormThresholds:
        return thresholds_for(self.form)

    @property
    def enforced(self) -> bool:
        """Whether this form's verdict participates in the exit code."""
        return self.thresholds.enforced

    @property
    def min_avg_active(self) -> float:
        """Effective average-active floor.

        The voice-count floor (0.66 x num_voices) is a common floor for every
        form. A form-specific target raises it further; the fugue forms pin the
        explicit value at the historical 1.95 and the voice-count floor is not
        applied to them so their enforcement is unchanged.
        """
        explicit = self.thresholds.min_avg_active
        if self.form in ("fugue", "prelude_and_fugue"):
            return explicit if explicit is not None else MIN_AVG_ACTIVE_VOICES
        floor = AVG_ACTIVE_VOICE_FRACTION * self.num_voices
        if explicit is None:
            return floor
        return max(explicit, floor)

    @property
    def model_scored(self) -> bool:
        return self.model_score >= 0.0

    @property
    def passes_model_score(self) -> bool:
        """Whether the model score satisfies the gate-3 threshold.

        True when enforcement is off, when the case was not scored (absent
        scorer must not fabricate a failure), or when the recorded probability
        is at or above the form's model threshold (MODEL_SCORE_THRESHOLD
        unless the form declares its own).
        """
        if not ENFORCE_MODEL_SCORE:
            return True
        if not self.model_scored:
            return True
        threshold = self.thresholds.model_score_threshold
        if threshold is None:
            threshold = MODEL_SCORE_THRESHOLD
        return self.model_score >= threshold

    @property
    def model_scored_v2(self) -> bool:
        return self.model_score_v2 >= 0.0

    @property
    def passes_model_score_v2(self) -> bool:
        """Whether the KL-model probability satisfies the form's v2 floor.

        Mirrors passes_model_score: True when enforcement is off or the case
        was not v2-scored (an absent or pre-v2 scorer must not fabricate a
        failure); otherwise the recorded v2 probability must reach the form's
        floor (MODEL_SCORE_V2_THRESHOLD unless the form declares its own).
        """
        if not ENFORCE_MODEL_SCORE:
            return True
        if not self.model_scored_v2:
            return True
        threshold = self.thresholds.model_score_v2_threshold
        if threshold is None:
            threshold = MODEL_SCORE_V2_THRESHOLD
        return self.model_score_v2 >= threshold

    @property
    def passes_parallel(self) -> bool:
        """Whether the parallel perfect-5th/8th count is within the corpus ceiling."""
        return self.parallel_perfect_count <= MAX_PARALLEL_PERFECT_COUNT

    def axis_results(self) -> dict[str, bool]:
        """Per-axis pass/fail map for this case, keyed by axis name.

        Every axis is evaluated for every form regardless of enforcement; the
        report shows the full map for both enforced and informational forms.
        Axes that do not apply to a form (no mono ceiling, no final-quarter
        floor, etc.) are omitted rather than reported as a pass.
        """
        thresholds = self.thresholds
        results: dict[str, bool] = {
            "generated": self.generated,
            "max_active_voices": self.max_active_voices == self.num_voices,
            "max_repeated_run": self.max_repeated_run <= 4,
            "avg_active_voices": self.avg_active_voices >= self.min_avg_active,
            "min_piece_voice_occupancy": (
                self.min_piece_voice_occupancy >= MIN_PIECE_VOICE_OCCUPANCY
            ),
            "parallel_perfect": self.passes_parallel,
            "model_score": self.passes_model_score,
            "model_score_v2": self.passes_model_score_v2,
        }
        # The v2 silence axis only applies to the 3-voice fugue gate; the
        # mono-ratio ceiling supersedes it for the uplift forms.
        if thresholds.max_mono_ratio is None:
            results["v2_silence_ratio"] = self.v2_silence_ratio <= 0.25
        else:
            results["mono_ratio"] = self.mono_ratio <= thresholds.max_mono_ratio
        if thresholds.require_v1_v2_occupancy:
            occupancy = self.piece_voice_occupancy or {}
            results["v1_v2_occupancy"] = (
                occupancy.get(1, 0.0) >= 0.5 and occupancy.get(2, 0.0) >= 0.5
            )
        if thresholds.min_final_quarter_avg_active is not None:
            results["final_quarter_avg_active"] = (
                self.final_quarter_avg_active >= thresholds.min_final_quarter_avg_active
            )
        return results

    @property
    def passes_all_axes(self) -> bool:
        """Whether every applicable axis passes (regardless of enforcement)."""
        return self.generated and all(self.axis_results().values())

    @property
    def passes_texture_gate(self) -> bool:
        """Exit-code verdict: True unless an enforced form fails an axis.

        Informational forms always return True here so they cannot flip the
        exit code; their per-axis detail is still recorded via axis_results.
        """
        if not self.enforced:
            return True
        return self.passes_all_axes

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data["num_voices"] = self.num_voices
        data["enforced"] = self.enforced
        data["min_avg_active"] = self.min_avg_active
        data["axis_results"] = self.axis_results()
        data["passes_all_axes"] = self.passes_all_axes
        data["passes_texture_gate"] = self.passes_texture_gate
        data["passes_parallel"] = self.passes_parallel
        data["model_scored"] = self.model_scored
        data["passes_model_score"] = self.passes_model_score
        data["model_scored_v2"] = self.model_scored_v2
        data["passes_model_score_v2"] = self.passes_model_score_v2
        data["verdict"] = (
            "enforced" if self.enforced else "informational"
        )
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


def count_num_voices(notes: list[dict[str, int]]) -> int:
    """Number of distinct voice ids present in a generated.v1 note array.

    Read from the output rather than hardcoded: passacaglia and chorale_prelude
    are currently 2-voice and will move to 3 later, and the voice-count floors
    must track whatever the builder actually emits.
    """
    return len({int(note["voice"]) for note in notes})


def compute_final_quarter_avg_active(notes: list[dict[str, int]]) -> float:
    """Tick-weighted average active voices over the final quarter of the span.

    The piece span is [first onset, last offset] (the same span used by
    compute_texture_metrics). The final quarter is the last 1/4 of that span by
    ticks, so it is meter-independent. Notes are clipped to the window before
    the active-voice decomposition. Returns 0.0 for empty or degenerate input.
    """
    if not notes:
        return 0.0
    first = min(int(note["start_tick"]) for note in notes)
    last = max(int(note["start_tick"]) + int(note["duration"]) for note in notes)
    span = last - first
    if span <= 0:
        return 0.0
    window_begin = last - span // 4
    voices = sorted({int(note["voice"]) for note in notes})
    boundaries = sorted(
        {
            tick
            for note in notes
            for tick in (
                int(note["start_tick"]),
                int(note["start_tick"]) + int(note["duration"]),
            )
            if window_begin <= tick <= last
        }
        | {window_begin, last}
    )
    active_voice_ticks = 0
    total_ticks = 0
    for begin, end in zip(boundaries, boundaries[1:]):
        if end <= begin:
            continue
        active = sum(
            1 for voice in voices if any(_active_at(note, begin, end, voice) for note in notes)
        )
        span_width = end - begin
        active_voice_ticks += active * span_width
        total_ticks += span_width
    return active_voice_ticks / total_ticks if total_ticks > 0 else 0.0


def _active_at(note: dict[str, int], begin: int, end: int, voice: int) -> bool:
    return (
        int(note["voice"]) == voice
        and int(note["start_tick"]) < end
        and begin < int(note["start_tick"]) + int(note["duration"])
    )


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
        mono_ratio=metrics.mono_ratio,
        num_voices=count_num_voices(notes),
        max_silence_ratio=max(silence_by_voice.values(), default=0.0),
        v2_silence_ratio=silence_by_voice.get(2, 1.0),
        max_repeated_run=max((voice.max_repeated_run for voice in metrics.voices), default=0),
        compass_violation_count=metrics.compass_violation_count,
        register_overlap_ratio=metrics.register_overlap_ratio,
        piece_voice_occupancy=piece_voice_occupancy,
        min_piece_voice_occupancy=min_piece_voice_occupancy,
        final_quarter_avg_active=compute_final_quarter_avg_active(notes),
        middle_entry_bars=entry_bars,
        entry_intervals=entry_intervals,
        entry_plan_nonperiodic=entry_nonperiodic,
        fortspinnung_span_count=count_intent_spans(provenance, "FortspinnungSpan"),
        stretto_span_count=count_intent_spans(provenance, "StrettoCarrier"),
        parallel_perfect_count=parallel_perfect,
        hidden_perfect_count=hidden_perfect,
    )


def model_score_for(index_js: Path | None, generated_json: Path) -> tuple[float, float]:
    """Score a generated.json with bach-mcp's corpus models ("gate-3").

    Reuses bachlib.common.score_generated / model_probability so the gate and
    the closure harness obtain the model probability through the same path. A
    missing scorer (no index_js, or a node/bach-mcp error) returns -1.0 so the
    case is recorded as "not scored" rather than failing on a fabricated value.

    @param index_js Path to bach-mcp/dist/index.js, or None to skip scoring.
    @param generated_json The generated.v1 JSON to score.
    @return (v1 probability, v2 KL-model probability); -1.0 when unavailable.
        Both probabilities participate in pass/fail: v1 against the historical
        gate-3 floor, v2 against the form's KL-model floor.
    """
    if index_js is None or not index_js.exists():
        return -1.0, -1.0
    try:
        score = score_generated(index_js, generated_json)
    except (RuntimeError, OSError):
        return -1.0, -1.0
    return model_probability(score), model_probability_v2(score)


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
    case.model_score, case.model_score_v2 = model_score_for(index_js, generated_json)
    return case


def _median(values: list[float]) -> float:
    """Median of a value list (0.0 for empty), order-independent."""
    if not values:
        return 0.0
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0


def summarize_form(form: str, cases: list[GateCase]) -> dict[str, Any]:
    """Per-form breakdown labelled enforced vs informational.

    Reports each axis's pass count over the form's generated cases plus the
    metric ranges (avg-active min/median, mono-ratio range, occupancy floor,
    parallel ceiling, model-score range). The verdict label is `enforced` when
    the form's failures flip the exit code, else `informational`.
    """
    generated = [case for case in cases if case.generated]
    scored = [case.model_score for case in generated if case.model_scored]
    thresholds = thresholds_for(form)
    axis_pass_counts: dict[str, int] = {}
    axis_total: dict[str, int] = {}
    for case in generated:
        for axis, passed in case.axis_results().items():
            axis_total[axis] = axis_total.get(axis, 0) + 1
            axis_pass_counts[axis] = axis_pass_counts.get(axis, 0) + (1 if passed else 0)
    occupancy_floor = min(
        (case.min_piece_voice_occupancy for case in generated), default=0.0
    )
    return {
        "verdict": "enforced" if thresholds.enforced else "informational",
        "enforced": thresholds.enforced,
        "total": len(cases),
        "generated": len(generated),
        "num_voices": generated[0].num_voices if generated else 0,
        "all_axes_passed": all(case.passes_all_axes for case in generated) and bool(generated),
        "axis_pass_counts": {
            axis: {"passed": axis_pass_counts[axis], "total": axis_total[axis]}
            for axis in sorted(axis_total)
        },
        "min_avg_active_voices": min(
            (case.avg_active_voices for case in generated), default=0.0
        ),
        "median_avg_active_voices": _median(
            [case.avg_active_voices for case in generated]
        ),
        "target_min_avg_active": generated[0].min_avg_active if generated else None,
        "min_mono_ratio": min((case.mono_ratio for case in generated), default=0.0),
        "max_mono_ratio": max((case.mono_ratio for case in generated), default=0.0),
        "target_max_mono_ratio": thresholds.max_mono_ratio,
        "min_piece_voice_occupancy": occupancy_floor,
        "min_final_quarter_avg_active": min(
            (case.final_quarter_avg_active for case in generated), default=0.0
        ),
        "target_min_final_quarter_avg_active": thresholds.min_final_quarter_avg_active,
        "max_parallel_perfect_count": max(
            (case.parallel_perfect_count for case in generated), default=0
        ),
        "min_model_score": min(scored, default=0.0),
        "max_model_score": max(scored, default=0.0),
        "model_scored_cases": len(scored),
        "min_model_score_v2": min(
            (case.model_score_v2 for case in generated if case.model_score_v2 >= 0.0),
            default=0.0,
        ),
        "max_model_score_v2": max(
            (case.model_score_v2 for case in generated if case.model_score_v2 >= 0.0),
            default=0.0,
        ),
        "target_model_score_v2": (
            thresholds.model_score_v2_threshold
            if thresholds.model_score_v2_threshold is not None
            else MODEL_SCORE_V2_THRESHOLD
        ),
    }


def summarize(cases: list[GateCase]) -> dict[str, Any]:
    # Only enforced-form failures appear in `failing`: informational forms
    # always return passes_texture_gate == True, so all_passed (and the exit
    # code derived from it) reflects enforced failures alone.
    failing = [case for case in cases if not case.passes_texture_gate]
    scored = [case.model_score for case in cases if case.model_scored]
    forms = sorted({case.form for case in cases})
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
        "min_model_score_v2": min(
            (case.model_score_v2 for case in cases if case.model_scored_v2),
            default=0.0,
        ),
        "model_scored_v2_cases": sum(1 for case in cases if case.model_scored_v2),
        "max_parallel_perfect_count": max(
            (case.parallel_perfect_count for case in cases if case.generated), default=0
        ),
        "max_hidden_perfect_count": max(
            (case.hidden_perfect_count for case in cases if case.generated), default=0
        ),
        "parallel_perfect_threshold": MAX_PARALLEL_PERFECT_COUNT,
        "forms": {form: summarize_form(form, [c for c in cases if c.form == form]) for form in forms},
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


def model_scoring_unavailable(summary: dict[str, Any], no_model_score: bool) -> bool:
    """Whether gate-3 model scoring was required but silently unavailable.

    Returns True when scoring was requested (no ``--no-model-score``) and
    enforced, the sweep ran at least one case, yet no case obtained a model
    score -- i.e. the bach-mcp scorer was absent. In that state the gate must
    fail rather than report a green, unscored sweep.

    @param summary The summarize() result for the sweep.
    @param no_model_score True when ``--no-model-score`` was passed.
    @return True when the gate should hard-fail for a missing scorer.
    """
    return (
        not no_model_score
        and ENFORCE_MODEL_SCORE
        and summary.get("total", 0) > 0
        and summary.get("model_scored_cases", 0) == 0
    )


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

    summary = report["summary"]
    # gate-3 (model probability) must not be silently skipped: if model scoring
    # was requested and enforced but the scorer was unavailable for every case,
    # fail the gate rather than green-light an unscored sweep. This mirrors the
    # closure harness, which hard-fails on a missing scorer; passing
    # --no-model-score is the explicit opt-out.
    if model_scoring_unavailable(summary, args.no_model_score):
        print(
            f"texture-gate: gate-3 model scorer unavailable (index_js={args.index_js}); "
            "no case was scored. Pass --no-model-score to skip gate-3 explicitly.",
            file=sys.stderr,
        )
        return 2

    return 0 if summary["all_passed"] else 1


def main() -> int:
    """Standalone entry point reusing the shared argument surface."""
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
