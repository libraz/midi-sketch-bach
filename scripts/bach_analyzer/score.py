"""Multi-dimensional Bach reference scoring.

Computes a numerical score (0-100) measuring how closely generated output
matches Bach's compositional patterns, based on reference data from 270 works.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

from .model import (
    TICKS_PER_BEAT,
    TICKS_PER_BAR,
    Note,
    NoteSource,
    Score,
    interval_class,
    is_consonant,
    is_perfect_consonance,
    sounding_note_at,
)
from .music_theory import INTERVAL_NAME_MAP as _INTERVAL_NAMES
from .profiles import (
    normalize as _normalize,
    js_divergence as jsd,
    jsd_to_points,
    compute_zscore,
    zscore_to_points,
    count_melodic_intervals,
    bin_durations,
    sample_vertical_intervals,
    sample_motion_types,
    sample_texture_density,
)
from .reference import load_category
from .rules.base import RuleResult, Severity


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class SubScore:
    """Individual metric within a dimension."""
    name: str
    value: float
    reference: float
    score: float
    method: str  # "jsd" or "zscore"
    detail: str = ""


@dataclass
class DimensionScore:
    """Score for one of the 6 dimensions."""
    dimension: str
    score: float
    applicable: bool
    sub_scores: List[SubScore] = field(default_factory=list)
    penalty: float = 0.0


@dataclass
class BachScore:
    """Complete scoring result."""
    total: float
    grade: str
    dimensions: Dict[str, DimensionScore] = field(default_factory=dict)
    form: str = ""
    category: str = ""


# ---------------------------------------------------------------------------
# Grade mapping
# ---------------------------------------------------------------------------


def _grade(score: float) -> str:
    if score >= 90:
        return "A"
    elif score >= 75:
        return "B"
    elif score >= 60:
        return "C"
    elif score >= 40:
        return "D"
    else:
        return "F"


# ---------------------------------------------------------------------------
# Profile extraction from Score
# ---------------------------------------------------------------------------


def extract_interval_profile(score: Score) -> Dict[str, Any]:
    """Extract melodic interval distribution from a Score."""
    all_counts: Dict[str, int] = {name: 0 for name in _INTERVAL_NAMES.values()}
    total = 0
    total_semitones = 0
    step_count = 0
    leap_count = 0

    for track in score.tracks:
        ic_counts, steps, leaps, semis = count_melodic_intervals(track.sorted_notes)
        for ic, cnt in ic_counts.items():
            name = _INTERVAL_NAMES.get(ic, "P1")
            all_counts[name] += cnt
        total += sum(ic_counts.values())
        total_semitones += semis
        step_count += steps
        leap_count += leaps

    return {
        "distribution": _normalize({k: float(v) for k, v in all_counts.items()}),
        "stepwise_ratio": step_count / total if total else 0.0,
        "leap_ratio": leap_count / total if total else 0.0,
        "avg_interval": total_semitones / total if total else 0.0,
        "total": total,
    }


def extract_rhythm_profile(score: Score) -> Dict[str, Any]:
    """Extract rhythm/duration distribution from a Score."""
    all_notes = [n for track in score.tracks for n in track.notes]
    bins = bin_durations(all_notes)
    return {"distribution": _normalize({k: float(v) for k, v in bins.items()})}


def extract_vertical_profile(score: Score) -> Dict[str, Any]:
    """Extract vertical interval distribution between voice pairs."""
    if score.num_voices < 2:
        return {"distribution": {}, "consonance_ratio": 0.0,
                "perfect_consonance_ratio": 0.0, "applicable": False}

    voices = score.voices_dict
    result = sample_vertical_intervals(voices, score.total_duration, TICKS_PER_BEAT)

    named_counts: Dict[str, int] = {name: 0 for name in _INTERVAL_NAMES.values()}
    for ic, cnt in result["counts"].items():
        name = _INTERVAL_NAMES.get(ic, "P1")
        named_counts[name] += cnt
    total = result["total"]

    return {
        "distribution": _normalize({k: float(v) for k, v in named_counts.items()}),
        "consonance_ratio": result["consonant"] / total if total else 0.0,
        "perfect_consonance_ratio": result["perfect"] / total if total else 0.0,
        "applicable": total > 0,
    }


def extract_motion_profile(score: Score) -> Dict[str, Any]:
    """Extract voice motion type distribution."""
    if score.num_voices < 2:
        return {"distribution": {}, "applicable": False}

    voices = score.voices_dict
    result = sample_motion_types(voices, score.total_duration, TICKS_PER_BEAT)
    motion = result["motion"]
    total = result["total"]

    dist = _normalize({k: float(v) for k, v in motion.items()}) if total else {}
    return {
        "distribution": dist,
        "contrary_ratio": motion["contrary"] / total if total else 0.0,
        "parallel_ratio": motion["parallel"] / total if total else 0.0,
        "applicable": total > 0,
    }


def extract_texture_profile(score: Score) -> Dict[str, Any]:
    """Extract texture density distribution."""
    voices = score.voices_dict
    result = sample_texture_density(voices, score.total_duration, TICKS_PER_BEAT)
    density_counts = result["density_counts"]
    total_samples = result["total_samples"]
    total_active = result["total_active"]

    return {
        "distribution": _normalize({k: float(v) for k, v in density_counts.items()}),
        "avg_active_voices": total_active / total_samples if total_samples else 0.0,
    }


def extract_bass_per_beat(score: Score) -> List[Optional[int]]:
    """Extract the lowest sounding pitch class at each beat position.

    Returns a list of pitch classes (0-11) or None where no note sounds.
    Used by harmonic function, degree, and cadence extraction.
    """
    voices = score.voices_dict
    voice_names = list(voices.keys())
    total_ticks = score.total_duration
    sample_step = TICKS_PER_BEAT

    bass_pcs: List[Optional[int]] = []
    for tick in range(0, total_ticks, sample_step):
        lowest_pitch: Optional[int] = None
        for vname in voice_names:
            note = sounding_note_at(voices[vname], tick)
            if note is not None:
                if lowest_pitch is None or note.pitch < lowest_pitch:
                    lowest_pitch = note.pitch
        if lowest_pitch is not None:
            bass_pcs.append(lowest_pitch % 12)
        else:
            bass_pcs.append(None)
    return bass_pcs


# Bass pitch-class to harmonic function mapping (C major internal)
# Diatonic: C=T, D=S, E=M, F=S, G=D, A=T, B=D
# Chromatic tones mapped to nearest functional category
_BASS_PC_TO_FUNCTION: Dict[int, str] = {
    0: "T",   # C -> I (Tonic)
    1: "D",   # C#/Db -> secondary dominant area
    2: "S",   # D -> ii (Subdominant)
    3: "S",   # D#/Eb -> borrowed bIII area (Subdominant)
    4: "M",   # E -> iii (Mediant)
    5: "S",   # F -> IV (Subdominant)
    6: "D",   # F#/Gb -> tritone, dominant function
    7: "D",   # G -> V (Dominant)
    8: "M",   # G#/Ab -> bVI area (Mediant)
    9: "T",   # A -> vi (Tonic)
    10: "D",  # A#/Bb -> bVII, dominant area
    11: "D",  # B -> vii (Dominant)
}

# Bass pitch-class to Roman numeral degree (C major internal)
_BASS_PC_TO_DEGREE: Dict[int, str] = {
    0: "I", 1: "bII", 2: "ii", 3: "bIII", 4: "iii", 5: "IV",
    6: "#IV", 7: "V", 8: "bVI", 9: "vi", 10: "bVII", 11: "vii",
}


def _extract_function_distribution(score: Score) -> Dict[str, float]:
    """Extract harmonic function distribution from bass pitch classes.

    Samples every beat, finds the lowest sounding pitch, and maps its
    pitch class to a harmonic function (T/S/D/M) using C major mapping.
    """
    bass_pcs = extract_bass_per_beat(score)
    counts: Dict[str, int] = {"T": 0, "S": 0, "D": 0, "M": 0}
    for pitch_class in bass_pcs:
        if pitch_class is not None:
            func = _BASS_PC_TO_FUNCTION.get(pitch_class, "T")
            counts[func] += 1
    return _normalize({k: float(v) for k, v in counts.items()})


def _extract_degree_distribution(score: Score) -> Dict[str, float]:
    """Extract Roman numeral degree distribution from bass pitch classes.

    Same sampling approach as function distribution, but maps to specific
    scale degrees (I, ii, iii, IV, V, vi, vii, plus chromatic alterations).
    """
    bass_pcs = extract_bass_per_beat(score)
    counts: Dict[str, int] = {}
    for pitch_class in bass_pcs:
        if pitch_class is not None:
            degree = _BASS_PC_TO_DEGREE.get(pitch_class, "I")
            counts[degree] = counts.get(degree, 0) + 1
    return _normalize({k: float(v) for k, v in counts.items()})


def _count_cadences_simplified(score: Score) -> float:
    """Count V->I bass motions per 8 bars (simplified cadence detection).

    Looks for bass pitch class 7 (G) followed by 0 (C) at adjacent beats,
    where the arrival (C) is on a strong beat (beat 1 or 3).
    """
    bass_pcs = extract_bass_per_beat(score)
    total_bars = score.total_bars
    if total_bars == 0:
        return 0.0

    cadence_count = 0
    for idx in range(1, len(bass_pcs)):
        prev_pc = bass_pcs[idx - 1]
        curr_pc = bass_pcs[idx]
        if prev_pc == 7 and curr_pc == 0:
            # Check if the arrival is on a strong beat (beat 1 or 3)
            tick = idx * TICKS_PER_BEAT
            beat_in_bar = (tick % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
            if beat_in_bar in (1, 3):
                cadence_count += 1

    # Normalize to per-8-bars
    return cadence_count * 8.0 / total_bars


def _extract_nct_distribution(score: Score) -> Dict[str, float]:
    """Extract simplified non-chord-tone type distribution.

    For each beat, determines chord tones from the bass pitch class (assuming
    root-position triads in C major), then classifies non-chord tones by
    their melodic context: passing (stepwise approach and departure in same
    direction), neighbor (stepwise approach and return), ornamental (toccata
    figuration notes identified by provenance), and other.

    Notes with ``toccata_figure`` provenance source are always classified as
    ``ornamental`` regardless of their intervallic behaviour, because toccata
    figurations follow gesture templates rather than standard NCT patterns.

    The returned distribution excludes chord tones so that it is directly
    comparable with reference NCT profiles.
    """
    if score.num_voices < 2:
        return {}

    # C major diatonic triads by bass PC (root, 3rd, 5th as PCs)
    _TRIADS: Dict[int, frozenset] = {
        0: frozenset({0, 4, 7}),    # C: C-E-G
        2: frozenset({2, 5, 9}),    # Dm: D-F-A
        4: frozenset({4, 7, 11}),   # Em: E-G-B
        5: frozenset({5, 9, 0}),    # F: F-A-C
        7: frozenset({7, 11, 2}),   # G: G-B-D
        9: frozenset({9, 0, 4}),    # Am: A-C-E
        11: frozenset({11, 2, 5}),  # Bdim: B-D-F
    }
    # Default triad for chromatic bass notes
    _DEFAULT_TRIAD = frozenset({0, 4, 7})

    bass_pcs = extract_bass_per_beat(score)
    nct_types: Dict[str, int] = {
        "passing": 0, "neighbor": 0, "ornamental": 0, "other": 0,
    }

    for track in score.tracks:
        notes = track.sorted_notes
        for note_idx in range(len(notes)):
            note = notes[note_idx]

            # Toccata figuration notes are always classified as ornamental,
            # before any intervallic analysis.
            if note.provenance and note.provenance.source == NoteSource.TOCCATA_FIGURE:
                nct_types["ornamental"] += 1
                continue

            # Determine which beat this note falls on
            beat_idx = note.start_tick // TICKS_PER_BEAT
            if beat_idx < 0 or beat_idx >= len(bass_pcs):
                continue
            bass_pc = bass_pcs[beat_idx]
            if bass_pc is None:
                continue
            chord_pcs = _TRIADS.get(bass_pc, _DEFAULT_TRIAD)
            note_pc = note.pitch % 12

            if note_pc in chord_pcs:
                # Chord tones are excluded from the NCT distribution.
                continue
            else:
                # Classify by melodic context
                prev_interval = None
                next_interval = None
                if note_idx > 0:
                    prev_interval = note.pitch - notes[note_idx - 1].pitch
                if note_idx < len(notes) - 1:
                    next_interval = notes[note_idx + 1].pitch - note.pitch

                is_step = lambda iv: iv is not None and abs(iv) <= 2  # noqa: E731
                if is_step(prev_interval) and is_step(next_interval):
                    if prev_interval is not None and next_interval is not None:
                        if (prev_interval > 0 and next_interval > 0) or \
                           (prev_interval < 0 and next_interval < 0):
                            nct_types["passing"] += 1
                        else:
                            nct_types["neighbor"] += 1
                    else:
                        nct_types["other"] += 1
                else:
                    nct_types["other"] += 1

    return _normalize({k: float(v) for k, v in nct_types.items()})


def _count_parallel_perfects(score: Score) -> float:
    """Count parallel perfect consonances per 100 beats.

    Detects parallel P5->P5 and P8->P8 (including P1->P1) motion between
    all voice pairs, sampling at each beat.
    """
    if score.num_voices < 2:
        return 0.0

    voices = score.voices_dict
    voice_names = list(voices.keys())
    total_ticks = score.total_duration
    sample_step = TICKS_PER_BEAT
    total_beats = total_ticks // sample_step if sample_step > 0 else 0

    if total_beats < 2:
        return 0.0

    parallel_count = 0

    for vi_idx in range(len(voice_names)):
        for vj_idx in range(vi_idx + 1, len(voice_names)):
            notes_a = voices[voice_names[vi_idx]]
            notes_b = voices[voice_names[vj_idx]]

            prev_a = sounding_note_at(notes_a, 0)
            prev_b = sounding_note_at(notes_b, 0)

            for tick in range(sample_step, total_ticks, sample_step):
                curr_a = sounding_note_at(notes_a, tick)
                curr_b = sounding_note_at(notes_b, tick)

                if prev_a and prev_b and curr_a and curr_b:
                    motion_a = curr_a.pitch - prev_a.pitch
                    motion_b = curr_b.pitch - prev_b.pitch

                    # Both voices must move in the same direction (parallel)
                    if motion_a != 0 and motion_b != 0 and \
                       ((motion_a > 0 and motion_b > 0) or (motion_a < 0 and motion_b < 0)):
                        prev_ic = abs(prev_a.pitch - prev_b.pitch) % 12
                        curr_ic = abs(curr_a.pitch - curr_b.pitch) % 12
                        # P5->P5 or P8/P1->P8/P1
                        if prev_ic == curr_ic and prev_ic in (0, 7):
                            parallel_count += 1

                prev_a = curr_a
                prev_b = curr_b

    return parallel_count * 100.0 / total_beats


def extract_voice_entries(score: Score) -> Dict[str, Any]:
    """Extract voice entry pattern from provenance data."""
    entries = []
    for track in score.tracks:
        entry_sources = {NoteSource.FUGUE_SUBJECT, NoteSource.FUGUE_ANSWER}
        first_tick = None
        first_pitch = None
        for note in track.sorted_notes:
            if note.provenance and note.provenance.source in entry_sources:
                first_tick = note.start_tick
                first_pitch = note.pitch
                break
        if first_tick is not None:
            entries.append({
                "voice": track.name,
                "tick": first_tick,
                "pitch": first_pitch,
            })

    entries.sort(key=lambda e: e["tick"])

    # Compute intervals between entries
    entry_intervals = []
    for i in range(1, len(entries)):
        semi = abs(entries[i]["pitch"] - entries[i - 1]["pitch"])
        ic = interval_class(semi)
        beat_gap = (entries[i]["tick"] - entries[i - 1]["tick"]) / TICKS_PER_BEAT
        entry_intervals.append({
            "interval_class": ic,
            "interval_name": _INTERVAL_NAMES.get(ic, "?"),
            "beat_gap": beat_gap,
            "is_fifth_or_fourth": ic in (5, 7),
        })

    has_entries = len(entries) >= 2
    fifth_fourth_ratio = 0.0
    if entry_intervals:
        fifth_fourth_ratio = sum(
            1 for e in entry_intervals if e["is_fifth_or_fourth"]
        ) / len(entry_intervals)

    return {
        "entries": entries,
        "entry_intervals": entry_intervals,
        "has_entries": has_entries,
        "fifth_fourth_ratio": fifth_fourth_ratio,
    }


# ---------------------------------------------------------------------------
# Violation penalty integration
# ---------------------------------------------------------------------------

_DIMENSION_RULE_MAP: Dict[str, set] = {
    "harmony": {"strong_beat_dissonance", "unresolved_dissonance"},
    "counterpoint": {"parallel_perfect", "hidden_perfect", "voice_crossing",
                     "cross_relation", "augmented_leap", "voice_interleaving"},
    "melody": {"excessive_leap", "leap_resolution", "stepwise_motion_ratio",
               "consecutive_repeated_notes"},
    "structure": {"exposition_completeness"},
    "rhythm": {"rhythm_diversity"},
    "texture": {"voice_independence", "voice_spacing"},
}

_SEVERITY_WEIGHTS = {
    Severity.CRITICAL: 5,
    Severity.ERROR: 3,
    Severity.WARNING: 1,
    Severity.INFO: 0,
}


def _compute_penalties(results: List[RuleResult]) -> Dict[str, float]:
    """Compute per-dimension penalty from validation results."""
    penalties: Dict[str, float] = {}
    for dim, rules in _DIMENSION_RULE_MAP.items():
        raw = 0.0
        for result in results:
            if result.rule_name in rules:
                for v in result.violations:
                    raw += _SEVERITY_WEIGHTS.get(v.severity, 0)
        penalties[dim] = min(20.0, raw)
    return penalties


# ---------------------------------------------------------------------------
# Dimension weights
# ---------------------------------------------------------------------------

_DIMENSION_WEIGHTS = {
    "structure": 0.10,
    "melody": 0.20,
    "harmony": 0.25,
    "counterpoint": 0.20,
    "rhythm": 0.15,
    "texture": 0.10,
}


# ---------------------------------------------------------------------------
# Form-specific structure helpers
# ---------------------------------------------------------------------------

# Forms that use ground bass repetition as a structural element.
_GROUND_BASS_FORMS = {"passacaglia", "chaconne"}

# Forms that use a cantus firmus (chorale melody) as a structural element.
_CANTUS_FIRMUS_FORMS = {"chorale_prelude"}

# Forms with a toccata section preceding a fugue section.
_TOCCATA_FUGUE_FORMS = {"toccata_and_fugue", "fantasia_and_fugue"}

# Note sources that indicate fugue material (as opposed to toccata/free sections).
_FUGUE_SOURCES = frozenset({
    NoteSource.FUGUE_SUBJECT,
    NoteSource.FUGUE_ANSWER,
    NoteSource.COUNTERSUBJECT,
    NoteSource.EPISODE_MATERIAL,
    NoteSource.FALSE_ENTRY,
    NoteSource.SEQUENCE_NOTE,
})

# Organ prelude/fantasia texture reference (organ_pf).
# Derived from Bach organ preludes, toccatas, and fantasias: avg_active ~2.75
# voices, with more single-voice and two-voice passages than pure fugues.
_ORGAN_PF_TEXTURE_REF: Dict[str, float] = {
    "0": 0.02,
    "1": 0.15,
    "2": 0.33,
    "3": 0.38,
    "4": 0.12,
}
_ORGAN_PF_AVG_ACTIVE: Dict[str, Any] = {
    "mean": 2.75,
    "std": 0.5,
}


def _detect_fugue_boundary_tick(score: Score) -> Optional[int]:
    """Detect the tick where the fugue section starts in a toccata+fugue form.

    Scans all notes for the earliest note with a fugue-specific provenance
    source (subject, answer, countersubject, episode_material, etc.).

    Returns:
        The start_tick of the first fugue note, or None if no fugue material found.
    """
    earliest_fugue_tick: Optional[int] = None
    for track in score.tracks:
        for note in track.sorted_notes:
            if note.provenance and note.provenance.source in _FUGUE_SOURCES:
                if earliest_fugue_tick is None or note.start_tick < earliest_fugue_tick:
                    earliest_fugue_tick = note.start_tick
                break  # Only need the first fugue note per track
    return earliest_fugue_tick


def _extract_degree_distribution_for_tick_range(
    score: Score,
    start_tick: int,
    end_tick: int,
) -> Tuple[Dict[str, float], int]:
    """Extract Roman numeral degree distribution for a tick range.

    Returns:
        Tuple of (normalized distribution, note count in range).
    """
    bass_pcs = extract_bass_per_beat(score)
    counts: Dict[str, int] = {}
    note_count = 0

    start_beat = start_tick // TICKS_PER_BEAT
    end_beat = end_tick // TICKS_PER_BEAT

    for beat_idx in range(start_beat, min(end_beat, len(bass_pcs))):
        pitch_class = bass_pcs[beat_idx]
        if pitch_class is not None:
            degree = _BASS_PC_TO_DEGREE.get(pitch_class, "I")
            counts[degree] = counts.get(degree, 0) + 1
            note_count += 1

    return _normalize({k: float(v) for k, v in counts.items()}), note_count


def _detect_ground_bass_regularity(score: Score) -> float:
    """Detect periodic bass patterns using pitch-class periodicity.

    Scores based on:
      - Period regularity: low variance in the detected period length.
      - Cadence anchor hit rate: tonic pitch class (PC=0, C major internal)
        appearing at regular period boundaries.

    Returns a score in 0-100.
    """
    bass_pcs = extract_bass_per_beat(score)
    if len(bass_pcs) < 8:
        return 0.0

    total_bars = score.total_bars
    if total_bars < 4:
        return 0.0

    # Build a bar-level bass pitch-class sequence (use beat 1 of each bar).
    beats_per_bar = TICKS_PER_BAR // TICKS_PER_BEAT  # 4
    bar_bass: List[Optional[int]] = []
    for bar_idx in range(total_bars):
        beat_idx = bar_idx * beats_per_bar
        if beat_idx < len(bass_pcs):
            bar_bass.append(bass_pcs[beat_idx])
        else:
            bar_bass.append(None)

    # Try candidate periods from 2 to 16 bars.
    best_period = 0
    best_similarity = 0.0

    for period in range(2, min(17, total_bars // 2 + 1)):
        # Compare each period-sized block to the first block using
        # pitch-class transposition: allow the pattern to be transposed
        # by a constant interval.
        if period > len(bar_bass):
            continue
        ref_block = bar_bass[:period]
        num_blocks = len(bar_bass) // period
        if num_blocks < 2:
            continue

        match_count = 0
        total_comparisons = 0

        for blk_idx in range(1, num_blocks):
            offset = blk_idx * period
            curr_block = bar_bass[offset:offset + period]

            # Find the transposition interval from the first non-None pair.
            transpose = None
            for pos in range(period):
                if ref_block[pos] is not None and curr_block[pos] is not None:
                    transpose = (curr_block[pos] - ref_block[pos]) % 12
                    break

            if transpose is None:
                continue

            # Count how many positions match under this transposition.
            for pos in range(period):
                if ref_block[pos] is not None and curr_block[pos] is not None:
                    expected = (ref_block[pos] + transpose) % 12
                    if curr_block[pos] == expected:
                        match_count += 1
                    total_comparisons += 1
                elif ref_block[pos] is None and curr_block[pos] is None:
                    match_count += 1
                    total_comparisons += 1
                else:
                    total_comparisons += 1

        similarity = match_count / total_comparisons if total_comparisons > 0 else 0.0
        if similarity > best_similarity:
            best_similarity = similarity
            best_period = period

    if best_period == 0:
        return 0.0

    # Period regularity score: how well the detected period repeats.
    # best_similarity is already 0-1; map to 0-60 points.
    regularity_pts = best_similarity * 60.0

    # Cadence anchor score: check tonic PC (0 = C) at period boundaries.
    anchor_hits = 0
    anchor_total = 0
    beats_per_bar_count = TICKS_PER_BAR // TICKS_PER_BEAT
    for boundary_bar in range(0, total_bars, best_period):
        beat_idx = boundary_bar * beats_per_bar_count
        if beat_idx < len(bass_pcs) and bass_pcs[beat_idx] is not None:
            anchor_total += 1
            if bass_pcs[beat_idx] == 0:  # Tonic PC in C major
                anchor_hits += 1

    anchor_ratio = anchor_hits / anchor_total if anchor_total > 0 else 0.0
    # Map to 0-40 points.
    anchor_pts = anchor_ratio * 40.0

    return min(100.0, regularity_pts + anchor_pts)


def _detect_cantus_firmus(score: Score) -> float:
    """Identify a cantus firmus voice and score its structural role.

    Looks for the voice with the longest average note duration (the CF
    candidate) and checks that its notes fall primarily on strong beats
    (beats 1 and 3 in 4/4 time).

    Scores based on:
      - Duration ratio: CF avg duration vs other voices' avg duration.
      - Strong beat alignment: fraction of CF notes on beats 1 or 3.

    Returns a score in 0-100.
    """
    if score.num_voices < 2:
        return 0.0

    # Compute average duration per voice.
    voice_avg_dur: List[Tuple[str, float, List[Note]]] = []
    for track in score.tracks:
        notes = track.sorted_notes
        if not notes:
            continue
        avg_dur = sum(n.duration for n in notes) / len(notes)
        voice_avg_dur.append((track.name, avg_dur, notes))

    if len(voice_avg_dur) < 2:
        return 0.0

    # The voice with the longest average duration is the CF candidate.
    voice_avg_dur.sort(key=lambda x: x[1], reverse=True)
    cf_name, cf_avg, cf_notes = voice_avg_dur[0]

    # Compute average duration of all other voices combined.
    other_total_dur = 0.0
    other_total_notes = 0
    for name, avg, notes in voice_avg_dur[1:]:
        other_total_dur += sum(n.duration for n in notes)
        other_total_notes += len(notes)

    if other_total_notes == 0:
        return 0.0

    other_avg = other_total_dur / other_total_notes

    # Duration ratio score: CF should be notably longer than other voices.
    # Ratio of 2.0+ is ideal (CF notes are twice as long).
    if other_avg > 0:
        dur_ratio = cf_avg / other_avg
    else:
        dur_ratio = 1.0

    # Map ratio to 0-50 points: ratio 1.0 -> 0, ratio 2.0 -> 40, ratio 3.0+ -> 50
    if dur_ratio <= 1.0:
        dur_pts = 0.0
    elif dur_ratio >= 3.0:
        dur_pts = 50.0
    else:
        dur_pts = (dur_ratio - 1.0) * 25.0  # linear: 1.0->0, 3.0->50

    # Strong beat alignment: fraction of CF notes on beats 1 or 3.
    strong_count = sum(1 for n in cf_notes if n.is_on_strong_beat)
    strong_ratio = strong_count / len(cf_notes) if cf_notes else 0.0

    # Map to 0-50 points: 50% strong -> 0, 80% -> 30, 100% -> 50
    if strong_ratio <= 0.5:
        beat_pts = 0.0
    elif strong_ratio >= 1.0:
        beat_pts = 50.0
    else:
        beat_pts = (strong_ratio - 0.5) * 100.0  # linear: 0.5->0, 1.0->50

    return min(100.0, dur_pts + beat_pts)


# ---------------------------------------------------------------------------
# 6-dimension scorer
# ---------------------------------------------------------------------------


def _score_structure(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
    form_name: str = "",
) -> DimensionScore:
    """I. Structure dimension (10%).

    Form-aware scoring:
      - Fugue forms: voice entry intervals + texture density.
      - Passacaglia/chaconne: ground bass regularity + texture density.
      - Chorale prelude: cantus firmus detection + texture density.
      - Other forms: texture density only.
    """
    sub_scores: List[SubScore] = []

    # Voice entries (fugue-specific)
    ve = extract_voice_entries(score)
    if ve["has_entries"]:
        entry_score = 100.0 if ve["fifth_fourth_ratio"] >= 0.5 else 50.0
        sub_scores.append(SubScore(
            name="voice_entry_intervals",
            value=ve["fifth_fourth_ratio"],
            reference=1.0,
            score=entry_score,
            method="boolean",
            detail=f"5th/4th ratio: {ve['fifth_fourth_ratio']:.2f}",
        ))

    # Ground bass regularity (passacaglia/chaconne)
    if form_name in _GROUND_BASS_FORMS:
        gb_score = _detect_ground_bass_regularity(score)
        sub_scores.append(SubScore(
            name="ground_bass_regularity",
            value=gb_score,
            reference=80.0,
            score=gb_score,
            method="form_specific",
            detail=f"ground bass regularity: {gb_score:.1f}/100",
        ))

    # Cantus firmus detection (chorale prelude)
    if form_name in _CANTUS_FIRMUS_FORMS:
        cf_score = _detect_cantus_firmus(score)
        sub_scores.append(SubScore(
            name="cantus_firmus",
            value=cf_score,
            reference=70.0,
            score=cf_score,
            method="form_specific",
            detail=f"cantus firmus score: {cf_score:.1f}/100",
        ))

    # Texture density JSD (use organ_pf reference for toccata+fugue forms)
    tex = extract_texture_profile(score)
    struct_ref_tex, _unused = _get_texture_reference(ref, form_name)
    if tex["distribution"] and struct_ref_tex:
        j = jsd(tex["distribution"], struct_ref_tex)
        pts = jsd_to_points(j)
        detail = f"JSD={j:.4f}"
        if form_name in _TOCCATA_FUGUE_FORMS:
            detail += " (organ_pf ref)"
        sub_scores.append(SubScore(
            name="texture_density_match",
            value=j,
            reference=0.0,
            score=pts,
            method="jsd",
            detail=detail,
        ))

    if not sub_scores:
        return DimensionScore("structure", 0, False)

    raw = sum(s.score for s in sub_scores) / len(sub_scores)
    return DimensionScore("structure", max(0, raw - penalty), True, sub_scores, penalty)


def _score_melody(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
) -> DimensionScore:
    """II. Melody dimension (20%)."""
    sub_scores: List[SubScore] = []
    ip = extract_interval_profile(score)
    scalars = ref.get("scalars", {})

    # Interval distribution JSD
    ref_int = ref.get("distributions", {}).get("interval", {})
    if ip["distribution"] and ref_int:
        j = jsd(ip["distribution"], ref_int)
        pts = jsd_to_points(j)
        sub_scores.append(SubScore(
            name="interval_distribution_jsd",
            value=j, reference=0.0, score=pts, method="jsd",
            detail=f"JSD={j:.4f}",
        ))

    # Stepwise ratio z-score
    sr = scalars.get("stepwise_ratio", {})
    if sr:
        z = compute_zscore(ip["stepwise_ratio"], sr["mean"], sr["std"])
        pts = zscore_to_points(z)
        sub_scores.append(SubScore(
            name="stepwise_ratio_zscore",
            value=ip["stepwise_ratio"], reference=sr["mean"], score=pts, method="zscore",
            detail=f"{ip['stepwise_ratio']:.3f} (z={z:.2f})",
        ))

    # Avg interval z-score
    ai = scalars.get("avg_interval", {})
    if ai:
        z = compute_zscore(ip["avg_interval"], ai["mean"], ai["std"])
        pts = zscore_to_points(z)
        sub_scores.append(SubScore(
            name="avg_interval_zscore",
            value=ip["avg_interval"], reference=ai["mean"], score=pts, method="zscore",
            detail=f"{ip['avg_interval']:.2f} (z={z:.2f})",
        ))

    if not sub_scores:
        return DimensionScore("melody", 0, False)

    raw = sum(s.score for s in sub_scores) / len(sub_scores)
    return DimensionScore("melody", max(0, raw - penalty), True, sub_scores, penalty)


def _compute_section_weighted_degree_jsd(
    score: Score,
    ref_deg: Dict[str, float],
    form_name: str,
) -> Optional[Tuple[float, float, str]]:
    """Compute harmony degree JSD with section-aware weighting.

    For toccata_and_fugue forms, computes degree JSD separately for toccata
    and fugue sections, weighting toccata at 0.5 (figuration makes strict
    harmony scoring less meaningful) and fugue at 1.0.

    Args:
        score: The Score to evaluate.
        ref_deg: Reference harmony degree distribution.
        form_name: Form name for section detection.

    Returns:
        Tuple of (jsd_value, weighted_points, detail_string) or None if
        no section boundary detected or form is not toccata+fugue.
    """
    if form_name not in _TOCCATA_FUGUE_FORMS:
        return None  # Caller should use default logic

    boundary_tick = _detect_fugue_boundary_tick(score)
    if boundary_tick is None or boundary_tick <= 0:
        return None  # No detectable boundary; fall back to default

    total_duration = score.total_duration

    # Compute degree distribution for each section.
    toc_deg, toc_count = _extract_degree_distribution_for_tick_range(
        score, 0, boundary_tick)
    fug_deg, fug_count = _extract_degree_distribution_for_tick_range(
        score, boundary_tick, total_duration)

    if toc_count == 0 and fug_count == 0:
        return None

    # Compute JSD for each section with available data.
    toc_weight = 0.5
    fug_weight = 1.0

    weighted_pts = 0.0
    total_weight = 0.0
    jsd_parts: List[str] = []

    if toc_deg and toc_count > 0:
        toc_jsd = jsd(toc_deg, ref_deg)
        toc_pts = jsd_to_points(toc_jsd)
        section_w = toc_weight * toc_count
        weighted_pts += toc_pts * section_w
        total_weight += section_w
        jsd_parts.append(f"toc={toc_jsd:.4f}*0.5")

    if fug_deg and fug_count > 0:
        fug_jsd = jsd(fug_deg, ref_deg)
        fug_pts = jsd_to_points(fug_jsd)
        section_w = fug_weight * fug_count
        weighted_pts += fug_pts * section_w
        total_weight += section_w
        jsd_parts.append(f"fug={fug_jsd:.4f}*1.0")

    if total_weight == 0:
        return None

    combined_pts = weighted_pts / total_weight
    # Also compute the overall JSD for display.
    gen_deg = _extract_degree_distribution(score)
    overall_jsd = jsd(gen_deg, ref_deg) if gen_deg else 0.0
    detail = f"JSD={overall_jsd:.4f} (section-weighted: {', '.join(jsd_parts)})"

    return overall_jsd, combined_pts, detail


def _score_harmony(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
    form_name: str = "",
) -> DimensionScore:
    """III. Harmony dimension (25%).

    For toccata_and_fugue forms, the harmony_degree sub-score uses
    section-weighted JSD: toccata sections weighted at 0.5 (figuration
    reduces strict harmony relevance), fugue sections at 1.0.
    """
    if score.num_voices < 2:
        return DimensionScore("harmony", 0, False)

    sub_scores: List[SubScore] = []
    vp = extract_vertical_profile(score)
    if not vp["applicable"]:
        return DimensionScore("harmony", 0, False)

    scalars = ref.get("scalars", {})

    # Vertical interval JSD
    ref_vert = ref.get("distributions", {}).get("vertical", {})
    if vp["distribution"] and ref_vert:
        j = jsd(vp["distribution"], ref_vert)
        pts = jsd_to_points(j)
        sub_scores.append(SubScore(
            name="vertical_interval_jsd",
            value=j, reference=0.0, score=pts, method="jsd",
            detail=f"JSD={j:.4f}",
        ))

    # Consonance ratio z-score
    cr = scalars.get("consonance_ratio", {})
    if cr:
        z = compute_zscore(vp["consonance_ratio"], cr["mean"], cr["std"])
        pts = zscore_to_points(z)
        sub_scores.append(SubScore(
            name="consonance_ratio_zscore",
            value=vp["consonance_ratio"], reference=cr["mean"], score=pts, method="zscore",
            detail=f"{vp['consonance_ratio']:.3f} (z={z:.2f})",
        ))

    # Perfect consonance z-score
    pc = scalars.get("perfect_consonance_ratio", {})
    if pc:
        z = compute_zscore(vp["perfect_consonance_ratio"], pc["mean"], pc["std"])
        pts = zscore_to_points(z)
        sub_scores.append(SubScore(
            name="perfect_consonance_zscore",
            value=vp["perfect_consonance_ratio"], reference=pc["mean"], score=pts,
            method="zscore",
            detail=f"{vp['perfect_consonance_ratio']:.3f} (z={z:.2f})",
        ))

    # Harmonic function distribution JSD (T/S/D/M)
    ref_func = ref.get("distributions", {}).get("function", {})
    if ref_func:
        gen_func = _extract_function_distribution(score)
        if gen_func:
            j = jsd(gen_func, ref_func)
            pts = jsd_to_points(j)
            sub_scores.append(SubScore(
                name="harmony_function_jsd",
                value=j, reference=0.0, score=pts, method="jsd",
                detail=f"JSD={j:.4f}",
            ))

    # Harmony degree distribution JSD (section-weighted for toccata+fugue forms)
    ref_deg = ref.get("distributions", {}).get("harmony_degrees", {})
    if ref_deg:
        section_result = _compute_section_weighted_degree_jsd(score, ref_deg, form_name)
        if section_result is not None:
            overall_jsd, weighted_pts, detail = section_result
            # Apply the half-weight factor that harmony_degree always uses.
            pts = weighted_pts * 0.5
            sub_scores.append(SubScore(
                name="harmony_degree_jsd",
                value=overall_jsd, reference=0.0, score=pts, method="jsd",
                detail=f"{detail} (half-weight)",
            ))
        else:
            # Default path: no section weighting (non-toccata forms or no boundary).
            gen_deg = _extract_degree_distribution(score)
            if gen_deg:
                j = jsd(gen_deg, ref_deg)
                pts = jsd_to_points(j) * 0.5
                sub_scores.append(SubScore(
                    name="harmony_degree_jsd",
                    value=j, reference=0.0, score=pts, method="jsd",
                    detail=f"JSD={j:.4f} (half-weight)",
                ))

    # Cadence density z-score
    ref_cad = scalars.get("cadences_per_8_bars", {})
    if ref_cad:
        gen_cad = _count_cadences_simplified(score)
        cad_std = max(ref_cad.get("std", 0.5), 0.5)
        z = compute_zscore(gen_cad, ref_cad["mean"], cad_std)
        pts = zscore_to_points(z)
        sub_scores.append(SubScore(
            name="cadence_density_zscore",
            value=gen_cad, reference=ref_cad["mean"], score=pts, method="zscore",
            detail=f"{gen_cad:.2f}/8bars (z={z:.2f})",
        ))

    if not sub_scores:
        return DimensionScore("harmony", 0, False)

    # Weighted average: harmony_degree_jsd gets half weight
    total_weight = 0.0
    weighted_sum = 0.0
    for sub in sub_scores:
        weight = 0.5 if sub.name == "harmony_degree_jsd" else 1.0
        weighted_sum += sub.score * weight
        total_weight += weight
    raw = weighted_sum / total_weight if total_weight > 0 else 0.0
    return DimensionScore("harmony", max(0, raw - penalty), True, sub_scores, penalty)


def _score_counterpoint(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
    counterpoint_enabled: bool,
) -> DimensionScore:
    """IV. Counterpoint dimension (20%)."""
    if not counterpoint_enabled or score.num_voices < 2:
        return DimensionScore("counterpoint", 0, False)

    sub_scores: List[SubScore] = []
    mp = extract_motion_profile(score)
    if not mp["applicable"]:
        return DimensionScore("counterpoint", 0, False)

    # Motion distribution JSD
    ref_mot = ref.get("distributions", {}).get("motion", {})
    if mp["distribution"] and ref_mot:
        j = jsd(mp["distribution"], ref_mot)
        pts = jsd_to_points(j)
        sub_scores.append(SubScore(
            name="motion_distribution_jsd",
            value=j, reference=0.0, score=pts, method="jsd",
            detail=f"JSD={j:.4f}",
        ))

    # Contrary ratio z-score (estimated std=0.10)
    if mp["contrary_ratio"] > 0:
        ref_contrary = ref_mot.get("contrary", 0.3) if ref_mot else 0.3
        z = compute_zscore(mp["contrary_ratio"], ref_contrary, 0.10)
        pts = zscore_to_points(z)
        sub_scores.append(SubScore(
            name="contrary_ratio_zscore",
            value=mp["contrary_ratio"], reference=ref_contrary, score=pts,
            method="zscore",
            detail=f"{mp['contrary_ratio']:.3f} (z={z:.2f})",
        ))

    # Parallel rate (low is good)
    par = mp["parallel_ratio"]
    ref_par = ref_mot.get("parallel", 0.04) if ref_mot else 0.04
    if par <= ref_par * 2:
        par_score = 100.0
    elif par <= 0.10:
        par_score = 80.0
    elif par <= 0.20:
        par_score = 50.0
    else:
        par_score = 20.0
    sub_scores.append(SubScore(
        name="parallel_rate",
        value=par, reference=ref_par, score=par_score, method="threshold",
        detail=f"{par:.3f} (ref: {ref_par:.3f})",
    ))

    # NCT type distribution JSD
    # Both profiles must use identical category sets for meaningful comparison.
    # Canonical categories: passing, neighbor, ornamental, other.
    # Reference data may still carry a legacy "suspension" key (always 0.0);
    # fold it into "other" for alignment.
    ref_nct = ref.get("distributions", {}).get("nct_types", {})
    if ref_nct:
        gen_nct = _extract_nct_distribution(score)
        if gen_nct:
            aligned_ref = dict(ref_nct)
            # Fold legacy "suspension" into "other" if present.
            if "suspension" in aligned_ref:
                aligned_ref["other"] = aligned_ref.get("other", 0.0) + aligned_ref.pop("suspension")
            # Ensure both profiles contain all canonical NCT categories.
            _NCT_CATEGORIES = ("passing", "neighbor", "ornamental", "other")
            for cat in _NCT_CATEGORIES:
                aligned_ref.setdefault(cat, 0.0)
                gen_nct.setdefault(cat, 0.0)
            j = jsd(gen_nct, aligned_ref)
            pts = jsd_to_points(j)
            sub_scores.append(SubScore(
                name="nct_distribution_jsd",
                value=j, reference=0.0, score=pts, method="jsd",
                detail=f"JSD={j:.4f}",
            ))

    # Parallel perfects rate z-score
    scalars = ref.get("scalars", {})
    ref_pp = scalars.get("parallel_perfects_per_100_beats", {})
    if ref_pp:
        gen_pp = _count_parallel_perfects(score)
        pp_std = max(ref_pp.get("std", 0.5), 0.5)
        z = compute_zscore(gen_pp, ref_pp["mean"], pp_std)
        pts = zscore_to_points(z)
        sub_scores.append(SubScore(
            name="parallel_perfects_zscore",
            value=gen_pp, reference=ref_pp["mean"], score=pts, method="zscore",
            detail=f"{gen_pp:.2f}/100beats (z={z:.2f})",
        ))

    if not sub_scores:
        return DimensionScore("counterpoint", 0, False)

    raw = sum(s.score for s in sub_scores) / len(sub_scores)
    return DimensionScore("counterpoint", max(0, raw - penalty), True, sub_scores, penalty)


def _score_rhythm(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
) -> DimensionScore:
    """V. Rhythm dimension (15%)."""
    sub_scores: List[SubScore] = []
    rp = extract_rhythm_profile(score)

    # Rhythm distribution JSD
    ref_rhy = ref.get("distributions", {}).get("rhythm", {})
    if rp["distribution"] and ref_rhy:
        j = jsd(rp["distribution"], ref_rhy)
        pts = jsd_to_points(j)
        sub_scores.append(SubScore(
            name="rhythm_distribution_jsd",
            value=j, reference=0.0, score=pts, method="jsd",
            detail=f"JSD={j:.4f}",
        ))

    # Voice rhythm differentiation (upper vs lower if multi-voice)
    if score.num_voices >= 2:
        tracks_sorted = sorted(score.tracks, key=lambda t: t.name)
        upper_notes = tracks_sorted[0].notes
        lower_notes = tracks_sorted[-1].notes
        upper_avg = (sum(n.duration for n in upper_notes) / len(upper_notes)
                     if upper_notes else 0)
        lower_avg = (sum(n.duration for n in lower_notes) / len(lower_notes)
                     if lower_notes else 0)
        if upper_avg > 0 and lower_avg > 0:
            diff_ratio = abs(upper_avg - lower_avg) / max(upper_avg, lower_avg)
            # In Bach, different voices have different rhythm profiles
            # Some differentiation is expected (0.1-0.5 range typical)
            diff_score = min(100.0, diff_ratio * 200.0)
            sub_scores.append(SubScore(
                name="voice_rhythm_differentiation",
                value=diff_ratio, reference=0.3, score=diff_score, method="ratio",
                detail=f"diff={diff_ratio:.3f}",
            ))

    if not sub_scores:
        return DimensionScore("rhythm", 0, False)

    raw = sum(s.score for s in sub_scores) / len(sub_scores)
    return DimensionScore("rhythm", max(0, raw - penalty), True, sub_scores, penalty)


def _get_texture_reference(
    ref: Dict[str, Any],
    form_name: str,
) -> Tuple[Dict[str, float], Dict[str, Any]]:
    """Get texture reference profile, using organ_pf for toccata+fugue forms.

    For toccata_and_fugue and fantasia_and_fugue forms, the organ_pf texture
    profile (more single/two-voice passages than pure fugue) is used instead
    of the category default. This reflects the mixed texture of free
    sections + fugue sections.

    Args:
        ref: The loaded reference category data.
        form_name: Resolved form name.

    Returns:
        Tuple of (texture_distribution, avg_active_voices_scalar).
    """
    if form_name in _TOCCATA_FUGUE_FORMS:
        return _ORGAN_PF_TEXTURE_REF, _ORGAN_PF_AVG_ACTIVE
    ref_tex = ref.get("distributions", {}).get("texture", {})
    av_scalar = ref.get("scalars", {}).get("avg_active_voices", {})
    return ref_tex, av_scalar


def _score_texture(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
    form_name: str = "",
) -> DimensionScore:
    """VI. Texture dimension (10%).

    For toccata_and_fugue forms, uses the organ_pf texture reference
    (avg_active ~2.75) instead of organ_fugue (avg_active ~2.66), reflecting
    the lighter texture of toccata free sections.
    """
    if score.num_voices < 2:
        # Still score texture for single-voice works
        pass

    sub_scores: List[SubScore] = []
    tp = extract_texture_profile(score)

    ref_tex, av_scalar = _get_texture_reference(ref, form_name)

    # Texture distribution JSD
    if tp["distribution"] and ref_tex:
        j = jsd(tp["distribution"], ref_tex)
        pts = jsd_to_points(j)
        detail = f"JSD={j:.4f}"
        if form_name in _TOCCATA_FUGUE_FORMS:
            detail += " (organ_pf ref)"
        sub_scores.append(SubScore(
            name="texture_distribution_jsd",
            value=j, reference=0.0, score=pts, method="jsd",
            detail=detail,
        ))

    # Avg active voices z-score
    if av_scalar:
        z = compute_zscore(tp["avg_active_voices"], av_scalar["mean"], av_scalar["std"])
        pts = zscore_to_points(z)
        detail = f"{tp['avg_active_voices']:.2f} (z={z:.2f})"
        if form_name in _TOCCATA_FUGUE_FORMS:
            detail += " (organ_pf ref)"
        sub_scores.append(SubScore(
            name="avg_active_voices_zscore",
            value=tp["avg_active_voices"], reference=av_scalar["mean"], score=pts,
            method="zscore",
            detail=detail,
        ))

    if not sub_scores:
        return DimensionScore("texture", 0, False)

    raw = sum(s.score for s in sub_scores) / len(sub_scores)
    return DimensionScore("texture", max(0, raw - penalty), True, sub_scores, penalty)


# ---------------------------------------------------------------------------
# Main scoring function
# ---------------------------------------------------------------------------


def compute_score(
    score: Score,
    category: str,
    counterpoint_enabled: bool = True,
    results: Optional[List[RuleResult]] = None,
    form_name: str = "",
) -> BachScore:
    """Compute multi-dimensional Bach reference score.

    Args:
        score: The Score to evaluate.
        category: Reference category name (e.g., "organ_fugue").
        counterpoint_enabled: Whether counterpoint rules apply.
        results: Optional validation results for penalty integration.
        form_name: Form name for form-specific structure scoring
            (e.g., "passacaglia", "chorale_prelude"). Falls back to
            score.form if empty.

    Returns:
        BachScore with total score, grade, and per-dimension breakdown.
    """
    ref = load_category(category)
    penalties = _compute_penalties(results) if results else {}

    resolved_form = form_name or score.form or ""

    dimensions: Dict[str, DimensionScore] = {}
    dimensions["structure"] = _score_structure(
        score, ref, penalties.get("structure", 0), form_name=resolved_form)
    dimensions["melody"] = _score_melody(
        score, ref, penalties.get("melody", 0))
    dimensions["harmony"] = _score_harmony(
        score, ref, penalties.get("harmony", 0), form_name=resolved_form)
    dimensions["counterpoint"] = _score_counterpoint(
        score, ref, penalties.get("counterpoint", 0), counterpoint_enabled)
    dimensions["rhythm"] = _score_rhythm(
        score, ref, penalties.get("rhythm", 0))
    dimensions["texture"] = _score_texture(
        score, ref, penalties.get("texture", 0), form_name=resolved_form)

    # Compute weighted total with weight redistribution for non-applicable dims
    applicable_weight = 0.0
    weighted_sum = 0.0

    for dim_name, dim_score in dimensions.items():
        if dim_score.applicable:
            applicable_weight += _DIMENSION_WEIGHTS[dim_name]

    if applicable_weight > 0:
        for dim_name, dim_score in dimensions.items():
            if dim_score.applicable:
                adjusted_weight = _DIMENSION_WEIGHTS[dim_name] / applicable_weight
                weighted_sum += dim_score.score * adjusted_weight
    else:
        weighted_sum = 0.0

    total = max(0.0, min(100.0, weighted_sum))

    return BachScore(
        total=round(total, 1),
        grade=_grade(total),
        dimensions=dimensions,
        form=score.form or "",
        category=category,
    )
