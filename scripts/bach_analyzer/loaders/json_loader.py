"""Load Score from output.json (with optional provenance)."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Dict, List, Optional, Union

from ..model import (
    Note,
    NoteSource,
    Provenance,
    Score,
    SOURCE_STRING_MAP,
    Track,
    TRANSFORM_STRING_MAP,
    TransformStep,
)


def _parse_provenance(prov_data: dict) -> Optional[Provenance]:
    """Parse a provenance dict from output.json."""
    if not prov_data:
        return None
    source_str = prov_data.get("source", "unknown")
    source = SOURCE_STRING_MAP.get(source_str, NoteSource.UNKNOWN)
    steps = []
    for step_str in prov_data.get("transform_steps", []):
        step = TRANSFORM_STRING_MAP.get(step_str, TransformStep.NONE)
        if step != TransformStep.NONE:
            steps.append(step)
    return Provenance(
        source=source,
        original_pitch=prov_data.get("original_pitch", 0),
        chord_degree=prov_data.get("chord_degree", -1),
        lookup_tick=prov_data.get("lookup_tick", 0),
        entry_number=prov_data.get("entry_number", 0),
        transform_steps=steps,
    )


def _parse_flat_source(source_str: Optional[str]) -> Optional[Provenance]:
    """Parse a flat 'source' field (no nested provenance object)."""
    if not source_str:
        return None
    source = SOURCE_STRING_MAP.get(source_str, NoteSource.UNKNOWN)
    if source == NoteSource.UNKNOWN and source_str != "unknown":
        return None
    return Provenance(source=source)


def _parse_note(note_data: dict, voice_name: str, voice_id: int, channel: int) -> Note:
    """Parse a single note dict."""
    return Note(
        pitch=note_data.get("pitch", 0),
        velocity=note_data.get("velocity", 80),
        start_tick=note_data.get("start_tick", 0),
        duration=note_data.get("duration", 0),
        voice=note_data["voice"] if isinstance(note_data.get("voice"), str) else voice_name,
        voice_id=note_data["voice"] if isinstance(note_data.get("voice"), int) else voice_id,
        channel=channel,
        provenance=_parse_provenance(note_data.get("provenance"))
        or _parse_flat_source(note_data.get("source")),
    )


def load_json(source: Union[str, Path, dict]) -> Score:
    """Load a Score from an output.json file or pre-parsed dict.

    Args:
        source: File path (str or Path) or already-parsed dict.

    Returns:
        A Score with tracks, notes, and optional provenance.
    """
    if isinstance(source, dict):
        data = source
    else:
        path = Path(source)
        with open(path) as fh:
            data = json.load(fh)

    tracks: List[Track] = []
    for idx, track_data in enumerate(data.get("tracks", [])):
        name = track_data.get("name", f"voice_{idx}")
        channel = track_data.get("channel", idx)
        program = track_data.get("program", 0)
        notes = [
            _parse_note(nd, name, idx, channel)
            for nd in track_data.get("notes", [])
        ]
        tracks.append(Track(name=name, channel=channel, program=program, notes=notes))

    source_file = None
    if not isinstance(source, dict):
        source_file = str(source)

    return Score(
        tracks=tracks,
        seed=data.get("seed"),
        form=data.get("form"),
        key=data.get("key"),
        voices=data.get("voices"),
        source_file=source_file,
    )
