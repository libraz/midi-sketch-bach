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
                      "pitch_class", or "vertical"
    """
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


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    server.run()
