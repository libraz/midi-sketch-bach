"""Multi-dimensional Bach reference scoring.

Computes a numerical score (0-100) measuring how closely generated output
matches Bach's compositional patterns, based on reference data from 270 works.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

from .model import (
    TICKS_PER_BEAT,
    TICKS_PER_BAR,
    NoteSource,
    Score,
    interval_class,
    is_consonant,
    is_perfect_consonance,
    sounding_note_at,
)
from .reference import load_category
from .rules.base import RuleResult, Severity

# ---------------------------------------------------------------------------
# Interval name table (matches MCP reference format)
# ---------------------------------------------------------------------------

_INTERVAL_NAMES = {
    0: "P1", 1: "m2", 2: "M2", 3: "m3", 4: "M3", 5: "P4",
    6: "TT", 7: "P5", 8: "m6", 9: "M6", 10: "m7", 11: "M7",
}

# ---------------------------------------------------------------------------
# Math foundations
# ---------------------------------------------------------------------------


def _normalize(d: Dict[str, float]) -> Dict[str, float]:
    """Normalize a distribution dict to sum to 1.0."""
    total = sum(d.values())
    if total == 0:
        return d
    return {k: v / total for k, v in d.items()}


def jsd(p: Dict[str, float], q: Dict[str, float]) -> float:
    """Jensen-Shannon Divergence between two distributions.

    Returns value in [0.0, 1.0] where 0.0 means identical.
    Both distributions are normalized internally.
    """
    p = _normalize(p)
    q = _normalize(q)
    all_keys = set(p) | set(q)
    if not all_keys:
        return 0.0

    # Build aligned arrays with smoothing
    eps = 1e-12
    m = {}
    for k in all_keys:
        pk = p.get(k, 0.0) + eps
        qk = q.get(k, 0.0) + eps
        m[k] = (pk + qk) / 2.0

    def _kl(a: Dict[str, float], b: Dict[str, float]) -> float:
        total = 0.0
        for k in all_keys:
            ak = a.get(k, 0.0) + eps
            bk = b[k]
            total += ak * math.log2(ak / bk)
        return total

    return 0.5 * _kl(p, m) + 0.5 * _kl(q, m)


def jsd_to_points(jsd_val: float) -> float:
    """Convert JSD to score points. JSD=0->100, higher JSD->lower score.

    Uses exponential decay: 100 * exp(-12 * jsd).
    """
    return max(0.0, min(100.0, 100.0 * math.exp(-12.0 * jsd_val)))


def compute_zscore(value: float, mean: float, std: float) -> float:
    """Compute z-score. Returns 0 if std is near zero."""
    if std < 1e-9:
        return 0.0
    return (value - mean) / std


def zscore_to_points(z: float) -> float:
    """Convert z-score to score points using Gaussian.

    |z|=0->100, |z|=1->~85, |z|=2->~55, |z|>3->~20 or less.
    Formula: 100 * exp(-0.5 * z^2)
    """
    return max(0.0, min(100.0, 100.0 * math.exp(-0.5 * z * z)))


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
    counts: Dict[str, int] = {name: 0 for name in _INTERVAL_NAMES.values()}
    total = 0
    total_semitones = 0

    for track in score.tracks:
        notes = track.sorted_notes
        for i in range(1, len(notes)):
            semi = abs(notes[i].pitch - notes[i - 1].pitch)
            ic = interval_class(semi)
            name = _INTERVAL_NAMES.get(ic, "P1")
            counts[name] += 1
            total += 1
            total_semitones += semi

    stepwise = counts.get("m2", 0) + counts.get("M2", 0)
    leap = total - stepwise - counts.get("P1", 0)

    return {
        "distribution": _normalize({k: float(v) for k, v in counts.items()}),
        "stepwise_ratio": stepwise / total if total else 0.0,
        "leap_ratio": leap / total if total else 0.0,
        "avg_interval": total_semitones / total if total else 0.0,
        "total": total,
    }


def extract_rhythm_profile(score: Score) -> Dict[str, Any]:
    """Extract rhythm/duration distribution from a Score."""
    bins = {"32nd": 0, "16th": 0, "8th": 0, "quarter": 0, "half": 0, "whole": 0, "longer": 0}

    for track in score.tracks:
        for note in track.notes:
            beats = note.duration / TICKS_PER_BEAT
            if beats < 0.1875:   # < 3/16 of a beat
                bins["32nd"] += 1
            elif beats < 0.375:  # < 3/8 of a beat
                bins["16th"] += 1
            elif beats < 0.75:   # < 3/4 of a beat
                bins["8th"] += 1
            elif beats < 1.5:
                bins["quarter"] += 1
            elif beats < 3.0:
                bins["half"] += 1
            elif beats < 6.0:
                bins["whole"] += 1
            else:
                bins["longer"] += 1

    return {"distribution": _normalize({k: float(v) for k, v in bins.items()})}


def extract_vertical_profile(score: Score) -> Dict[str, Any]:
    """Extract vertical interval distribution between voice pairs."""
    if score.num_voices < 2:
        return {"distribution": {}, "consonance_ratio": 0.0,
                "perfect_consonance_ratio": 0.0, "applicable": False}

    counts: Dict[str, int] = {name: 0 for name in _INTERVAL_NAMES.values()}
    consonant = 0
    perfect = 0
    total = 0
    voices = score.voices_dict
    voice_names = list(voices.keys())

    total_ticks = score.total_duration
    sample_step = TICKS_PER_BEAT  # Sample every beat

    for tick in range(0, total_ticks, sample_step):
        pitches = []
        for vname in voice_names:
            n = sounding_note_at(voices[vname], tick)
            if n is not None:
                pitches.append(n.pitch)
        # Check all pairs
        for i in range(len(pitches)):
            for j in range(i + 1, len(pitches)):
                semi = abs(pitches[i] - pitches[j])
                ic = interval_class(semi)
                name = _INTERVAL_NAMES.get(ic, "P1")
                counts[name] += 1
                total += 1
                if is_consonant(semi):
                    consonant += 1
                if is_perfect_consonance(semi):
                    perfect += 1

    return {
        "distribution": _normalize({k: float(v) for k, v in counts.items()}),
        "consonance_ratio": consonant / total if total else 0.0,
        "perfect_consonance_ratio": perfect / total if total else 0.0,
        "applicable": total > 0,
    }


def extract_motion_profile(score: Score) -> Dict[str, Any]:
    """Extract voice motion type distribution."""
    if score.num_voices < 2:
        return {"distribution": {}, "applicable": False}

    motion = {"oblique": 0, "contrary": 0, "similar": 0, "parallel": 0}
    total = 0
    voices = score.voices_dict
    voice_names = list(voices.keys())

    total_ticks = score.total_duration
    sample_step = TICKS_PER_BEAT

    for vi in range(len(voice_names)):
        for vj in range(vi + 1, len(voice_names)):
            notes_a = voices[voice_names[vi]]
            notes_b = voices[voice_names[vj]]

            prev_a = sounding_note_at(notes_a, 0)
            prev_b = sounding_note_at(notes_b, 0)

            for tick in range(sample_step, total_ticks, sample_step):
                curr_a = sounding_note_at(notes_a, tick)
                curr_b = sounding_note_at(notes_b, tick)

                if prev_a and prev_b and curr_a and curr_b:
                    da = curr_a.pitch - prev_a.pitch
                    db = curr_b.pitch - prev_b.pitch

                    if da == 0 and db == 0:
                        pass  # No motion (skip)
                    elif da == 0 or db == 0:
                        motion["oblique"] += 1
                        total += 1
                    elif (da > 0 and db < 0) or (da < 0 and db > 0):
                        motion["contrary"] += 1
                        total += 1
                    elif da == db:
                        motion["parallel"] += 1
                        total += 1
                    else:
                        motion["similar"] += 1
                        total += 1

                prev_a = curr_a
                prev_b = curr_b

    dist = _normalize({k: float(v) for k, v in motion.items()}) if total else {}
    return {
        "distribution": dist,
        "contrary_ratio": motion["contrary"] / total if total else 0.0,
        "parallel_ratio": motion["parallel"] / total if total else 0.0,
        "applicable": total > 0,
    }


def extract_texture_profile(score: Score) -> Dict[str, Any]:
    """Extract texture density distribution."""
    density_counts: Dict[str, int] = {}
    total_samples = 0
    total_active = 0

    voices = score.voices_dict
    voice_names = list(voices.keys())
    total_ticks = score.total_duration
    sample_step = TICKS_PER_BEAT

    for tick in range(0, total_ticks, sample_step):
        active = 0
        for vname in voice_names:
            n = sounding_note_at(voices[vname], tick)
            if n is not None:
                active += 1
        key = str(active)
        density_counts[key] = density_counts.get(key, 0) + 1
        total_samples += 1
        total_active += active

    return {
        "distribution": _normalize({k: float(v) for k, v in density_counts.items()}),
        "avg_active_voices": total_active / total_samples if total_samples else 0.0,
    }


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
# 6-dimension scorer
# ---------------------------------------------------------------------------


def _score_structure(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
) -> DimensionScore:
    """I. Structure dimension (10%)."""
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

    # Texture density JSD
    tex = extract_texture_profile(score)
    ref_tex = ref.get("distributions", {}).get("texture", {})
    if tex["distribution"] and ref_tex:
        j = jsd(tex["distribution"], ref_tex)
        pts = jsd_to_points(j)
        sub_scores.append(SubScore(
            name="texture_density_match",
            value=j,
            reference=0.0,
            score=pts,
            method="jsd",
            detail=f"JSD={j:.4f}",
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


def _score_harmony(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
) -> DimensionScore:
    """III. Harmony dimension (25%)."""
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

    if not sub_scores:
        return DimensionScore("harmony", 0, False)

    raw = sum(s.score for s in sub_scores) / len(sub_scores)
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


def _score_texture(
    score: Score,
    ref: Dict[str, Any],
    penalty: float,
) -> DimensionScore:
    """VI. Texture dimension (10%)."""
    if score.num_voices < 2:
        # Still score texture for single-voice works
        pass

    sub_scores: List[SubScore] = []
    tp = extract_texture_profile(score)
    scalars = ref.get("scalars", {})

    # Texture distribution JSD
    ref_tex = ref.get("distributions", {}).get("texture", {})
    if tp["distribution"] and ref_tex:
        j = jsd(tp["distribution"], ref_tex)
        pts = jsd_to_points(j)
        sub_scores.append(SubScore(
            name="texture_distribution_jsd",
            value=j, reference=0.0, score=pts, method="jsd",
            detail=f"JSD={j:.4f}",
        ))

    # Avg active voices z-score
    av = scalars.get("avg_active_voices", {})
    if av:
        z = compute_zscore(tp["avg_active_voices"], av["mean"], av["std"])
        pts = zscore_to_points(z)
        sub_scores.append(SubScore(
            name="avg_active_voices_zscore",
            value=tp["avg_active_voices"], reference=av["mean"], score=pts,
            method="zscore",
            detail=f"{tp['avg_active_voices']:.2f} (z={z:.2f})",
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
) -> BachScore:
    """Compute multi-dimensional Bach reference score.

    Args:
        score: The Score to evaluate.
        category: Reference category name (e.g., "organ_fugue").
        counterpoint_enabled: Whether counterpoint rules apply.
        results: Optional validation results for penalty integration.

    Returns:
        BachScore with total score, grade, and per-dimension breakdown.
    """
    ref = load_category(category)
    penalties = _compute_penalties(results) if results else {}

    dimensions: Dict[str, DimensionScore] = {}
    dimensions["structure"] = _score_structure(
        score, ref, penalties.get("structure", 0))
    dimensions["melody"] = _score_melody(
        score, ref, penalties.get("melody", 0))
    dimensions["harmony"] = _score_harmony(
        score, ref, penalties.get("harmony", 0))
    dimensions["counterpoint"] = _score_counterpoint(
        score, ref, penalties.get("counterpoint", 0), counterpoint_enabled)
    dimensions["rhythm"] = _score_rhythm(
        score, ref, penalties.get("rhythm", 0))
    dimensions["texture"] = _score_texture(
        score, ref, penalties.get("texture", 0))

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
