"""Unified data model for Bach MIDI analysis.

Mirrors core C++ types (BachNoteSource, intervals, pitch constants) and provides
Score/Track/Note dataclasses consumed by all rule checkers.
"""

from __future__ import annotations

from bisect import bisect_right
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Dict, List, Optional

# ---------------------------------------------------------------------------
# Time constants (mirrors src/core/basic_types.h)
# ---------------------------------------------------------------------------

TICKS_PER_BEAT = 480
TICKS_PER_BAR = 1920

# ---------------------------------------------------------------------------
# Interval constants (mirrors src/core/pitch_utils.h)
# ---------------------------------------------------------------------------

UNISON = 0
MINOR_2ND = 1
MAJOR_2ND = 2
MINOR_3RD = 3
MAJOR_3RD = 4
PERFECT_4TH = 5
TRITONE = 6
PERFECT_5TH = 7
MINOR_6TH = 8
MAJOR_6TH = 9
MINOR_7TH = 10
MAJOR_7TH = 11
OCTAVE = 12

PERFECT_CONSONANCES = frozenset({UNISON, PERFECT_5TH, OCTAVE})
IMPERFECT_CONSONANCES = frozenset({MINOR_3RD, MAJOR_3RD, MINOR_6TH, MAJOR_6TH})
CONSONANCES = PERFECT_CONSONANCES | IMPERFECT_CONSONANCES
DISSONANCES = frozenset({MINOR_2ND, MAJOR_2ND, PERFECT_4TH, TRITONE, MINOR_7TH, MAJOR_7TH})

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# ---------------------------------------------------------------------------
# BachNoteSource enum (mirrors src/core/note_source.h)
# ---------------------------------------------------------------------------


class NoteSource(IntEnum):
    """Mirrors BachNoteSource in C++."""
    UNKNOWN = 0
    FUGUE_SUBJECT = 1
    FUGUE_ANSWER = 2
    COUNTERSUBJECT = 3
    EPISODE_MATERIAL = 4
    FREE_COUNTERPOINT = 5
    CANTUS_FIXED = 6
    ORNAMENT = 7
    PEDAL_POINT = 8
    ARPEGGIO_FLOW = 9
    TEXTURE_NOTE = 10
    GROUND_BASS = 11
    COLLISION_AVOID = 12
    POST_PROCESS = 13
    CHROMATIC_PASSING = 14
    FALSE_ENTRY = 15
    CODA = 16


# String name used in output.json -> NoteSource mapping.
SOURCE_STRING_MAP: Dict[str, NoteSource] = {
    "unknown": NoteSource.UNKNOWN,
    "fugue_subject": NoteSource.FUGUE_SUBJECT,
    "fugue_answer": NoteSource.FUGUE_ANSWER,
    "countersubject": NoteSource.COUNTERSUBJECT,
    "episode_material": NoteSource.EPISODE_MATERIAL,
    "free_counterpoint": NoteSource.FREE_COUNTERPOINT,
    "cantus_fixed": NoteSource.CANTUS_FIXED,
    "ornament": NoteSource.ORNAMENT,
    "pedal_point": NoteSource.PEDAL_POINT,
    "arpeggio_flow": NoteSource.ARPEGGIO_FLOW,
    "texture_note": NoteSource.TEXTURE_NOTE,
    "ground_bass": NoteSource.GROUND_BASS,
    "collision_avoid": NoteSource.COLLISION_AVOID,
    "post_process": NoteSource.POST_PROCESS,
    "chromatic_passing": NoteSource.CHROMATIC_PASSING,
    "false_entry": NoteSource.FALSE_ENTRY,
    "coda": NoteSource.CODA,
}


class TransformStep(IntEnum):
    """Mirrors BachTransformStep in C++."""
    NONE = 0
    TONAL_ANSWER = 1
    REAL_ANSWER = 2
    INVERSION = 3
    RETROGRADE = 4
    AUGMENTATION = 5
    DIMINUTION = 6
    SEQUENCE = 7
    COLLISION_AVOID = 8
    RANGE_CLAMP = 9
    OCTAVE_ADJUST = 10
    KEY_TRANSPOSE = 11


TRANSFORM_STRING_MAP: Dict[str, TransformStep] = {
    "tonal_answer": TransformStep.TONAL_ANSWER,
    "real_answer": TransformStep.REAL_ANSWER,
    "inversion": TransformStep.INVERSION,
    "retrograde": TransformStep.RETROGRADE,
    "augmentation": TransformStep.AUGMENTATION,
    "diminution": TransformStep.DIMINUTION,
    "sequence": TransformStep.SEQUENCE,
    "collision_avoid": TransformStep.COLLISION_AVOID,
    "range_clamp": TransformStep.RANGE_CLAMP,
    "octave_adjust": TransformStep.OCTAVE_ADJUST,
    "key_transpose": TransformStep.KEY_TRANSPOSE,
}

# ---------------------------------------------------------------------------
# Provenance
# ---------------------------------------------------------------------------


@dataclass
class Provenance:
    """Note provenance from output.json."""
    source: NoteSource = NoteSource.UNKNOWN
    original_pitch: int = 0
    chord_degree: int = -1
    lookup_tick: int = 0
    entry_number: int = 0
    transform_steps: List[TransformStep] = field(default_factory=list)

    @property
    def source_string(self) -> str:
        """Return the provenance source as a human-readable string."""
        for name, val in SOURCE_STRING_MAP.items():
            if val == self.source:
                return name
        return "unknown"


# ---------------------------------------------------------------------------
# Note / Track / Score
# ---------------------------------------------------------------------------


@dataclass
class Note:
    """A single MIDI note with optional provenance."""
    pitch: int
    velocity: int
    start_tick: int
    duration: int
    voice: str
    voice_id: int = 0
    channel: int = 0
    provenance: Optional[Provenance] = None

    @property
    def end_tick(self) -> int:
        return self.start_tick + self.duration

    @property
    def bar(self) -> int:
        """1-based bar number."""
        return self.start_tick // TICKS_PER_BAR + 1

    @property
    def beat(self) -> int:
        """1-based beat within the bar."""
        return (self.start_tick % TICKS_PER_BAR) // TICKS_PER_BEAT + 1

    @property
    def is_on_strong_beat(self) -> bool:
        """True if on beat 1 or beat 3 (4/4 time)."""
        return self.beat in (1, 3)

    @property
    def pitch_class(self) -> int:
        return self.pitch % 12

    @property
    def note_name(self) -> str:
        return NOTE_NAMES[self.pitch_class]

    @property
    def octave(self) -> int:
        return self.pitch // 12 - 1


@dataclass
class Track:
    """A single voice/track containing notes."""
    name: str
    channel: int = 0
    program: int = 0
    notes: List[Note] = field(default_factory=list)

    @property
    def sorted_notes(self) -> List[Note]:
        return sorted(self.notes, key=lambda n: n.start_tick)


@dataclass
class Score:
    """Complete musical score with metadata."""
    tracks: List[Track] = field(default_factory=list)
    seed: Optional[int] = None
    form: Optional[str] = None
    key: Optional[str] = None
    voices: Optional[int] = None
    source_file: Optional[str] = None

    @property
    def all_notes(self) -> List[Note]:
        """All notes across all tracks, sorted by start_tick."""
        notes = []
        for track in self.tracks:
            notes.extend(track.notes)
        return sorted(notes, key=lambda n: n.start_tick)

    @property
    def voices_dict(self) -> Dict[str, List[Note]]:
        """Notes grouped by voice name, each list sorted by start_tick."""
        result: Dict[str, List[Note]] = {}
        for track in self.tracks:
            result[track.name] = track.sorted_notes
        return result

    @property
    def num_voices(self) -> int:
        return len(self.tracks)

    @property
    def total_notes(self) -> int:
        return sum(len(t.notes) for t in self.tracks)

    @property
    def total_duration(self) -> int:
        """Total duration in ticks (end of last note)."""
        notes = self.all_notes
        if not notes:
            return 0
        return max(n.end_tick for n in notes)

    @property
    def total_bars(self) -> int:
        return (self.total_duration + TICKS_PER_BAR - 1) // TICKS_PER_BAR

    @property
    def has_provenance(self) -> bool:
        """True if at least one note has provenance data."""
        return any(
            n.provenance is not None and n.provenance.source != NoteSource.UNKNOWN
            for n in self.all_notes
        )


# ---------------------------------------------------------------------------
# Utility functions
# ---------------------------------------------------------------------------


def interval_class(semitones: int) -> int:
    """Reduce an interval to 0-11 range (mod 12)."""
    return abs(semitones) % 12


def is_consonant(semitones: int) -> bool:
    """True if the interval (mod 12) is consonant."""
    return interval_class(semitones) in CONSONANCES


def is_perfect_consonance(semitones: int) -> bool:
    """True if the interval (mod 12) is a perfect consonance."""
    return interval_class(semitones) in PERFECT_CONSONANCES


def is_dissonant(semitones: int) -> bool:
    """True if the interval (mod 12) is dissonant."""
    return interval_class(semitones) in DISSONANCES


def pitch_to_name(pitch: int) -> str:
    """Convert MIDI pitch to note name with octave (e.g., 'C4')."""
    return f"{NOTE_NAMES[pitch % 12]}{pitch // 12 - 1}"


def sounding_note_at(sorted_notes: List[Note], tick: int) -> Optional[Note]:
    """Return the note sounding at the given tick (considering sustain), or None.

    Mirrors C++ soundingPitch() in counterpoint_analyzer.cpp.  Notes must be
    sorted by start_tick (ascending).  Uses binary search to skip notes that
    start after *tick*, then scans backwards over candidates.
    """
    if not sorted_notes:
        return None
    # bisect_right gives the first index where start_tick > tick.
    # All candidates have index < hi.
    hi = bisect_right(sorted_notes, tick, key=lambda n: n.start_tick)
    # Scan backwards: the last note whose duration covers tick wins (C++ semantics).
    for i in range(hi - 1, -1, -1):
        n = sorted_notes[i]
        if n.start_tick <= tick < n.end_tick:
            return n
    return None
