"""Pure music theory functions: scales, intervals, beat classification.

No I/O or MCP dependencies. Used by profiles.py, harmony.py, ngrams.py,
score.py, and bach_reference_server.py.
"""

from __future__ import annotations

from typing import NamedTuple

from .model import TICKS_PER_BAR, TICKS_PER_BEAT

# ---------------------------------------------------------------------------
# Interval & note name constants
# ---------------------------------------------------------------------------

INTERVAL_NAMES: list[str] = [
    "P1", "m2", "M2", "m3", "M3", "P4",
    "TT", "P5", "m6", "M6", "m7", "M7",
]

INTERVAL_NAME_MAP: dict[int, str] = dict(enumerate(INTERVAL_NAMES))

NOTE_NAMES_12: list[str] = [
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
]

# ---------------------------------------------------------------------------
# Scale constants
# ---------------------------------------------------------------------------

MAJOR_SCALE_SEMITONES: list[int] = [0, 2, 4, 5, 7, 9, 11]
MINOR_SCALE_SEMITONES: list[int] = [0, 2, 3, 5, 7, 8, 10]

TONIC_TO_PC: dict[str, int] = {
    "C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3,
    "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8,
    "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11,
}

# ---------------------------------------------------------------------------
# Pitch-class to scale degree mapping
# ---------------------------------------------------------------------------


def build_pc_to_degree_map(scale: list[int]) -> list[tuple[int, int]]:
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


PC_DEGREE_MAJOR: list[tuple[int, int]] = build_pc_to_degree_map(MAJOR_SCALE_SEMITONES)
PC_DEGREE_MINOR: list[tuple[int, int]] = build_pc_to_degree_map(MINOR_SCALE_SEMITONES)

# ---------------------------------------------------------------------------
# ScaleDegree
# ---------------------------------------------------------------------------


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
    pc_map = PC_DEGREE_MINOR if is_minor else PC_DEGREE_MAJOR
    rel = pitch - tonic
    octave = rel // 12
    pc = rel % 12
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
# Beat / rhythm classification
# ---------------------------------------------------------------------------


def classify_beat_position(tick: int) -> str:
    """Classify tick as 'strong' (beat 1), 'mid' (beat 3), or 'weak'."""
    beat = (tick % TICKS_PER_BAR) // TICKS_PER_BEAT
    if beat == 0:
        return "strong"
    if beat == 2:
        return "mid"
    return "weak"


def beat_strength(tick: int) -> float:
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


def quantize_duration(dur_beats: float, grid: str) -> int:
    """Quantize duration to grid units.

    Args:
        dur_beats: Duration in beats.
        grid: 'sixteenth' (default), 'eighth', or 'quarter'.
    Returns:
        Duration in grid units (minimum 1).
    """
    divisor = {"sixteenth": 0.25, "eighth": 0.5, "quarter": 1.0}.get(grid, 0.25)
    return max(1, round(dur_beats / divisor))


def accent_char(tick: int) -> str:
    """Return accent character for a tick position (s/m/w)."""
    pos = classify_beat_position(tick)
    return {"strong": "s", "mid": "m", "weak": "w"}[pos]


def is_chord_tone_simple(pitch: int, bass_pitch: int) -> bool:
    """Simplified chord-tone check: root, m3, M3, P5, m6, M6 from bass."""
    iv = abs(pitch - bass_pitch) % 12
    return iv in (0, 3, 4, 7, 8, 9)
