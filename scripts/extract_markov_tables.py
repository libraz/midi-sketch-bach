#!/usr/bin/env python3
"""Extract Markov transition tables from Bach reference JSON files.

Reads reference JSON files from data/reference/BWV*.json and computes pitch
and duration Markov transition tables conditioned on musical context. Outputs
C++ constexpr tables suitable for inclusion in the bach namespace.

Six models are generated:
  - FugueUpper: Upper voices from organ fugues, WTC, trio sonatas
  - FuguePedal: Bass/pedal voices from the same categories
  - Cello: Solo cello suites and lute suites
  - Violin: Solo violin sonatas and partitas
  - ToccataUpper: Upper voices from organ preludes/toccatas/fantasias (non-fugue)
  - ToccataPedal: Bass/pedal voices from the same non-fugue organ works

Usage:
    python3 scripts/extract_markov_tables.py
    python3 scripts/extract_markov_tables.py --data-dir data/reference --output src/core/markov_tables_data.inc
    python3 -m scripts.extract_markov_tables
"""

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Categories included in each model
FUGUE_CATEGORIES = frozenset({
    "organ_fugue", "organ_pf", "wtc1", "wtc2", "trio_sonata",
})
CELLO_CATEGORIES = frozenset({
    "solo_cello_suite", "lute_suite",
})
VIOLIN_CATEGORIES = frozenset({
    "solo_violin_sonata", "solo_violin_partita",
})
TOCCATA_CATEGORIES = frozenset({"organ_pf"})

# Movement-level filtering: exclude fugue movements within organ_pf
TOCCATA_EXCLUDE_SUFFIXES = {"_fugue"}

# Pedal/bass role identifiers
PEDAL_ROLES = frozenset({"pedal", "v4"})

# Krumhansl-Kessler key profiles (pitch class 0=C through 11=B)
KK_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
KK_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]

# Tonic name to pitch class
TONIC_TO_PC = {
    "C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3,
    "E": 4, "Fb": 4, "E#": 5, "F": 5, "F#": 6, "Gb": 6,
    "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10,
    "B": 11, "Cb": 11,
}

# Degree step range: [-9, +9] with -9 = LargeLeapDown, +9 = LargeLeapUp
STEP_COUNT = 19  # indices 0..18, value = index - 9

# Degree class: Stable=0, Dominant=1, Motion=2
DC_STABLE = 0
DC_DOMINANT = 1
DC_MOTION = 2
DC_COUNT = 3

# Beat position: Bar=0, Beat=1, Off8=2, Off16=3
BP_BAR = 0
BP_BEAT = 1
BP_OFF8 = 2
BP_OFF16 = 3
BP_COUNT = 4

# Duration categories: S16=0, S8=1, Dot8=2, Qtr=3, HalfPlus=4
DUR_S16 = 0
DUR_S8 = 1
DUR_DOT8 = 2
DUR_QTR = 3
DUR_HALFPLUS = 4
DUR_COUNT = 5

# Directed interval class: StepUp=0, StepDown=1, SkipUp=2, SkipDown=3, LeapUp=4, LeapDown=5
DIC_STEP_UP = 0
DIC_STEP_DOWN = 1
DIC_SKIP_UP = 2
DIC_SKIP_DOWN = 3
DIC_LEAP_UP = 4
DIC_LEAP_DOWN = 5
DIC_COUNT = 6

# Pitch table dimensions
PITCH_ROWS = STEP_COUNT * DC_COUNT * BP_COUNT  # 19 * 3 * 4 = 228
PITCH_COLS = STEP_COUNT  # 19

# Duration table dimensions
DUR_ROWS = DUR_COUNT * DIC_COUNT  # 5 * 6 = 30
DUR_COLS = DUR_COUNT  # 5

# Vertical interval table dimensions
VB_COUNT = 3    # voice_bin: 0=2voices, 1=3voices, 2=4+voices
HF_COUNT = 3    # harm_func: 0=Tonic, 1=Subdominant, 2=Dominant
PC_OFFSET_COUNT = 12  # pitch class offset 0-11 from bass
VERT_ROWS = 7 * BP_COUNT * VB_COUNT * HF_COUNT  # 7 * 4 * 3 * 3 = 252
VERT_COLS = PC_OFFSET_COUNT  # 12

# Harmonic function classification
HF_TONIC = 0       # I, vi, iii (degrees 0, 5, 2)
HF_SUBDOMINANT = 1  # IV, ii (degrees 3, 1)
HF_DOMINANT = 2     # V, vii (degrees 4, 6)

# Smoothing constant
SMOOTH_K = 0.1

# Normalization target (stored as uint16_t, rows sum to this)
NORM_SUM = 10000

# Maximum segment length in bars for local key estimation
MAX_SEGMENT_BARS = 8


# ---------------------------------------------------------------------------
# Scales for degree computation
# ---------------------------------------------------------------------------

MAJOR_SCALE = [0, 2, 4, 5, 7, 9, 11]
MINOR_SCALE = [0, 2, 3, 5, 7, 8, 10]


# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------

def pitch_to_abs_degree(pitch: int, key: int, is_minor: bool) -> int:
    """Convert MIDI pitch to absolute scale degree.

    Args:
        pitch: MIDI pitch number (0-127).
        key: Key root as pitch class (0=C, 1=C#, ..., 11=B).
        is_minor: True for minor key, False for major.

    Returns:
        Absolute scale degree (octave * 7 + degree_within_octave).
    """
    scale = MINOR_SCALE if is_minor else MAJOR_SCALE
    octave = pitch // 12
    pitch_class = pitch % 12
    relative_pc = (pitch_class - key) % 12

    best_deg = 0
    best_dist = 99
    for idx, scale_pc in enumerate(scale):
        dist = min(abs(relative_pc - scale_pc), 12 - abs(relative_pc - scale_pc))
        if dist < best_dist:
            best_dist = dist
            best_deg = idx

    return octave * 7 + best_deg


def compute_degree_step(from_pitch: int, to_pitch: int, key: int, is_minor: bool) -> int:
    """Compute the directed degree step between two pitches.

    Args:
        from_pitch: Source MIDI pitch.
        to_pitch: Target MIDI pitch.
        key: Key root as pitch class.
        is_minor: True for minor key.

    Returns:
        Clamped degree step in [-9, +9] where +/-9 means large leap.
    """
    deg_from = pitch_to_abs_degree(from_pitch, key, is_minor)
    deg_to = pitch_to_abs_degree(to_pitch, key, is_minor)
    step = deg_to - deg_from
    if step > 8:
        return 9   # LargeLeapUp
    if step < -8:
        return -9  # LargeLeapDown
    return step


def step_to_index(step: int) -> int:
    """Convert degree step [-9, +9] to array index [0, 18]."""
    return step + 9


def degree_class(pitch: int, key: int, is_minor: bool) -> int:
    """Classify a pitch's scale degree.

    Args:
        pitch: MIDI pitch number.
        key: Key root as pitch class.
        is_minor: True for minor key.

    Returns:
        DC_STABLE (degrees 0, 2), DC_DOMINANT (degrees 4, 6), or DC_MOTION (degrees 1, 3, 5).
    """
    scale = MINOR_SCALE if is_minor else MAJOR_SCALE
    relative_pc = (pitch % 12 - key) % 12

    best_deg = 0
    best_dist = 99
    for idx, scale_pc in enumerate(scale):
        dist = min(abs(relative_pc - scale_pc), 12 - abs(relative_pc - scale_pc))
        if dist < best_dist:
            best_dist = dist
            best_deg = idx

    if best_deg in (0, 2):
        return DC_STABLE
    if best_deg in (4, 6):
        return DC_DOMINANT
    return DC_MOTION


def beat_position(onset: float, numerator: int, denominator: int) -> int:
    """Classify the metric position of a note onset.

    Args:
        onset: Note onset in beats (quarter note = 1.0).
        numerator: Time signature numerator.
        denominator: Time signature denominator.

    Returns:
        BP_BAR, BP_BEAT, BP_OFF8, or BP_OFF16.
    """
    # Compute bar length in beats (quarter notes).
    # For 4/4: 4 * (4/4) = 4 beats.  For 3/8: 3 * (4/8) = 1.5 beats.
    # For 6/8: 6 * (4/8) = 3 beats.  For 2/2: 2 * (4/2) = 4 beats.
    bar_length = numerator * (4.0 / denominator)

    # Position within bar
    pos_in_bar = onset % bar_length
    epsilon = 0.01

    # Bar start
    if abs(pos_in_bar) < epsilon or abs(pos_in_bar - bar_length) < epsilon:
        return BP_BAR

    # Beat positions: depends on denominator
    # For denominator=4: beats at 0, 1, 2, 3, ...
    # For denominator=8: beats at 0, 0.5, 1.0, 1.5, ...
    # For denominator=2: beats at 0, 2, 4, ...
    beat_unit = 4.0 / denominator
    beats_in_bar = numerator
    for beat_idx in range(1, beats_in_bar):
        beat_pos = beat_idx * beat_unit
        if abs(pos_in_bar - beat_pos) < epsilon:
            return BP_BEAT

    # Off8: position ends in .5 (eighth note offbeat)
    fractional = pos_in_bar % 1.0
    if abs(fractional - 0.5) < epsilon:
        return BP_OFF8

    # Everything else is Off16
    return BP_OFF16


def duration_category(duration: float) -> int:
    """Classify note duration into a category.

    Args:
        duration: Note duration in beats (quarter note = 1.0).

    Returns:
        DUR_S16, DUR_S8, DUR_DOT8, DUR_QTR, or DUR_HALFPLUS.
    """
    if duration < 0.1875:
        return DUR_S16
    if duration < 0.375:
        return DUR_S8
    if duration < 0.625:
        return DUR_DOT8
    if duration < 1.25:
        return DUR_QTR
    return DUR_HALFPLUS


def directed_interval_class(degree_step: int) -> Optional[int]:
    """Classify a degree step into a directed interval class.

    Args:
        degree_step: Degree step in [-9, +9].

    Returns:
        Directed interval class index, or None if step is 0 (unison).
    """
    if degree_step == 0:
        return None  # Unison handled separately
    if 1 <= degree_step <= 2:
        return DIC_STEP_UP
    if -2 <= degree_step <= -1:
        return DIC_STEP_DOWN
    if 3 <= degree_step <= 4:
        return DIC_SKIP_UP
    if -4 <= degree_step <= -3:
        return DIC_SKIP_DOWN
    if degree_step >= 5:
        return DIC_LEAP_UP
    # degree_step <= -5
    return DIC_LEAP_DOWN


def classify_harm_func(bass_degree: int) -> int:
    """Classify a bass scale degree (0-6) to harmonic function T/S/D.

    Args:
        bass_degree: Scale degree 0-6 (0=tonic, 1=supertonic, ..., 6=leading tone).

    Returns:
        HF_TONIC (I, vi, iii), HF_SUBDOMINANT (IV, ii), or HF_DOMINANT (V, vii).
    """
    degree = bass_degree % 7
    if degree in (0, 5, 2):
        return HF_TONIC       # I, vi, iii
    if degree in (3, 1):
        return HF_SUBDOMINANT  # IV, ii
    return HF_DOMINANT         # V, vii (degrees 4, 6)


def pearson_correlation(vec_a: list[float], vec_b: list[float]) -> float:
    """Compute Pearson correlation between two equal-length vectors.

    Args:
        vec_a: First vector.
        vec_b: Second vector.

    Returns:
        Pearson correlation coefficient in [-1, +1].
    """
    length = len(vec_a)
    mean_a = sum(vec_a) / length
    mean_b = sum(vec_b) / length
    cov = sum((vec_a[idx] - mean_a) * (vec_b[idx] - mean_b) for idx in range(length))
    var_a = sum((vec_a[idx] - mean_a) ** 2 for idx in range(length))
    var_b = sum((vec_b[idx] - mean_b) ** 2 for idx in range(length))
    denom = math.sqrt(var_a * var_b)
    if denom < 1e-12:
        return 0.0
    return cov / denom


def rotate_profile(profile: list[float], shift: int) -> list[float]:
    """Rotate a 12-element pitch class profile by shift semitones.

    Args:
        profile: 12-element pitch class profile (index 0 = C).
        shift: Number of semitones to rotate (key root pitch class).

    Returns:
        Rotated profile aligned to the given key root.
    """
    return [profile[(idx + shift) % 12] for idx in range(12)]


# ---------------------------------------------------------------------------
# Local key estimation (Krumhansl-Kessler)
# ---------------------------------------------------------------------------

def find_cadence_points(notes: list[dict], bar_length: float) -> list[float]:
    """Find potential cadence points in a note sequence.

    Cadence points are notes with duration >= 2.0 beats that fall on beat 0 or 1,
    or gaps > 0.5 beats between consecutive notes.

    Args:
        notes: Sorted list of note dicts with 'onset' and 'duration'.
        bar_length: Bar length in beats.

    Returns:
        Sorted list of onset positions where segments should be split.
    """
    points = set()

    for idx, note in enumerate(notes):
        # Long notes on strong beats
        if note["duration"] >= 2.0:
            pos_in_bar = note["onset"] % bar_length
            beat_unit = 1.0  # quarter note
            if pos_in_bar < 0.01 or abs(pos_in_bar - beat_unit) < 0.01:
                points.add(note["onset"])

        # Gaps between notes
        if idx > 0:
            prev_end = notes[idx - 1]["onset"] + notes[idx - 1]["duration"]
            gap = note["onset"] - prev_end
            if gap > 0.5:
                points.add(note["onset"])

    return sorted(points)


def estimate_local_keys(
    notes: list[dict],
    numerator: int,
    denominator: int,
) -> list[tuple[float, float, int, bool]]:
    """Estimate local keys using sliding Krumhansl-Kessler correlation.

    Args:
        notes: Sorted list of note dicts with 'pitch', 'onset', 'duration'.
        numerator: Time signature numerator.
        denominator: Time signature denominator.

    Returns:
        List of (segment_start, segment_end, key_pc, is_minor) tuples.
    """
    if not notes:
        return [(0.0, 0.0, 0, False)]

    bar_length = numerator * (4.0 / denominator)
    max_segment_length = MAX_SEGMENT_BARS * bar_length

    # Find cadence points
    cadence_pts = find_cadence_points(notes, bar_length)

    # Build segment boundaries
    track_start = notes[0]["onset"]
    track_end = notes[-1]["onset"] + notes[-1]["duration"]

    boundaries = [track_start]
    for point in cadence_pts:
        if point > boundaries[-1] + bar_length:  # minimum 1 bar between splits
            boundaries.append(point)

    # Enforce max segment length
    refined = [boundaries[0]]
    for idx in range(1, len(boundaries)):
        prev = refined[-1]
        curr = boundaries[idx]
        # If segment is too long, split at bar boundaries
        while curr - prev > max_segment_length:
            prev += max_segment_length
            refined.append(prev)
        refined.append(curr)
    boundaries = refined

    if boundaries[-1] < track_end:
        boundaries.append(track_end)

    # Estimate key for each segment
    segments = []
    prev_key = 0
    prev_minor = False

    for seg_idx in range(len(boundaries) - 1):
        seg_start = boundaries[seg_idx]
        seg_end = boundaries[seg_idx + 1]

        # Collect notes in this segment
        seg_notes = [
            note for note in notes
            if note["onset"] >= seg_start - 0.01 and note["onset"] < seg_end + 0.01
        ]

        if len(seg_notes) < 4:
            # Fallback to previous segment's key
            segments.append((seg_start, seg_end, prev_key, prev_minor))
            continue

        # Build duration-weighted pitch class histogram
        histogram = [0.0] * 12
        for note in seg_notes:
            pitch_class = note["pitch"] % 12
            histogram[pitch_class] += note["duration"]

        # Correlate against all 24 keys
        best_corr = -2.0
        best_key = 0
        best_minor = False

        for key_pc in range(12):
            # Major
            rotated_major = rotate_profile(KK_MAJOR, key_pc)
            corr_major = pearson_correlation(histogram, rotated_major)
            if corr_major > best_corr:
                best_corr = corr_major
                best_key = key_pc
                best_minor = False

            # Minor
            rotated_minor = rotate_profile(KK_MINOR, key_pc)
            corr_minor = pearson_correlation(histogram, rotated_minor)
            if corr_minor > best_corr:
                best_corr = corr_minor
                best_key = key_pc
                best_minor = True

        segments.append((seg_start, seg_end, best_key, best_minor))
        prev_key = best_key
        prev_minor = best_minor

    return segments


def get_key_at_onset(
    segments: list[tuple[float, float, int, bool]], onset: float
) -> tuple[int, bool]:
    """Look up the local key at a given onset time.

    Args:
        segments: List of (start, end, key_pc, is_minor) from estimate_local_keys.
        onset: Note onset in beats.

    Returns:
        (key_pc, is_minor) tuple.
    """
    for seg_start, seg_end, key_pc, is_minor in segments:
        if seg_start - 0.01 <= onset <= seg_end + 0.01:
            return key_pc, is_minor
    # Fallback: return last segment's key
    if segments:
        return segments[-1][2], segments[-1][3]
    return 0, False


# ---------------------------------------------------------------------------
# Time signature lookup
# ---------------------------------------------------------------------------

def build_time_sig_map(
    time_signatures: list[dict], ticks_per_beat: int
) -> list[tuple[float, int, int]]:
    """Build a sorted list of (onset_beats, numerator, denominator) from time signatures.

    Args:
        time_signatures: List of time signature dicts with 'tick', 'numerator', 'denominator'.
        ticks_per_beat: Ticks per beat from the JSON file.

    Returns:
        Sorted list of (onset_in_beats, numerator, denominator).
    """
    result = []
    for ts_entry in time_signatures:
        onset_beats = ts_entry["tick"] / ticks_per_beat
        result.append((onset_beats, ts_entry["numerator"], ts_entry["denominator"]))
    result.sort(key=lambda entry: entry[0])
    return result


def get_time_sig_at_onset(
    ts_map: list[tuple[float, int, int]], onset: float
) -> tuple[int, int]:
    """Look up the time signature at a given onset.

    Args:
        ts_map: Sorted list from build_time_sig_map.
        onset: Note onset in beats.

    Returns:
        (numerator, denominator) tuple.
    """
    num = 4
    den = 4
    for ts_onset, ts_num, ts_den in ts_map:
        if ts_onset <= onset + 0.01:
            num = ts_num
            den = ts_den
        else:
            break
    return num, den


# ---------------------------------------------------------------------------
# Track filtering
# ---------------------------------------------------------------------------

def is_pedal_track(role: str, track_idx: int, voice_count: int, total_tracks: int) -> bool:
    """Determine if a track is a pedal/bass track for fugue filtering.

    A track is pedal/bass if:
      - Its role is 'pedal' or 'v4', OR
      - It is the last track and voice_count >= 3.

    Args:
        role: Track role string (e.g., 'v1', 'pedal', 'manual').
        track_idx: Zero-based index of this track among all tracks.
        voice_count: Declared voice count from the JSON file.
        total_tracks: Total number of tracks in the file.

    Returns:
        True if this track should be classified as pedal/bass.
    """
    if role in PEDAL_ROLES:
        return True
    if voice_count >= 3 and track_idx == total_tracks - 1:
        return True
    return False


# ---------------------------------------------------------------------------
# Transition counting
# ---------------------------------------------------------------------------

class TransitionCounter:
    """Accumulates pitch and duration Markov transition counts."""

    def __init__(self, name: str):
        self.name = name
        self.pitch_counts = [[0] * PITCH_COLS for _ in range(PITCH_ROWS)]
        self.dur_counts = [[0] * DUR_COLS for _ in range(DUR_ROWS)]
        self.total_pitch_transitions = 0
        self.total_dur_transitions = 0
        self.file_count = 0
        self.track_count = 0

    def pitch_row_index(self, prev_step_idx: int, deg_cls: int, beat_pos_val: int) -> int:
        """Compute the row index into the pitch transition table.

        Index = prev_step_idx * DC_COUNT * BP_COUNT + deg_cls * BP_COUNT + beat_pos_val
        """
        return prev_step_idx * DC_COUNT * BP_COUNT + deg_cls * BP_COUNT + beat_pos_val

    def dur_row_index(self, prev_dur_cat: int, dic_val: int) -> int:
        """Compute the row index into the duration transition table.

        Index = prev_dur_cat * DIC_COUNT + dic_val
        """
        return prev_dur_cat * DIC_COUNT + dic_val

    def add_track(
        self,
        notes: list[dict],
        key_segments: list[tuple[float, float, int, bool]],
        ts_map: list[tuple[float, int, int]],
    ) -> None:
        """Process a single track and accumulate transitions.

        Args:
            notes: Sorted list of note dicts (onset order, ties filtered).
            key_segments: Local key segments from estimate_local_keys.
            ts_map: Time signature map from build_time_sig_map.
        """
        if len(notes) < 3:
            return

        self.track_count += 1

        # Process consecutive note triples for pitch transitions (need prev_step)
        # and consecutive note pairs for duration transitions
        for idx in range(2, len(notes)):
            note_prev2 = notes[idx - 2]
            note_prev = notes[idx - 1]
            note_curr = notes[idx]

            # Local key at the current note
            key_pc, is_minor = get_key_at_onset(key_segments, note_curr["onset"])

            # Degree steps
            prev_step = compute_degree_step(
                note_prev2["pitch"], note_prev["pitch"], key_pc, is_minor
            )
            curr_step = compute_degree_step(
                note_prev["pitch"], note_curr["pitch"], key_pc, is_minor
            )

            # Degree class of previous note
            deg_cls = degree_class(note_prev["pitch"], key_pc, is_minor)

            # Beat position of current note
            num, den = get_time_sig_at_onset(ts_map, note_curr["onset"])
            bp_val = beat_position(note_curr["onset"], num, den)

            # Accumulate pitch transition
            row = self.pitch_row_index(step_to_index(prev_step), deg_cls, bp_val)
            col = step_to_index(curr_step)
            self.pitch_counts[row][col] += 1
            self.total_pitch_transitions += 1

        # Duration transitions: need pairs with directed interval class
        for idx in range(1, len(notes)):
            note_prev = notes[idx - 1]
            note_curr = notes[idx]

            key_pc, is_minor = get_key_at_onset(key_segments, note_curr["onset"])
            step = compute_degree_step(
                note_prev["pitch"], note_curr["pitch"], key_pc, is_minor
            )

            dic_val = directed_interval_class(step)
            if dic_val is None:
                # Unison: treat as StepUp (small motion)
                dic_val = DIC_STEP_UP

            prev_dur_cat = duration_category(note_prev["duration"])
            curr_dur_cat = duration_category(note_curr["duration"])

            row = self.dur_row_index(prev_dur_cat, dic_val)
            self.dur_counts[row][curr_dur_cat] += 1
            self.total_dur_transitions += 1


class VerticalTransitionCounter:
    """Accumulates vertical (bass-relative) pitch class transition counts."""

    def __init__(self, name: str):
        self.name = name
        self.vert_counts = [[0] * VERT_COLS for _ in range(VERT_ROWS)]
        self.total_samples = 0
        self.file_count = 0
        self.track_count = 0

    def vert_row_index(
        self, bass_deg: int, beat_pos_val: int, voice_bin: int, harm_func: int
    ) -> int:
        """Compute the row index into the vertical interval table.

        Index = bass_deg * BP_COUNT * VB_COUNT * HF_COUNT
              + beat_pos_val * VB_COUNT * HF_COUNT
              + voice_bin * HF_COUNT
              + harm_func

        Args:
            bass_deg: Bass scale degree (0-6).
            beat_pos_val: Beat position category (BP_BAR..BP_OFF16).
            voice_bin: Voice count bin (0=2v, 1=3v, 2=4+v).
            harm_func: Harmonic function (HF_TONIC, HF_SUBDOMINANT, HF_DOMINANT).

        Returns:
            Row index [0, VERT_ROWS).
        """
        return (
            bass_deg * BP_COUNT * VB_COUNT * HF_COUNT
            + beat_pos_val * VB_COUNT * HF_COUNT
            + voice_bin * HF_COUNT
            + harm_func
        )

    def add_vertical_samples(
        self,
        all_tracks: list[tuple[list[dict], bool]],
        key_segments: list[tuple[float, float, int, bool]],
        ts_map: list[tuple[float, int, int]],
    ) -> None:
        """Process multiple tracks to extract vertical intervals.

        For each upper voice note onset, find the sounding bass note, compute
        the pitch class offset, and accumulate.

        Args:
            all_tracks: List of (notes_list, is_bass_flag) tuples.
            key_segments: Local key segments from estimate_local_keys.
            ts_map: Time signature map from build_time_sig_map.
        """
        if len(all_tracks) < 2:
            return

        self.track_count += 1
        voice_count = len(all_tracks)
        voice_bin = 0 if voice_count <= 2 else (1 if voice_count == 3 else 2)

        # Find bass track (first one marked as bass, or the last track)
        bass_idx = len(all_tracks) - 1
        for idx, (_, is_bass) in enumerate(all_tracks):
            if is_bass:
                bass_idx = idx
                break

        bass_notes = all_tracks[bass_idx][0]
        if not bass_notes:
            return

        # Build sorted bass note list for onset lookup
        bass_sorted = sorted(bass_notes, key=lambda note: note["onset"])

        # Process each upper voice
        for track_idx, (notes, is_bass) in enumerate(all_tracks):
            if track_idx == bass_idx:
                continue

            for note in notes:
                onset = note["onset"]
                pitch = note["pitch"]

                # Find sounding bass at this onset
                bass_pitch = None
                for bass_note in reversed(bass_sorted):
                    if bass_note["onset"] <= onset + 0.01:
                        bass_end = bass_note["onset"] + bass_note["duration"]
                        if bass_end > onset - 0.01:
                            bass_pitch = bass_note["pitch"]
                        break

                if bass_pitch is None:
                    continue

                # Get key at this onset
                key_pc, is_minor = get_key_at_onset(key_segments, onset)

                # Bass scale degree
                scale = MINOR_SCALE if is_minor else MAJOR_SCALE
                bass_rel_pc = (bass_pitch % 12 - key_pc) % 12
                best_bass_deg = 0
                best_dist = 99
                for idx_d, scale_pc in enumerate(scale):
                    dist = min(
                        abs(bass_rel_pc - scale_pc),
                        12 - abs(bass_rel_pc - scale_pc),
                    )
                    if dist < best_dist:
                        best_dist = dist
                        best_bass_deg = idx_d

                # Harmonic function
                harm_func = classify_harm_func(best_bass_deg)

                # Beat position
                num, den = get_time_sig_at_onset(ts_map, onset)
                bp_val = beat_position(onset, num, den)

                # Pitch class offset from bass
                pc_offset = (pitch - bass_pitch) % 12

                # Accumulate
                row = self.vert_row_index(best_bass_deg, bp_val, voice_bin, harm_func)
                if 0 <= row < VERT_ROWS:
                    self.vert_counts[row][pc_offset] += 1
                    self.total_samples += 1


def smooth_and_normalize(counts: list[list[int]], smooth_k: float) -> list[list[int]]:
    """Apply add-k smoothing and normalize rows to sum to NORM_SUM.

    Args:
        counts: 2D list of raw transition counts.
        smooth_k: Smoothing constant to add to each cell.

    Returns:
        2D list of normalized uint16_t values (rows sum to NORM_SUM).
    """
    num_rows = len(counts)
    num_cols = len(counts[0])
    result = [[0] * num_cols for _ in range(num_rows)]

    for row_idx in range(num_rows):
        smoothed = [counts[row_idx][col] + smooth_k for col in range(num_cols)]
        total = sum(smoothed)

        if total < 1e-12:
            # Uniform distribution
            uniform = NORM_SUM // num_cols
            remainder = NORM_SUM - uniform * num_cols
            for col in range(num_cols):
                result[row_idx][col] = uniform + (1 if col < remainder else 0)
            continue

        # Normalize to NORM_SUM
        raw_probs = [val / total * NORM_SUM for val in smoothed]

        # Round using largest-remainder method to ensure exact sum
        floored = [int(prob) for prob in raw_probs]
        remainders = [(raw_probs[col] - floored[col], col) for col in range(num_cols)]
        deficit = NORM_SUM - sum(floored)

        # Sort by remainder descending, distribute deficit
        remainders.sort(key=lambda entry: -entry[0])
        for rank in range(deficit):
            floored[remainders[rank][1]] += 1

        for col in range(num_cols):
            result[row_idx][col] = floored[col]

    return result


# ---------------------------------------------------------------------------
# File processing
# ---------------------------------------------------------------------------

def load_reference_file(filepath: Path) -> Optional[dict]:
    """Load and validate a reference JSON file.

    Args:
        filepath: Path to the JSON file.

    Returns:
        Parsed JSON dict, or None if the file is invalid.
    """
    try:
        with open(filepath, "r", encoding="utf-8") as json_file:
            data = json.load(json_file)
    except (json.JSONDecodeError, OSError) as err:
        print(f"  WARNING: Failed to load {filepath.name}: {err}", file=sys.stderr)
        return None

    required_keys = {"category", "ticks_per_beat", "time_signatures", "tracks"}
    if not required_keys.issubset(data.keys()):
        missing = required_keys - data.keys()
        print(f"  WARNING: {filepath.name} missing keys: {missing}", file=sys.stderr)
        return None

    return data


def prepare_notes(notes: list[dict]) -> list[dict]:
    """Filter and sort notes for transition extraction.

    Removes tie notes (velocity=1), sorts by onset then pitch (for chords).

    Args:
        notes: Raw note list from JSON.

    Returns:
        Filtered and sorted note list.
    """
    # Filter out ties
    filtered = [note for note in notes if note.get("velocity", 100) != 1]

    # Sort by onset, then by pitch (lowest first for chords)
    filtered.sort(key=lambda note: (note["onset"], note["pitch"]))

    return filtered


def process_file(
    filepath: Path,
    counters: dict[str, TransitionCounter],
    key_signatures: dict[str, dict],
    vert_counter: Optional["VerticalTransitionCounter"] = None,
) -> None:
    """Process a single reference JSON file and accumulate transitions.

    Args:
        filepath: Path to the JSON file.
        counters: Dict mapping model name to TransitionCounter.
        key_signatures: Key signature lookup from key_signatures.json.
        vert_counter: Optional vertical interval counter for fugue files.
    """
    data = load_reference_file(filepath)
    if data is None:
        return

    category = data["category"]
    voice_count = data.get("voice_count", 1)
    ticks_per_beat = data["ticks_per_beat"]
    tracks = data.get("tracks", [])
    time_signatures = data.get("time_signatures", [{"tick": 0, "numerator": 4, "denominator": 4}])
    total_tracks = len(tracks)

    # Build time signature map
    ts_map = build_time_sig_map(time_signatures, ticks_per_beat)

    # Determine which counters to update
    is_fugue = category in FUGUE_CATEGORIES
    is_cello = category in CELLO_CATEGORIES
    is_violin = category in VIOLIN_CATEGORIES
    is_toccata = category in TOCCATA_CATEGORIES and not any(
        filepath.stem.endswith(suffix) for suffix in TOCCATA_EXCLUDE_SUFFIXES)

    if not (is_fugue or is_cello or is_violin or is_toccata):
        return

    # Get global key from key_signatures.json for initial key estimation fallback
    work_id = filepath.stem
    global_key_pc = 0
    global_is_minor = False
    if work_id in key_signatures:
        ks_entry = key_signatures[work_id]
        tonic = ks_entry.get("tonic", "C")
        mode = ks_entry.get("mode", "major")
        global_key_pc = TONIC_TO_PC.get(tonic, 0)
        global_is_minor = (mode == "minor")

    # Collect all track data for vertical interval extraction
    vert_all_tracks: list[tuple[list[dict], bool]] = []

    for track_idx, track in enumerate(tracks):
        role = track.get("role", "")
        raw_notes = track.get("notes", [])
        notes = prepare_notes(raw_notes)

        if len(notes) < 3:
            continue

        # Estimate local keys for this track
        if time_signatures:
            first_num = time_signatures[0].get("numerator", 4)
            first_den = time_signatures[0].get("denominator", 4)
        else:
            first_num = 4
            first_den = 4

        key_segments = estimate_local_keys(notes, first_num, first_den)

        # If key estimation returned only defaults, use global key
        if len(key_segments) == 1 and key_segments[0][2] == 0 and not key_segments[0][3]:
            # Check if global key is different
            if global_key_pc != 0 or global_is_minor:
                total_end = notes[-1]["onset"] + notes[-1]["duration"]
                key_segments = [(notes[0]["onset"], total_end, global_key_pc, global_is_minor)]

        if is_fugue:
            is_pedal = is_pedal_track(role, track_idx, voice_count, total_tracks)
            if is_pedal:
                counters["FuguePedal"].add_track(notes, key_segments, ts_map)
            else:
                counters["FugueUpper"].add_track(notes, key_segments, ts_map)

            # Collect track for vertical interval extraction
            vert_all_tracks.append((notes, is_pedal))

        if is_cello:
            counters["Cello"].add_track(notes, key_segments, ts_map)

        if is_violin:
            counters["Violin"].add_track(notes, key_segments, ts_map)

        if is_toccata:
            is_pedal = is_pedal_track(role, track_idx, voice_count, total_tracks)
            if is_pedal:
                counters["ToccataPedal"].add_track(notes, key_segments, ts_map)
            else:
                counters["ToccataUpper"].add_track(notes, key_segments, ts_map)

    # Vertical interval extraction for fugues with 2+ voices
    if is_fugue and vert_counter is not None and voice_count >= 2 and len(vert_all_tracks) >= 2:
        # Use key segments from the first track for vertical analysis
        first_track_notes = vert_all_tracks[0][0]
        if time_signatures:
            first_num = time_signatures[0].get("numerator", 4)
            first_den = time_signatures[0].get("denominator", 4)
        else:
            first_num = 4
            first_den = 4
        vert_key_segments = estimate_local_keys(first_track_notes, first_num, first_den)
        if len(vert_key_segments) == 1 and vert_key_segments[0][2] == 0 and not vert_key_segments[0][3]:
            if global_key_pc != 0 or global_is_minor:
                total_end = first_track_notes[-1]["onset"] + first_track_notes[-1]["duration"]
                vert_key_segments = [
                    (first_track_notes[0]["onset"], total_end, global_key_pc, global_is_minor)
                ]
        vert_counter.add_vertical_samples(vert_all_tracks, vert_key_segments, ts_map)
        vert_counter.file_count += 1

    # Increment file counts
    if is_fugue:
        counters["FugueUpper"].file_count += 1
        counters["FuguePedal"].file_count += 1
    if is_cello:
        counters["Cello"].file_count += 1
    if is_violin:
        counters["Violin"].file_count += 1
    if is_toccata:
        counters["ToccataUpper"].file_count += 1
        counters["ToccataPedal"].file_count += 1


# ---------------------------------------------------------------------------
# C++ output generation
# ---------------------------------------------------------------------------

def format_row(values: list[int], indent: str = "  ") -> str:
    """Format a single row of values as a C++ initializer.

    Args:
        values: List of integer values.
        indent: Indentation string.

    Returns:
        Formatted C++ initializer string like '  {526, 526, 526, ...},'.
    """
    parts = ", ".join(str(val) for val in values)
    return f"{indent}{{{parts}}},"


def format_table(
    name: str,
    values: list[list[int]],
    num_rows: int,
    num_cols: int,
    row_comment_fn=None,
) -> str:
    """Format a complete C++ constexpr table.

    Args:
        name: C++ variable name.
        values: 2D list of values.
        num_rows: Number of rows.
        num_cols: Number of columns.
        row_comment_fn: Optional function(row_index) -> comment string.

    Returns:
        Complete C++ table declaration string.
    """
    lines = []
    lines.append(f"static constexpr uint16_t {name}[{num_rows}][{num_cols}] = {{")
    for row_idx in range(num_rows):
        comment = ""
        if row_comment_fn:
            comment = row_comment_fn(row_idx)
        row_str = format_row(values[row_idx])
        if comment:
            lines.append(f"{row_str}  // {comment}")
        else:
            lines.append(row_str)
    lines.append("};")
    return "\n".join(lines)


def pitch_row_comment(row_idx: int) -> str:
    """Generate a comment describing a pitch table row.

    Args:
        row_idx: Row index [0, PITCH_ROWS).

    Returns:
        Human-readable description like 'ps=-9 dc=Stable bp=Bar'.
    """
    bp_val = row_idx % BP_COUNT
    remaining = row_idx // BP_COUNT
    dc_val = remaining % DC_COUNT
    ps_val = remaining // DC_COUNT

    step = ps_val - 9
    dc_names = ["Stable", "Dominant", "Motion"]
    bp_names = ["Bar", "Beat", "Off8", "Off16"]

    return f"ps={step:+d} dc={dc_names[dc_val]} bp={bp_names[bp_val]}"


def dur_row_comment(row_idx: int) -> str:
    """Generate a comment describing a duration table row.

    Args:
        row_idx: Row index [0, DUR_ROWS).

    Returns:
        Human-readable description like 'dur=S16 dic=StepUp'.
    """
    dic_val = row_idx % DIC_COUNT
    dur_val = row_idx // DIC_COUNT

    dur_names = ["S16", "S8", "Dot8", "Qtr", "HalfPlus"]
    dic_names = ["StepUp", "StepDown", "SkipUp", "SkipDown", "LeapUp", "LeapDown"]

    return f"dur={dur_names[dur_val]} dic={dic_names[dic_val]}"


def vert_row_comment(row_idx: int) -> str:
    """Generate a comment describing a vertical interval table row.

    Args:
        row_idx: Row index [0, VERT_ROWS).

    Returns:
        Human-readable description like 'bd=0 bp=Bar vb=2v hf=T'.
    """
    hf_val = row_idx % HF_COUNT
    remaining = row_idx // HF_COUNT
    vb_val = remaining % VB_COUNT
    remaining = remaining // VB_COUNT
    bp_val = remaining % BP_COUNT
    bd_val = remaining // BP_COUNT

    bp_names = ["Bar", "Beat", "Off8", "Off16"]
    vb_names = ["2v", "3v", "4v"]
    hf_names = ["T", "S", "D"]

    return f"bd={bd_val} bp={bp_names[bp_val]} vb={vb_names[vb_val]} hf={hf_names[hf_val]}"


def generate_cpp_output(
    counters: dict[str, TransitionCounter],
    vert_counter: Optional["VerticalTransitionCounter"] = None,
) -> str:
    """Generate the complete C++ source file content.

    Args:
        counters: Dict mapping model name to TransitionCounter.
        vert_counter: Optional vertical interval counter for fugue files.

    Returns:
        Complete C++ source file content as a string.
    """
    sections = []

    sections.append("// Auto-generated by scripts/extract_markov_tables.py")
    sections.append("// Do not edit manually.")
    sections.append("")
    sections.append("#include <cstdint>")
    sections.append("")
    sections.append("namespace markov_data {")
    sections.append("")

    model_order = ["FugueUpper", "FuguePedal", "Cello", "Violin", "ToccataUpper", "ToccataPedal"]
    cpp_names = {
        "FugueUpper": "kFugueUpper",
        "FuguePedal": "kFuguePedal",
        "Cello": "kCello",
        "Violin": "kViolin",
        "ToccataUpper": "kToccataUpper",
        "ToccataPedal": "kToccataPedal",
    }

    for model_name in model_order:
        counter = counters[model_name]
        cpp_base = cpp_names[model_name]

        # Pitch table
        normalized_pitch = smooth_and_normalize(counter.pitch_counts, SMOOTH_K)
        sections.append(f"// {model_name} pitch transitions")
        sections.append(
            f"// [{STEP_COUNT} prev_step * {DC_COUNT} degree_class * {BP_COUNT} beat_pos]"
            f"[{STEP_COUNT} next_step]"
        )
        sections.append(
            f"// Sources: {counter.file_count} files, {counter.track_count} tracks, "
            f"{counter.total_pitch_transitions} transitions"
        )
        sections.append(
            format_table(
                f"{cpp_base}Pitch",
                normalized_pitch,
                PITCH_ROWS,
                PITCH_COLS,
                pitch_row_comment,
            )
        )
        sections.append("")

        # Duration table
        normalized_dur = smooth_and_normalize(counter.dur_counts, SMOOTH_K)
        sections.append(f"// {model_name} duration transitions")
        sections.append(
            f"// [{DUR_COUNT} prev_duration * {DIC_COUNT} directed_interval_class]"
            f"[{DUR_COUNT} next_duration]"
        )
        sections.append(
            f"// Sources: {counter.total_dur_transitions} transitions"
        )
        sections.append(
            format_table(
                f"{cpp_base}Dur",
                normalized_dur,
                DUR_ROWS,
                DUR_COLS,
                dur_row_comment,
            )
        )
        sections.append("")

    # Vertical interval table (fugue only)
    if vert_counter is not None:
        normalized_vert = smooth_and_normalize(vert_counter.vert_counts, SMOOTH_K)
        sections.append(f"// FugueVertical interval transitions")
        sections.append(
            f"// [7 bass_degree * {BP_COUNT} beat_pos * {VB_COUNT} voice_bin"
            f" * {HF_COUNT} harm_func][{PC_OFFSET_COUNT} pc_offset]"
        )
        sections.append(
            f"// Sources: {vert_counter.file_count} files, "
            f"{vert_counter.track_count} tracks, "
            f"{vert_counter.total_samples} samples"
        )
        sections.append(
            format_table(
                "kFugueVertical",
                normalized_vert,
                VERT_ROWS,
                VERT_COLS,
                vert_row_comment,
            )
        )
        sections.append("")

    sections.append("}  // namespace markov_data")
    sections.append("")

    return "\n".join(sections)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def load_key_signatures(data_dir: Path) -> dict[str, dict]:
    """Load key_signatures.json from the data directory.

    Args:
        data_dir: Path to the reference data directory.

    Returns:
        Dict mapping work_id to key signature info, or empty dict on failure.
    """
    ks_path = data_dir / "key_signatures.json"
    if not ks_path.exists():
        print(f"  WARNING: key_signatures.json not found at {ks_path}", file=sys.stderr)
        print("  Local key estimation will be used for all files.", file=sys.stderr)
        return {}

    try:
        with open(ks_path, "r", encoding="utf-8") as ks_file:
            return json.load(ks_file)
    except (json.JSONDecodeError, OSError) as err:
        print(f"  WARNING: Failed to load key_signatures.json: {err}", file=sys.stderr)
        return {}


def main() -> int:
    """Entry point: extract Markov tables and write C++ output."""
    parser = argparse.ArgumentParser(
        description="Extract Markov transition tables from Bach reference JSON files."
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=Path("data/reference"),
        help="Directory containing BWV*.json reference files (default: data/reference)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("src/core/markov_tables_data.inc"),
        help="Output C++ file path (default: src/core/markov_tables_data.inc)",
    )
    args = parser.parse_args()

    data_dir = args.data_dir
    output_path = args.output

    if not data_dir.exists():
        print(f"ERROR: Data directory not found: {data_dir}", file=sys.stderr)
        return 1

    # Find all reference files
    reference_files = sorted(data_dir.glob("BWV*.json"))
    if not reference_files:
        print(f"ERROR: No BWV*.json files found in {data_dir}", file=sys.stderr)
        return 1

    print(f"Found {len(reference_files)} reference files in {data_dir}", file=sys.stderr)

    # Load key signatures
    key_signatures = load_key_signatures(data_dir)
    if key_signatures:
        print(f"Loaded {len(key_signatures)} key signatures", file=sys.stderr)

    # Initialize counters
    counters = {
        "FugueUpper": TransitionCounter("FugueUpper"),
        "FuguePedal": TransitionCounter("FuguePedal"),
        "Cello": TransitionCounter("Cello"),
        "Violin": TransitionCounter("Violin"),
        "ToccataUpper": TransitionCounter("ToccataUpper"),
        "ToccataPedal": TransitionCounter("ToccataPedal"),
    }
    vert_counter = VerticalTransitionCounter("FugueVertical")

    # Count files per relevant category
    category_counts: dict[str, int] = {}
    skipped_count = 0

    for filepath in reference_files:
        # Quick category check before full processing
        try:
            with open(filepath, "r", encoding="utf-8") as quick_file:
                data = json.load(quick_file)
                category = data.get("category", "")
        except (json.JSONDecodeError, OSError):
            skipped_count += 1
            continue

        all_categories = FUGUE_CATEGORIES | CELLO_CATEGORIES | VIOLIN_CATEGORIES | TOCCATA_CATEGORIES
        if category not in all_categories:
            skipped_count += 1
            continue

        category_counts[category] = category_counts.get(category, 0) + 1
        process_file(filepath, counters, key_signatures, vert_counter)

    # Print progress summary
    print("", file=sys.stderr)
    print("Files per category:", file=sys.stderr)
    for cat in sorted(category_counts):
        models = []
        if cat in FUGUE_CATEGORIES:
            models.append("Fugue")
        if cat in CELLO_CATEGORIES:
            models.append("Cello")
        if cat in VIOLIN_CATEGORIES:
            models.append("Violin")
        if cat in TOCCATA_CATEGORIES:
            models.append("Toccata (non-fugue movements)")
        model = ", ".join(models) if models else "Unknown"
        print(f"  {cat}: {category_counts[cat]} files -> {model}", file=sys.stderr)
    print(f"  (skipped {skipped_count} files from other categories)", file=sys.stderr)

    print("", file=sys.stderr)
    print("Transition counts:", file=sys.stderr)
    for model_name in ["FugueUpper", "FuguePedal", "Cello", "Violin",
                        "ToccataUpper", "ToccataPedal"]:
        counter = counters[model_name]
        print(
            f"  {model_name}: {counter.file_count} files, {counter.track_count} tracks, "
            f"{counter.total_pitch_transitions} pitch trans, "
            f"{counter.total_dur_transitions} dur trans",
            file=sys.stderr,
        )
    print(
        f"  FugueVertical: {vert_counter.file_count} files, "
        f"{vert_counter.track_count} tracks, "
        f"{vert_counter.total_samples} samples",
        file=sys.stderr,
    )

    # Smooth, normalize, and verify
    print("", file=sys.stderr)
    print("Row sum verification:", file=sys.stderr)
    all_ok = True

    for model_name in ["FugueUpper", "FuguePedal", "Cello", "Violin",
                        "ToccataUpper", "ToccataPedal"]:
        counter = counters[model_name]

        pitch_norm = smooth_and_normalize(counter.pitch_counts, SMOOTH_K)
        dur_norm = smooth_and_normalize(counter.dur_counts, SMOOTH_K)

        # Verify all rows sum to NORM_SUM
        pitch_ok = all(sum(row) == NORM_SUM for row in pitch_norm)
        dur_ok = all(sum(row) == NORM_SUM for row in dur_norm)

        status_pitch = "OK" if pitch_ok else "FAIL"
        status_dur = "OK" if dur_ok else "FAIL"

        print(
            f"  {model_name}: pitch[{PITCH_ROWS}x{PITCH_COLS}]={status_pitch}, "
            f"dur[{DUR_ROWS}x{DUR_COLS}]={status_dur}",
            file=sys.stderr,
        )

        if not pitch_ok or not dur_ok:
            all_ok = False
            # Print failing rows for diagnosis
            for row_idx, row in enumerate(pitch_norm):
                row_sum = sum(row)
                if row_sum != NORM_SUM:
                    print(
                        f"    PITCH row {row_idx}: sum={row_sum} (expected {NORM_SUM})",
                        file=sys.stderr,
                    )
            for row_idx, row in enumerate(dur_norm):
                row_sum = sum(row)
                if row_sum != NORM_SUM:
                    print(
                        f"    DUR row {row_idx}: sum={row_sum} (expected {NORM_SUM})",
                        file=sys.stderr,
                    )

    # Verify vertical table
    vert_norm = smooth_and_normalize(vert_counter.vert_counts, SMOOTH_K)
    vert_ok = all(sum(row) == NORM_SUM for row in vert_norm)
    status_vert = "OK" if vert_ok else "FAIL"
    print(
        f"  FugueVertical: vert[{VERT_ROWS}x{VERT_COLS}]={status_vert}",
        file=sys.stderr,
    )
    if not vert_ok:
        all_ok = False
        for row_idx, row in enumerate(vert_norm):
            row_sum = sum(row)
            if row_sum != NORM_SUM:
                print(
                    f"    VERT row {row_idx}: sum={row_sum} (expected {NORM_SUM})",
                    file=sys.stderr,
                )

    if not all_ok:
        print("", file=sys.stderr)
        print("ERROR: Row sum verification failed.", file=sys.stderr)
        return 1

    # Generate C++ output
    cpp_content = generate_cpp_output(counters, vert_counter)

    # Write output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as out_file:
        out_file.write(cpp_content)

    print("", file=sys.stderr)
    print(f"Wrote {output_path} ({len(cpp_content)} bytes)", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
