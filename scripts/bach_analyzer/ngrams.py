"""N-gram extraction helpers and core algorithms.

Pure functions for melodic, rhythm, and combined figure extraction.
Used by bach_reference_server.py MCP tools.
"""

from __future__ import annotations

from collections import Counter
from typing import Any, Optional

from .model import TICKS_PER_BEAT, Note
from .music_theory import (
    ScaleDegree,
    classify_beat_position,
    beat_strength,
    degree_interval,
    is_chord_tone_simple,
    pitch_to_scale_degree,
    quantize_duration,
)
from .work_index import WorkIndex, get_key_info

# ---------------------------------------------------------------------------
# Track note iteration
# ---------------------------------------------------------------------------


def iter_track_notes(
    data: dict, track_filter: Optional[str],
) -> list[tuple[str, list[Note]]]:
    """Yield (role, sorted_notes) pairs from raw reference data."""
    tpb = data.get("ticks_per_beat", TICKS_PER_BEAT)
    pairs: list[tuple[str, list[Note]]] = []
    for t in data.get("tracks", []):
        role = t.get("role", "unknown")
        if track_filter and role != track_filter:
            continue
        notes = []
        for n in t.get("notes", []):
            notes.append(Note(
                pitch=n["pitch"],
                velocity=n.get("velocity", 80),
                start_tick=int(n["onset"] * tpb),
                duration=max(1, int(n["duration"] * tpb)),
                voice=role,
            ))
        notes.sort(key=lambda x: x.start_tick)
        if notes:
            pairs.append((role, notes))
    return pairs


# ---------------------------------------------------------------------------
# Interval computation
# ---------------------------------------------------------------------------


def compute_intervals(
    notes: list[Note],
    interval_mode: str,
    tonic: Optional[int],
    is_minor: Optional[bool],
) -> list[Any]:
    """Compute consecutive intervals as tuples.

    Returns list of interval representations (length = len(notes) - 1).
    For 'semitone': int (signed semitone diff).
    For 'degree': (degree_diff, chroma_diff).
    For 'diatonic': int (degree_diff only).
    Falls back to semitone if key info unavailable for degree modes.
    """
    if len(notes) < 2:
        return []
    use_degree = interval_mode in ("degree", "diatonic") and tonic is not None
    intervals: list[Any] = []
    for i in range(len(notes) - 1):
        diff = notes[i + 1].pitch - notes[i].pitch
        if interval_mode == "semitone" or not use_degree:
            intervals.append(diff)
        else:
            sd_a = pitch_to_scale_degree(notes[i].pitch, tonic, is_minor or False)
            sd_b = pitch_to_scale_degree(
                notes[i + 1].pitch, tonic, is_minor or False,
            )
            dd, cd = degree_interval(sd_a, sd_b)
            if interval_mode == "degree":
                intervals.append((dd, cd))
            else:
                intervals.append(dd)
    return intervals


# ---------------------------------------------------------------------------
# Beat helpers
# ---------------------------------------------------------------------------


def strongest_beat_hit(notes: list[Note]) -> int:
    """Return index of the note on the strongest beat within the group."""
    best_idx = 0
    best_s = -1.0
    for i, n in enumerate(notes):
        s = beat_strength(n.start_tick)
        if s > best_s:
            best_s = s
            best_idx = i
    return best_idx


# ---------------------------------------------------------------------------
# Labeling helpers
# ---------------------------------------------------------------------------


def label_melodic_ngram(intervals: tuple, mode: str) -> str:
    """Generate a human-readable label for a melodic n-gram."""
    if mode == "semitone":
        dirs = [("+" if v > 0 else "" if v < 0 else "=") + str(v) for v in intervals]
        return "st:" + ",".join(dirs)
    if mode == "diatonic":
        dirs = [("+" if v > 0 else "" if v < 0 else "=") + str(v) for v in intervals]
        return "dia:" + ",".join(dirs)
    parts = []
    for dd, cd in intervals:
        s = ("+" if dd > 0 else "" if dd < 0 else "=") + str(dd)
        if cd != 0:
            s += ("+" if cd > 0 else "") + str(cd) + "c"
        parts.append(s)
    return "deg:" + ",".join(parts)


def dur_name(grid_units: int, grid_beat: float) -> str:
    """Human-readable duration name from grid units."""
    beats = grid_units * grid_beat
    if beats <= 0.125:
        return "32nd"
    if beats <= 0.25:
        return "16th"
    if beats <= 0.5:
        return "8th"
    if beats <= 1.0:
        return "qtr"
    if beats <= 2.0:
        return "half"
    return "whole+"


def figuration_label(num_voices: int, slots: tuple[int, ...]) -> str:
    """Generate label for a figuration slot pattern."""
    if len(slots) < 2:
        return f"{num_voices}v-single"
    diffs = [slots[i + 1] - slots[i] for i in range(len(slots) - 1)]
    if all(d > 0 for d in diffs):
        direction = "rising"
    elif all(d < 0 for d in diffs):
        direction = "falling"
    elif all(d >= 0 for d in diffs[:len(diffs) // 2]) and all(
        d <= 0 for d in diffs[len(diffs) // 2:]
    ):
        direction = "arch"
    else:
        direction = "mixed"
    return f"{num_voices}v-{direction}-{len(slots)}notes"


# ---------------------------------------------------------------------------
# Category work loading
# ---------------------------------------------------------------------------


def load_works_for_category(
    category: str, index: WorkIndex,
) -> list[tuple[str, dict]]:
    """Return list of (work_id, full_json) for all works in category."""
    works = index.filter(category=category)
    result: list[tuple[str, dict]] = []
    for w in works:
        data = index.load_full(w["id"])
        if data:
            result.append((w["id"], data))
    return result


# ---------------------------------------------------------------------------
# Core extraction: melodic n-grams
# ---------------------------------------------------------------------------


def extract_melodic_ngrams_data(
    category: str,
    index: WorkIndex,
    n: int = 3,
    track: Optional[str] = None,
    interval_mode: str = "degree",
    min_occurrences: int = 5,
    top_k: int = 30,
) -> dict[str, Any]:
    """Extract melodic interval n-grams — returns dict (no JSON)."""
    works = load_works_for_category(category, index)
    if not works:
        return {"error": f"No works found for category '{category}'."}

    counts: Counter = Counter()
    work_sets: dict[tuple, set[str]] = {}
    beat_pos: dict[tuple, Counter] = {}
    strongest_hits: dict[tuple, Counter] = {}
    examples: dict[tuple, dict] = {}
    total_ngrams = 0

    for work_id, data in works:
        tonic, is_minor, conf = get_key_info(work_id)
        use_tonic = tonic if conf >= 0.7 else None
        if interval_mode in ("degree", "diatonic") and use_tonic is None:
            eff_mode = "semitone"
        else:
            eff_mode = interval_mode

        for role, notes in iter_track_notes(data, track):
            ivs = compute_intervals(notes, eff_mode, use_tonic, is_minor)
            if len(ivs) < n:
                continue
            for i in range(len(ivs) - n + 1):
                window_ivs = tuple(ivs[i:i + n])
                window_notes = notes[i:i + n + 1]
                key = window_ivs

                counts[key] += 1
                total_ngrams += 1
                work_sets.setdefault(key, set()).add(work_id)

                bp = classify_beat_position(window_notes[0].start_tick)
                beat_pos.setdefault(key, Counter())[bp] += 1

                sbh = strongest_beat_hit(window_notes)
                strongest_hits.setdefault(key, Counter())[sbh] += 1

                if key not in examples:
                    examples[key] = {
                        "work_id": work_id,
                        "track": role,
                        "bar": window_notes[0].bar,
                        "beat_index": window_notes[0].beat - 1,
                        "starting_pitch": window_notes[0].pitch,
                    }

    ngrams_out = []
    for key, count in counts.most_common():
        if count < min_occurrences:
            break
        bp = beat_pos.get(key, Counter())
        bp_total = sum(bp.values()) or 1
        sh = strongest_hits.get(key, Counter())
        sh_total = sum(sh.values()) or 1

        entry: dict[str, Any] = {
            "intervals": [
                list(iv) if isinstance(iv, tuple) else iv for iv in key
            ],
            "label": label_melodic_ngram(key, interval_mode),
            "count": count,
            "works_containing": len(work_sets.get(key, set())),
            "frequency_per_1000": round(count / max(total_ngrams, 1) * 1000, 1),
            "beat_position_distribution": {
                "strong": round(bp.get("strong", 0) / bp_total, 2),
                "mid": round(bp.get("mid", 0) / bp_total, 2),
                "weak": round(bp.get("weak", 0) / bp_total, 2),
            },
            "strongest_beat_hit_position": [
                round(sh.get(p, 0) / sh_total, 2) for p in range(n + 1)
            ],
            "example": examples.get(key),
        }
        ngrams_out.append(entry)
        if len(ngrams_out) >= top_k:
            break

    return {
        "category": category,
        "n": n,
        "interval_mode": interval_mode,
        "total_works_scanned": len(works),
        "total_ngrams_extracted": total_ngrams,
        "ngrams": ngrams_out,
    }


# ---------------------------------------------------------------------------
# Core extraction: rhythm n-grams
# ---------------------------------------------------------------------------


def extract_rhythm_ngrams_data(
    category: str,
    index: WorkIndex,
    n: int = 4,
    track: Optional[str] = None,
    quantize: str = "sixteenth",
    min_occurrences: int = 5,
    top_k: int = 30,
) -> dict[str, Any]:
    """Extract rhythm n-grams — returns dict (no JSON)."""
    works = load_works_for_category(category, index)
    if not works:
        return {"error": f"No works found for category '{category}'."}

    grid_beat = {"sixteenth": 0.25, "eighth": 0.5, "quarter": 1.0}.get(
        quantize, 0.25,
    )

    counts: Counter = Counter()
    work_sets: dict[tuple, set[str]] = {}
    beat_pos_map: dict[tuple, Counter] = {}
    strongest_hits: dict[tuple, Counter] = {}
    total_ngrams = 0

    for work_id, data in works:
        tpb = data.get("ticks_per_beat", TICKS_PER_BEAT)
        for role, notes in iter_track_notes(data, track):
            if len(notes) < n:
                continue
            for i in range(len(notes) - n + 1):
                window = notes[i:i + n]
                durs = tuple(
                    max(1, round(nt.duration / tpb / grid_beat))
                    for nt in window
                )
                key = durs
                counts[key] += 1
                total_ngrams += 1
                work_sets.setdefault(key, set()).add(work_id)

                bp = classify_beat_position(window[0].start_tick)
                beat_pos_map.setdefault(key, Counter())[bp] += 1

                sbh = strongest_beat_hit(window)
                strongest_hits.setdefault(key, Counter())[sbh] += 1

    ngrams_out = []
    for key, count in counts.most_common():
        if count < min_occurrences:
            break
        bp = beat_pos_map.get(key, Counter())
        bp_total = sum(bp.values()) or 1
        sh = strongest_hits.get(key, Counter())
        sh_total = sum(sh.values()) or 1

        dur_beats = [v * grid_beat for v in key]
        onset_positions: list[float] = [0.0]
        for d in dur_beats[:-1]:
            onset_positions.append(round(onset_positions[-1] + d, 4))

        if all(d == key[0] for d in key):
            label = f"{n}x{dur_name(key[0], grid_beat)} (running)"
        else:
            label = "-".join(dur_name(d, grid_beat) for d in key)

        entry: dict[str, Any] = {
            "durations_grid": list(key),
            "durations_beats": [round(d, 4) for d in dur_beats],
            "onset_in_beat": onset_positions,
            "label": label,
            "count": count,
            "works_containing": len(work_sets.get(key, set())),
            "frequency_per_1000": round(count / max(total_ngrams, 1) * 1000, 1),
            "beat_position_distribution": {
                "strong": round(bp.get("strong", 0) / bp_total, 2),
                "mid": round(bp.get("mid", 0) / bp_total, 2),
                "weak": round(bp.get("weak", 0) / bp_total, 2),
            },
            "strongest_beat_hit_position": [
                round(sh.get(p, 0) / sh_total, 2) for p in range(n)
            ],
        }
        ngrams_out.append(entry)
        if len(ngrams_out) >= top_k:
            break

    return {
        "category": category,
        "n": n,
        "quantize": quantize,
        "total_works_scanned": len(works),
        "total_ngrams_extracted": total_ngrams,
        "ngrams": ngrams_out,
    }


# ---------------------------------------------------------------------------
# Core extraction: combined figures
# ---------------------------------------------------------------------------


def extract_combined_figures_data(
    category: str,
    index: WorkIndex,
    n: int = 4,
    track: Optional[str] = None,
    interval_mode: str = "degree",
    min_occurrences: int = 3,
    top_k: int = 25,
) -> dict[str, Any]:
    """Extract combined melodic+rhythm figures — returns dict (no JSON)."""
    works = load_works_for_category(category, index)
    if not works:
        return {"error": f"No works found for category '{category}'."}

    counts: Counter = Counter()
    work_sets: dict[tuple, set[str]] = {}
    beat_pos_map: dict[tuple, Counter] = {}
    strongest_hits: dict[tuple, Counter] = {}
    chord_tone_sums: dict[tuple, list[float]] = {}
    examples: dict[tuple, dict] = {}
    total_figures = 0

    for work_id, data in works:
        tonic, is_minor, conf = get_key_info(work_id)
        use_tonic = tonic if conf >= 0.7 else None
        if interval_mode in ("degree", "diatonic") and use_tonic is None:
            eff_mode = "semitone"
        else:
            eff_mode = interval_mode
        tpb = data.get("ticks_per_beat", TICKS_PER_BEAT)

        for role, notes in iter_track_notes(data, track):
            if len(notes) < n:
                continue
            ivs = compute_intervals(notes, eff_mode, use_tonic, is_minor)
            for i in range(len(notes) - n + 1):
                window = notes[i:i + n]
                window_ivs = tuple(ivs[i:i + n - 1]) if (i + n - 1) <= len(ivs) else None
                if window_ivs is None or len(window_ivs) != n - 1:
                    continue

                first_dur = window[0].duration / tpb
                if first_dur <= 0:
                    continue
                dur_ratios = tuple(
                    round(nt.duration / tpb / first_dur * 4) / 4
                    for nt in window
                )
                t0 = window[0].start_tick
                onset_ratios = tuple(
                    round((nt.start_tick - t0) / tpb / first_dur * 4) / 4
                    for nt in window
                )

                key = (window_ivs, dur_ratios, onset_ratios)
                counts[key] += 1
                total_figures += 1
                work_sets.setdefault(key, set()).add(work_id)

                bp = classify_beat_position(window[0].start_tick)
                beat_pos_map.setdefault(key, Counter())[bp] += 1

                sbh = strongest_beat_hit(window)
                strongest_hits.setdefault(key, Counter())[sbh] += 1

                bass_pitch = min(nt.pitch for nt in window)
                ct_count = sum(
                    1 for nt in window if is_chord_tone_simple(nt.pitch, bass_pitch)
                )
                chord_tone_sums.setdefault(key, []).append(ct_count / n)

                if key not in examples:
                    examples[key] = {
                        "work_id": work_id,
                        "track": role,
                        "bar": window[0].bar,
                        "beat_index": window[0].beat - 1,
                        "pitches": [nt.pitch for nt in window],
                    }

    figures_out = []
    for key, count in counts.most_common():
        if count < min_occurrences:
            break
        window_ivs, dur_ratios, onset_ratios = key
        bp = beat_pos_map.get(key, Counter())
        bp_total = sum(bp.values()) or 1
        sh = strongest_hits.get(key, Counter())
        sh_total = sum(sh.values()) or 1
        ct_vals = chord_tone_sums.get(key, [])

        signed_dirs = []
        for iv in window_ivs:
            if isinstance(iv, tuple):
                signed_dirs.append(iv[0])
            else:
                signed_dirs.append(iv)
        if all(d > 0 for d in signed_dirs):
            contour = "ascending"
        elif all(d < 0 for d in signed_dirs):
            contour = "descending"
        elif all(d == 0 for d in signed_dirs):
            contour = "repeated"
        else:
            contour = "mixed"

        is_stepwise = all(
            abs(d) <= 2 if isinstance(d, int) else abs(d[0]) <= 1
            for d in window_ivs
        )

        entry: dict[str, Any] = {
            "intervals": [
                list(iv) if isinstance(iv, tuple) else iv for iv in window_ivs
            ],
            "duration_ratios": list(dur_ratios),
            "onset_ratios": list(onset_ratios),
            "label": label_melodic_ngram(window_ivs, interval_mode) + " " + contour,
            "contour": contour,
            "is_stepwise": is_stepwise,
            "chord_tone_ratio": round(sum(ct_vals) / len(ct_vals), 2) if ct_vals else 0,
            "count": count,
            "works_containing": len(work_sets.get(key, set())),
            "frequency_per_1000": round(count / max(total_figures, 1) * 1000, 1),
            "beat_position_distribution": {
                "strong": round(bp.get("strong", 0) / bp_total, 2),
                "mid": round(bp.get("mid", 0) / bp_total, 2),
                "weak": round(bp.get("weak", 0) / bp_total, 2),
            },
            "strongest_beat_hit_position": [
                round(sh.get(p, 0) / sh_total, 2) for p in range(n)
            ],
            "example": examples.get(key),
        }
        figures_out.append(entry)
        if len(figures_out) >= top_k:
            break

    return {
        "category": category,
        "n": n,
        "interval_mode": interval_mode,
        "total_works_scanned": len(works),
        "total_figures_extracted": total_figures,
        "figures": figures_out,
    }


# ---------------------------------------------------------------------------
# Core extraction: figuration slots
# ---------------------------------------------------------------------------


def detect_figuration_slots_data(
    category: str,
    index: WorkIndex,
    beats_per_pattern: int = 1,
    min_pattern_notes: int = 3,
    chord_tones_only: bool = True,
    top_k: int = 20,
) -> dict[str, Any]:
    """Detect figuration slot patterns — returns dict (no JSON)."""
    works = load_works_for_category(category, index)
    if not works:
        return {"error": f"No works found for category '{category}'."}

    pattern_ticks = beats_per_pattern * TICKS_PER_BEAT
    counts: Counter = Counter()
    work_sets: dict[tuple, set[str]] = {}
    non_ct_sums: dict[tuple, list[float]] = {}
    bass_degrees: dict[tuple, Counter] = {}
    examples: dict[tuple, dict] = {}
    total_patterns = 0

    for work_id, data in works:
        tonic, is_minor, conf = get_key_info(work_id)
        tpb = data.get("ticks_per_beat", TICKS_PER_BEAT)

        all_notes: list[Note] = []
        for t in data.get("tracks", []):
            for nt in t.get("notes", []):
                all_notes.append(Note(
                    pitch=nt["pitch"],
                    velocity=nt.get("velocity", 80),
                    start_tick=int(nt["onset"] * tpb),
                    duration=max(1, int(nt["duration"] * tpb)),
                    voice=t.get("role", "unknown"),
                ))
        all_notes.sort(key=lambda x: x.start_tick)
        if not all_notes:
            continue

        max_tick = all_notes[-1].start_tick
        tick = 0
        while tick <= max_tick:
            window_end = tick + pattern_ticks
            window_notes = [
                nt for nt in all_notes
                if tick <= nt.start_tick < window_end
            ]
            if len(window_notes) < min_pattern_notes:
                tick += pattern_ticks
                continue

            unique_pitches = sorted(set(nt.pitch for nt in window_notes))
            pitch_to_slot = {p: i for i, p in enumerate(unique_pitches)}
            num_voices = len(unique_pitches)
            bass_pitch = unique_pitches[0]

            if chord_tones_only:
                ct_notes = [
                    nt for nt in window_notes
                    if is_chord_tone_simple(nt.pitch, bass_pitch)
                ]
                non_ct_count = len(window_notes) - len(ct_notes)
                non_ct_ratio = non_ct_count / len(window_notes)
                if len(ct_notes) < min_pattern_notes:
                    tick += pattern_ticks
                    continue
                ct_pitches = sorted(set(nt.pitch for nt in ct_notes))
                pitch_to_slot = {p: i for i, p in enumerate(ct_pitches)}
                num_voices = len(ct_pitches)
                bass_pitch = ct_pitches[0]
                use_notes = ct_notes
            else:
                use_notes = window_notes
                non_ct_ratio = 0.0

            use_notes.sort(key=lambda x: x.start_tick)
            slot_seq = tuple(pitch_to_slot[nt.pitch] for nt in use_notes)
            key = (num_voices, slot_seq)

            counts[key] += 1
            total_patterns += 1
            work_sets.setdefault(key, set()).add(work_id)
            non_ct_sums.setdefault(key, []).append(non_ct_ratio)

            if tonic is not None and conf >= 0.7:
                sd = pitch_to_scale_degree(bass_pitch, tonic, is_minor or False)
                bass_degrees.setdefault(key, Counter())[sd.degree] += 1

            if key not in examples:
                examples[key] = {
                    "work_id": work_id,
                    "bar": use_notes[0].bar,
                    "pitches": [nt.pitch for nt in use_notes],
                    "bass_pitch": bass_pitch,
                }

            tick += pattern_ticks

    patterns_out = []
    for key, count in counts.most_common():
        num_voices, slot_seq = key
        nct_vals = non_ct_sums.get(key, [])
        bd = bass_degrees.get(key, Counter())
        bd_most = bd.most_common(1)[0][0] if bd else 0

        entry: dict[str, Any] = {
            "num_voices": num_voices,
            "slot_sequence": list(slot_seq),
            "bass_degree": bd_most,
            "non_chord_tone_ratio": (
                round(sum(nct_vals) / len(nct_vals), 2) if nct_vals else 0
            ),
            "count": count,
            "works_containing": len(work_sets.get(key, set())),
            "label": figuration_label(num_voices, slot_seq),
            "example": examples.get(key),
        }
        patterns_out.append(entry)
        if len(patterns_out) >= top_k:
            break

    return {
        "category": category,
        "beats_per_pattern": beats_per_pattern,
        "chord_tones_only": chord_tones_only,
        "total_works_scanned": len(works),
        "total_patterns_extracted": total_patterns,
        "figuration_patterns": patterns_out,
    }
