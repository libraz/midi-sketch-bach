"""Profile computation building blocks.

Pure functions that take Score/Note data and return dicts. Shared by
score.py (scoring) and bach_reference_server.py (MCP tools).
"""

from __future__ import annotations

import math
from collections import Counter
from typing import Dict, List, Optional

from .model import (
    TICKS_PER_BEAT,
    Note,
    interval_class,
    is_consonant,
    is_perfect_consonance,
    sounding_note_at,
)
from .music_theory import INTERVAL_NAME_MAP

# ---------------------------------------------------------------------------
# Distribution math
# ---------------------------------------------------------------------------


def normalize(d: Dict[str, float]) -> Dict[str, float]:
    """Normalize a distribution dict to sum to 1.0."""
    total = sum(d.values())
    if total == 0:
        return d
    return {k: v / total for k, v in d.items()}


def js_divergence(p: Dict[str, float], q: Dict[str, float]) -> float:
    """Jensen-Shannon Divergence between two distributions.

    Returns value in [0.0, 1.0] where 0.0 means identical.
    Both distributions are normalized internally.
    """
    p = normalize(p)
    q = normalize(q)
    all_keys = set(p) | set(q)
    if not all_keys:
        return 0.0

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
    """Convert JSD to score points. JSD=0->100, higher JSD->lower score."""
    return max(0.0, min(100.0, 100.0 * math.exp(-12.0 * jsd_val)))


def compute_zscore(value: float, mean: float, std: float) -> float:
    """Compute z-score. Returns 0 if std is near zero."""
    if std < 1e-9:
        return 0.0
    return (value - mean) / std


def zscore_to_points(z: float) -> float:
    """Convert z-score to score points using Gaussian."""
    return max(0.0, min(100.0, 100.0 * math.exp(-0.5 * z * z)))


# ---------------------------------------------------------------------------
# Melodic interval counting
# ---------------------------------------------------------------------------


def count_melodic_intervals(
    notes: List[Note],
) -> tuple[Counter, int, int, int]:
    """Count melodic intervals from a sorted note list.

    Args:
        notes: Notes sorted by start_tick.

    Returns:
        (interval_class_counts, step_count, leap_count, total_semitones)
        where interval_class_counts is a Counter keyed by interval class (0-11).
    """
    counts: Counter = Counter()
    step_count = 0
    leap_count = 0
    total_semitones = 0

    for i in range(1, len(notes)):
        semi = abs(notes[i].pitch - notes[i - 1].pitch)
        ic = interval_class(semi)
        counts[ic] += 1
        total_semitones += semi
        if ic in (1, 2):
            step_count += 1
        elif ic > 2:
            leap_count += 1

    return counts, step_count, leap_count, total_semitones


# ---------------------------------------------------------------------------
# Duration binning
# ---------------------------------------------------------------------------

_DURATION_BINS = [
    (0.1875, "32nd"),
    (0.375, "16th"),
    (0.75, "8th"),
    (1.5, "quarter"),
    (3.0, "half"),
    (6.0, "whole"),
]


def bin_durations(notes: List[Note]) -> Counter:
    """Categorize note durations into rhythm bins.

    Returns Counter keyed by bin name: 32nd, 16th, 8th, quarter, half, whole, longer.
    """
    bins: Counter = Counter()
    for note in notes:
        beats = note.duration / TICKS_PER_BEAT
        assigned = False
        for threshold, name in _DURATION_BINS:
            if beats < threshold:
                bins[name] += 1
                assigned = True
                break
        if not assigned:
            bins["longer"] += 1
    return bins


# ---------------------------------------------------------------------------
# Vertical interval sampling
# ---------------------------------------------------------------------------


def sample_vertical_intervals(
    voices: Dict[str, List[Note]],
    total_ticks: int,
    step: int,
) -> dict:
    """Sample vertical intervals between all voice pairs at regular intervals.

    Args:
        voices: Dict of voice_name -> sorted notes.
        total_ticks: Total duration in ticks.
        step: Sampling step in ticks.

    Returns:
        {"counts": Counter (keyed by interval class), "total": int,
         "consonant": int, "perfect": int}
    """
    voice_names = list(voices.keys())
    counts: Counter = Counter()
    consonant = 0
    perfect = 0
    total = 0

    for tick in range(0, total_ticks, step):
        pitches = []
        for vname in voice_names:
            n = sounding_note_at(voices[vname], tick)
            if n is not None:
                pitches.append(n.pitch)
        for i in range(len(pitches)):
            for j in range(i + 1, len(pitches)):
                semi = abs(pitches[i] - pitches[j])
                ic = interval_class(semi)
                counts[ic] += 1
                total += 1
                if is_consonant(semi):
                    consonant += 1
                if is_perfect_consonance(semi):
                    perfect += 1

    return {"counts": counts, "total": total, "consonant": consonant, "perfect": perfect}


# ---------------------------------------------------------------------------
# Voice motion sampling
# ---------------------------------------------------------------------------


def sample_motion_types(
    voices: Dict[str, List[Note]],
    total_ticks: int,
    step: int,
) -> dict:
    """Sample voice motion types between all voice pairs.

    Args:
        voices: Dict of voice_name -> sorted notes.
        total_ticks: Total duration in ticks.
        step: Sampling step in ticks.

    Returns:
        {"motion": {"oblique": int, "contrary": int, "similar": int,
         "parallel": int}, "total": int}
    """
    voice_names = list(voices.keys())
    motion = {"oblique": 0, "contrary": 0, "similar": 0, "parallel": 0}
    total = 0

    for vi in range(len(voice_names)):
        for vj in range(vi + 1, len(voice_names)):
            notes_a = voices[voice_names[vi]]
            notes_b = voices[voice_names[vj]]

            prev_a = sounding_note_at(notes_a, 0)
            prev_b = sounding_note_at(notes_b, 0)

            for tick in range(step, total_ticks, step):
                curr_a = sounding_note_at(notes_a, tick)
                curr_b = sounding_note_at(notes_b, tick)

                if prev_a and prev_b and curr_a and curr_b:
                    da = curr_a.pitch - prev_a.pitch
                    db = curr_b.pitch - prev_b.pitch

                    if da == 0 and db == 0:
                        pass
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

    return {"motion": motion, "total": total}


# ---------------------------------------------------------------------------
# Texture density sampling
# ---------------------------------------------------------------------------


def sample_texture_density(
    voices: Dict[str, List[Note]],
    total_ticks: int,
    step: int,
) -> dict:
    """Sample texture density (active voice count) at regular intervals.

    Args:
        voices: Dict of voice_name -> sorted notes.
        total_ticks: Total duration in ticks.
        step: Sampling step in ticks.

    Returns:
        {"density_counts": Counter (keyed by active count as str),
         "total_samples": int, "total_active": int}
    """
    voice_names = list(voices.keys())
    density_counts: Counter = Counter()
    total_samples = 0
    total_active = 0

    for tick in range(0, total_ticks, step):
        active = 0
        for vname in voice_names:
            n = sounding_note_at(voices[vname], tick)
            if n is not None:
                active += 1
        key = str(active)
        density_counts[key] += 1
        total_samples += 1
        total_active += active

    return {
        "density_counts": density_counts,
        "total_samples": total_samples,
        "total_active": total_active,
    }
