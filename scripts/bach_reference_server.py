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
import math
import sys
from collections import Counter
from pathlib import Path
from typing import Any, NamedTuple, Optional

# Add project root to sys.path for bach_analyzer imports.
_PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_PROJECT_ROOT))

from mcp.server.fastmcp import FastMCP

from scripts.bach_analyzer.model import (
    CONSONANCES,
    DISSONANCES,
    PERFECT_CONSONANCES,
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

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DATA_DIR = _PROJECT_ROOT / "data" / "reference"
MAX_NOTES_RETURN = 500

INTERVAL_NAMES = [
    "P1", "m2", "M2", "m3", "M3", "P4",
    "TT", "P5", "m6", "M6", "m7", "M7",
]
NOTE_NAMES_12 = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


# ---------------------------------------------------------------------------
# Scale degree utilities (for degree-mode n-gram extraction)
# ---------------------------------------------------------------------------

MAJOR_SCALE_SEMITONES = [0, 2, 4, 5, 7, 9, 11]
MINOR_SCALE_SEMITONES = [0, 2, 3, 5, 7, 8, 10]

# Map tonic name -> MIDI pitch class (0-11).
TONIC_TO_PC: dict[str, int] = {
    "C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3,
    "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8,
    "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11,
}


def _build_pc_to_degree_map(scale: list[int]) -> list[tuple[int, int]]:
    """For pitch classes 0-11 (relative to tonic), return (degree, accidental).

    Prefers lower degree when distance ties; preserves chromatic alterations.
    """
    result: list[tuple[int, int]] = []
    for pc in range(12):
        best_d = 0
        best_acc = 99
        for d, s in enumerate(scale):
            acc = pc - s
            if abs(acc) < abs(best_acc) or (
                abs(acc) == abs(best_acc) and acc > 0
            ):
                best_d = d
                best_acc = acc
        result.append((best_d, best_acc))
    return result


# Precomputed maps: pc_in_scale -> (degree, accidental).
_PC_DEGREE_MAJOR = _build_pc_to_degree_map(MAJOR_SCALE_SEMITONES)
_PC_DEGREE_MINOR = _build_pc_to_degree_map(MINOR_SCALE_SEMITONES)


class ScaleDegree(NamedTuple):
    """Diatonic scale degree with chromatic accidental.

    Non-scale tones are NOT snapped — they are stored with accidental != 0.
    octave is scale-relative (pitch-tonic divided by 12).
    """
    degree: int       # 0-6 within one octave of the scale
    accidental: int   # -1=flat, 0=natural, +1=sharp (vs diatonic scale)
    octave: int       # scale-relative octave ((pitch - tonic) // 12)


def pitch_to_scale_degree(
    pitch: int, tonic: int, is_minor: bool,
) -> ScaleDegree:
    """Convert MIDI pitch to ScaleDegree.

    Args:
        pitch: MIDI pitch (0-127).
        tonic: Tonic pitch class (0-11, C=0, G=7, etc.).
        is_minor: True for natural minor scale.
    """
    pc_map = _PC_DEGREE_MINOR if is_minor else _PC_DEGREE_MAJOR
    rel = pitch - tonic
    # Python's divmod handles negatives correctly for our needs:
    # (-1) // 12 == -1, (-1) % 12 == 11.
    octave = rel // 12
    pc = rel % 12  # always 0-11
    degree, accidental = pc_map[pc]
    return ScaleDegree(degree=degree, accidental=accidental, octave=octave)


def degree_interval(a: ScaleDegree, b: ScaleDegree) -> tuple[int, int]:
    """Signed diatonic interval between two ScaleDegrees.

    Returns:
        (degree_diff, chroma_diff) where degree_diff is signed absolute
        diatonic distance (C4->D5 = +8) and chroma_diff is accidental
        difference (augmented = +1, diminished = -1).
    """
    return (
        (b.octave * 7 + b.degree) - (a.octave * 7 + a.degree),
        b.accidental - a.accidental,
    )


# ---------------------------------------------------------------------------
# Key signature metadata
# ---------------------------------------------------------------------------

_KEY_SIGS: dict[str, dict] = {}


def _load_key_signatures() -> dict[str, dict]:
    """Load key_signatures.json lazily."""
    global _KEY_SIGS
    if _KEY_SIGS:
        return _KEY_SIGS
    ks_path = DATA_DIR / "key_signatures.json"
    if ks_path.is_file():
        with open(ks_path) as f:
            _KEY_SIGS = json.load(f)
    return _KEY_SIGS


def _get_key_info(work_id: str) -> tuple[Optional[int], Optional[bool], float]:
    """Get (tonic_pc, is_minor, confidence) for a work.

    Returns (None, None, 0.0) if key is unavailable.
    """
    ks = _load_key_signatures()
    info = ks.get(work_id)
    if not info:
        return None, None, 0.0
    tonic_pc = TONIC_TO_PC.get(info.get("tonic", ""))
    if tonic_pc is None:
        return None, None, 0.0
    is_minor = info.get("mode") == "minor"
    conf = 1.0 if info.get("confidence") == "verified" else 0.8
    return tonic_pc, is_minor, conf


# ---------------------------------------------------------------------------
# Beat / rhythm classification helpers
# ---------------------------------------------------------------------------


def _classify_beat_position(tick: int) -> str:
    """Classify tick as 'strong' (beat 1), 'mid' (beat 3), or 'weak'."""
    beat = (tick % TICKS_PER_BAR) // TICKS_PER_BEAT
    if beat == 0:
        return "strong"
    if beat == 2:
        return "mid"
    return "weak"


def _beat_strength(tick: int) -> float:
    """Metric strength at tick (4/4 assumed, 16th resolution)."""
    pos = tick % TICKS_PER_BAR
    if pos == 0:
        return 1.0
    if pos == 2 * TICKS_PER_BEAT:
        return 0.75
    if pos % TICKS_PER_BEAT == 0:
        return 0.5
    if pos % (TICKS_PER_BEAT // 2) == 0:
        return 0.25
    return 0.125


def _quantize_duration(dur_beats: float, grid: str) -> int:
    """Quantize duration to grid units.

    Args:
        dur_beats: Duration in beats.
        grid: 'sixteenth' (default), 'eighth', or 'quarter'.
    Returns:
        Duration in grid units (minimum 1).
    """
    divisor = {"sixteenth": 0.25, "eighth": 0.5, "quarter": 1.0}.get(grid, 0.25)
    return max(1, round(dur_beats / divisor))


def _accent_char(tick: int) -> str:
    """Return accent character for a tick position (s/m/w)."""
    pos = _classify_beat_position(tick)
    return {"strong": "s", "mid": "m", "weak": "w"}[pos]


def _is_chord_tone_simple(pitch: int, bass_pitch: int) -> bool:
    """Simplified chord-tone check: root, m3, M3, P5, m6, M6 from bass."""
    iv = abs(pitch - bass_pitch) % 12
    return iv in (0, 3, 4, 7, 8, 9)


# ---------------------------------------------------------------------------
# Adapter: Reference JSON (beat-based) -> bach_analyzer Score (tick-based)
# ---------------------------------------------------------------------------


def reference_to_score(data: dict) -> Score:
    """Convert reference JSON to bach_analyzer Score."""
    tpb = data.get("ticks_per_beat", TICKS_PER_BEAT)
    tracks = []
    for t in data.get("tracks", []):
        notes = []
        for n in t.get("notes", []):
            start_tick = int(n["onset"] * tpb)
            duration = max(1, int(n["duration"] * tpb))
            notes.append(Note(
                pitch=n["pitch"],
                velocity=n.get("velocity", 80),
                start_tick=start_tick,
                duration=duration,
                voice=t.get("role", "unknown"),
            ))
        tracks.append(Track(name=t.get("role", "unknown"), notes=notes))
    return Score(
        tracks=tracks,
        form=data.get("form") or data.get("category", ""),
        key=None,
        voices=data.get("voice_count"),
    )


# ---------------------------------------------------------------------------
# WorkIndex: lightweight metadata index built at startup
# ---------------------------------------------------------------------------


class WorkIndex:
    """In-memory index of reference work metadata (no note data).

    Tracks the data directory's modification time and rebuilds
    automatically when files are added or removed.
    """

    def __init__(self, data_dir: Path):
        self._data_dir = data_dir
        self._index: dict[str, dict[str, Any]] = {}
        self._dir_mtime: float = 0.0
        self._build()

    def _current_dir_mtime(self) -> float:
        """Get the data directory's current modification time."""
        try:
            return self._data_dir.stat().st_mtime
        except OSError:
            return 0.0

    def needs_refresh(self) -> bool:
        """Check if the data directory has been modified since last build."""
        return self._current_dir_mtime() != self._dir_mtime

    def refresh(self) -> None:
        """Rebuild the index if the data directory has changed."""
        if self.needs_refresh():
            self._index.clear()
            self._build()

    def _build(self) -> None:
        if not self._data_dir.is_dir():
            return
        self._dir_mtime = self._current_dir_mtime()
        for p in sorted(self._data_dir.glob("*.json")):
            try:
                with open(p) as f:
                    data = json.load(f)
                key = p.stem
                self._index[key] = {
                    "file": str(p),
                    "bwv": data.get("bwv"),
                    "category": data.get("category", ""),
                    "instrument": data.get("instrument", ""),
                    "form": data.get("form", ""),
                    "movement": data.get("movement"),
                    "voice_count": data.get("voice_count", 0),
                    "total_notes": data.get("total_notes", 0),
                    "duration_seconds": data.get("duration_seconds", 0),
                    "track_type": data.get("track_type", ""),
                    "roles": [t.get("role", "") for t in data.get("tracks", [])],
                    "time_signatures": data.get("time_signatures", []),
                }
            except (json.JSONDecodeError, KeyError):
                continue

    @property
    def count(self) -> int:
        return len(self._index)

    def list_keys(self) -> list[str]:
        return list(self._index.keys())

    def get_meta(self, key: str) -> Optional[dict]:
        return self._index.get(key)

    def filter(
        self,
        category: Optional[str] = None,
        instrument: Optional[str] = None,
        form: Optional[str] = None,
        min_voices: Optional[int] = None,
        max_voices: Optional[int] = None,
    ) -> list[dict]:
        results = []
        for k, meta in self._index.items():
            if category and meta["category"] != category:
                continue
            if instrument and meta["instrument"] != instrument:
                continue
            if form and meta["form"] != form:
                continue
            if min_voices is not None and meta["voice_count"] < min_voices:
                continue
            if max_voices is not None and meta["voice_count"] > max_voices:
                continue
            results.append({"id": k, **meta})
        return results

    def load_full(self, key: str) -> Optional[dict]:
        """Load full JSON data for a work."""
        meta = self._index.get(key)
        if not meta:
            return None
        with open(meta["file"]) as f:
            return json.load(f)

    def categories(self) -> dict[str, int]:
        c: Counter = Counter()
        for meta in self._index.values():
            c[meta["category"]] += 1
        return dict(sorted(c.items(), key=lambda x: -x[1]))

    def instruments(self) -> dict[str, int]:
        c: Counter = Counter()
        for meta in self._index.values():
            c[meta["instrument"]] += 1
        return dict(sorted(c.items(), key=lambda x: -x[1]))


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
        for n in tr.sorted_notes:
            if bar_start and n.bar < bar_start:
                continue
            if bar_end and n.bar > bar_end:
                continue
            notes.append({
                "pitch": n.pitch,
                "name": pitch_to_name(n.pitch),
                "onset_beat": n.start_tick / TICKS_PER_BEAT,
                "duration_beat": n.duration / TICKS_PER_BEAT,
                "bar": n.bar,
                "beat": n.beat,
                "velocity": n.velocity,
                "voice": n.voice,
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
        for i in range(len(sorted_n) - 1):
            iv = abs(sorted_n[i + 1].pitch - sorted_n[i].pitch)
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

    def categorize(dur_ticks: int) -> str:
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

    per_track: dict[str, dict] = {}
    all_dur: Counter = Counter()

    for tr in score.tracks:
        if track and tr.name != track:
            continue
        t_dur: Counter = Counter()
        durations: list[float] = []
        for n in tr.notes:
            t_dur[categorize(n.duration)] += 1
            durations.append(n.duration / TICKS_PER_BEAT)
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
        pitches = [n.pitch for n in tr.notes]
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

    for i in range(1, len(entries)):
        iv = entries[i]["pitch"] - entries[i - 1]["pitch"]
        entries[i]["interval_from_prev"] = iv
        entries[i]["interval_class"] = interval_class(iv)

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
            sorted_n[i + 1].pitch - sorted_n[i].pitch
            for i in range(len(sorted_n) - 1)
        ]
        for i in range(len(intervals) - len(pattern) + 1):
            if all(
                abs(intervals[i + j] - p) <= tolerance
                for j, p in enumerate(pattern)
            ):
                n0 = sorted_n[i]
                matches.append({
                    "track": tr.name,
                    "bar": n0.bar,
                    "beat": n0.beat,
                    "onset_beat": n0.start_tick / TICKS_PER_BEAT,
                    "starting_pitch": n0.pitch,
                    "starting_name": pitch_to_name(n0.pitch),
                    "matched_intervals": intervals[i:i + len(pattern)],
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


def _js_divergence(p: dict[str, float], q: dict[str, float]) -> float:
    """Jensen-Shannon divergence between two distributions."""
    all_keys = set(p.keys()) | set(q.keys())
    if not all_keys:
        return 0.0
    p_total = sum(p.values()) or 1
    q_total = sum(q.values()) or 1
    jsd = 0.0
    for k in all_keys:
        pk = p.get(k, 0) / p_total
        qk = q.get(k, 0) / q_total
        mk = (pk + qk) / 2
        if pk > 0 and mk > 0:
            jsd += pk * math.log2(pk / mk) / 2
        if qk > 0 and mk > 0:
            jsd += qk * math.log2(qk / mk) / 2
    return round(jsd, 6)


def _get_distribution(score: Score, profile_type: str) -> dict[str, float]:
    """Extract a distribution from a score for comparison."""
    if profile_type == "interval":
        counts: Counter = Counter()
        for tr in score.tracks:
            sn = tr.sorted_notes
            for i in range(len(sn) - 1):
                ic = interval_class(sn[i + 1].pitch - sn[i].pitch)
                counts[INTERVAL_NAMES[ic]] += 1
        return dict(counts)

    if profile_type == "rhythm":
        counts = Counter()
        for tr in score.tracks:
            for n in tr.notes:
                beats = n.duration / TICKS_PER_BEAT
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
            for n in tr.notes:
                counts[NOTE_NAMES_12[n.pitch % 12]] += 1
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


def _get_chord_distribution(
    work_id: str, profile_type: str,
) -> dict[str, float]:
    """Build a distribution from chord estimation for comparison.

    Args:
        work_id: Work identifier.
        profile_type: "harmony_degrees" or "function".
    """
    chords, stats = _estimate_chords_for_work(work_id, 1.0)
    if not chords:
        return {}
    tonic, is_minor, conf = _get_key_info(work_id)
    counts: Counter = Counter()
    for c in chords:
        if c.confidence <= 0.0:
            continue
        if profile_type == "function":
            counts[_DEGREE_TO_FUNCTION.get(c.degree, "T")] += 1
        else:  # harmony_degrees
            counts[_degree_to_roman(c.degree, c.quality, is_minor or False)] += 1
    return dict(counts)


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
    # Chord-based profile types use work_id directly.
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
    jsd = _js_divergence(pa, pb)

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

        # Interval stats
        steps = 0
        leaps = 0
        iv_total = 0
        iv_sum = 0
        for tr in score.tracks:
            sn = tr.sorted_notes
            for i in range(len(sn) - 1):
                iv = abs(sn[i + 1].pitch - sn[i].pitch)
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

        # Consonance (multi-voice only)
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
        m = sum(lst) / len(lst)
        return round(
            (sum((x - m) ** 2 for x in lst) / (len(lst) - 1)) ** 0.5, 4
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


def _load_works_for_category(category: str) -> list[tuple[str, dict]]:
    """Return list of (work_id, full_json) for all works in category."""
    idx = _get_index()
    works = idx.filter(category=category)
    result: list[tuple[str, dict]] = []
    for w in works:
        data = idx.load_full(w["id"])
        if data:
            result.append((w["id"], data))
    return result


def _iter_track_notes(
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


def _compute_intervals(
    notes: list[Note],
    interval_mode: str,
    tonic: Optional[int],
    is_minor: Optional[bool],
) -> list[Any]:
    """Compute consecutive intervals as tuples.

    Returns list of interval representations (length = len(notes) - 1).
    For 'semitone': int (signed semitone diff).
    For 'degree': (degree_diff, chroma_diff).
    For 'diatonic': int (degree_diff only, no accidental).
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
            else:  # diatonic
                intervals.append(dd)
    return intervals


def _strongest_beat_hit(notes: list[Note]) -> int:
    """Return index of the note on the strongest beat within the group."""
    best_idx = 0
    best_s = -1.0
    for i, n in enumerate(notes):
        s = _beat_strength(n.start_tick)
        if s > best_s:
            best_s = s
            best_idx = i
    return best_idx


def _label_melodic_ngram(intervals: tuple, mode: str) -> str:
    """Generate a human-readable label for a melodic n-gram."""
    if mode == "semitone":
        dirs = [("+" if v > 0 else "" if v < 0 else "=") + str(v) for v in intervals]
        return "st:" + ",".join(dirs)
    if mode == "diatonic":
        dirs = [("+" if v > 0 else "" if v < 0 else "=") + str(v) for v in intervals]
        return "dia:" + ",".join(dirs)
    # degree mode: list of (dd, cd)
    parts = []
    for dd, cd in intervals:
        s = ("+" if dd > 0 else "" if dd < 0 else "=") + str(dd)
        if cd != 0:
            s += ("+" if cd > 0 else "") + str(cd) + "c"
        parts.append(s)
    return "deg:" + ",".join(parts)


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

    works = _load_works_for_category(category)
    if not works:
        return json.dumps({"error": f"No works found for category '{category}'."})

    # Accumulators per n-gram key.
    counts: Counter = Counter()
    work_sets: dict[tuple, set[str]] = {}
    beat_pos: dict[tuple, Counter] = {}
    strongest_hits: dict[tuple, Counter] = {}
    examples: dict[tuple, dict] = {}
    total_ngrams = 0

    for work_id, data in works:
        tonic, is_minor, conf = _get_key_info(work_id)
        # Skip degree modes for low-confidence keys.
        use_tonic = tonic if conf >= 0.7 else None
        if interval_mode in ("degree", "diatonic") and use_tonic is None:
            # Fallback to semitone silently.
            eff_mode = "semitone"
        else:
            eff_mode = interval_mode

        for role, notes in _iter_track_notes(data, track):
            ivs = _compute_intervals(notes, eff_mode, use_tonic, is_minor)
            if len(ivs) < n:
                continue
            for i in range(len(ivs) - n + 1):
                window_ivs = tuple(ivs[i:i + n])
                window_notes = notes[i:i + n + 1]
                key = window_ivs

                counts[key] += 1
                total_ngrams += 1
                work_sets.setdefault(key, set()).add(work_id)

                bp = _classify_beat_position(window_notes[0].start_tick)
                beat_pos.setdefault(key, Counter())[bp] += 1

                sbh = _strongest_beat_hit(window_notes)
                strongest_hits.setdefault(key, Counter())[sbh] += 1

                if key not in examples:
                    examples[key] = {
                        "work_id": work_id,
                        "track": role,
                        "bar": window_notes[0].bar,
                        "beat_index": window_notes[0].beat - 1,
                        "starting_pitch": window_notes[0].pitch,
                    }

    # Build results.
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
            "label": _label_melodic_ngram(key, interval_mode),
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

    return json.dumps({
        "category": category,
        "n": n,
        "interval_mode": interval_mode,
        "total_works_scanned": len(works),
        "total_ngrams_extracted": total_ngrams,
        "ngrams": ngrams_out,
    }, indent=2)


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
    works = _load_works_for_category(category)
    if not works:
        return json.dumps({"error": f"No works found for category '{category}'."})

    grid_beat = {"sixteenth": 0.25, "eighth": 0.5, "quarter": 1.0}.get(
        quantize, 0.25,
    )

    counts: Counter = Counter()
    work_sets: dict[tuple, set[str]] = {}
    beat_pos: dict[tuple, Counter] = {}
    strongest_hits: dict[tuple, Counter] = {}
    total_ngrams = 0

    for work_id, data in works:
        tpb = data.get("ticks_per_beat", TICKS_PER_BEAT)
        for role, notes in _iter_track_notes(data, track):
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

                bp = _classify_beat_position(window[0].start_tick)
                beat_pos.setdefault(key, Counter())[bp] += 1

                sbh = _strongest_beat_hit(window)
                strongest_hits.setdefault(key, Counter())[sbh] += 1

    ngrams_out = []
    for key, count in counts.most_common():
        if count < min_occurrences:
            break
        bp = beat_pos.get(key, Counter())
        bp_total = sum(bp.values()) or 1
        sh = strongest_hits.get(key, Counter())
        sh_total = sum(sh.values()) or 1

        dur_beats = [v * grid_beat for v in key]
        # Compute onset_in_beat for this pattern (cumulative from first note).
        onset_positions: list[float] = [0.0]
        for d in dur_beats[:-1]:
            onset_positions.append(round(onset_positions[-1] + d, 4))

        # Label.
        if all(d == key[0] for d in key):
            label = f"{n}x{_dur_name(key[0], grid_beat)} (running)"
        else:
            label = "-".join(_dur_name(d, grid_beat) for d in key)

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

    return json.dumps({
        "category": category,
        "n": n,
        "quantize": quantize,
        "total_works_scanned": len(works),
        "total_ngrams_extracted": total_ngrams,
        "ngrams": ngrams_out,
    }, indent=2)


def _dur_name(grid_units: int, grid_beat: float) -> str:
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

    works = _load_works_for_category(category)
    if not works:
        return json.dumps({"error": f"No works found for category '{category}'."})

    counts: Counter = Counter()
    work_sets: dict[tuple, set[str]] = {}
    beat_pos: dict[tuple, Counter] = {}
    strongest_hits: dict[tuple, Counter] = {}
    chord_tone_sums: dict[tuple, list[float]] = {}
    examples: dict[tuple, dict] = {}
    total_figures = 0

    for work_id, data in works:
        tonic, is_minor, conf = _get_key_info(work_id)
        use_tonic = tonic if conf >= 0.7 else None
        if interval_mode in ("degree", "diatonic") and use_tonic is None:
            eff_mode = "semitone"
        else:
            eff_mode = interval_mode
        tpb = data.get("ticks_per_beat", TICKS_PER_BEAT)

        for role, notes in _iter_track_notes(data, track):
            if len(notes) < n:
                continue
            ivs = _compute_intervals(notes, eff_mode, use_tonic, is_minor)
            for i in range(len(notes) - n + 1):
                window = notes[i:i + n]
                window_ivs = tuple(ivs[i:i + n - 1]) if (i + n - 1) <= len(ivs) else None
                if window_ivs is None or len(window_ivs) != n - 1:
                    continue

                # Duration ratios: relative to first note's duration.
                first_dur = window[0].duration / tpb
                if first_dur <= 0:
                    continue
                dur_ratios = tuple(
                    round(nt.duration / tpb / first_dur * 4) / 4  # quantize to 0.25
                    for nt in window
                )
                # Onset ratios: relative to first note, in first_dur units.
                t0 = window[0].start_tick
                onset_ratios = tuple(
                    round((nt.start_tick - t0) / tpb / first_dur * 4) / 4
                    for nt in window
                )

                key = (window_ivs, dur_ratios, onset_ratios)
                counts[key] += 1
                total_figures += 1
                work_sets.setdefault(key, set()).add(work_id)

                bp = _classify_beat_position(window[0].start_tick)
                beat_pos.setdefault(key, Counter())[bp] += 1

                sbh = _strongest_beat_hit(window)
                strongest_hits.setdefault(key, Counter())[sbh] += 1

                # Chord tone ratio: simplified check vs bass.
                bass_pitch = min(nt.pitch for nt in window)
                ct_count = sum(
                    1 for nt in window if _is_chord_tone_simple(nt.pitch, bass_pitch)
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
        bp = beat_pos.get(key, Counter())
        bp_total = sum(bp.values()) or 1
        sh = strongest_hits.get(key, Counter())
        sh_total = sum(sh.values()) or 1
        ct_vals = chord_tone_sums.get(key, [])

        # Determine contour.
        signed_dirs = []
        for iv in window_ivs:
            if isinstance(iv, tuple):
                signed_dirs.append(iv[0])  # degree_diff
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
            "label": _label_melodic_ngram(window_ivs, interval_mode) + " " + contour,
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

    return json.dumps({
        "category": category,
        "n": n,
        "interval_mode": interval_mode,
        "total_works_scanned": len(works),
        "total_figures_extracted": total_figures,
        "figures": figures_out,
    }, indent=2)


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
    works = _load_works_for_category(category)
    if not works:
        return json.dumps({"error": f"No works found for category '{category}'."})

    pattern_ticks = beats_per_pattern * TICKS_PER_BEAT
    counts: Counter = Counter()
    work_sets: dict[tuple, set[str]] = {}
    non_ct_sums: dict[tuple, list[float]] = {}
    bass_degrees: dict[tuple, Counter] = {}
    examples: dict[tuple, dict] = {}
    total_patterns = 0

    for work_id, data in works:
        tonic, is_minor, conf = _get_key_info(work_id)
        tpb = data.get("ticks_per_beat", TICKS_PER_BEAT)

        # Merge all tracks for figuration analysis.
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

        # Process beat windows.
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

            # Sort by pitch to assign slots.
            unique_pitches = sorted(set(nt.pitch for nt in window_notes))
            pitch_to_slot = {p: i for i, p in enumerate(unique_pitches)}
            num_voices = len(unique_pitches)
            bass_pitch = unique_pitches[0]

            # Filter to chord tones if requested.
            if chord_tones_only:
                ct_notes = [
                    nt for nt in window_notes
                    if _is_chord_tone_simple(nt.pitch, bass_pitch)
                ]
                non_ct_count = len(window_notes) - len(ct_notes)
                non_ct_ratio = non_ct_count / len(window_notes)
                if len(ct_notes) < min_pattern_notes:
                    tick += pattern_ticks
                    continue
                # Re-assign slots for chord-tone-only set.
                ct_pitches = sorted(set(nt.pitch for nt in ct_notes))
                pitch_to_slot = {p: i for i, p in enumerate(ct_pitches)}
                num_voices = len(ct_pitches)
                bass_pitch = ct_pitches[0]
                use_notes = ct_notes
            else:
                use_notes = window_notes
                non_ct_ratio = 0.0

            # Build slot sequence (temporal order).
            use_notes.sort(key=lambda x: x.start_tick)
            slot_seq = tuple(pitch_to_slot[nt.pitch] for nt in use_notes)
            key = (num_voices, slot_seq)

            counts[key] += 1
            total_patterns += 1
            work_sets.setdefault(key, set()).add(work_id)
            non_ct_sums.setdefault(key, []).append(non_ct_ratio)

            # Track bass degree.
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
            "label": _figuration_label(num_voices, slot_seq),
            "example": examples.get(key),
        }
        patterns_out.append(entry)
        if len(patterns_out) >= top_k:
            break

    return json.dumps({
        "category": category,
        "beats_per_pattern": beats_per_pattern,
        "chord_tones_only": chord_tones_only,
        "total_works_scanned": len(works),
        "total_patterns_extracted": total_patterns,
        "figuration_patterns": patterns_out,
    }, indent=2)


def _figuration_label(num_voices: int, slots: tuple[int, ...]) -> str:
    """Generate label for a figuration slot pattern."""
    if len(slots) < 2:
        return f"{num_voices}v-single"
    # Check direction.
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


# ===== G. Harmonic analysis (2) =============================================

# Chord templates: pitch class sets relative to root.
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
# Pairs are (from_degree, to_degree) -> weight multiplier.
_TRANSITION_PRIOR: dict[tuple[int, int], float] = {
    (4, 0): 1.5, (3, 4): 1.3, (1, 4): 1.3, (0, 3): 1.2,
    (0, 4): 1.2, (4, 5): 1.2, (5, 3): 1.1, (6, 0): 1.2,
}

# Scale degree -> harmonic function.
_DEGREE_TO_FUNCTION: dict[int, str] = {
    0: "T", 1: "S", 2: "M", 3: "S", 4: "D", 5: "T", 6: "D",
}

# Roman numeral labels per mode.
_MAJOR_NUMERALS = ["I", "ii", "iii", "IV", "V", "vi", "vii\u00b0"]
_MINOR_NUMERALS = ["i", "ii\u00b0", "III", "iv", "V", "VI", "vii\u00b0"]


class ChordEstimate(NamedTuple):
    """Estimated chord at a sample point."""
    root_pc: int        # 0-11 pitch class of root
    quality: str        # key in CHORD_TEMPLATES, or "" for unclassified
    degree: int         # 0-6 scale degree
    inversion: str      # "root", "1st", "2nd", "3rd"
    confidence: float   # 0.0-1.0
    bass_pc: Optional[int]  # bass pitch class, or None


# Module-level chord cache for memoization.
_CHORD_CACHE: dict[str, tuple[list[ChordEstimate], dict]] = {}


def _extract_chord_support_pcs(
    tracks_notes: list[tuple[str, list[Note]]],
    tick: int,
    tpb: int,
) -> tuple[dict[int, float], Optional[int]]:
    """Extract weighted pitch-class support at a given tick.

    For each track, finds the sounding note at tick and computes a weight
    based on duration, metric position, and voice role.

    Returns:
        (pc_weights, bass_pc) where pc_weights maps pitch class to
        accumulated weight, and bass_pc is the pitch class of the lowest
        voice's sounding note (or None).
    """
    pc_weights: dict[int, float] = {}
    bass_pc: Optional[int] = None
    num_tracks = len(tracks_notes)
    tick_strength = _beat_strength(tick)

    for track_idx, (role, sorted_notes) in enumerate(tracks_notes):
        note = sounding_note_at(sorted_notes, tick)
        if note is None:
            continue

        dur_beats = note.duration / tpb
        is_bass = (track_idx == num_tracks - 1)

        # Short note handling (< 0.25 beats).
        if dur_beats < 0.25:
            note_start_strength = _beat_strength(note.start_tick)
            weight = 0.2 if note_start_strength >= 0.5 else 0.1
        else:
            # Duration weight: capped at 2 beats.
            duration_weight = min(dur_beats, 2.0) / 2.0

            # Metric weight at note onset.
            metric_weight = _beat_strength(note.start_tick)

            # Tie handling: note started before tick on a strong beat.
            if note.start_tick < tick and tick_strength >= 0.5:
                metric_weight = max(metric_weight, 0.8)

            # Tick-level bonus for strong metric positions.
            if tick_strength >= 0.75:
                metric_weight += 0.1

            # Voice weight: bass gets 1.2x.
            voice_weight = 1.2 if is_bass else 1.0

            weight = duration_weight * metric_weight * voice_weight

        pitch_class = note.pitch % 12
        pc_weights[pitch_class] = pc_weights.get(pitch_class, 0.0) + weight

        # Track bass PC (last track = bass).
        if is_bass:
            bass_pc = pitch_class

    return pc_weights, bass_pc


def _estimate_chord(
    weighted_pcs: dict[int, float],
    bass_pc: Optional[int],
    tonic: int,
    is_minor: bool,
    prev_chord: Optional[ChordEstimate] = None,
) -> ChordEstimate:
    """Estimate the most likely chord from weighted pitch classes.

    Uses template matching with inversion scoring and Markov priors.

    Args:
        weighted_pcs: Pitch class -> weight mapping.
        bass_pc: Bass pitch class (or None).
        tonic: Tonic pitch class (0-11).
        is_minor: True for minor key.
        prev_chord: Previous chord estimate for Markov prior.

    Returns:
        Best ChordEstimate.
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

    # Single PC: low confidence.
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
            # Transpose template to this root.
            transposed = frozenset((root + pc) % 12 for pc in template)

            # Matched weight: sum of weights for PCs in template.
            matched_weight = sum(
                weighted_pcs.get(pc, 0.0) for pc in transposed
            )

            # Count how many template PCs are present.
            matched_pcs = len(transposed & pc_set)
            template_size = len(template)
            coverage = matched_pcs / template_size

            # Skip 7th chords with poor coverage.
            if template_size >= 4 and coverage < 0.75:
                continue

            base_score = matched_weight / total_weight

            # Coverage bonus: prefer templates that explain more of their PCs.
            base_score *= coverage

            # Parsimony: penalize extra PCs not explained by the template.
            extra_pcs = len(pc_set - transposed)
            if extra_pcs > 0:
                base_score *= 1.0 / (1.0 + 0.15 * extra_pcs)

            # Inversion scoring based on bass.
            inversion = "root"
            if bass_pc is not None:
                bass_offset = (bass_pc - root) % 12
                if bass_offset == 0:
                    # Root position — no adjustment.
                    pass
                elif bass_offset in (3, 4):
                    # 3rd in bass.
                    base_score += 0.05
                    inversion = "1st"
                elif bass_offset in (6, 7, 8):
                    # 5th in bass.
                    base_score += 0.03
                    inversion = "2nd"
                elif bass_offset in (9, 10):
                    # 7th in bass.
                    base_score += 0.02
                    inversion = "3rd"
                elif bass_pc not in transposed:
                    # Bass not in template.
                    base_score -= 0.1

            # Map root to scale degree (find closest in scale).
            rel_pc = (root - tonic) % 12
            degree = 0
            min_dist = 12
            for deg_idx, sem in enumerate(scale):
                dist = min(abs(rel_pc - sem), 12 - abs(rel_pc - sem))
                if dist < min_dist:
                    min_dist = dist
                    degree = deg_idx

            # Markov prior.
            if prev_chord is not None and prev_chord.confidence > 0.0:
                pair = (prev_chord.degree, degree)
                prior = _TRANSITION_PRIOR.get(pair, 1.0)
                base_score *= 1.0 + 0.2 * (prior - 1.0)

            if base_score > best_score:
                second_best_score = best_score
                best_score = base_score
                best_estimate = ChordEstimate(
                    root_pc=root,
                    quality=quality,
                    degree=degree,
                    inversion=inversion,
                    confidence=0.0,  # computed below
                    bass_pc=bass_pc,
                )
            elif base_score > second_best_score:
                second_best_score = base_score

    # Compute confidence.
    if best_score <= 0:
        return best_estimate

    # Uniqueness: ratio of best to second-best.
    uniqueness = (
        best_score / second_best_score
        if second_best_score > 0 else 2.0
    )

    # Coverage of the best template.
    best_template = CHORD_TEMPLATES.get(best_estimate.quality, frozenset())
    best_transposed = frozenset(
        (best_estimate.root_pc + pc) % 12 for pc in best_template
    )
    best_coverage = (
        len(best_transposed & pc_set) / len(best_template)
        if best_template else 0.0
    )

    confidence = best_coverage * min(uniqueness, 2.0) / 2.0

    # Clamp low coverage.
    if best_coverage < 0.4:
        confidence = min(confidence, 0.3)

    # Penalize low uniqueness.
    if uniqueness < 1.1:
        confidence *= 0.8

    # Limit confidence for sparse data.
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


def _estimate_chords_for_work(
    work_id: str,
    sample_interval_beats: float = 1.0,
) -> tuple[list[ChordEstimate], dict[str, Any]]:
    """Estimate chords for all sample points in a work.

    Uses module-level _CHORD_CACHE for memoization.

    Returns:
        (chord_estimates, stats) where stats contains mean_confidence,
        unclassified_ratio, and low_confidence_ratio.
    """
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

    tonic_pc, is_minor, key_conf = _get_key_info(work_id)
    if tonic_pc is None or is_minor is None:
        # Fall back to C major if key unknown.
        tonic_pc = 0
        is_minor = False

    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)
    tracks_notes = _iter_track_notes(raw_data, None)
    if not tracks_notes:
        empty_list: list[ChordEstimate] = []
        empty_stats: dict[str, Any] = {
            "mean_confidence": 0.0,
            "unclassified_ratio": 1.0,
            "low_confidence_ratio": 1.0,
        }
        return empty_list, empty_stats

    # Determine total duration.
    max_tick = 0
    for _, sorted_notes in tracks_notes:
        if sorted_notes:
            last = sorted_notes[-1]
            max_tick = max(max_tick, last.start_tick + last.duration)

    sample_ticks = int(sample_interval_beats * tpb)
    if sample_ticks <= 0:
        sample_ticks = tpb

    chords: list[ChordEstimate] = []
    prev_chord: Optional[ChordEstimate] = None
    tick = 0

    while tick < max_tick:
        weighted_pcs, bass_pc = _extract_chord_support_pcs(
            tracks_notes, tick, tpb,
        )
        chord = _estimate_chord(
            weighted_pcs, bass_pc, tonic_pc, is_minor, prev_chord,
        )
        chords.append(chord)
        if chord.confidence > 0.0:
            prev_chord = chord
        tick += sample_ticks

    # Compute stats.
    total = len(chords)
    if total == 0:
        stats_out: dict[str, Any] = {
            "mean_confidence": 0.0,
            "unclassified_ratio": 1.0,
            "low_confidence_ratio": 1.0,
        }
    else:
        confidences = [c.confidence for c in chords]
        unclassified = sum(1 for c in chords if c.quality == "")
        low_conf = sum(1 for c in chords if 0.0 < c.confidence < 0.4)
        stats_out = {
            "mean_confidence": round(sum(confidences) / total, 3),
            "unclassified_ratio": round(unclassified / total, 3),
            "low_confidence_ratio": round(low_conf / total, 3),
        }

    _CHORD_CACHE[cache_key] = (chords, stats_out)
    return chords, stats_out


def _degree_to_roman(degree: int, quality: str, is_minor: bool) -> str:
    """Convert scale degree + quality to Roman numeral label."""
    numerals = _MINOR_NUMERALS if is_minor else _MAJOR_NUMERALS
    if 0 <= degree < len(numerals):
        base = numerals[degree]
    else:
        base = str(degree)

    # Override case/suffix based on actual quality.
    root_numeral = ["I", "II", "III", "IV", "V", "VI", "VII"][degree] if 0 <= degree < 7 else str(degree)
    if quality in ("m", "m7"):
        return root_numeral.lower()
    elif quality in ("dim", "dim7", "hdim7"):
        return root_numeral.lower() + "\u00b0"
    elif quality == "aug":
        return root_numeral + "+"
    elif quality in ("M", "dom7"):
        return root_numeral
    return base


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
    tonic_pc, is_minor, key_conf = _get_key_info(work_id)
    if tonic_pc is None:
        # Try to load anyway with C major fallback.
        tonic_pc = 0
        is_minor = False

    chords, est_quality = _estimate_chords_for_work(
        work_id, sample_interval_beats,
    )
    if not chords:
        return json.dumps({"error": f"No chords estimated for '{work_id}'."})

    # Key label.
    key_name = NOTE_NAMES_12[tonic_pc]
    key_label = f"{key_name} {'minor' if is_minor else 'major'}"

    # Filter to confident chords for distributions.
    confident = [c for c in chords if c.confidence > 0.0]
    total_confident = len(confident)

    # Degree distribution.
    degree_counts: Counter[str] = Counter()
    for chord in confident:
        roman = _degree_to_roman(chord.degree, chord.quality, is_minor)
        degree_counts[roman] += 1

    degree_dist: dict[str, float] = {}
    if total_confident > 0:
        for roman, count in degree_counts.most_common():
            degree_dist[roman] = round(count / total_confident, 3)

    # Quality distribution.
    quality_counts: Counter[str] = Counter()
    for chord in confident:
        if chord.quality:
            quality_counts[chord.quality] += 1

    total_quality = sum(quality_counts.values())
    quality_dist: dict[str, float] = {}
    if total_quality > 0:
        for qual, count in quality_counts.most_common():
            quality_dist[qual] = round(count / total_quality, 3)

    # Function distribution.
    func_counts: Counter[str] = Counter()
    for chord in confident:
        func = _DEGREE_TO_FUNCTION.get(chord.degree, "?")
        func_counts[func] += 1

    func_dist: dict[str, float] = {}
    if total_confident > 0:
        for func, count in func_counts.most_common():
            func_dist[func] = round(count / total_confident, 3)

    # Inversion distribution.
    inv_counts: Counter[str] = Counter()
    for chord in confident:
        inv_counts[chord.inversion] += 1

    inv_dist: dict[str, float] = {}
    if total_confident > 0:
        for inv, count in inv_counts.most_common():
            inv_dist[inv] = round(count / total_confident, 3)

    # Harmonic rhythm: count chord changes.
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
    total_bars = total_beats / 4.0  # 4/4 time

    avg_beats_per_change = (
        round(total_beats / changes, 2) if changes > 0 else 0.0
    )
    changes_per_bar = (
        round(changes / total_bars, 2) if total_bars > 0 else 0.0
    )

    # Degree bigrams (top 10).
    bigram_counts: Counter[tuple[str, str]] = Counter()
    prev_roman: Optional[str] = None
    for chord in chords:
        if chord.confidence > 0.0:
            roman = _degree_to_roman(chord.degree, chord.quality, is_minor)
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
    tonic_pc, is_minor, key_conf = _get_key_info(work_id)
    if tonic_pc is None:
        tonic_pc = 0
        is_minor = False

    # Use 0.5-beat resolution for finer cadence detection.
    chords, _ = _estimate_chords_for_work(work_id, 0.5)
    if not chords:
        return json.dumps({"error": f"No chords estimated for '{work_id}'."})

    score, raw_data, err = _load_score(work_id)
    if err or score is None or raw_data is None:
        return json.dumps({"error": err})

    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)
    tracks_notes = _iter_track_notes(raw_data, None)
    sample_ticks = tpb // 2  # 0.5 beats

    cadences: list[dict[str, Any]] = []
    cadence_counts: Counter[str] = Counter()

    # Helper: check if upper voice has leading tone resolution (7 -> 1).
    def _has_leading_tone_resolution(idx: int) -> bool:
        """Check for leading tone to tonic resolution at sample idx."""
        if idx < 1 or not tracks_notes:
            return False
        tick_before = (idx - 1) * sample_ticks
        tick_at = idx * sample_ticks
        leading_tone = (tonic_pc - 1) % 12  # semitone below tonic

        # Check upper voices (all except last = bass).
        for track_idx, (role, sorted_notes) in enumerate(tracks_notes):
            if track_idx == len(tracks_notes) - 1:
                continue  # skip bass
            note_before = sounding_note_at(sorted_notes, tick_before)
            note_at = sounding_note_at(sorted_notes, tick_at)
            if (note_before is not None and note_at is not None
                    and note_before.pitch % 12 == leading_tone
                    and note_at.pitch % 12 == tonic_pc):
                return True
        return False

    # Helper: check bass motion (5th down / 4th up).
    def _has_bass_fifth_motion(idx: int) -> bool:
        """Check for bass V-I motion (descending 5th or ascending 4th)."""
        if idx < 1 or not tracks_notes:
            return False
        tick_before = (idx - 1) * sample_ticks
        tick_at = idx * sample_ticks
        # Bass is last track.
        _, bass_notes = tracks_notes[-1]
        note_before = sounding_note_at(bass_notes, tick_before)
        note_at = sounding_note_at(bass_notes, tick_at)
        if note_before is not None and note_at is not None:
            interval = (note_at.pitch - note_before.pitch) % 12
            # 5th down = 7 semitones up, 4th up = 5 semitones up.
            if interval == 5 or interval == 7:
                return True
        return False

    # Helper: check for long note / fermata at resolution.
    def _has_long_resolution(idx: int) -> bool:
        """Check if the resolution chord has a notably long note."""
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

    # Helper: check for texture decrease (fewer voices at idx than idx-1).
    def _has_texture_decrease(idx: int) -> bool:
        """Check if fewer voices are sounding at idx vs idx-1."""
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

    # Helper: check bass descending by semitone (for Phrygian).
    def _has_bass_semitone_descent(idx: int) -> bool:
        """Check if bass descends by semitone at idx."""
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

    # Scan chord pairs for cadence patterns.
    for idx in range(1, len(chords)):
        prev = chords[idx - 1]
        curr = chords[idx]

        # Skip low-confidence pairs.
        if prev.confidence < 0.3 or curr.confidence < 0.3:
            continue

        cadence_type: Optional[str] = None
        base_confidence = min(prev.confidence, curr.confidence)

        # PAC / IAC: V -> I
        if prev.degree == 4 and curr.degree == 0:
            if curr.inversion == "root" and curr.confidence >= 0.5:
                cadence_type = "PAC"
            else:
                cadence_type = "IAC"

        # HC: -> V where V sustains for >= 2 beats (4 samples at 0.5 beat).
        elif curr.degree == 4:
            # Check if V sustains.
            sustain_count = 0
            for look in range(idx, min(idx + 4, len(chords))):
                if chords[look].degree == 4 and chords[look].confidence > 0.2:
                    sustain_count += 1
                else:
                    break
            if sustain_count >= 4 or _has_texture_decrease(idx):
                cadence_type = "HC"

        # DC: V -> vi (or V -> VI in minor).
        elif prev.degree == 4 and curr.degree == 5:
            cadence_type = "DC"

        # PHR: iv6 -> V in minor (bass semitone descent).
        elif (is_minor and prev.degree == 3 and curr.degree == 4
              and prev.inversion == "1st"
              and _has_bass_semitone_descent(idx)):
            cadence_type = "PHR"

        # EVAD: V -> (not I, not vi).
        elif prev.degree == 4 and curr.degree not in (0, 5):
            cadence_type = "EVAD"

        if cadence_type is None:
            continue

        # Compute confidence with enhancement signals.
        confidence = base_confidence
        tick_at = idx * sample_ticks

        # Strong beat arrival.
        beat_str = _beat_strength(tick_at)
        if beat_str >= 0.75:
            confidence += 0.2

        # Bass 5th down / 4th up.
        if _has_bass_fifth_motion(idx):
            confidence += 0.3

        # Leading tone resolution.
        if _has_leading_tone_resolution(idx):
            confidence += 0.3

        # Long note / fermata at resolution.
        if _has_long_resolution(idx):
            confidence += 0.1

        confidence = min(confidence, 1.0)

        # Location.
        bar = tick_at // TICKS_PER_BAR + 1
        beat = (tick_at % TICKS_PER_BAR) // tpb + 1

        # Avoid duplicate cadences within 2 beats (4 samples).
        if cadences:
            last_cad = cadences[-1]
            last_tick = (last_cad["bar"] - 1) * TICKS_PER_BAR + (last_cad["beat"] - 1) * tpb
            if tick_at - last_tick < 2 * tpb and last_cad["type"] == cadence_type:
                # Keep the one with higher confidence.
                if confidence > last_cad["confidence"]:
                    cadence_counts[last_cad["type"]] -= 1
                    cadences[-1] = {
                        "bar": bar,
                        "beat": beat,
                        "type": cadence_type,
                        "confidence": round(confidence, 2),
                        "key_context": _degree_to_roman(
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
            "key_context": _degree_to_roman(
                curr.degree, curr.quality, is_minor,
            ),
        })

    # Compute summary stats.
    total_bars = score.total_bars if score.total_bars > 0 else 1
    cadences_per_8_bars = (
        round(len(cadences) / total_bars * 8, 1)
        if total_bars > 0 else 0.0
    )
    avg_confidence = (
        round(sum(c["confidence"] for c in cadences) / len(cadences), 2)
        if cadences else 0.0
    )

    # Final cadence.
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


def _detect_suspension(
    note: Note,
    prev_note: Optional[Note],
    next_note: Optional[Note],
    chord_est: ChordEstimate,
    next_chord_est: Optional[ChordEstimate],
) -> bool:
    """Detect whether a note is a suspension.

    A suspension requires:
    - Same pitch as previous note (tied / preparation)
    - Previous note was a chord tone in its own chord context
    - Resolves down by step (1-2 semitones) to the next note
    - Falls on a strong beat (beat strength >= 0.5)

    Args:
        note: The candidate suspension note.
        prev_note: The note immediately before.
        next_note: The note immediately after (resolution).
        chord_est: Chord estimate at the suspension note's position.
        next_chord_est: Chord estimate at the resolution note's position.

    Returns:
        True if the note qualifies as a suspension.
    """
    if prev_note is None or next_note is None:
        return False

    # Preparation: same pitch as previous note.
    if note.pitch != prev_note.pitch:
        return False

    # Strong beat requirement.
    if _beat_strength(note.start_tick) < 0.5:
        return False

    # Resolution: descend by step (1-2 semitones).
    resolution_interval = note.pitch - next_note.pitch
    if resolution_interval < 1 or resolution_interval > 2:
        return False

    # Previous note should have been a chord tone (in its own chord context).
    # We approximate: the preparation pitch is consonant with the chord at
    # the previous position.  Since we don't always have the prev chord in
    # this helper, we relax this to: the prev note existed (already checked).

    return True


def _classify_suspension_pattern(
    note: Note,
    next_note: Optional[Note],
    chord_est: ChordEstimate,
) -> Optional[str]:
    """Classify a suspension into 4-3, 7-6, or 9-8 pattern.

    Measures the interval from the suspension note down to the chord root,
    then checks the resolution interval.

    Returns:
        Pattern string ("4-3", "7-6", "9-8") or None if unclassifiable.
    """
    if next_note is None or chord_est.quality == "":
        return None

    root_pc = chord_est.root_pc
    susp_pc = note.pitch % 12
    interval_from_root = (susp_pc - root_pc) % 12
    resolution_semitones = note.pitch - next_note.pitch

    if resolution_semitones < 1 or resolution_semitones > 2:
        return None

    # 4-3: suspension is P4 (5 semitones) above root, resolves to M3/m3.
    if interval_from_root == 5:
        return "4-3"
    # 7-6: suspension is m7/M7 (10 or 11 semitones) above root, resolves to 6th.
    if interval_from_root in (10, 11):
        return "7-6"
    # 9-8: suspension is m9/M9 (1 or 2 semitones) above root, resolves to octave.
    if interval_from_root in (1, 2):
        return "9-8"

    return None


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
    tonic_pc, is_minor, key_conf = _get_key_info(work_id)
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

    # Collect all notes from selected tracks.
    all_notes: list[Note] = []
    for trk in score.tracks:
        if track and trk.name != track:
            continue
        all_notes.extend(trk.sorted_notes)
    all_notes.sort(key=lambda note_obj: (note_obj.start_tick, note_obj.pitch))

    if not all_notes:
        return json.dumps({"error": f"No notes found for '{work_id}'."})

    # Helper: get chord estimate at a given tick.
    def _chord_at_tick(tick: int) -> Optional[ChordEstimate]:
        if sample_ticks <= 0:
            return None
        idx = tick // sample_ticks
        if 0 <= idx < len(chords):
            return chords[idx]
        return None

    # Helper: get template PCs for a chord.
    def _chord_pcs(chord: ChordEstimate) -> frozenset[int]:
        template = CHORD_TEMPLATES.get(chord.quality, frozenset())
        return frozenset((chord.root_pc + pc) % 12 for pc in template)

    # Classify each note.
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

        # Beat position tracking.
        beat_str = _beat_strength(note_obj.start_tick)
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

        # Get neighboring notes for NCT classification.
        prev_note = all_notes[note_idx - 1] if note_idx > 0 else None
        next_note = all_notes[note_idx + 1] if note_idx + 1 < len(all_notes) else None

        next_chord = _chord_at_tick(next_note.start_tick) if next_note else None

        # Check suspension first (strong beat pattern).
        if _detect_suspension(note_obj, prev_note, next_note, chord, next_chord):
            nct_types["suspension"] += 1
            pattern = _classify_suspension_pattern(note_obj, next_note, chord)
            if pattern:
                suspension_patterns[pattern] += 1
            continue

        # Passing and neighbor tone classification.
        if prev_note is not None and next_note is not None:
            iv_in = note_obj.pitch - prev_note.pitch
            iv_out = next_note.pitch - note_obj.pitch

            is_step_in = 1 <= abs(iv_in) <= 2
            is_step_out = 1 <= abs(iv_out) <= 2

            # Check if neighbors are chord tones.
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
                # Same direction = passing, opposite direction = neighbor.
                if (iv_in > 0 and iv_out > 0) or (iv_in < 0 and iv_out < 0):
                    nct_types["passing"] += 1
                    continue
                else:
                    nct_types["neighbor"] += 1
                    continue

        nct_types["other"] += 1

    # Compute ratios.
    classified_total = ct_count + nct_count
    total_all = classified_total + uncertain_count

    ct_ratio = round(ct_count / classified_total, 3) if classified_total > 0 else 0.0
    nct_ratio = round(nct_count / classified_total, 3) if classified_total > 0 else 0.0

    nct_type_dist: dict[str, float] = {}
    if nct_count > 0:
        for ntype in ("passing", "neighbor", "suspension", "other"):
            nct_type_dist[ntype] = round(nct_types.get(ntype, 0) / nct_count, 3)

    # NCT per beat.
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

    # Determine which track pairs to analyze.
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

    # Accumulators across all pairs.
    sum_parallel_perfects = 0
    sum_hidden_perfects = 0
    sum_voice_crossings = 0
    pair_results: dict[str, dict[str, Any]] = {}

    # Suspension chain tracking.
    all_suspension_chains: list[int] = []

    for ta_obj, tb_obj in pairs:
        pair_key = f"{ta_obj.name}-{tb_obj.name}"
        notes_a = ta_obj.sorted_notes
        notes_b = tb_obj.sorted_notes

        parallel_perfects = 0
        hidden_perfects = 0
        voice_crossings = 0
        pair_beat_count = 0

        # Suspension chain tracking for this pair.
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

                # Voice crossing: upper track (a) goes below lower track (b).
                # Track a is the higher voice; if its pitch < track b's pitch,
                # it's a crossing.
                if na.pitch < nb.pitch:
                    voice_crossings += 1

                if prev_note_a is not None and prev_note_b is not None and prev_ic is not None:
                    da = na.pitch - prev_note_a.pitch
                    db = nb.pitch - prev_note_b.pitch

                    same_direction = (
                        (da > 0 and db > 0) or (da < 0 and db < 0)
                    )

                    # Parallel perfects: same interval class, same direction,
                    # both are P5 (7) or P8 (0, but not P1 with both stationary).
                    is_curr_perfect = curr_ic in (0, 7)
                    is_prev_perfect = prev_ic in (0, 7)

                    if same_direction and is_curr_perfect and is_prev_perfect:
                        if curr_ic == prev_ic:
                            parallel_perfects += 1

                    # Hidden perfects: same direction arriving at P5/P8 but
                    # not previously at the same perfect interval.
                    if (same_direction and is_curr_perfect
                            and not (is_prev_perfect and curr_ic == prev_ic)):
                        hidden_perfects += 1

                    # Suspension detection for chains.
                    # A suspension occurs when one voice holds while the other
                    # moves, creating a dissonance on a strong beat.
                    is_suspension = False
                    if _beat_strength(tick) >= 0.5:
                        # One voice holds, other moves.
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
            tick += tpb  # sample every beat

        # Flush last chain.
        if current_chain_length >= 2:
            pair_chains.append(current_chain_length)

        # Per-100-beats rates.
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

    # Summary across all pairs.
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


def _categorize_duration(dur_ticks: int) -> str:
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


def _detect_bass_track(score: Score) -> Optional[Track]:
    """Auto-detect the bass track from a score.

    Priority: track named 'pedal' > 'lower' > 'bass' > lowest avg pitch.
    """
    if not score.tracks:
        return None

    # Check by name priority.
    for name in ("pedal", "lower", "bass"):
        for trk in score.tracks:
            if trk.name == name:
                return trk

    # Fall back to lowest average pitch.
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
        trk = _detect_bass_track(score)
        if trk is None:
            return json.dumps({"error": "No bass track detected."})

    notes = trk.sorted_notes
    if not notes:
        return json.dumps({"error": f"Bass track '{trk.name}' has no notes."})

    total_notes = len(notes)
    pitches = [n.pitch for n in notes]
    avg_pitch = round(sum(pitches) / len(pitches), 1)
    pitch_range = [min(pitches), max(pitches)]

    # Strong-beat bass motion: notes on strong beats or lasting >= 0.5 beats.
    strong_beat_notes: list[Note] = []
    for note_obj in notes:
        beat_str = _beat_strength(note_obj.start_tick)
        dur_beats = note_obj.duration / TICKS_PER_BEAT
        if beat_str >= 0.5 or dur_beats >= 0.5:
            strong_beat_notes.append(note_obj)

    # Compute intervals between consecutive strong-beat notes.
    strong_intervals: list[int] = []
    for idx in range(len(strong_beat_notes) - 1):
        iv = abs(strong_beat_notes[idx + 1].pitch - strong_beat_notes[idx].pitch)
        strong_intervals.append(iv)

    # P4/P5 motion ratio among strong-beat intervals.
    p4p5_count = sum(1 for iv in strong_intervals if interval_class(iv) in (5, 7))
    strong_beat_p4p5_ratio = (
        round(p4p5_count / len(strong_intervals), 3) if strong_intervals else 0.0
    )

    # Leap-then-stepback pattern.
    # After a leap (>2 semitones), next interval is step (1-2) in opposite direction.
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
                # Opposite direction check.
                if (iv > 0 and next_iv < 0) or (iv < 0 and next_iv > 0):
                    leap_stepback_count += 1

    leap_then_stepback_ratio = (
        round(leap_stepback_count / leap_count, 3) if leap_count > 0 else 0.0
    )

    # Interval distribution (absolute, as interval classes).
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

    # Rhythm distribution.
    rhythm_counts: Counter[str] = Counter()
    for note_obj in notes:
        rhythm_counts[_categorize_duration(note_obj.duration)] += 1

    rhythm_profile: dict[str, float] = {}
    if total_notes > 0:
        for cat, count in sorted(rhythm_counts.items(), key=lambda x: -x[1]):
            rhythm_profile[cat] = round(count / total_notes, 3)

    # Ostinato detection: look for repeating pitch+rhythm patterns.
    # Extract pitch intervals and durations as a sequence, then search for
    # repeating sub-sequences of length >= 2 notes with >= 3 repetitions.
    ostinato_detected = False
    if len(notes) >= 6:
        # Build a sequence of (interval, duration_quantized) tuples.
        pattern_seq: list[tuple[int, int]] = []
        for idx in range(len(notes) - 1):
            iv = notes[idx + 1].pitch - notes[idx].pitch
            dur_q = max(1, round(notes[idx].duration / (TICKS_PER_BEAT // 4)))
            pattern_seq.append((iv, dur_q))

        # Search for patterns of length 2 to 8.
        for pat_len in range(2, min(9, len(pattern_seq) // 3 + 1)):
            pattern_counts: Counter[tuple] = Counter()
            for idx in range(len(pattern_seq) - pat_len + 1):
                sub = tuple(pattern_seq[idx:idx + pat_len])
                pattern_counts[sub] += 1
            # Check for >= 3 consecutive or near-consecutive occurrences.
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

    # Get cadence data (use cached chords at 0.5-beat resolution).
    chords, _ = _estimate_chords_for_work(work_id, 0.5)
    cadence_sample_ticks = tpb // 2

    # Build a set of cadence tick positions with confidence >= 0.5.
    cadence_ticks: set[int] = set()
    if chords and len(chords) > 1:
        tonic_pc, is_minor_val, _ = _get_key_info(work_id)
        if tonic_pc is None:
            tonic_pc = 0
            is_minor_val = False

        for idx in range(1, len(chords)):
            prev_c = chords[idx - 1]
            curr_c = chords[idx]
            if prev_c.confidence < 0.3 or curr_c.confidence < 0.3:
                continue
            # Detect cadence patterns (simplified: V->I, ->V sustained, V->vi).
            is_cadence = False
            base_conf = min(prev_c.confidence, curr_c.confidence)
            tick_at = idx * cadence_sample_ticks

            if prev_c.degree == 4 and curr_c.degree == 0:
                is_cadence = True
            elif prev_c.degree == 4 and curr_c.degree == 5:
                is_cadence = True
            elif curr_c.degree == 4:
                # Half cadence: check sustain.
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

    # Prepare track notes for rest/texture analysis.
    sorted_tracks = [(trk.name, trk.sorted_notes) for trk in score.tracks]
    track_notes_for_rest: list[tuple[str, list[Note]]] = []
    if track:
        for trk_name, trk_notes in sorted_tracks:
            if trk_name == track:
                track_notes_for_rest.append((trk_name, trk_notes))
    else:
        track_notes_for_rest = sorted_tracks

    # Score boundary signals at each beat.
    boundary_scores: list[tuple[int, float, dict[str, float]]] = []
    signal_counts: Counter[str] = Counter()

    tick = 0
    while tick < total_dur:
        signals: dict[str, float] = {}

        # 1. Cadence signal (weight 0.4): cadence within 1 beat.
        cadence_signal = 0.0
        for offset in range(-tpb, tpb + 1, cadence_sample_ticks):
            if (tick + offset) in cadence_ticks:
                cadence_signal = 1.0
                break
        signals["cadence"] = cadence_signal

        # 2. Rest signal (weight 0.3): note ending followed by gap >= 0.5 beats,
        #    with preceding note >= 1 beat.
        rest_signal = 0.0
        for _, trk_notes in track_notes_for_rest:
            for note_obj in trk_notes:
                note_end = note_obj.start_tick + note_obj.duration
                # Note ends near this tick (within half a beat).
                if abs(note_end - tick) <= tpb // 2:
                    if note_obj.duration >= tpb:  # preceding note >= 1 beat
                        # Check for gap: no note starting in the next 0.5 beats.
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

        # 3. Texture decrease (weight 0.2): active voices decrease by >= 1
        #    compared to 2 beats earlier.
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

        # 4. Barline signal (weight 0.1): position is on beat 1 of a bar.
        barline_signal = 1.0 if tick % TICKS_PER_BAR == 0 else 0.0
        signals["barline"] = barline_signal

        # Weighted boundary score.
        boundary_score = (
            0.4 * cadence_signal
            + 0.3 * rest_signal
            + 0.2 * texture_signal
            + 0.1 * barline_signal
        )

        boundary_scores.append((tick, boundary_score, signals))
        tick += tpb  # sample every beat

    # Extract phrase boundaries (score > 0.5) with minimum 2-bar separation.
    min_phrase_ticks = 2 * TICKS_PER_BAR
    phrase_boundaries: list[int] = [0]  # start of piece is always a boundary
    boundary_signal_counts: Counter[str] = Counter()

    for tick_pos, score_val, signals in boundary_scores:
        if tick_pos == 0:
            continue
        if score_val > 0.5:
            # Check minimum phrase length from last boundary.
            if tick_pos - phrase_boundaries[-1] >= min_phrase_ticks:
                phrase_boundaries.append(tick_pos)
                for sig_name, sig_val in signals.items():
                    if sig_val > 0:
                        boundary_signal_counts[sig_name] += 1

    # Build phrase list.
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
    works = _load_works_for_category(category)
    if not works:
        return json.dumps({"error": f"No works found for category '{category}'."})

    degree_counts: Counter[tuple[str, ...]] = Counter()
    degree_work_sets: dict[tuple[str, ...], set[str]] = {}
    func_counts: Counter[tuple[str, ...]] = Counter()
    func_work_sets: dict[tuple[str, ...], set[str]] = {}
    total_ngrams = 0

    for work_id, _ in works:
        tonic_pc, is_minor_val, key_conf = _get_key_info(work_id)
        if tonic_pc is None:
            tonic_pc = 0
            is_minor_val = False

        chords, _ = _estimate_chords_for_work(work_id, 1.0)
        if not chords:
            continue

        # Filter to confident chords (skip unclassified).
        filtered_chords: list[ChordEstimate] = []
        for chord in chords:
            if chord.confidence > 0.0 and chord.quality != "":
                filtered_chords.append(chord)

        if len(filtered_chords) < n:
            continue

        # Extract degree and function sequences.
        degree_seq: list[str] = []
        func_seq: list[str] = []
        for chord in filtered_chords:
            roman = _degree_to_roman(chord.degree, chord.quality, is_minor_val)
            func = _DEGREE_TO_FUNCTION.get(chord.degree, "?")
            degree_seq.append(roman)
            func_seq.append(func)

        # Deduplicate consecutive identical chords in sequence.
        dedup_degree: list[str] = [degree_seq[0]]
        dedup_func: list[str] = [func_seq[0]]
        for idx in range(1, len(degree_seq)):
            if degree_seq[idx] != degree_seq[idx - 1]:
                dedup_degree.append(degree_seq[idx])
                dedup_func.append(func_seq[idx])

        # Extract n-grams from deduplicated sequences.
        for idx in range(len(dedup_degree) - n + 1):
            deg_ngram = tuple(dedup_degree[idx:idx + n])
            func_ngram = tuple(dedup_func[idx:idx + n])

            degree_counts[deg_ngram] += 1
            degree_work_sets.setdefault(deg_ngram, set()).add(work_id)

            func_counts[func_ngram] += 1
            func_work_sets.setdefault(func_ngram, set()).add(work_id)
            total_ngrams += 1

    # Build degree n-gram results.
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

    # Build function n-gram results.
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

    # Detect chord change points.
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

    # Section analysis: beginning (first 25%), middle (50%), ending (25%).
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
