"""Load Score from a standard MIDI file using mido."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Union

from ..model import Note, Score, Track

# Voice name assignment by MIDI channel (organ mapping from CLAUDE.md).
_ORGAN_CHANNEL_NAMES: Dict[int, str] = {
    0: "manual_i",
    1: "manual_ii",
    2: "manual_iii",
    3: "pedal",
}


def _channel_to_voice_name(channel: int) -> str:
    return _ORGAN_CHANNEL_NAMES.get(channel, f"ch_{channel}")


def load_midi(source: Union[str, Path]) -> Score:
    """Load a Score from a .mid file.

    Requires the ``mido`` package.  No provenance information is available
    when loading from MIDI.

    Args:
        source: Path to a .mid file.

    Returns:
        A Score with one Track per MIDI channel (excluding channel 15 metadata).
    """
    try:
        import mido
    except ImportError as exc:
        raise ImportError(
            "mido is required for MIDI loading. Install with: pip install mido"
        ) from exc

    mid = mido.MidiFile(str(source))

    # Collect notes per channel.
    channel_notes: Dict[int, List[Note]] = {}
    channel_programs: Dict[int, int] = {}

    for track in mid.tracks:
        abs_tick = 0
        pending: Dict[int, Dict[int, int]] = {}  # channel -> {pitch: start_tick}
        for msg in track:
            abs_tick += msg.time
            if msg.type == "program_change":
                channel_programs[msg.channel] = msg.program
            elif msg.type == "note_on" and msg.velocity > 0:
                pending.setdefault(msg.channel, {})[msg.pitch] = abs_tick
            elif msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
                ch_pending = pending.get(msg.channel, {})
                start = ch_pending.pop(msg.pitch, None)
                if start is not None:
                    voice_name = _channel_to_voice_name(msg.channel)
                    note = Note(
                        pitch=msg.pitch,
                        velocity=msg.velocity if msg.type != "note_off" else 80,
                        start_tick=start,
                        duration=abs_tick - start,
                        voice=voice_name,
                        voice_id=msg.channel,
                        channel=msg.channel,
                    )
                    channel_notes.setdefault(msg.channel, []).append(note)

    # Build tracks (skip metadata channel 15).
    tracks: List[Track] = []
    for ch in sorted(channel_notes.keys()):
        if ch == 15:
            continue
        name = _channel_to_voice_name(ch)
        tracks.append(
            Track(
                name=name,
                channel=ch,
                program=channel_programs.get(ch, 0),
                notes=sorted(channel_notes[ch], key=lambda n: n.start_tick),
            )
        )

    return Score(tracks=tracks, source_file=str(source))
