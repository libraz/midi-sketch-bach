"""Harmonic estimation engine, cadence detection, suspension classification.

Pure functions operating on Score/Note data. The chord cache and I/O
wrappers remain in bach_reference_server.py.
"""

from __future__ import annotations

from typing import Any, NamedTuple, Optional

from .model import (
    TICKS_PER_BEAT,
    Note,
    Score,
    Track,
    interval_class,
    is_dissonant,
    sounding_note_at,
)
from .music_theory import (
    MAJOR_SCALE_SEMITONES,
    MINOR_SCALE_SEMITONES,
    beat_strength,
)

# ---------------------------------------------------------------------------
# Chord templates
# ---------------------------------------------------------------------------

CHORD_TEMPLATES: dict[str, frozenset[int]] = {
    "M": frozenset({0, 4, 7}),
    "m": frozenset({0, 3, 7}),
    "dim": frozenset({0, 3, 6}),
    "aug": frozenset({0, 4, 8}),
    "dom7": frozenset({0, 4, 7, 10}),
    "m7": frozenset({0, 3, 7, 10}),
    "dim7": frozenset({0, 3, 6, 9}),
    "hdim7": frozenset({0, 3, 6, 10}),
}

# Markov transition prior for Bach progressions (scale-degree bigrams).
TRANSITION_PRIOR: dict[tuple[int, int], float] = {
    (4, 0): 1.5, (3, 4): 1.3, (1, 4): 1.3, (0, 3): 1.2,
    (0, 4): 1.2, (4, 5): 1.2, (5, 3): 1.1, (6, 0): 1.2,
}

DEGREE_TO_FUNCTION: dict[int, str] = {
    0: "T", 1: "S", 2: "M", 3: "S", 4: "D", 5: "T", 6: "D",
}

MAJOR_NUMERALS = ["I", "ii", "iii", "IV", "V", "vi", "vii\u00b0"]
MINOR_NUMERALS = ["i", "ii\u00b0", "III", "iv", "V", "VI", "vii\u00b0"]


# ---------------------------------------------------------------------------
# ChordEstimate
# ---------------------------------------------------------------------------


class ChordEstimate(NamedTuple):
    """Estimated chord at a sample point."""
    root_pc: int
    quality: str
    degree: int
    inversion: str
    confidence: float
    bass_pc: Optional[int]


# ---------------------------------------------------------------------------
# Chord support extraction
# ---------------------------------------------------------------------------


def extract_chord_support_pcs(
    tracks_notes: list[tuple[str, list[Note]]],
    tick: int,
    tpb: int,
) -> tuple[dict[int, float], Optional[int]]:
    """Extract weighted pitch-class support at a given tick.

    Returns:
        (pc_weights, bass_pc) where pc_weights maps pitch class to
        accumulated weight, and bass_pc is the pitch class of the lowest
        voice's sounding note (or None).
    """
    pc_weights: dict[int, float] = {}
    bass_pc: Optional[int] = None
    num_tracks = len(tracks_notes)
    tick_strength = beat_strength(tick)

    for track_idx, (role, sorted_notes) in enumerate(tracks_notes):
        note = sounding_note_at(sorted_notes, tick)
        if note is None:
            continue

        dur_beats = note.duration / tpb
        is_bass = (track_idx == num_tracks - 1)

        if dur_beats < 0.25:
            note_start_strength = beat_strength(note.start_tick)
            weight = 0.2 if note_start_strength >= 0.5 else 0.1
        else:
            duration_weight = min(dur_beats, 2.0) / 2.0
            metric_weight = beat_strength(note.start_tick)

            if note.start_tick < tick and tick_strength >= 0.5:
                metric_weight = max(metric_weight, 0.8)
            if tick_strength >= 0.75:
                metric_weight += 0.1

            voice_weight = 1.2 if is_bass else 1.0
            weight = duration_weight * metric_weight * voice_weight

        pitch_class = note.pitch % 12
        pc_weights[pitch_class] = pc_weights.get(pitch_class, 0.0) + weight

        if is_bass:
            bass_pc = pitch_class

    return pc_weights, bass_pc


# ---------------------------------------------------------------------------
# Chord estimation
# ---------------------------------------------------------------------------


def estimate_chord(
    weighted_pcs: dict[int, float],
    bass_pc: Optional[int],
    tonic: int,
    is_minor: bool,
    prev_chord: Optional[ChordEstimate] = None,
) -> ChordEstimate:
    """Estimate the most likely chord from weighted pitch classes.

    Uses template matching with inversion scoring and Markov priors.
    """
    if not weighted_pcs:
        return ChordEstimate(
            root_pc=0, quality="", degree=0,
            inversion="root", confidence=0.0, bass_pc=bass_pc,
        )

    total_weight = sum(weighted_pcs.values())
    if total_weight < 1e-9:
        return ChordEstimate(
            root_pc=0, quality="", degree=0,
            inversion="root", confidence=0.0, bass_pc=bass_pc,
        )

    pc_set = set(weighted_pcs.keys())
    num_pcs = len(pc_set)

    if num_pcs <= 1:
        single_pc = next(iter(pc_set)) if pc_set else 0
        scale = MINOR_SCALE_SEMITONES if is_minor else MAJOR_SCALE_SEMITONES
        rel = (single_pc - tonic) % 12
        degree = 0
        min_dist = 12
        for deg_idx, sem in enumerate(scale):
            dist = min(abs(rel - sem), 12 - abs(rel - sem))
            if dist < min_dist:
                min_dist = dist
                degree = deg_idx
        return ChordEstimate(
            root_pc=single_pc, quality="",
            degree=degree, inversion="root",
            confidence=0.15, bass_pc=bass_pc,
        )

    scale = MINOR_SCALE_SEMITONES if is_minor else MAJOR_SCALE_SEMITONES
    best_score = -1.0
    best_estimate = ChordEstimate(
        root_pc=0, quality="", degree=0,
        inversion="root", confidence=0.0, bass_pc=bass_pc,
    )
    second_best_score = -1.0

    for root in range(12):
        for quality, template in CHORD_TEMPLATES.items():
            transposed = frozenset((root + pc) % 12 for pc in template)
            matched_weight = sum(
                weighted_pcs.get(pc, 0.0) for pc in transposed
            )
            matched_pcs = len(transposed & pc_set)
            template_size = len(template)
            coverage = matched_pcs / template_size

            if template_size >= 4 and coverage < 0.75:
                continue

            base_score = matched_weight / total_weight
            base_score *= coverage

            extra_pcs = len(pc_set - transposed)
            if extra_pcs > 0:
                base_score *= 1.0 / (1.0 + 0.15 * extra_pcs)

            inversion = "root"
            if bass_pc is not None:
                bass_offset = (bass_pc - root) % 12
                if bass_offset == 0:
                    pass
                elif bass_offset in (3, 4):
                    base_score += 0.05
                    inversion = "1st"
                elif bass_offset in (6, 7, 8):
                    base_score += 0.03
                    inversion = "2nd"
                elif bass_offset in (9, 10):
                    base_score += 0.02
                    inversion = "3rd"
                elif bass_pc not in transposed:
                    base_score -= 0.1

            rel_pc = (root - tonic) % 12
            degree = 0
            min_dist = 12
            for deg_idx, sem in enumerate(scale):
                dist = min(abs(rel_pc - sem), 12 - abs(rel_pc - sem))
                if dist < min_dist:
                    min_dist = dist
                    degree = deg_idx

            if prev_chord is not None and prev_chord.confidence > 0.0:
                pair = (prev_chord.degree, degree)
                prior = TRANSITION_PRIOR.get(pair, 1.0)
                base_score *= 1.0 + 0.2 * (prior - 1.0)

            if base_score > best_score:
                second_best_score = best_score
                best_score = base_score
                best_estimate = ChordEstimate(
                    root_pc=root,
                    quality=quality,
                    degree=degree,
                    inversion=inversion,
                    confidence=0.0,
                    bass_pc=bass_pc,
                )
            elif base_score > second_best_score:
                second_best_score = base_score

    if best_score <= 0:
        return best_estimate

    uniqueness = (
        best_score / second_best_score
        if second_best_score > 0 else 2.0
    )

    best_template = CHORD_TEMPLATES.get(best_estimate.quality, frozenset())
    best_transposed = frozenset(
        (best_estimate.root_pc + pc) % 12 for pc in best_template
    )
    best_coverage = (
        len(best_transposed & pc_set) / len(best_template)
        if best_template else 0.0
    )

    confidence = best_coverage * min(uniqueness, 2.0) / 2.0

    if best_coverage < 0.4:
        confidence = min(confidence, 0.3)
    if uniqueness < 1.1:
        confidence *= 0.8
    if num_pcs <= 2:
        confidence = min(confidence, 0.6)

    return ChordEstimate(
        root_pc=best_estimate.root_pc,
        quality=best_estimate.quality,
        degree=best_estimate.degree,
        inversion=best_estimate.inversion,
        confidence=confidence,
        bass_pc=best_estimate.bass_pc,
    )


# ---------------------------------------------------------------------------
# Chord estimation for a score (pure — no I/O, no cache)
# ---------------------------------------------------------------------------


def estimate_chords_for_score(
    tracks_notes: list[tuple[str, list[Note]]],
    tonic_pc: int,
    is_minor: bool,
    tpb: int,
    max_tick: int,
    sample_interval_beats: float = 1.0,
) -> tuple[list[ChordEstimate], dict[str, Any]]:
    """Estimate chords for all sample points in pre-loaded track data.

    Args:
        tracks_notes: List of (role, sorted_notes) pairs.
        tonic_pc: Tonic pitch class (0-11).
        is_minor: True for minor key.
        tpb: Ticks per beat.
        max_tick: Total duration in ticks.
        sample_interval_beats: Sampling interval in beats.

    Returns:
        (chord_estimates, stats).
    """
    if not tracks_notes:
        return [], {
            "mean_confidence": 0.0,
            "unclassified_ratio": 1.0,
            "low_confidence_ratio": 1.0,
        }

    sample_ticks = int(sample_interval_beats * tpb)
    if sample_ticks <= 0:
        sample_ticks = tpb

    chords: list[ChordEstimate] = []
    prev_chord: Optional[ChordEstimate] = None
    tick = 0

    while tick < max_tick:
        weighted_pcs, bass_pc = extract_chord_support_pcs(
            tracks_notes, tick, tpb,
        )
        chord = estimate_chord(
            weighted_pcs, bass_pc, tonic_pc, is_minor, prev_chord,
        )
        chords.append(chord)
        if chord.confidence > 0.0:
            prev_chord = chord
        tick += sample_ticks

    total = len(chords)
    if total == 0:
        stats: dict[str, Any] = {
            "mean_confidence": 0.0,
            "unclassified_ratio": 1.0,
            "low_confidence_ratio": 1.0,
        }
    else:
        confidences = [c.confidence for c in chords]
        unclassified = sum(1 for c in chords if c.quality == "")
        low_conf = sum(1 for c in chords if 0.0 < c.confidence < 0.4)
        stats = {
            "mean_confidence": round(sum(confidences) / total, 3),
            "unclassified_ratio": round(unclassified / total, 3),
            "low_confidence_ratio": round(low_conf / total, 3),
        }

    return chords, stats


# ---------------------------------------------------------------------------
# Roman numeral conversion
# ---------------------------------------------------------------------------


def degree_to_roman(degree: int, quality: str, is_minor: bool) -> str:
    """Convert scale degree + quality to Roman numeral label."""
    numerals = MINOR_NUMERALS if is_minor else MAJOR_NUMERALS
    if 0 <= degree < len(numerals):
        base = numerals[degree]
    else:
        base = str(degree)

    root_numeral = (
        ["I", "II", "III", "IV", "V", "VI", "VII"][degree]
        if 0 <= degree < 7 else str(degree)
    )
    if quality in ("m", "m7"):
        return root_numeral.lower()
    elif quality in ("dim", "dim7", "hdim7"):
        return root_numeral.lower() + "\u00b0"
    elif quality == "aug":
        return root_numeral + "+"
    elif quality in ("M", "dom7"):
        return root_numeral
    return base


# ---------------------------------------------------------------------------
# Suspension detection
# ---------------------------------------------------------------------------


def detect_suspension(
    note: Note,
    prev_note: Optional[Note],
    next_note: Optional[Note],
    chord_est: ChordEstimate,
    next_chord_est: Optional[ChordEstimate],
) -> bool:
    """Detect whether a note is a suspension."""
    if prev_note is None or next_note is None:
        return False
    if note.pitch != prev_note.pitch:
        return False
    if beat_strength(note.start_tick) < 0.5:
        return False
    resolution_interval = note.pitch - next_note.pitch
    if resolution_interval < 1 or resolution_interval > 2:
        return False
    return True


def classify_suspension_pattern(
    note: Note,
    next_note: Optional[Note],
    chord_est: ChordEstimate,
) -> Optional[str]:
    """Classify a suspension into 4-3, 7-6, or 9-8 pattern."""
    if next_note is None or chord_est.quality == "":
        return None

    root_pc = chord_est.root_pc
    susp_pc = note.pitch % 12
    interval_from_root = (susp_pc - root_pc) % 12
    resolution_semitones = note.pitch - next_note.pitch

    if resolution_semitones < 1 or resolution_semitones > 2:
        return None

    if interval_from_root == 5:
        return "4-3"
    if interval_from_root in (10, 11):
        return "7-6"
    if interval_from_root in (1, 2):
        return "9-8"

    return None


# ---------------------------------------------------------------------------
# Duration categorization
# ---------------------------------------------------------------------------


def categorize_duration(dur_ticks: int) -> str:
    """Categorize note duration in ticks to a rhythm name."""
    beats = dur_ticks / TICKS_PER_BEAT
    if beats <= 0.1875:
        return "32nd"
    if beats <= 0.375:
        return "16th"
    if beats <= 0.75:
        return "8th"
    if beats <= 1.5:
        return "quarter"
    if beats <= 3.0:
        return "half"
    if beats <= 6.0:
        return "whole"
    return "longer"


# ---------------------------------------------------------------------------
# Bass track detection
# ---------------------------------------------------------------------------


def detect_bass_track(score: Score) -> Optional[Track]:
    """Auto-detect the bass track from a score.

    Priority: track named 'pedal' > 'lower' > 'bass' > lowest avg pitch.
    """
    if not score.tracks:
        return None

    for name in ("pedal", "lower", "bass"):
        for trk in score.tracks:
            if trk.name == name:
                return trk

    best_track: Optional[Track] = None
    best_avg = 999.0
    for trk in score.tracks:
        if not trk.sorted_notes:
            continue
        avg_pitch = sum(n.pitch for n in trk.sorted_notes) / len(trk.sorted_notes)
        if avg_pitch < best_avg:
            best_avg = avg_pitch
            best_track = trk
    return best_track
