# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "mcp[cli]>=1.0.0",
# ]
# ///
"""Bach Reference MCP Server — query normalized Bach reference works.

Provides tools for melodic, harmonic, vertical, and structural analysis
of reference Bach MIDI data. Works are dynamically discovered from
data/reference/*.json. Reuses scripts/bach_analyzer model and utilities.
"""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Optional

# Add project root to sys.path for bach_analyzer imports.
_PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_PROJECT_ROOT))

from mcp.server.fastmcp import FastMCP

from scripts.bach_analyzer.model import (
    TICKS_PER_BAR,
    TICKS_PER_BEAT,
    Note,
    Score,
    Track,
    interval_class,
    is_consonant,
    is_dissonant,
    is_perfect_consonance,
    pitch_to_name,
    sounding_note_at,
)
from scripts.bach_analyzer.music_theory import (
    INTERVAL_NAMES,
    NOTE_NAMES_12,
    beat_strength,
)
from scripts.bach_analyzer.work_index import (
    DATA_DIR,
    MAX_NOTES_RETURN,
    WorkIndex,
    get_key_info,
    reference_to_score,
)
from scripts.bach_analyzer.profiles import (
    js_divergence,
)
from scripts.bach_analyzer.harmony import (
    CHORD_TEMPLATES,
    DEGREE_TO_FUNCTION,
    ChordEstimate,
    categorize_duration,
    classify_suspension_pattern,
    degree_to_roman,
    detect_bass_track,
    detect_suspension,
    estimate_chords_for_score,
)
from scripts.bach_analyzer.ngrams import (
    detect_figuration_slots_data,
    extract_combined_figures_data,
    extract_melodic_ngrams_data,
    extract_rhythm_ngrams_data,
    iter_track_notes,
)

# ---------------------------------------------------------------------------
# MCP Server
# ---------------------------------------------------------------------------

server = FastMCP(
    "bach-reference",
    instructions=(
        "Query normalized Bach reference works. "
        "Use list_works to find work IDs, then analyze with profile tools."
    ),
)

_index: Optional[WorkIndex] = None


def _get_index() -> WorkIndex:
    global _index
    if _index is None:
        _index = WorkIndex(DATA_DIR)
    else:
        _index.refresh()
    return _index


def _load_score(work_id: str) -> tuple[Optional[Score], Optional[dict], str]:
    """Load a work as Score. Returns (score, raw_data, error_msg)."""
    idx = _get_index()
    data = idx.load_full(work_id)
    if data is None:
        return None, None, f"Work '{work_id}' not found."
    return reference_to_score(data), data, ""


# ---------------------------------------------------------------------------
# Chord estimation cache and wrapper
# ---------------------------------------------------------------------------

_CHORD_CACHE: dict[str, tuple[list[ChordEstimate], dict]] = {}


def _estimate_chords_for_work(
    work_id: str,
    sample_interval_beats: float = 1.0,
) -> tuple[list[ChordEstimate], dict[str, Any]]:
    """Estimate chords for all sample points in a work (cached)."""
    cache_key = f"{work_id}:{sample_interval_beats}"
    if cache_key in _CHORD_CACHE:
        return _CHORD_CACHE[cache_key]

    score, raw_data, err = _load_score(work_id)
    if err or score is None or raw_data is None:
        empty: list[ChordEstimate] = []
        stats: dict[str, Any] = {
            "mean_confidence": 0.0,
            "unclassified_ratio": 1.0,
            "low_confidence_ratio": 1.0,
        }
        return empty, stats

    tonic_pc, is_minor, key_conf = get_key_info(work_id)
    if tonic_pc is None or is_minor is None:
        tonic_pc = 0
        is_minor = False

    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)
    tracks_notes = iter_track_notes(raw_data, None)
    if not tracks_notes:
        empty_list: list[ChordEstimate] = []
        empty_stats: dict[str, Any] = {
            "mean_confidence": 0.0,
            "unclassified_ratio": 1.0,
            "low_confidence_ratio": 1.0,
        }
        return empty_list, empty_stats

    max_tick = 0
    for _, sorted_notes in tracks_notes:
        if sorted_notes:
            last = sorted_notes[-1]
            max_tick = max(max_tick, last.start_tick + last.duration)

    chords, stats_out = estimate_chords_for_score(
        tracks_notes, tonic_pc, is_minor, tpb, max_tick, sample_interval_beats,
    )

    _CHORD_CACHE[cache_key] = (chords, stats_out)
    return chords, stats_out


# ---------------------------------------------------------------------------
# Chord distribution helpers
# ---------------------------------------------------------------------------

def _get_chord_distribution(
    work_id: str, profile_type: str,
) -> dict[str, float]:
    """Build a distribution from chord estimation for comparison."""
    chords, stats = _estimate_chords_for_work(work_id, 1.0)
    if not chords:
        return {}
    tonic, is_minor, conf = get_key_info(work_id)
    counts: Counter = Counter()
    for chord in chords:
        if chord.confidence <= 0.0:
            continue
        if profile_type == "function":
            counts[DEGREE_TO_FUNCTION.get(chord.degree, "T")] += 1
        else:  # harmony_degrees
            counts[degree_to_roman(chord.degree, chord.quality, is_minor or False)] += 1
    return dict(counts)


def _get_distribution(score: Score, profile_type: str) -> dict[str, float]:
    """Extract a distribution from a score for comparison."""
    if profile_type == "interval":
        counts: Counter = Counter()
        for tr in score.tracks:
            sn = tr.sorted_notes
            for idx in range(len(sn) - 1):
                ic = interval_class(sn[idx + 1].pitch - sn[idx].pitch)
                counts[INTERVAL_NAMES[ic]] += 1
        return dict(counts)

    if profile_type == "rhythm":
        counts = Counter()
        for tr in score.tracks:
            for note in tr.notes:
                beats = note.duration / TICKS_PER_BEAT
                if beats <= 0.375:
                    counts["16th"] += 1
                elif beats <= 0.75:
                    counts["8th"] += 1
                elif beats <= 1.5:
                    counts["quarter"] += 1
                elif beats <= 3.0:
                    counts["half"] += 1
                else:
                    counts["whole+"] += 1
        return dict(counts)

    if profile_type == "pitch_class":
        counts = Counter()
        for tr in score.tracks:
            for note in tr.notes:
                counts[NOTE_NAMES_12[note.pitch % 12]] += 1
        return dict(counts)

    if profile_type == "vertical":
        if len(score.tracks) < 2:
            return {}
        na_sorted = score.tracks[0].sorted_notes
        nb_sorted = score.tracks[-1].sorted_notes
        counts = Counter()
        tick = 0
        while tick < score.total_duration:
            na = sounding_note_at(na_sorted, tick)
            nb = sounding_note_at(nb_sorted, tick)
            if na and nb:
                counts[INTERVAL_NAMES[interval_class(na.pitch - nb.pitch)]] += 1
            tick += TICKS_PER_BEAT
        return dict(counts)

    return {}


# ---------------------------------------------------------------------------
# Track resolution helper
# ---------------------------------------------------------------------------

def _resolve_tracks(
    score: Score,
    track_a: Optional[str],
    track_b: Optional[str],
) -> tuple[Optional[Track], Optional[Track], str]:
    """Resolve two tracks by name, defaulting to first and last."""
    if len(score.tracks) < 2:
        return None, None, "Need at least 2 tracks for this analysis."
    ta = (
        next((t for t in score.tracks if t.name == track_a), None)
        if track_a else score.tracks[0]
    )
    tb = (
        next((t for t in score.tracks if t.name == track_b), None)
        if track_b else score.tracks[-1]
    )
    if ta is None:
        return None, None, f"Track '{track_a}' not found."
    if tb is None:
        return None, None, f"Track '{track_b}' not found."
    return ta, tb, ""


# ===== A. Basic queries (3) =================================================


@server.tool()
def list_works(
    category: Optional[str] = None,
    instrument: Optional[str] = None,
    form: Optional[str] = None,
    min_voices: Optional[int] = None,
    max_voices: Optional[int] = None,
) -> str:
    """List available Bach reference works with optional filters.

    Call with no arguments to see available categories and instruments.

    Args:
        category: Filter by category (call with no args to see available values)
        instrument: Filter by instrument (call with no args to see available values)
        form: Filter by form name
        min_voices: Minimum voice count
        max_voices: Maximum voice count
    """
    idx = _get_index()
    works = idx.filter(
        category=category, instrument=instrument, form=form,
        min_voices=min_voices, max_voices=max_voices,
    )
    if not works:
        return json.dumps({
            "count": 0, "works": [],
            "available_categories": idx.categories(),
            "available_instruments": idx.instruments(),
        })
    compact = [
        {
            "id": w["id"],
            "bwv": w["bwv"],
            "category": w["category"],
            "instrument": w["instrument"],
            "voices": w["voice_count"],
            "notes": w["total_notes"],
            "duration_s": w["duration_seconds"],
            "roles": w["roles"],
        }
        for w in works
    ]
    result: dict[str, Any] = {"count": len(compact), "works": compact}
    no_filter = all(
        v is None for v in [category, instrument, form, min_voices, max_voices]
    )
    if no_filter:
        result["available_categories"] = idx.categories()
        result["available_instruments"] = idx.instruments()
    return json.dumps(result, indent=2)


@server.tool()
def get_work_info(work_id: str) -> str:
    """Get detailed metadata for a specific work (no notes).

    Args:
        work_id: Work identifier (e.g. BWV846_1, BWV988_1, BWV1007_1)
    """
    idx = _get_index()
    meta = idx.get_meta(work_id)
    if not meta:
        return json.dumps({
            "error": f"Work '{work_id}' not found.",
            "hint": "Use list_works to find valid IDs.",
        })
    return json.dumps(meta, indent=2)


@server.tool()
def get_notes(
    work_id: str,
    track: Optional[str] = None,
    bar_start: Optional[int] = None,
    bar_end: Optional[int] = None,
    limit: int = 500,
) -> str:
    """Get notes from a work, optionally filtered by track and bar range.

    Args:
        work_id: Work identifier
        track: Track role to filter (e.g. upper, lower, pedal, solo_0)
        bar_start: Start bar (1-based, inclusive)
        bar_end: End bar (1-based, inclusive)
        limit: Maximum notes to return (default 500)
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    limit = min(limit, MAX_NOTES_RETURN)
    notes = []
    for tr in score.tracks:
        if track and tr.name != track:
            continue
        for note in tr.sorted_notes:
            if bar_start and note.bar < bar_start:
                continue
            if bar_end and note.bar > bar_end:
                continue
            notes.append({
                "pitch": note.pitch,
                "name": pitch_to_name(note.pitch),
                "onset_beat": note.start_tick / TICKS_PER_BEAT,
                "duration_beat": note.duration / TICKS_PER_BEAT,
                "bar": note.bar,
                "beat": note.beat,
                "velocity": note.velocity,
                "voice": note.voice,
            })
    notes.sort(key=lambda x: (x["onset_beat"], x["pitch"]))
    truncated = len(notes) > limit
    notes = notes[:limit]
    return json.dumps({
        "work_id": work_id,
        "count": len(notes),
        "truncated": truncated,
        "notes": notes,
    }, indent=2)


# ===== B. Melodic analysis (3) ==============================================


@server.tool()
def get_interval_profile(
    work_id: str,
    track: Optional[str] = None,
) -> str:
    """Analyze melodic interval distribution for a work.

    Returns interval histogram (P1 through M7), stepwise motion ratio,
    leap percentage, and average interval size.

    Args:
        work_id: Work identifier
        track: Optional track role to analyze (all tracks if omitted)
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    all_intervals: Counter = Counter()
    total_intervals = 0
    step_count = 0
    leap_count = 0
    per_track: dict[str, dict] = {}

    for tr in score.tracks:
        if track and tr.name != track:
            continue
        sorted_n = tr.sorted_notes
        if len(sorted_n) < 2:
            continue
        t_iv: Counter = Counter()
        t_steps = 0
        t_leaps = 0
        for idx in range(len(sorted_n) - 1):
            iv = abs(sorted_n[idx + 1].pitch - sorted_n[idx].pitch)
            ic = interval_class(iv)
            t_iv[ic] += 1
            if ic in (1, 2):
                t_steps += 1
            elif ic > 2:
                t_leaps += 1
        total = sum(t_iv.values())
        all_intervals += t_iv
        total_intervals += total
        step_count += t_steps
        leap_count += t_leaps
        per_track[tr.name] = {
            "intervals": {INTERVAL_NAMES[k]: v for k, v in sorted(t_iv.items())},
            "total": total,
            "stepwise_ratio": round(t_steps / total, 4) if total else 0,
            "leap_ratio": round(t_leaps / total, 4) if total else 0,
            "avg_interval": round(
                sum(k * v for k, v in t_iv.items()) / total, 2
            ) if total else 0,
        }

    result: dict[str, Any] = {
        "work_id": work_id,
        "combined": {
            "intervals": {
                INTERVAL_NAMES[k]: v for k, v in sorted(all_intervals.items())
            },
            "total": total_intervals,
            "stepwise_ratio": round(
                step_count / total_intervals, 4
            ) if total_intervals else 0,
            "leap_ratio": round(
                leap_count / total_intervals, 4
            ) if total_intervals else 0,
            "avg_interval": round(
                sum(k * v for k, v in all_intervals.items()) / total_intervals, 2
            ) if total_intervals else 0,
        },
    }
    if len(per_track) > 1:
        result["per_track"] = per_track
    return json.dumps(result, indent=2)


@server.tool()
def get_rhythm_profile(
    work_id: str,
    track: Optional[str] = None,
) -> str:
    """Analyze rhythm/duration distribution for a work.

    Categorizes notes as 32nd, 16th, 8th, quarter, half, whole, or longer.

    Args:
        work_id: Work identifier
        track: Optional track role to analyze
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    per_track: dict[str, dict] = {}
    all_dur: Counter = Counter()

    for tr in score.tracks:
        if track and tr.name != track:
            continue
        t_dur: Counter = Counter()
        durations: list[float] = []
        for note in tr.notes:
            t_dur[categorize_duration(note.duration)] += 1
            durations.append(note.duration / TICKS_PER_BEAT)
        all_dur += t_dur
        total = sum(t_dur.values())
        per_track[tr.name] = {
            "distribution": dict(sorted(t_dur.items(), key=lambda x: -x[1])),
            "total_notes": total,
            "avg_duration_beats": round(
                sum(durations) / len(durations), 4
            ) if durations else 0,
            "min_duration_beats": round(min(durations), 4) if durations else 0,
            "max_duration_beats": round(max(durations), 4) if durations else 0,
        }

    result: dict[str, Any] = {
        "work_id": work_id,
        "combined": {
            "distribution": dict(sorted(all_dur.items(), key=lambda x: -x[1])),
            "total_notes": sum(all_dur.values()),
        },
    }
    if len(per_track) > 1:
        result["per_track"] = per_track
    elif per_track:
        result["combined"].update(list(per_track.values())[0])
    return json.dumps(result, indent=2)


@server.tool()
def get_pitch_profile(
    work_id: str,
    track: Optional[str] = None,
) -> str:
    """Analyze pitch distribution for a work.

    Returns pitch class histogram, octave distribution, range, and average pitch.

    Args:
        work_id: Work identifier
        track: Optional track role to analyze
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    per_track: dict[str, dict] = {}
    all_pc: Counter = Counter()
    all_oct: Counter = Counter()
    all_pitches: list[int] = []

    for tr in score.tracks:
        if track and tr.name != track:
            continue
        pitches = [note.pitch for note in tr.notes]
        if not pitches:
            continue
        all_pitches.extend(pitches)
        t_pc: Counter = Counter()
        t_oct: Counter = Counter()
        for p in pitches:
            t_pc[NOTE_NAMES_12[p % 12]] += 1
            t_oct[p // 12 - 1] += 1
        all_pc += t_pc
        all_oct += t_oct
        per_track[tr.name] = {
            "pitch_class": dict(sorted(t_pc.items(), key=lambda x: -x[1])),
            "octave": dict(sorted(t_oct.items())),
            "range": [min(pitches), max(pitches)],
            "range_names": [pitch_to_name(min(pitches)), pitch_to_name(max(pitches))],
            "range_semitones": max(pitches) - min(pitches),
            "avg_pitch": round(sum(pitches) / len(pitches), 1),
            "note_count": len(pitches),
        }

    result: dict[str, Any] = {
        "work_id": work_id,
        "combined": {
            "pitch_class": dict(sorted(all_pc.items(), key=lambda x: -x[1])),
            "octave": dict(sorted(all_oct.items())),
            "range": (
                [min(all_pitches), max(all_pitches)] if all_pitches else []
            ),
            "range_names": (
                [pitch_to_name(min(all_pitches)), pitch_to_name(max(all_pitches))]
                if all_pitches else []
            ),
            "avg_pitch": (
                round(sum(all_pitches) / len(all_pitches), 1)
                if all_pitches else 0
            ),
            "total_notes": len(all_pitches),
        },
    }
    if len(per_track) > 1:
        result["per_track"] = per_track
    return json.dumps(result, indent=2)


# ===== C. Counterpoint analysis (3) =========================================


@server.tool()
def get_vertical_profile(
    work_id: str,
    track_a: Optional[str] = None,
    track_b: Optional[str] = None,
    sample_interval_beats: float = 1.0,
) -> str:
    """Analyze vertical (simultaneous) intervals between two voices.

    Samples at regular intervals and classifies as consonant/dissonant.
    Uses sounding_note_at() for sustained-note accuracy.

    Args:
        work_id: Work identifier
        track_a: First track (default: first track)
        track_b: Second track (default: last track)
        sample_interval_beats: Sampling interval in beats (default 1.0)
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    ta, tb, terr = _resolve_tracks(score, track_a, track_b)
    if terr:
        return json.dumps({"error": terr})

    notes_a = ta.sorted_notes
    notes_b = tb.sorted_notes
    sample_ticks = int(sample_interval_beats * TICKS_PER_BEAT)
    total_dur = score.total_duration

    iv_counts: Counter = Counter()
    consonant = 0
    dissonant = 0
    perfect_cons = 0
    total_samples = 0

    tick = 0
    while tick < total_dur:
        na = sounding_note_at(notes_a, tick)
        nb = sounding_note_at(notes_b, tick)
        if na is not None and nb is not None:
            diff = na.pitch - nb.pitch
            ic = interval_class(diff)
            iv_counts[ic] += 1
            total_samples += 1
            if is_consonant(diff):
                consonant += 1
            if is_perfect_consonance(diff):
                perfect_cons += 1
            if is_dissonant(diff):
                dissonant += 1
        tick += sample_ticks

    return json.dumps({
        "work_id": work_id,
        "track_a": ta.name,
        "track_b": tb.name,
        "sample_interval_beats": sample_interval_beats,
        "total_samples": total_samples,
        "intervals": {
            INTERVAL_NAMES[k]: v for k, v in sorted(iv_counts.items())
        },
        "consonance_ratio": round(
            consonant / total_samples, 4
        ) if total_samples else 0,
        "perfect_consonance_ratio": round(
            perfect_cons / total_samples, 4
        ) if total_samples else 0,
        "dissonance_ratio": round(
            dissonant / total_samples, 4
        ) if total_samples else 0,
    }, indent=2)


@server.tool()
def get_motion_profile(
    work_id: str,
    track_a: Optional[str] = None,
    track_b: Optional[str] = None,
) -> str:
    """Analyze voice motion types between two tracks.

    Classifies each beat-to-beat transition as parallel, similar, contrary,
    or oblique motion.

    Args:
        work_id: Work identifier
        track_a: First track (default: first)
        track_b: Second track (default: last)
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    ta, tb, terr = _resolve_tracks(score, track_a, track_b)
    if terr:
        return json.dumps({"error": terr})

    notes_a = ta.sorted_notes
    notes_b = tb.sorted_notes
    total_dur = score.total_duration

    motions: Counter = Counter()
    total = 0
    prev_a: Optional[Note] = None
    prev_b: Optional[Note] = None

    tick = 0
    while tick < total_dur:
        na = sounding_note_at(notes_a, tick)
        nb = sounding_note_at(notes_b, tick)
        if (na is not None and nb is not None
                and prev_a is not None and prev_b is not None):
            da = na.pitch - prev_a.pitch
            db = nb.pitch - prev_b.pitch
            if da == 0 and db == 0:
                pass  # no motion
            elif da == 0 or db == 0:
                motions["oblique"] += 1
                total += 1
            elif (da > 0 and db > 0) or (da < 0 and db < 0):
                iv_cur = interval_class(na.pitch - nb.pitch)
                iv_prev = interval_class(prev_a.pitch - prev_b.pitch)
                if iv_cur == iv_prev:
                    motions["parallel"] += 1
                else:
                    motions["similar"] += 1
                total += 1
            else:
                motions["contrary"] += 1
                total += 1
        prev_a = na
        prev_b = nb
        tick += TICKS_PER_BEAT

    return json.dumps({
        "work_id": work_id,
        "track_a": ta.name,
        "track_b": tb.name,
        "total_transitions": total,
        "motion_types": dict(
            sorted(motions.items(), key=lambda x: -x[1])
        ),
        "contrary_ratio": round(
            motions.get("contrary", 0) / total, 4
        ) if total else 0,
        "parallel_ratio": round(
            motions.get("parallel", 0) / total, 4
        ) if total else 0,
        "similar_ratio": round(
            motions.get("similar", 0) / total, 4
        ) if total else 0,
        "oblique_ratio": round(
            motions.get("oblique", 0) / total, 4
        ) if total else 0,
    }, indent=2)


@server.tool()
def get_voice_entry_pattern(work_id: str) -> str:
    """Analyze voice entry order and pitch relationships.

    Returns the first note of each track, entry order by onset, and
    interval relationships between consecutive entries.

    Args:
        work_id: Work identifier
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    entries = []
    for tr in score.tracks:
        sorted_n = tr.sorted_notes
        if sorted_n:
            first = sorted_n[0]
            entries.append({
                "track": tr.name,
                "onset_beat": first.start_tick / TICKS_PER_BEAT,
                "bar": first.bar,
                "beat": first.beat,
                "pitch": first.pitch,
                "name": pitch_to_name(first.pitch),
            })

    entries.sort(key=lambda x: x["onset_beat"])

    for idx in range(1, len(entries)):
        iv = entries[idx]["pitch"] - entries[idx - 1]["pitch"]
        entries[idx]["interval_from_prev"] = iv
        entries[idx]["interval_class"] = interval_class(iv)

    return json.dumps({
        "work_id": work_id,
        "entry_count": len(entries),
        "entries": entries,
    }, indent=2)


# ===== D. Pattern analysis (2) ==============================================


@server.tool()
def get_texture_profile(
    work_id: str,
    sample_interval_beats: float = 1.0,
) -> str:
    """Analyze texture density (active voice count per sample point).

    Samples at regular intervals to count how many voices are sounding.

    Args:
        work_id: Work identifier
        sample_interval_beats: Sampling interval in beats (default 1.0)
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    sample_ticks = int(sample_interval_beats * TICKS_PER_BEAT)
    total_dur = score.total_duration
    sorted_tracks = [(tr.name, tr.sorted_notes) for tr in score.tracks]

    density_counts: Counter = Counter()
    total_samples = 0

    tick = 0
    while tick < total_dur:
        active = sum(
            1 for _, notes in sorted_tracks
            if sounding_note_at(notes, tick) is not None
        )
        density_counts[active] += 1
        total_samples += 1
        tick += sample_ticks

    avg_density = (
        sum(k * v for k, v in density_counts.items()) / total_samples
        if total_samples else 0
    )

    return json.dumps({
        "work_id": work_id,
        "total_tracks": len(score.tracks),
        "sample_interval_beats": sample_interval_beats,
        "total_samples": total_samples,
        "density_distribution": {
            str(k): v for k, v in sorted(density_counts.items())
        },
        "avg_active_voices": round(avg_density, 2),
        "max_active_voices": max(density_counts.keys()) if density_counts else 0,
    }, indent=2)


@server.tool()
def search_pattern(
    work_id: str,
    interval_pattern: str,
    track: Optional[str] = None,
    tolerance: int = 0,
) -> str:
    """Search for a melodic interval pattern in a work.

    Matches a sequence of directed intervals (semitones) against each track.

    Args:
        work_id: Work identifier
        interval_pattern: Comma-separated directed intervals,
                          e.g. "2,-1,2" for up-step, down-step, up-step
        track: Optional track to search in
        tolerance: Allowed deviation per interval (default 0 = exact)
    """
    score, _, err = _load_score(work_id)
    if err:
        return json.dumps({"error": err})

    try:
        pattern = [int(x.strip()) for x in interval_pattern.split(",")]
    except ValueError:
        return json.dumps({
            "error": "Invalid interval_pattern. Use comma-separated integers.",
        })

    if not pattern:
        return json.dumps({"error": "Empty pattern."})

    matches = []
    max_matches = 50

    for tr in score.tracks:
        if track and tr.name != track:
            continue
        sorted_n = tr.sorted_notes
        if len(sorted_n) < len(pattern) + 1:
            continue
        intervals = [
            sorted_n[idx + 1].pitch - sorted_n[idx].pitch
            for idx in range(len(sorted_n) - 1)
        ]
        for idx in range(len(intervals) - len(pattern) + 1):
            if all(
                abs(intervals[idx + jdx] - p) <= tolerance
                for jdx, p in enumerate(pattern)
            ):
                n0 = sorted_n[idx]
                matches.append({
                    "track": tr.name,
                    "bar": n0.bar,
                    "beat": n0.beat,
                    "onset_beat": n0.start_tick / TICKS_PER_BEAT,
                    "starting_pitch": n0.pitch,
                    "starting_name": pitch_to_name(n0.pitch),
                    "matched_intervals": intervals[idx:idx + len(pattern)],
                })
                if len(matches) >= max_matches:
                    break
        if len(matches) >= max_matches:
            break

    return json.dumps({
        "work_id": work_id,
        "pattern": pattern,
        "tolerance": tolerance,
        "match_count": len(matches),
        "truncated": len(matches) >= max_matches,
        "matches": matches,
    }, indent=2)


# ===== E. Comparison & summary (2) ==========================================


@server.tool()
def compare_profiles(
    work_id_a: str,
    work_id_b: str,
    profile_type: str = "interval",
) -> str:
    """Compare two works using Jensen-Shannon divergence.

    Args:
        work_id_a: First work identifier
        work_id_b: Second work identifier
        profile_type: Profile to compare: "interval", "rhythm",
                      "pitch_class", "vertical", "harmony_degrees", or "function"
    """
    if profile_type in ("harmony_degrees", "function"):
        pa = _get_chord_distribution(work_id_a, profile_type)
        pb = _get_chord_distribution(work_id_b, profile_type)
        if not pa or not pb:
            return json.dumps({"error": "Chord estimation unavailable for one or both works."})
    else:
        score_a, _, err_a = _load_score(work_id_a)
        if err_a:
            return json.dumps({"error": err_a})
        score_b, _, err_b = _load_score(work_id_b)
        if err_b:
            return json.dumps({"error": err_b})
        pa = _get_distribution(score_a, profile_type)
        pb = _get_distribution(score_b, profile_type)
    jsd = round(js_divergence(pa, pb), 6)

    if jsd < 0.01:
        interp = "identical"
    elif jsd < 0.05:
        interp = "very similar"
    elif jsd < 0.15:
        interp = "similar"
    elif jsd < 0.3:
        interp = "different"
    else:
        interp = "very different"

    return json.dumps({
        "work_a": work_id_a,
        "work_b": work_id_b,
        "profile_type": profile_type,
        "js_divergence": jsd,
        "interpretation": interp,
        "profile_a": pa,
        "profile_b": pb,
    }, indent=2)


@server.tool()
def get_category_summary(category: str) -> str:
    """Get aggregate statistics across all works in a category.

    Computes average stepwise ratio, leap ratio, consonance ratio,
    voice count, and note density for the entire category.

    Args:
        category: Category name (use list_works to see available categories)
    """
    idx = _get_index()
    works = idx.filter(category=category)
    if not works:
        return json.dumps({
            "error": f"No works in category '{category}'.",
            "available": idx.categories(),
        })

    stats: dict[str, list[float]] = {
        "stepwise_ratios": [],
        "leap_ratios": [],
        "consonance_ratios": [],
        "voice_counts": [],
        "notes_per_bar": [],
        "avg_intervals": [],
    }
    all_intervals: Counter = Counter()
    total_interval_count = 0

    for w in works:
        data = idx.load_full(w["id"])
        if not data:
            continue
        score = reference_to_score(data)

        steps = 0
        leaps = 0
        iv_total = 0
        iv_sum = 0
        for tr in score.tracks:
            sn = tr.sorted_notes
            for nidx in range(len(sn) - 1):
                iv = abs(sn[nidx + 1].pitch - sn[nidx].pitch)
                ic = interval_class(iv)
                all_intervals[ic] += 1
                iv_sum += ic
                iv_total += 1
                if ic in (1, 2):
                    steps += 1
                elif ic > 2:
                    leaps += 1
        if iv_total:
            stats["stepwise_ratios"].append(steps / iv_total)
            stats["leap_ratios"].append(leaps / iv_total)
            stats["avg_intervals"].append(iv_sum / iv_total)
            total_interval_count += iv_total

        if len(score.tracks) >= 2:
            cons = 0
            cons_total = 0
            na_sorted = score.tracks[0].sorted_notes
            nb_sorted = score.tracks[-1].sorted_notes
            tick = 0
            while tick < score.total_duration:
                na = sounding_note_at(na_sorted, tick)
                nb = sounding_note_at(nb_sorted, tick)
                if na and nb:
                    cons_total += 1
                    if is_consonant(na.pitch - nb.pitch):
                        cons += 1
                tick += TICKS_PER_BEAT
            if cons_total:
                stats["consonance_ratios"].append(cons / cons_total)

        stats["voice_counts"].append(float(score.num_voices))
        if score.total_bars:
            stats["notes_per_bar"].append(score.total_notes / score.total_bars)

    def _avg(lst: list[float]) -> float:
        return round(sum(lst) / len(lst), 4) if lst else 0.0

    def _std(lst: list[float]) -> float:
        if len(lst) < 2:
            return 0.0
        mean = sum(lst) / len(lst)
        return round(
            (sum((x - mean) ** 2 for x in lst) / (len(lst) - 1)) ** 0.5, 4
        )

    return json.dumps({
        "category": category,
        "work_count": len(works),
        "avg_stepwise_ratio": _avg(stats["stepwise_ratios"]),
        "std_stepwise_ratio": _std(stats["stepwise_ratios"]),
        "avg_leap_ratio": _avg(stats["leap_ratios"]),
        "avg_consonance_ratio": _avg(stats["consonance_ratios"]),
        "std_consonance_ratio": _std(stats["consonance_ratios"]),
        "avg_voice_count": _avg(stats["voice_counts"]),
        "avg_notes_per_bar": _avg(stats["notes_per_bar"]),
        "avg_interval_size": _avg(stats["avg_intervals"]),
        "interval_distribution": {
            INTERVAL_NAMES[k]: v for k, v in sorted(all_intervals.items())
        } if all_intervals else {},
    }, indent=2)


# ===== F. Pattern vocabulary extraction (4) ==================================


@server.tool()
def extract_melodic_ngrams(
    category: str,
    n: int = 3,
    track: Optional[str] = None,
    interval_mode: str = "degree",
    min_occurrences: int = 5,
    top_k: int = 30,
) -> str:
    """Extract melodic interval n-grams from all works in a category.

    Scans all tracks (or a filtered track) in every work of the given category,
    extracts consecutive interval n-grams, and returns the most frequent ones
    with beat-position statistics and examples.

    Args:
        category: Reference category (e.g. organ_fugue, wtc1, solo_cello_suite)
        n: N-gram size (number of intervals = n, notes = n+1). Default 3.
        track: Optional track role filter (e.g. 'upper', 'pedal', 'v1')
        interval_mode: 'semitone', 'degree', or 'diatonic'. Default 'degree'.
        min_occurrences: Minimum count to include in results. Default 5.
        top_k: Maximum number of n-grams to return. Default 30.
    """
    if interval_mode not in ("semitone", "degree", "diatonic"):
        return json.dumps({"error": f"Invalid interval_mode: {interval_mode}"})

    result = extract_melodic_ngrams_data(
        category, _get_index(), n=n, track=track,
        interval_mode=interval_mode, min_occurrences=min_occurrences, top_k=top_k,
    )
    return json.dumps(result, indent=2)


@server.tool()
def extract_rhythm_ngrams(
    category: str,
    n: int = 4,
    track: Optional[str] = None,
    quantize: str = "sixteenth",
    min_occurrences: int = 5,
    top_k: int = 30,
) -> str:
    """Extract rhythm n-grams (duration sequences) from all works in a category.

    Durations are quantized to the specified grid. Returns the most frequent
    duration patterns with beat-position and accent statistics.

    Args:
        category: Reference category (e.g. organ_fugue, wtc1)
        n: N-gram size (number of notes). Default 4.
        track: Optional track role filter
        quantize: Grid for quantization: 'sixteenth', 'eighth', 'quarter'. Default 'sixteenth'.
        min_occurrences: Minimum count to include. Default 5.
        top_k: Maximum results. Default 30.
    """
    result = extract_rhythm_ngrams_data(
        category, _get_index(), n=n, track=track,
        quantize=quantize, min_occurrences=min_occurrences, top_k=top_k,
    )
    return json.dumps(result, indent=2)


@server.tool()
def extract_combined_figures(
    category: str,
    n: int = 4,
    track: Optional[str] = None,
    interval_mode: str = "degree",
    min_occurrences: int = 3,
    top_k: int = 25,
) -> str:
    """Extract combined melodic+rhythm figures from all works in a category.

    Each figure is a combination of interval sequence, duration ratios, and
    onset ratios. This is the core pattern extraction tool.

    Args:
        category: Reference category
        n: Figure size (number of notes). Default 4.
        track: Optional track role filter
        interval_mode: 'semitone', 'degree', or 'diatonic'. Default 'degree'.
        min_occurrences: Minimum count. Default 3.
        top_k: Maximum results. Default 25.
    """
    if interval_mode not in ("semitone", "degree", "diatonic"):
        return json.dumps({"error": f"Invalid interval_mode: {interval_mode}"})

    result = extract_combined_figures_data(
        category, _get_index(), n=n, track=track,
        interval_mode=interval_mode, min_occurrences=min_occurrences, top_k=top_k,
    )
    return json.dumps(result, indent=2)


@server.tool()
def detect_figuration_slots(
    category: str,
    beats_per_pattern: int = 1,
    min_pattern_notes: int = 3,
    chord_tones_only: bool = True,
    top_k: int = 20,
) -> str:
    """Detect figuration slot patterns (arpeggio orderings) in a category.

    Analyzes how chord tones are voiced in each beat group, mapping notes to
    vertical slots (0=bass, N-1=soprano) and recording the temporal access order.

    Args:
        category: Reference category
        beats_per_pattern: Number of beats per pattern window. Default 1.
        min_pattern_notes: Minimum notes in a pattern to include. Default 3.
        chord_tones_only: If True, only chord tones are slotted. Default True.
        top_k: Maximum results. Default 20.
    """
    result = detect_figuration_slots_data(
        category, _get_index(), beats_per_pattern=beats_per_pattern,
        min_pattern_notes=min_pattern_notes, chord_tones_only=chord_tones_only,
        top_k=top_k,
    )
    return json.dumps(result, indent=2)


# ===== G. Harmonic analysis (2) =============================================


@server.tool()
def get_harmonic_profile(
    work_id: str,
    sample_interval_beats: float = 1.0,
) -> str:
    """Analyze harmonic content of a Bach work via chord estimation.

    Estimates chords at regular intervals using template matching with
    weighted pitch classes, inversion scoring, and Markov priors for
    Bach-typical progressions.

    Args:
        work_id: Work identifier (e.g. BWV578, BWV846_1)
        sample_interval_beats: Interval between chord samples in beats
            (default 1.0 = quarter note resolution)
    """
    tonic_pc, is_minor, key_conf = get_key_info(work_id)
    if tonic_pc is None:
        tonic_pc = 0
        is_minor = False

    chords, est_quality = _estimate_chords_for_work(
        work_id, sample_interval_beats,
    )
    if not chords:
        return json.dumps({"error": f"No chords estimated for '{work_id}'."})

    key_name = NOTE_NAMES_12[tonic_pc]
    key_label = f"{key_name} {'minor' if is_minor else 'major'}"

    confident = [c for c in chords if c.confidence > 0.0]
    total_confident = len(confident)

    degree_counts: Counter[str] = Counter()
    for chord in confident:
        roman = degree_to_roman(chord.degree, chord.quality, is_minor)
        degree_counts[roman] += 1

    degree_dist: dict[str, float] = {}
    if total_confident > 0:
        for roman, count in degree_counts.most_common():
            degree_dist[roman] = round(count / total_confident, 3)

    quality_counts: Counter[str] = Counter()
    for chord in confident:
        if chord.quality:
            quality_counts[chord.quality] += 1

    total_quality = sum(quality_counts.values())
    quality_dist: dict[str, float] = {}
    if total_quality > 0:
        for qual, count in quality_counts.most_common():
            quality_dist[qual] = round(count / total_quality, 3)

    func_counts: Counter[str] = Counter()
    for chord in confident:
        func = DEGREE_TO_FUNCTION.get(chord.degree, "?")
        func_counts[func] += 1

    func_dist: dict[str, float] = {}
    if total_confident > 0:
        for func, count in func_counts.most_common():
            func_dist[func] = round(count / total_confident, 3)

    inv_counts: Counter[str] = Counter()
    for chord in confident:
        inv_counts[chord.inversion] += 1

    inv_dist: dict[str, float] = {}
    if total_confident > 0:
        for inv, count in inv_counts.most_common():
            inv_dist[inv] = round(count / total_confident, 3)

    changes = 0
    prev_deg = -1
    prev_qual = ""
    for chord in chords:
        if chord.confidence > 0.0:
            if (chord.degree != prev_deg or chord.quality != prev_qual):
                if prev_deg >= 0:
                    changes += 1
                prev_deg = chord.degree
                prev_qual = chord.quality

    total_samples = len(chords)
    total_beats = total_samples * sample_interval_beats
    total_bars = total_beats / 4.0

    avg_beats_per_change = (
        round(total_beats / changes, 2) if changes > 0 else 0.0
    )
    changes_per_bar = (
        round(changes / total_bars, 2) if total_bars > 0 else 0.0
    )

    bigram_counts: Counter[tuple[str, str]] = Counter()
    prev_roman: Optional[str] = None
    for chord in chords:
        if chord.confidence > 0.0:
            roman = degree_to_roman(chord.degree, chord.quality, is_minor)
            if prev_roman is not None and roman != prev_roman:
                bigram_counts[(prev_roman, roman)] += 1
            prev_roman = roman

    total_bigrams = sum(bigram_counts.values())
    bigrams_top10 = []
    for (from_r, to_r), count in bigram_counts.most_common(10):
        bigrams_top10.append({
            "from": from_r,
            "to": to_r,
            "count": count,
            "ratio": round(count / total_bigrams, 3) if total_bigrams > 0 else 0.0,
        })

    result: dict[str, Any] = {
        "work_id": work_id,
        "key": key_label,
        "total_samples": total_samples,
        "estimation_quality": est_quality,
        "degree_distribution": degree_dist,
        "quality_distribution": quality_dist,
        "function_distribution": func_dist,
        "inversion_distribution": inv_dist,
        "harmonic_rhythm": {
            "avg_beats_per_chord_change": avg_beats_per_change,
            "changes_per_bar": changes_per_bar,
        },
        "degree_bigrams_top10": bigrams_top10,
    }
    return json.dumps(result, indent=2)


@server.tool()
def get_cadence_profile(work_id: str) -> str:
    """Detect and classify cadences in a Bach work.

    Analyzes chord progressions at half-beat resolution to identify
    cadence patterns (PAC, IAC, HC, DC, PHR, EVAD) with confidence
    scoring based on multiple musical signals.

    Args:
        work_id: Work identifier (e.g. BWV578, BWV846_1)
    """
    tonic_pc, is_minor, key_conf = get_key_info(work_id)
    if tonic_pc is None:
        tonic_pc = 0
        is_minor = False

    chords, _ = _estimate_chords_for_work(work_id, 0.5)
    if not chords:
        return json.dumps({"error": f"No chords estimated for '{work_id}'."})

    score, raw_data, err = _load_score(work_id)
    if err or score is None or raw_data is None:
        return json.dumps({"error": err})

    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)
    tracks_notes = iter_track_notes(raw_data, None)
    sample_ticks = tpb // 2

    cadences: list[dict[str, Any]] = []
    cadence_counts: Counter[str] = Counter()

    def _has_leading_tone_resolution(idx: int) -> bool:
        if idx < 1 or not tracks_notes:
            return False
        tick_before = (idx - 1) * sample_ticks
        tick_at = idx * sample_ticks
        leading_tone = (tonic_pc - 1) % 12
        for track_idx, (role, sorted_notes) in enumerate(tracks_notes):
            if track_idx == len(tracks_notes) - 1:
                continue
            note_before = sounding_note_at(sorted_notes, tick_before)
            note_at = sounding_note_at(sorted_notes, tick_at)
            if (note_before is not None and note_at is not None
                    and note_before.pitch % 12 == leading_tone
                    and note_at.pitch % 12 == tonic_pc):
                return True
        return False

    def _has_bass_fifth_motion(idx: int) -> bool:
        if idx < 1 or not tracks_notes:
            return False
        tick_before = (idx - 1) * sample_ticks
        tick_at = idx * sample_ticks
        _, bass_notes = tracks_notes[-1]
        note_before = sounding_note_at(bass_notes, tick_before)
        note_at = sounding_note_at(bass_notes, tick_at)
        if note_before is not None and note_at is not None:
            interval = (note_at.pitch - note_before.pitch) % 12
            if interval == 5 or interval == 7:
                return True
        return False

    def _has_long_resolution(idx: int) -> bool:
        if not tracks_notes:
            return False
        tick_at = idx * sample_ticks
        for _, sorted_notes in tracks_notes:
            note = sounding_note_at(sorted_notes, tick_at)
            if note is not None:
                dur_beats = note.duration / tpb
                if dur_beats >= 2.0:
                    return True
        return False

    def _has_texture_decrease(idx: int) -> bool:
        if idx < 1 or not tracks_notes:
            return False
        tick_before = (idx - 1) * sample_ticks
        tick_at = idx * sample_ticks
        voices_before = sum(
            1 for _, sn in tracks_notes
            if sounding_note_at(sn, tick_before) is not None
        )
        voices_at = sum(
            1 for _, sn in tracks_notes
            if sounding_note_at(sn, tick_at) is not None
        )
        return voices_at < voices_before

    def _has_bass_semitone_descent(idx: int) -> bool:
        if idx < 1 or not tracks_notes:
            return False
        tick_before = (idx - 1) * sample_ticks
        tick_at = idx * sample_ticks
        _, bass_notes = tracks_notes[-1]
        note_before = sounding_note_at(bass_notes, tick_before)
        note_at = sounding_note_at(bass_notes, tick_at)
        if note_before is not None and note_at is not None:
            return note_before.pitch - note_at.pitch == 1
        return False

    for idx in range(1, len(chords)):
        prev = chords[idx - 1]
        curr = chords[idx]

        if prev.confidence < 0.3 or curr.confidence < 0.3:
            continue

        cadence_type: Optional[str] = None
        base_confidence = min(prev.confidence, curr.confidence)

        if prev.degree == 4 and curr.degree == 0:
            if curr.inversion == "root" and curr.confidence >= 0.5:
                cadence_type = "PAC"
            else:
                cadence_type = "IAC"
        elif curr.degree == 4:
            sustain_count = 0
            for look in range(idx, min(idx + 4, len(chords))):
                if chords[look].degree == 4 and chords[look].confidence > 0.2:
                    sustain_count += 1
                else:
                    break
            if sustain_count >= 4 or _has_texture_decrease(idx):
                cadence_type = "HC"
        elif prev.degree == 4 and curr.degree == 5:
            cadence_type = "DC"
        elif (is_minor and prev.degree == 3 and curr.degree == 4
              and prev.inversion == "1st"
              and _has_bass_semitone_descent(idx)):
            cadence_type = "PHR"
        elif prev.degree == 4 and curr.degree not in (0, 5):
            cadence_type = "EVAD"

        if cadence_type is None:
            continue

        confidence = base_confidence
        tick_at = idx * sample_ticks

        if beat_strength(tick_at) >= 0.75:
            confidence += 0.2
        if _has_bass_fifth_motion(idx):
            confidence += 0.3
        if _has_leading_tone_resolution(idx):
            confidence += 0.3
        if _has_long_resolution(idx):
            confidence += 0.1

        confidence = min(confidence, 1.0)

        bar = tick_at // TICKS_PER_BAR + 1
        beat = (tick_at % TICKS_PER_BAR) // tpb + 1

        if cadences:
            last_cad = cadences[-1]
            last_tick = (last_cad["bar"] - 1) * TICKS_PER_BAR + (last_cad["beat"] - 1) * tpb
            if tick_at - last_tick < 2 * tpb and last_cad["type"] == cadence_type:
                if confidence > last_cad["confidence"]:
                    cadence_counts[last_cad["type"]] -= 1
                    cadences[-1] = {
                        "bar": bar,
                        "beat": beat,
                        "type": cadence_type,
                        "confidence": round(confidence, 2),
                        "key_context": degree_to_roman(
                            curr.degree, curr.quality, is_minor,
                        ),
                    }
                    cadence_counts[cadence_type] += 1
                continue

        cadence_counts[cadence_type] += 1
        cadences.append({
            "bar": bar,
            "beat": beat,
            "type": cadence_type,
            "confidence": round(confidence, 2),
            "key_context": degree_to_roman(
                curr.degree, curr.quality, is_minor,
            ),
        })

    total_bars = score.total_bars if score.total_bars > 0 else 1
    cadences_per_8_bars = (
        round(len(cadences) / total_bars * 8, 1)
        if total_bars > 0 else 0.0
    )
    avg_confidence = (
        round(sum(c["confidence"] for c in cadences) / len(cadences), 2)
        if cadences else 0.0
    )

    final_cadence: Optional[dict[str, Any]] = None
    if cadences:
        final_cadence = {
            "type": cadences[-1]["type"],
            "bar": cadences[-1]["bar"],
        }

    result: dict[str, Any] = {
        "work_id": work_id,
        "cadence_types": dict(cadence_counts.most_common()),
        "cadences_per_8_bars": cadences_per_8_bars,
        "cadence_list": cadences,
        "final_cadence": final_cadence,
        "avg_confidence": avg_confidence,
    }
    return json.dumps(result, indent=2)


# ===== H. Non-chord tone analysis (1) ========================================


@server.tool()
def get_nct_profile(
    work_id: str,
    track: Optional[str] = None,
    sample_interval_beats: float = 1.0,
) -> str:
    """Analyze non-chord tone usage in a Bach work.

    For each note, determines if it is a chord tone (CT) or non-chord tone
    (NCT) based on estimated chords. NCTs are further classified as passing,
    neighbor, suspension, or other.

    Args:
        work_id: Work identifier (e.g. BWV578, BWV846_1)
        track: Optional track role to analyze (all tracks if omitted)
        sample_interval_beats: Chord estimation interval in beats (default 1.0)
    """
    tonic_pc, is_minor, key_conf = get_key_info(work_id)
    if tonic_pc is None:
        tonic_pc = 0
        is_minor = False

    chords, est_quality = _estimate_chords_for_work(work_id, sample_interval_beats)
    if not chords:
        return json.dumps({"error": f"No chords estimated for '{work_id}'."})

    score, raw_data, err = _load_score(work_id)
    if err or score is None or raw_data is None:
        return json.dumps({"error": err})

    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)
    sample_ticks = int(sample_interval_beats * tpb)

    all_notes: list[Note] = []
    for trk in score.tracks:
        if track and trk.name != track:
            continue
        all_notes.extend(trk.sorted_notes)
    all_notes.sort(key=lambda note_obj: (note_obj.start_tick, note_obj.pitch))

    if not all_notes:
        return json.dumps({"error": f"No notes found for '{work_id}'."})

    def _chord_at_tick(tick: int) -> Optional[ChordEstimate]:
        if sample_ticks <= 0:
            return None
        idx = tick // sample_ticks
        if 0 <= idx < len(chords):
            return chords[idx]
        return None

    def _chord_pcs(chord: ChordEstimate) -> frozenset[int]:
        template = CHORD_TEMPLATES.get(chord.quality, frozenset())
        return frozenset((chord.root_pc + pc) % 12 for pc in template)

    ct_count = 0
    nct_count = 0
    uncertain_count = 0
    nct_types: Counter[str] = Counter()
    suspension_patterns: Counter[str] = Counter()
    strong_beat_total = 0
    strong_beat_nct = 0
    weak_beat_total = 0
    weak_beat_nct = 0

    for note_idx, note_obj in enumerate(all_notes):
        chord = _chord_at_tick(note_obj.start_tick)
        if chord is None or chord.quality == "":
            uncertain_count += 1
            continue

        if chord.confidence < 0.4:
            uncertain_count += 1
            continue

        note_pc = note_obj.pitch % 12
        chord_pc_set = _chord_pcs(chord)
        is_ct = note_pc in chord_pc_set

        beat_str = beat_strength(note_obj.start_tick)
        is_strong_beat = beat_str >= 0.5

        if is_strong_beat:
            strong_beat_total += 1
        else:
            weak_beat_total += 1

        if is_ct:
            ct_count += 1
            continue

        nct_count += 1
        if is_strong_beat:
            strong_beat_nct += 1
        else:
            weak_beat_nct += 1

        prev_note = all_notes[note_idx - 1] if note_idx > 0 else None
        next_note = all_notes[note_idx + 1] if note_idx + 1 < len(all_notes) else None

        next_chord = _chord_at_tick(next_note.start_tick) if next_note else None

        if detect_suspension(note_obj, prev_note, next_note, chord, next_chord):
            nct_types["suspension"] += 1
            pattern = classify_suspension_pattern(note_obj, next_note, chord)
            if pattern:
                suspension_patterns[pattern] += 1
            continue

        if prev_note is not None and next_note is not None:
            iv_in = note_obj.pitch - prev_note.pitch
            iv_out = next_note.pitch - note_obj.pitch

            is_step_in = 1 <= abs(iv_in) <= 2
            is_step_out = 1 <= abs(iv_out) <= 2

            prev_chord = _chord_at_tick(prev_note.start_tick)
            prev_is_ct = (
                prev_chord is not None
                and prev_chord.quality != ""
                and prev_note.pitch % 12 in _chord_pcs(prev_chord)
            )
            next_is_ct = (
                next_chord is not None
                and next_chord.quality != ""
                and next_note.pitch % 12 in _chord_pcs(next_chord)
            )

            if is_step_in and is_step_out and prev_is_ct and next_is_ct:
                if (iv_in > 0 and iv_out > 0) or (iv_in < 0 and iv_out < 0):
                    nct_types["passing"] += 1
                    continue
                else:
                    nct_types["neighbor"] += 1
                    continue

        nct_types["other"] += 1

    classified_total = ct_count + nct_count
    total_all = classified_total + uncertain_count

    ct_ratio = round(ct_count / classified_total, 3) if classified_total > 0 else 0.0
    nct_ratio = round(nct_count / classified_total, 3) if classified_total > 0 else 0.0

    nct_type_dist: dict[str, float] = {}
    if nct_count > 0:
        for ntype in ("passing", "neighbor", "suspension", "other"):
            nct_type_dist[ntype] = round(nct_types.get(ntype, 0) / nct_count, 3)

    total_beats = score.total_duration / tpb if tpb > 0 else 1.0
    nct_per_beat = round(nct_count / total_beats, 2) if total_beats > 0 else 0.0

    strong_beat_nct_ratio = (
        round(strong_beat_nct / strong_beat_total, 3) if strong_beat_total > 0 else 0.0
    )
    weak_beat_nct_ratio = (
        round(weak_beat_nct / weak_beat_total, 3) if weak_beat_total > 0 else 0.0
    )

    uncertain_ratio = round(uncertain_count / total_all, 3) if total_all > 0 else 0.0

    result: dict[str, Any] = {
        "work_id": work_id,
        "ct_ratio": ct_ratio,
        "nct_ratio": nct_ratio,
        "nct_types": nct_type_dist,
        "nct_per_beat": nct_per_beat,
        "strong_beat_nct_ratio": strong_beat_nct_ratio,
        "weak_beat_nct_ratio": weak_beat_nct_ratio,
        "suspension_patterns": dict(suspension_patterns.most_common()),
        "uncertain_ratio": uncertain_ratio,
        "estimation_quality": {"mean_chord_confidence": est_quality.get("mean_confidence", 0.0)},
    }
    return json.dumps(result, indent=2)


# ===== I. Voice leading analysis (1) =========================================


@server.tool()
def get_voice_leading_profile(
    work_id: str,
    track_a: Optional[str] = None,
    track_b: Optional[str] = None,
) -> str:
    """Analyze voice leading quality between voice pairs.

    Detects parallel perfects (P5->P5, P8->P8 in same direction), hidden
    perfects (same direction motion arriving at P5/P8), voice crossings,
    and suspension chains.

    Args:
        work_id: Work identifier (e.g. BWV578, BWV846_1)
        track_a: First track (analyzes specific pair if both given)
        track_b: Second track (analyzes specific pair if both given)
    """
    score, raw_data, err = _load_score(work_id)
    if err or score is None or raw_data is None:
        return json.dumps({"error": err})

    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)

    if track_a and track_b:
        ta_obj, tb_obj, terr = _resolve_tracks(score, track_a, track_b)
        if terr:
            return json.dumps({"error": terr})
        pairs = [(ta_obj, tb_obj)]
    else:
        if len(score.tracks) < 2:
            return json.dumps({"error": "Need at least 2 tracks for voice leading analysis."})
        pairs = []
        for idx_a in range(len(score.tracks)):
            for idx_b in range(idx_a + 1, len(score.tracks)):
                pairs.append((score.tracks[idx_a], score.tracks[idx_b]))

    total_dur = score.total_duration
    total_beats = total_dur / tpb if tpb > 0 else 1.0

    sum_parallel_perfects = 0
    sum_hidden_perfects = 0
    sum_voice_crossings = 0
    pair_results: dict[str, dict[str, Any]] = {}

    all_suspension_chains: list[int] = []

    for ta_obj, tb_obj in pairs:
        pair_key = f"{ta_obj.name}-{tb_obj.name}"
        notes_a = ta_obj.sorted_notes
        notes_b = tb_obj.sorted_notes

        parallel_perfects = 0
        hidden_perfects = 0
        voice_crossings = 0
        pair_beat_count = 0

        current_chain_length = 0
        pair_chains: list[int] = []

        prev_note_a: Optional[Note] = None
        prev_note_b: Optional[Note] = None
        prev_ic: Optional[int] = None

        tick = 0
        while tick < total_dur:
            na = sounding_note_at(notes_a, tick)
            nb = sounding_note_at(notes_b, tick)

            if na is not None and nb is not None:
                pair_beat_count += 1
                curr_ic = interval_class(na.pitch - nb.pitch)

                if na.pitch < nb.pitch:
                    voice_crossings += 1

                if prev_note_a is not None and prev_note_b is not None and prev_ic is not None:
                    da = na.pitch - prev_note_a.pitch
                    db = nb.pitch - prev_note_b.pitch

                    same_direction = (
                        (da > 0 and db > 0) or (da < 0 and db < 0)
                    )

                    is_curr_perfect = curr_ic in (0, 7)
                    is_prev_perfect = prev_ic in (0, 7)

                    if same_direction and is_curr_perfect and is_prev_perfect:
                        if curr_ic == prev_ic:
                            parallel_perfects += 1

                    if (same_direction and is_curr_perfect
                            and not (is_prev_perfect and curr_ic == prev_ic)):
                        hidden_perfects += 1

                    is_suspension = False
                    if beat_strength(tick) >= 0.5:
                        if (da == 0 and db != 0) or (da != 0 and db == 0):
                            if is_dissonant(na.pitch - nb.pitch):
                                is_suspension = True

                    if is_suspension:
                        current_chain_length += 1
                    else:
                        if current_chain_length >= 2:
                            pair_chains.append(current_chain_length)
                        current_chain_length = 0

                prev_ic = curr_ic
            else:
                if current_chain_length >= 2:
                    pair_chains.append(current_chain_length)
                current_chain_length = 0
                prev_ic = None

            prev_note_a = na
            prev_note_b = nb
            tick += tpb

        if current_chain_length >= 2:
            pair_chains.append(current_chain_length)

        if pair_beat_count > 0:
            scale = 100.0 / pair_beat_count
        else:
            scale = 0.0

        pair_results[pair_key] = {
            "parallel_perfects": parallel_perfects,
            "parallel_perfects_per_100_beats": round(parallel_perfects * scale, 2),
            "hidden_perfects": hidden_perfects,
            "hidden_perfects_per_100_beats": round(hidden_perfects * scale, 2),
            "voice_crossings": voice_crossings,
            "voice_crossings_per_100_beats": round(voice_crossings * scale, 2),
            "suspension_chains": len(pair_chains),
            "suspension_chain_avg_length": (
                round(sum(pair_chains) / len(pair_chains), 1) if pair_chains else 0.0
            ),
        }

        sum_parallel_perfects += parallel_perfects
        sum_hidden_perfects += hidden_perfects
        sum_voice_crossings += voice_crossings
        all_suspension_chains.extend(pair_chains)

    if total_beats > 0:
        summary_scale = 100.0 / total_beats
    else:
        summary_scale = 0.0

    summary: dict[str, Any] = {
        "parallel_perfects_per_100_beats": round(sum_parallel_perfects * summary_scale, 2),
        "hidden_perfects_per_100_beats": round(sum_hidden_perfects * summary_scale, 2),
        "voice_crossings_per_100_beats": round(sum_voice_crossings * summary_scale, 2),
        "suspension_chain_count": len(all_suspension_chains),
        "suspension_chain_avg_length": (
            round(sum(all_suspension_chains) / len(all_suspension_chains), 1)
            if all_suspension_chains else 0.0
        ),
    }

    result: dict[str, Any] = {
        "work_id": work_id,
        "summary": summary,
        "voice_pairs": pair_results,
    }
    return json.dumps(result, indent=2)


# ===== J. Bass line analysis (1) =============================================


@server.tool()
def get_bass_profile(
    work_id: str,
    bass_track: Optional[str] = None,
) -> str:
    """Analyze bass line characteristics of a Bach work.

    Auto-detects the bass track (pedal > lower > bass > lowest pitch) or
    uses the specified track. Analyzes strong-beat intervals, P4/P5 motion,
    leap-then-stepback patterns, and ostinato detection.

    Args:
        work_id: Work identifier (e.g. BWV578, BWV846_1)
        bass_track: Explicit bass track name (auto-detect if omitted)
    """
    score, _, err = _load_score(work_id)
    if err or score is None:
        return json.dumps({"error": err})

    if bass_track:
        trk = next((t for t in score.tracks if t.name == bass_track), None)
        if trk is None:
            return json.dumps({"error": f"Track '{bass_track}' not found."})
    else:
        trk = detect_bass_track(score)
        if trk is None:
            return json.dumps({"error": "No bass track detected."})

    notes = trk.sorted_notes
    if not notes:
        return json.dumps({"error": f"Bass track '{trk.name}' has no notes."})

    total_notes = len(notes)
    pitches = [note.pitch for note in notes]
    avg_pitch = round(sum(pitches) / len(pitches), 1)
    pitch_range = [min(pitches), max(pitches)]

    strong_beat_notes: list[Note] = []
    for note_obj in notes:
        beat_str = beat_strength(note_obj.start_tick)
        dur_beats = note_obj.duration / TICKS_PER_BEAT
        if beat_str >= 0.5 or dur_beats >= 0.5:
            strong_beat_notes.append(note_obj)

    strong_intervals: list[int] = []
    for idx in range(len(strong_beat_notes) - 1):
        iv = abs(strong_beat_notes[idx + 1].pitch - strong_beat_notes[idx].pitch)
        strong_intervals.append(iv)

    p4p5_count = sum(1 for iv in strong_intervals if interval_class(iv) in (5, 7))
    strong_beat_p4p5_ratio = (
        round(p4p5_count / len(strong_intervals), 3) if strong_intervals else 0.0
    )

    all_intervals_signed: list[int] = []
    for idx in range(len(notes) - 1):
        all_intervals_signed.append(notes[idx + 1].pitch - notes[idx].pitch)

    leap_stepback_count = 0
    leap_count = 0
    for idx in range(len(all_intervals_signed) - 1):
        iv = all_intervals_signed[idx]
        if abs(iv) > 2:
            leap_count += 1
            next_iv = all_intervals_signed[idx + 1]
            if 1 <= abs(next_iv) <= 2:
                if (iv > 0 and next_iv < 0) or (iv < 0 and next_iv > 0):
                    leap_stepback_count += 1

    leap_then_stepback_ratio = (
        round(leap_stepback_count / leap_count, 3) if leap_count > 0 else 0.0
    )

    iv_counts: Counter[str] = Counter()
    for idx in range(len(notes) - 1):
        iv = abs(notes[idx + 1].pitch - notes[idx].pitch)
        ic = interval_class(iv)
        iv_counts[INTERVAL_NAMES[ic]] += 1

    total_ivs = sum(iv_counts.values())
    iv_profile: dict[str, float] = {}
    if total_ivs > 0:
        for name, count in sorted(iv_counts.items()):
            iv_profile[name] = round(count / total_ivs, 3)

    rhythm_counts: Counter[str] = Counter()
    for note_obj in notes:
        rhythm_counts[categorize_duration(note_obj.duration)] += 1

    rhythm_profile: dict[str, float] = {}
    if total_notes > 0:
        for cat, count in sorted(rhythm_counts.items(), key=lambda x: -x[1]):
            rhythm_profile[cat] = round(count / total_notes, 3)

    ostinato_detected = False
    if len(notes) >= 6:
        pattern_seq: list[tuple[int, int]] = []
        for idx in range(len(notes) - 1):
            iv = notes[idx + 1].pitch - notes[idx].pitch
            dur_q = max(1, round(notes[idx].duration / (TICKS_PER_BEAT // 4)))
            pattern_seq.append((iv, dur_q))

        for pat_len in range(2, min(9, len(pattern_seq) // 3 + 1)):
            pattern_counts: Counter[tuple] = Counter()
            for idx in range(len(pattern_seq) - pat_len + 1):
                sub = tuple(pattern_seq[idx:idx + pat_len])
                pattern_counts[sub] += 1
            for sub, count in pattern_counts.most_common(5):
                if count >= 3:
                    ostinato_detected = True
                    break
            if ostinato_detected:
                break

    result: dict[str, Any] = {
        "work_id": work_id,
        "bass_track": trk.name,
        "total_notes": total_notes,
        "strong_beat_p4p5_ratio": strong_beat_p4p5_ratio,
        "leap_then_stepback_ratio": leap_then_stepback_ratio,
        "ostinato_detected": ostinato_detected,
        "interval_profile": iv_profile,
        "rhythm_profile": rhythm_profile,
        "avg_pitch": avg_pitch,
        "range": pitch_range,
    }
    return json.dumps(result, indent=2)


# ===== K. Phrase structure analysis (1) ======================================


@server.tool()
def get_phrase_structure(
    work_id: str,
    track: Optional[str] = None,
) -> str:
    """Detect phrase boundaries in a Bach work using multiple signals.

    Combines cadence detection, rest/gap analysis, texture decrease, and
    barline alignment to identify phrase boundaries. Minimum phrase length
    is 2 bars.

    Args:
        work_id: Work identifier (e.g. BWV578, BWV846_1)
        track: Optional track to focus gap/rest analysis on (all tracks if omitted)
    """
    score, raw_data, err = _load_score(work_id)
    if err or score is None or raw_data is None:
        return json.dumps({"error": err})

    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)
    total_dur = score.total_duration
    total_bars = score.total_bars if score.total_bars > 0 else max(1, total_dur // TICKS_PER_BAR)

    chords, _ = _estimate_chords_for_work(work_id, 0.5)
    cadence_sample_ticks = tpb // 2

    cadence_ticks: set[int] = set()
    if chords and len(chords) > 1:
        tonic_pc, is_minor_val, _ = get_key_info(work_id)
        if tonic_pc is None:
            tonic_pc = 0
            is_minor_val = False

        for idx in range(1, len(chords)):
            prev_c = chords[idx - 1]
            curr_c = chords[idx]
            if prev_c.confidence < 0.3 or curr_c.confidence < 0.3:
                continue
            is_cadence = False
            base_conf = min(prev_c.confidence, curr_c.confidence)
            tick_at = idx * cadence_sample_ticks

            if prev_c.degree == 4 and curr_c.degree == 0:
                is_cadence = True
            elif prev_c.degree == 4 and curr_c.degree == 5:
                is_cadence = True
            elif curr_c.degree == 4:
                sustain = 0
                for look in range(idx, min(idx + 4, len(chords))):
                    if chords[look].degree == 4 and chords[look].confidence > 0.2:
                        sustain += 1
                    else:
                        break
                if sustain >= 4:
                    is_cadence = True

            if is_cadence and base_conf >= 0.5:
                cadence_ticks.add(tick_at)

    sorted_tracks = [(trk.name, trk.sorted_notes) for trk in score.tracks]
    track_notes_for_rest: list[tuple[str, list[Note]]] = []
    if track:
        for trk_name, trk_notes in sorted_tracks:
            if trk_name == track:
                track_notes_for_rest.append((trk_name, trk_notes))
    else:
        track_notes_for_rest = sorted_tracks

    boundary_scores: list[tuple[int, float, dict[str, float]]] = []

    tick = 0
    while tick < total_dur:
        signals: dict[str, float] = {}

        cadence_signal = 0.0
        for offset in range(-tpb, tpb + 1, cadence_sample_ticks):
            if (tick + offset) in cadence_ticks:
                cadence_signal = 1.0
                break
        signals["cadence"] = cadence_signal

        rest_signal = 0.0
        for _, trk_notes in track_notes_for_rest:
            for note_obj in trk_notes:
                note_end = note_obj.start_tick + note_obj.duration
                if abs(note_end - tick) <= tpb // 2:
                    if note_obj.duration >= tpb:
                        has_gap = True
                        for check_note in trk_notes:
                            if note_end <= check_note.start_tick < note_end + tpb // 2:
                                has_gap = False
                                break
                        if has_gap:
                            rest_signal = 1.0
                            break
            if rest_signal > 0:
                break
        signals["rest"] = rest_signal

        texture_signal = 0.0
        if tick >= 2 * tpb:
            voices_now = sum(
                1 for _, trk_n in sorted_tracks
                if sounding_note_at(trk_n, tick) is not None
            )
            voices_before = sum(
                1 for _, trk_n in sorted_tracks
                if sounding_note_at(trk_n, tick - 2 * tpb) is not None
            )
            if voices_before - voices_now >= 1:
                texture_signal = 1.0
        signals["texture"] = texture_signal

        barline_signal = 1.0 if tick % TICKS_PER_BAR == 0 else 0.0
        signals["barline"] = barline_signal

        boundary_score = (
            0.4 * cadence_signal
            + 0.3 * rest_signal
            + 0.2 * texture_signal
            + 0.1 * barline_signal
        )

        boundary_scores.append((tick, boundary_score, signals))
        tick += tpb

    min_phrase_ticks = 2 * TICKS_PER_BAR
    phrase_boundaries: list[int] = [0]
    boundary_signal_counts: Counter[str] = Counter()

    for tick_pos, score_val, signals in boundary_scores:
        if tick_pos == 0:
            continue
        if score_val > 0.5:
            if tick_pos - phrase_boundaries[-1] >= min_phrase_ticks:
                phrase_boundaries.append(tick_pos)
                for sig_name, sig_val in signals.items():
                    if sig_val > 0:
                        boundary_signal_counts[sig_name] += 1

    phrases: list[dict[str, Any]] = []
    phrase_lengths: list[int] = []

    for idx in range(len(phrase_boundaries)):
        start_tick = phrase_boundaries[idx]
        if idx + 1 < len(phrase_boundaries):
            end_tick = phrase_boundaries[idx + 1]
        else:
            end_tick = total_dur

        start_bar = start_tick // TICKS_PER_BAR + 1
        end_bar = max(start_bar, (end_tick - 1) // TICKS_PER_BAR + 1)
        length_bars = end_bar - start_bar + 1

        phrases.append({
            "start_bar": start_bar,
            "end_bar": end_bar,
            "length_bars": length_bars,
        })
        phrase_lengths.append(length_bars)

    avg_phrase_length = (
        round(sum(phrase_lengths) / len(phrase_lengths), 1) if phrase_lengths else 0.0
    )

    result: dict[str, Any] = {
        "work_id": work_id,
        "phrase_count": len(phrases),
        "avg_phrase_length_bars": avg_phrase_length,
        "phrase_lengths": phrase_lengths,
        "phrases": phrases,
        "boundary_signals": dict(boundary_signal_counts.most_common()),
    }
    return json.dumps(result, indent=2)


# ===== L. Harmonic n-gram extraction (1) =====================================


@server.tool()
def extract_harmonic_ngrams(
    category: str,
    n: int = 2,
    min_occurrences: int = 5,
    top_k: int = 30,
) -> str:
    """Extract harmonic n-grams (degree and function sequences) from a category.

    For each work in the category, estimates chords and extracts n-grams of
    scale degree sequences (e.g. V-I, ii-V-I) and harmonic function sequences
    (e.g. D-T, S-D-T).

    Args:
        category: Reference category (e.g. organ_fugue, wtc1, solo_cello_suite)
        n: N-gram size (number of chords). Default 2 (bigrams).
        min_occurrences: Minimum count to include in results. Default 5.
        top_k: Maximum number of n-grams to return. Default 30.
    """
    idx = _get_index()
    works = idx.filter(category=category)
    if not works:
        return json.dumps({"error": f"No works found for category '{category}'."})

    degree_counts: Counter[tuple[str, ...]] = Counter()
    degree_work_sets: dict[tuple[str, ...], set[str]] = {}
    func_counts: Counter[tuple[str, ...]] = Counter()
    func_work_sets: dict[tuple[str, ...], set[str]] = {}
    total_ngrams = 0

    for w in works:
        work_id = w["id"]
        tonic_pc, is_minor_val, key_conf = get_key_info(work_id)
        if tonic_pc is None:
            tonic_pc = 0
            is_minor_val = False

        chords, _ = _estimate_chords_for_work(work_id, 1.0)
        if not chords:
            continue

        filtered_chords: list[ChordEstimate] = []
        for chord in chords:
            if chord.confidence > 0.0 and chord.quality != "":
                filtered_chords.append(chord)

        if len(filtered_chords) < n:
            continue

        degree_seq: list[str] = []
        func_seq: list[str] = []
        for chord in filtered_chords:
            roman = degree_to_roman(chord.degree, chord.quality, is_minor_val)
            func = DEGREE_TO_FUNCTION.get(chord.degree, "?")
            degree_seq.append(roman)
            func_seq.append(func)

        dedup_degree: list[str] = [degree_seq[0]]
        dedup_func: list[str] = [func_seq[0]]
        for nidx in range(1, len(degree_seq)):
            if degree_seq[nidx] != degree_seq[nidx - 1]:
                dedup_degree.append(degree_seq[nidx])
                dedup_func.append(func_seq[nidx])

        for nidx in range(len(dedup_degree) - n + 1):
            deg_ngram = tuple(dedup_degree[nidx:nidx + n])
            func_ngram = tuple(dedup_func[nidx:nidx + n])

            degree_counts[deg_ngram] += 1
            degree_work_sets.setdefault(deg_ngram, set()).add(work_id)

            func_counts[func_ngram] += 1
            func_work_sets.setdefault(func_ngram, set()).add(work_id)
            total_ngrams += 1

    degree_ngrams_out: list[dict[str, Any]] = []
    for ngram, count in degree_counts.most_common():
        if count < min_occurrences:
            break
        degree_ngrams_out.append({
            "ngram": "-".join(ngram),
            "count": count,
            "works_containing": len(degree_work_sets.get(ngram, set())),
            "frequency_per_1000": round(count / max(total_ngrams, 1) * 1000, 1),
        })
        if len(degree_ngrams_out) >= top_k:
            break

    func_ngrams_out: list[dict[str, Any]] = []
    for ngram, count in func_counts.most_common():
        if count < min_occurrences:
            break
        func_ngrams_out.append({
            "ngram": "-".join(ngram),
            "count": count,
            "works_containing": len(func_work_sets.get(ngram, set())),
            "frequency_per_1000": round(count / max(total_ngrams, 1) * 1000, 1),
        })
        if len(func_ngrams_out) >= top_k:
            break

    result: dict[str, Any] = {
        "category": category,
        "n": n,
        "total_works_scanned": len(works),
        "total_ngrams": total_ngrams,
        "degree_ngrams": degree_ngrams_out,
        "function_ngrams": func_ngrams_out,
    }
    return json.dumps(result, indent=2)


# ===== M. Harmonic rhythm analysis (1) =======================================


@server.tool()
def get_harmonic_rhythm_profile(work_id: str) -> str:
    """Analyze harmonic rhythm (rate of chord changes) in a Bach work.

    Estimates chords at 0.5-beat resolution and detects chord change points.
    Reports overall and per-section (beginning/middle/ending) harmonic rhythm.

    Args:
        work_id: Work identifier (e.g. BWV578, BWV846_1)
    """
    chords, est_quality = _estimate_chords_for_work(work_id, 0.5)
    if not chords:
        return json.dumps({"error": f"No chords estimated for '{work_id}'."})

    score, _, err = _load_score(work_id)
    if err or score is None:
        return json.dumps({"error": err})

    total_bars = score.total_bars if score.total_bars > 0 else 1

    sample_beats = 0.5
    change_indices: list[int] = []
    prev_degree = -1
    prev_quality = ""

    for idx, chord in enumerate(chords):
        if chord.confidence <= 0.0 or chord.quality == "":
            continue
        if chord.degree != prev_degree or chord.quality != prev_quality:
            if prev_degree >= 0:
                change_indices.append(idx)
            prev_degree = chord.degree
            prev_quality = chord.quality

    total_changes = len(change_indices)
    total_samples = len(chords)
    total_beats = total_samples * sample_beats

    avg_beats_per_change = (
        round(total_beats / total_changes, 2) if total_changes > 0 else 0.0
    )
    changes_per_bar = (
        round(total_changes / (total_beats / 4.0), 2) if total_beats > 0 else 0.0
    )

    def _section_stats(
        start_idx: int, end_idx: int,
    ) -> dict[str, float]:
        section_changes = sum(
            1 for ci in change_indices if start_idx <= ci < end_idx
        )
        section_samples = end_idx - start_idx
        section_beats = section_samples * sample_beats
        section_bars = section_beats / 4.0

        return {
            "changes_per_bar": (
                round(section_changes / section_bars, 2) if section_bars > 0 else 0.0
            ),
            "avg_beats_per_change": (
                round(section_beats / section_changes, 2) if section_changes > 0 else 0.0
            ),
        }

    quarter = total_samples // 4
    half = total_samples // 2

    beginning_end = quarter
    middle_end = quarter + half
    ending_end = total_samples

    by_section: dict[str, dict[str, float]] = {
        "beginning": _section_stats(0, beginning_end),
        "middle": _section_stats(beginning_end, middle_end),
        "ending": _section_stats(middle_end, ending_end),
    }

    result: dict[str, Any] = {
        "work_id": work_id,
        "total_chord_changes": total_changes,
        "avg_beats_per_change": avg_beats_per_change,
        "changes_per_bar": changes_per_bar,
        "by_section": by_section,
        "estimation_quality": est_quality,
    }
    return json.dumps(result, indent=2)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    server.run()
