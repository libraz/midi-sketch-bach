"""Reference data loading and indexing.

Provides WorkIndex for metadata queries and helpers for loading reference
works as Score objects. No MCP dependency.
"""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import Any, Optional

from .model import TICKS_PER_BEAT, Note, Score, Track
from .music_theory import TONIC_TO_PC

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
DATA_DIR = _PROJECT_ROOT / "data" / "reference"
MAX_NOTES_RETURN = 500

# ---------------------------------------------------------------------------
# Reference JSON -> Score adapter
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
    tonic = data.get("tonic", "")
    mode = data.get("mode", "")
    key_str = f"{tonic}_{mode}" if tonic and mode else None
    return Score(
        tracks=tracks,
        form=data.get("form") or data.get("category", ""),
        key=key_str,
        voices=data.get("voice_count"),
    )


# ---------------------------------------------------------------------------
# Key signature metadata (read from individual reference JSON files)
# ---------------------------------------------------------------------------

_KEY_CACHE: dict[str, tuple[Optional[int], Optional[bool], float]] = {}


def get_key_info(work_id: str) -> tuple[Optional[int], Optional[bool], float]:
    """Get (tonic_pc, is_minor, confidence) for a work.

    Reads tonic/mode/confidence from the individual reference JSON file.
    Results are cached in _KEY_CACHE for subsequent lookups.

    Returns (None, None, 0.0) if key is unavailable.
    """
    if work_id in _KEY_CACHE:
        return _KEY_CACHE[work_id]

    json_path = DATA_DIR / f"{work_id}.json"
    if not json_path.is_file():
        _KEY_CACHE[work_id] = (None, None, 0.0)
        return None, None, 0.0

    try:
        with open(json_path) as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError):
        _KEY_CACHE[work_id] = (None, None, 0.0)
        return None, None, 0.0

    tonic_str = data.get("tonic", "")
    tonic_pc = TONIC_TO_PC.get(tonic_str)
    if tonic_pc is None:
        _KEY_CACHE[work_id] = (None, None, 0.0)
        return None, None, 0.0

    is_minor = data.get("mode") == "minor"
    conf = 1.0 if data.get("confidence") == "verified" else 0.8
    result = (tonic_pc, is_minor, conf)
    _KEY_CACHE[work_id] = result
    return result


# ---------------------------------------------------------------------------
# WorkIndex
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
                # Pre-populate _KEY_CACHE for bulk operations
                tonic_str = data.get("tonic", "")
                tonic_pc = TONIC_TO_PC.get(tonic_str)
                if tonic_pc is not None:
                    is_minor = data.get("mode") == "minor"
                    conf = 1.0 if data.get("confidence") == "verified" else 0.8
                    _KEY_CACHE[key] = (tonic_pc, is_minor, conf)
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
