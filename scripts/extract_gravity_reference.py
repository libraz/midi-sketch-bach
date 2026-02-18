#!/usr/bin/env python3
"""Extract gravity reference data from Bach corpus for GravityConfig system.

Reads pre-computed category statistics from organ_fugue.json and individual
BWV*_fugue.json reference files. Generates C++ constexpr .inc files with
uint16_t normalized probability distributions (x10000, largest-remainder
rounding).

Two output files:
  - gravity_reference_data.inc: rhythm, harmony, NCT, motion, interval,
    vertical, texture, and bass strong-beat targets
  - harmonic_bigram_data.inc: degree-to-degree harmonic transition matrix

Usage:
    python3 scripts/extract_gravity_reference.py
    python3 scripts/extract_gravity_reference.py --output-dir src/core
    python3 -m scripts.extract_gravity_reference
"""

import argparse
import json
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Normalization target (stored as uint16_t, rows sum to this)
NORM_SUM = 10000

# Ticks per beat in our internal representation
TICKS_PER_BEAT = 480

# Pedal/bass role identifiers
PEDAL_ROLES = frozenset({"pedal", "v4"})

# Rhythm category labels (ordered for kRhythmTarget[7])
RHYTHM_LABELS = ["32nd", "16th", "8th", "quarter", "half", "whole", "longer"]

# NCT type labels (ordered for kNctTypeTarget[4])
NCT_LABELS = ["passing", "neighbor", "ornamental", "other"]

# Motion type labels (ordered for kMotionTarget[4])
MOTION_LABELS = ["oblique", "contrary", "similar", "parallel"]

# Interval labels P1 through M7 (ordered for kIntervalTarget[12])
INTERVAL_LABELS = ["P1", "m2", "M2", "m3", "M3", "P4", "TT", "P5", "m6", "M6", "m7", "M7"]

# Texture labels: 0-4 voices active (ordered for kTextureTarget[5])
TEXTURE_LABELS = ["0", "1", "2", "3", "4"]

# Maximum harmony degrees to keep
MAX_HARMONY_DEGREES = 12

# Major and minor scales for degree classification
MAJOR_SCALE = [0, 2, 4, 5, 7, 9, 11]
MINOR_SCALE = [0, 2, 3, 5, 7, 8, 10]

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


# ---------------------------------------------------------------------------
# Normalization (largest-remainder rounding, matching extract_markov_tables.py)
# ---------------------------------------------------------------------------

def smooth_and_normalize_1d(raw_values: list[float], smooth_k: float = 1.0) -> list[int]:
    """Apply add-k smoothing and normalize to sum to NORM_SUM.

    For probability inputs (values summing to ~1.0), the values are scaled up
    by NORM_SUM before smoothing so the add-1 constant has proportional effect
    (roughly 1/10000 floor per bin). For count inputs (sum >> 1), values are
    used directly.

    Args:
        raw_values: Raw probability or count values.
        smooth_k: Smoothing constant added to each bin (add-1 default).

    Returns:
        List of uint16_t values summing to NORM_SUM.
    """
    num_cols = len(raw_values)
    # Scale up probability-like inputs so add-1 smoothing is proportional
    total_raw = sum(raw_values)
    if 0.0 < total_raw < 2.0:
        # Input is probabilities (sum ~1.0): scale to count-like values
        scale = NORM_SUM / total_raw
        scaled = [val * scale for val in raw_values]
    else:
        scaled = list(raw_values)
    smoothed = [val + smooth_k for val in scaled]
    total = sum(smoothed)

    if total < 1e-12:
        uniform = NORM_SUM // num_cols
        remainder = NORM_SUM - uniform * num_cols
        return [uniform + (1 if idx < remainder else 0) for idx in range(num_cols)]

    raw_probs = [val / total * NORM_SUM for val in smoothed]
    floored = [int(prob) for prob in raw_probs]
    remainders = [(raw_probs[col] - floored[col], col) for col in range(num_cols)]
    deficit = NORM_SUM - sum(floored)
    remainders.sort(key=lambda entry: -entry[0])
    for rank in range(deficit):
        floored[remainders[rank][1]] += 1

    return floored


def smooth_and_normalize_2d(
    counts: list[list[float]], smooth_k: float = 1.0
) -> list[list[int]]:
    """Apply add-k smoothing and normalize each row to sum to NORM_SUM.

    Args:
        counts: 2D list of raw count values.
        smooth_k: Smoothing constant added to each cell.

    Returns:
        2D list of uint16_t values (each row sums to NORM_SUM).
    """
    return [smooth_and_normalize_1d(row, smooth_k) for row in counts]


# ---------------------------------------------------------------------------
# Category data extraction (items 1-7 from organ_fugue.json)
# ---------------------------------------------------------------------------

def extract_category_distributions(category_path: Path) -> dict:
    """Read organ_fugue.json and extract all distribution arrays.

    Args:
        category_path: Path to organ_fugue.json.

    Returns:
        Dict with keys: rhythm, harmony_degrees, nct_types, motion,
        interval, vertical, texture, work_count.
    """
    with open(category_path, "r", encoding="utf-8") as json_file:
        data = json.load(json_file)

    distributions = data["distributions"]
    work_count = data.get("work_count", 0)

    # 1. Rhythm distribution -> kRhythmTarget[7]
    rhythm_raw = [distributions["rhythm"].get(label, 0.0) for label in RHYTHM_LABELS]

    # 2. Harmony degrees -> kHarmonyDegreeTarget[N] (top 12)
    harmony_dict = distributions["harmony_degrees"]
    sorted_degrees = sorted(harmony_dict.items(), key=lambda item: -item[1])
    top_degrees = sorted_degrees[:MAX_HARMONY_DEGREES]
    harmony_labels = [entry[0] for entry in top_degrees]
    harmony_raw = [entry[1] for entry in top_degrees]

    # 3. NCT types -> kNctTypeTarget[4]
    nct_raw = [distributions["nct_types"].get(label, 0.0) for label in NCT_LABELS]

    # 4. Motion -> kMotionTarget[4]
    motion_raw = [distributions["motion"].get(label, 0.0) for label in MOTION_LABELS]

    # 5. Interval -> kIntervalTarget[12]
    interval_raw = [distributions["interval"].get(label, 0.0) for label in INTERVAL_LABELS]

    # 6. Vertical -> kVerticalTarget[12]
    vertical_raw = [distributions["vertical"].get(label, 0.0) for label in INTERVAL_LABELS]

    # 7. Texture -> kTextureTarget[5]
    texture_raw = [distributions["texture"].get(label, 0.0) for label in TEXTURE_LABELS]

    return {
        "rhythm": rhythm_raw,
        "harmony_degrees": harmony_raw,
        "harmony_labels": harmony_labels,
        "nct_types": nct_raw,
        "motion": motion_raw,
        "interval": interval_raw,
        "vertical": vertical_raw,
        "texture": texture_raw,
        "work_count": work_count,
    }


# ---------------------------------------------------------------------------
# Key estimation helpers
# ---------------------------------------------------------------------------

def pearson_correlation(vec_a: list[float], vec_b: list[float]) -> float:
    """Compute Pearson correlation between two equal-length vectors.

    Args:
        vec_a: First vector.
        vec_b: Second vector.

    Returns:
        Pearson correlation coefficient in [-1, +1].
    """
    import math
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
    """Rotate a 12-element pitch class profile by shift semitones."""
    return [profile[(idx + shift) % 12] for idx in range(12)]


def estimate_key_from_notes(notes: list[dict]) -> tuple[int, bool]:
    """Estimate the key of a note sequence using Krumhansl-Kessler.

    Args:
        notes: List of note dicts with 'pitch' and 'duration'.

    Returns:
        (key_pitch_class, is_minor) tuple.
    """
    if not notes:
        return 0, False

    histogram = [0.0] * 12
    for note in notes:
        pitch_class = note["pitch"] % 12
        histogram[pitch_class] += note.get("duration", 1.0)

    best_corr = -2.0
    best_key = 0
    best_minor = False

    for key_pc in range(12):
        corr_major = pearson_correlation(histogram, rotate_profile(KK_MAJOR, key_pc))
        if corr_major > best_corr:
            best_corr = corr_major
            best_key = key_pc
            best_minor = False

        corr_minor = pearson_correlation(histogram, rotate_profile(KK_MINOR, key_pc))
        if corr_minor > best_corr:
            best_corr = corr_minor
            best_key = key_pc
            best_minor = True

    return best_key, best_minor


def pitch_to_scale_degree(pitch: int, key: int, is_minor: bool) -> int:
    """Convert MIDI pitch to scale degree (0-6).

    Args:
        pitch: MIDI pitch number.
        key: Key root as pitch class (0=C..11=B).
        is_minor: True for minor key.

    Returns:
        Scale degree 0-6.
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

    return best_deg


# ---------------------------------------------------------------------------
# Individual work extraction (items 8-9)
# ---------------------------------------------------------------------------

def extract_harmonic_bigrams(
    reference_files: list[Path],
    key_signatures: dict[str, dict],
) -> list[list[float]]:
    """Extract chord-to-chord bigram transitions from individual fugue files.

    Uses pitch class transitions at beat boundaries (every ticks_per_beat).
    Simplification: the chord at each beat is the most common pitch class
    from all notes sounding at that beat position across all tracks.

    Args:
        reference_files: List of paths to BWV*_fugue.json files.
        key_signatures: Key signature lookup from key_signatures.json.

    Returns:
        12x12 raw count matrix [from_degree][to_degree].
    """
    bigram_counts = [[0.0] * 12 for _ in range(12)]
    files_used = 0

    for filepath in reference_files:
        try:
            with open(filepath, "r", encoding="utf-8") as json_file:
                data = json.load(json_file)
        except (json.JSONDecodeError, OSError) as err:
            print(f"  WARNING: Failed to load {filepath.name}: {err}", file=sys.stderr)
            continue

        tracks = data.get("tracks", [])
        if not tracks:
            continue

        ticks_per_beat = data.get("ticks_per_beat", 480)

        # Collect all notes across tracks, converting onset to beat-aligned ticks
        all_notes = []
        for track in tracks:
            for note in track.get("notes", []):
                if note.get("velocity", 100) == 1:
                    continue  # skip ties
                all_notes.append(note)

        if len(all_notes) < 4:
            continue

        # Determine key from key_signatures.json or estimate
        work_id = filepath.stem
        if work_id in key_signatures:
            ks_entry = key_signatures[work_id]
            key_pc = TONIC_TO_PC.get(ks_entry.get("tonic", "C"), 0)
            is_minor = ks_entry.get("mode", "major") == "minor"
        else:
            key_pc, is_minor = estimate_key_from_notes(all_notes)

        # Build beat-aligned chord sequence: for each beat position, find the
        # dominant pitch class from notes sounding at that time
        max_onset = max(note["onset"] for note in all_notes)
        num_beats = int(max_onset) + 2  # +2 for safety

        # For each beat, accumulate pitch classes weighted by duration overlap
        beat_degrees = []
        for beat_idx in range(num_beats):
            beat_start = float(beat_idx)
            beat_end = beat_start + 1.0

            pc_weight = [0.0] * 12
            for note in all_notes:
                note_start = note["onset"]
                note_end = note_start + note["duration"]
                # Check overlap with this beat
                overlap_start = max(note_start, beat_start)
                overlap_end = min(note_end, beat_end)
                if overlap_end > overlap_start:
                    overlap = overlap_end - overlap_start
                    relative_pc = (note["pitch"] % 12 - key_pc) % 12
                    pc_weight[relative_pc] += overlap

            # Find dominant pitch class -> infer scale degree
            total_weight = sum(pc_weight)
            if total_weight < 0.01:
                beat_degrees.append(-1)  # silence
                continue

            dominant_pc = max(range(12), key=lambda idx: pc_weight[idx])
            # Map pitch class to scale degree
            scale = MINOR_SCALE if is_minor else MAJOR_SCALE
            best_deg = 0
            best_dist = 99
            for deg_idx, scale_pc in enumerate(scale):
                dist = min(abs(dominant_pc - scale_pc), 12 - abs(dominant_pc - scale_pc))
                if dist < best_dist:
                    best_dist = dist
                    best_deg = deg_idx
            beat_degrees.append(best_deg)

        # Extract bigrams from consecutive non-silent beats
        prev_degree = -1
        for degree in beat_degrees:
            if degree < 0:
                prev_degree = -1
                continue
            if prev_degree >= 0:
                bigram_counts[prev_degree][degree] += 1
            prev_degree = degree

        files_used += 1

    print(f"  Harmonic bigrams: {files_used} files processed", file=sys.stderr)
    return bigram_counts


def extract_bass_strong_beat(
    reference_files: list[Path],
    key_signatures: dict[str, dict],
) -> list[float]:
    """Extract bass strong-beat interval distribution from pedal/v4 tracks.

    Args:
        reference_files: List of paths to BWV*_fugue.json files.
        key_signatures: Key signature lookup from key_signatures.json.

    Returns:
        12-element raw count array [P1..M7] of intervals on strong beats.
    """
    interval_counts = [0.0] * 12
    files_used = 0

    for filepath in reference_files:
        try:
            with open(filepath, "r", encoding="utf-8") as json_file:
                data = json.load(json_file)
        except (json.JSONDecodeError, OSError) as err:
            print(f"  WARNING: Failed to load {filepath.name}: {err}", file=sys.stderr)
            continue

        tracks = data.get("tracks", [])
        voice_count = data.get("voice_count", 1)
        total_tracks = len(tracks)
        ticks_per_beat = data.get("ticks_per_beat", 480)

        # Find pedal/bass tracks
        bass_notes = []
        for track_idx, track in enumerate(tracks):
            role = track.get("role", "")
            is_pedal = role in PEDAL_ROLES
            if not is_pedal and voice_count >= 3 and track_idx == total_tracks - 1:
                is_pedal = True
            if is_pedal:
                for note in track.get("notes", []):
                    if note.get("velocity", 100) != 1:
                        bass_notes.append(note)

        if len(bass_notes) < 3:
            continue

        bass_notes.sort(key=lambda note: note["onset"])

        # Determine key
        work_id = filepath.stem
        if work_id in key_signatures:
            ks_entry = key_signatures[work_id]
            key_pc = TONIC_TO_PC.get(ks_entry.get("tonic", "C"), 0)
            is_minor = ks_entry.get("mode", "major") == "minor"
        else:
            key_pc, is_minor = estimate_key_from_notes(bass_notes)

        # Get time signature for bar length computation
        time_sigs = data.get("time_signatures",
                             [{"tick": 0, "numerator": 4, "denominator": 4}])
        first_num = time_sigs[0].get("numerator", 4)
        first_den = time_sigs[0].get("denominator", 4)
        bar_length = first_num * (4.0 / first_den)

        # Filter to strong-beat notes (beat 0 or beat 2 in 4/4)
        # Strong beats: position within bar is 0 or half of bar_length
        epsilon = 0.05
        prev_pitch = -1
        for note in bass_notes:
            pos_in_bar = note["onset"] % bar_length
            is_strong = (abs(pos_in_bar) < epsilon
                         or abs(pos_in_bar - bar_length) < epsilon)
            if first_num == 4 and first_den == 4:
                # Also beat 3 (half bar) is strong
                is_strong = is_strong or abs(pos_in_bar - 2.0) < epsilon

            if not is_strong:
                continue

            if prev_pitch >= 0:
                interval_semitones = abs(note["pitch"] - prev_pitch) % 12
                interval_counts[interval_semitones] += 1

            prev_pitch = note["pitch"]

        files_used += 1

    print(f"  Bass strong-beat intervals: {files_used} files processed", file=sys.stderr)
    return interval_counts


# ---------------------------------------------------------------------------
# C++ output formatting
# ---------------------------------------------------------------------------

def format_1d_array(
    name: str,
    values: list[int],
    size: int,
    comment: str,
    label_list: list[str] | None = None,
) -> str:
    """Format a 1D constexpr array declaration.

    Args:
        name: C++ variable name.
        values: Normalized uint16_t values.
        size: Array size.
        comment: Source comment.
        label_list: Optional per-element labels for inline comments.

    Returns:
        C++ array declaration string.
    """
    lines = []
    lines.append(f"// {comment}")
    if label_list:
        # Format with per-element comments
        lines.append(f"static constexpr uint16_t {name}[{size}] = {{")
        for idx, val in enumerate(values):
            label = label_list[idx] if idx < len(label_list) else f"[{idx}]"
            comma = "," if idx < len(values) - 1 else ""
            lines.append(f"  {val}{comma}  // {label}")
        lines.append("};")
    else:
        parts = ", ".join(str(val) for val in values)
        lines.append(f"static constexpr uint16_t {name}[{size}] = {{{parts}}};")
    return "\n".join(lines)


def format_2d_array(
    name: str,
    values: list[list[int]],
    rows: int,
    cols: int,
    comment: str,
    row_comment_fn=None,
) -> str:
    """Format a 2D constexpr array declaration.

    Args:
        name: C++ variable name.
        values: 2D normalized uint16_t values.
        rows: Number of rows.
        cols: Number of columns.
        comment: Source comment.
        row_comment_fn: Optional function(row_idx) -> comment string.

    Returns:
        C++ 2D array declaration string.
    """
    lines = []
    lines.append(f"// {comment}")
    lines.append(f"static constexpr uint16_t {name}[{rows}][{cols}] = {{")
    for row_idx in range(rows):
        parts = ", ".join(str(val) for val in values[row_idx])
        row_str = f"  {{{parts}}},"
        if row_comment_fn:
            row_str += f"  // {row_comment_fn(row_idx)}"
        lines.append(row_str)
    lines.append("};")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Key signatures loader
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
        return {}

    try:
        with open(ks_path, "r", encoding="utf-8") as ks_file:
            return json.load(ks_file)
    except (json.JSONDecodeError, OSError) as err:
        print(f"  WARNING: Failed to load key_signatures.json: {err}", file=sys.stderr)
        return {}


# ---------------------------------------------------------------------------
# Output generation
# ---------------------------------------------------------------------------

def generate_gravity_reference(
    category_data: dict,
    bass_strong_beat: list[int],
) -> str:
    """Generate gravity_reference_data.inc content.

    Args:
        category_data: Extracted category distributions.
        bass_strong_beat: Normalized bass strong-beat interval counts.

    Returns:
        Complete C++ .inc file content.
    """
    work_count = category_data["work_count"]
    source = f"Source: organ_fugue.json, {work_count} works"

    # Normalize all distributions
    rhythm_norm = smooth_and_normalize_1d(category_data["rhythm"])
    harmony_norm = smooth_and_normalize_1d(category_data["harmony_degrees"])
    nct_norm = smooth_and_normalize_1d(category_data["nct_types"])
    motion_norm = smooth_and_normalize_1d(category_data["motion"])
    interval_norm = smooth_and_normalize_1d(category_data["interval"])
    vertical_norm = smooth_and_normalize_1d(category_data["vertical"])
    texture_norm = smooth_and_normalize_1d(category_data["texture"])

    harmony_size = len(harmony_norm)

    sections = []
    sections.append("// Auto-generated by scripts/extract_gravity_reference.py")
    sections.append("// Do not edit manually.")
    sections.append("")
    sections.append("#include <cstdint>")
    sections.append("")
    sections.append("namespace gravity_data {")
    sections.append("")

    # 1. Rhythm
    sections.append(format_1d_array(
        "kRhythmTarget", rhythm_norm, 7,
        f"{source} -- rhythm distribution",
        RHYTHM_LABELS,
    ))
    sections.append("")

    # 2. Harmony degrees (top N)
    sections.append(format_1d_array(
        "kHarmonyDegreeTarget", harmony_norm, harmony_size,
        f"{source} -- top {harmony_size} harmony degrees",
        category_data["harmony_labels"],
    ))
    sections.append("")

    # Emit the label-to-index mapping as a comment block for C++ reference
    sections.append("// Harmony degree label index mapping:")
    for idx, label in enumerate(category_data["harmony_labels"]):
        sections.append(f"//   [{idx}] = \"{label}\"")
    sections.append("")

    # 3. NCT types
    sections.append(format_1d_array(
        "kNctTypeTarget", nct_norm, 4,
        f"{source} -- non-chord tone types",
        NCT_LABELS,
    ))
    sections.append("")

    # 4. Motion
    sections.append(format_1d_array(
        "kMotionTarget", motion_norm, 4,
        f"{source} -- voice motion distribution",
        MOTION_LABELS,
    ))
    sections.append("")

    # 5. Interval
    sections.append(format_1d_array(
        "kIntervalTarget", interval_norm, 12,
        f"{source} -- melodic interval distribution (P1-M7)",
        INTERVAL_LABELS,
    ))
    sections.append("")

    # 6. Vertical
    sections.append(format_1d_array(
        "kVerticalTarget", vertical_norm, 12,
        f"{source} -- vertical interval distribution (P1-M7)",
        INTERVAL_LABELS,
    ))
    sections.append("")

    # 7. Texture
    sections.append(format_1d_array(
        "kTextureTarget", texture_norm, 5,
        f"{source} -- texture density (0-4 voices active)",
        TEXTURE_LABELS,
    ))
    sections.append("")

    # 9. Bass strong-beat intervals
    sections.append(format_1d_array(
        "kBassStrongBeatInterval", bass_strong_beat, 12,
        "Source: BWV*_fugue.json pedal tracks -- bass strong-beat interval (P1-M7)",
        INTERVAL_LABELS,
    ))
    sections.append("")

    sections.append("}  // namespace gravity_data")
    sections.append("")

    return "\n".join(sections)


def generate_harmonic_bigram(bigram_norm: list[list[int]]) -> str:
    """Generate harmonic_bigram_data.inc content.

    Args:
        bigram_norm: 12x12 normalized bigram counts (7x7 used, padded to 12).

    Returns:
        Complete C++ .inc file content.
    """
    degree_labels = ["I/i", "II/ii", "III/iii", "IV/iv", "V/v", "VI/vi", "VII/vii"]

    def row_comment(row_idx: int) -> str:
        if row_idx < len(degree_labels):
            return f"from {degree_labels[row_idx]}"
        return f"from deg{row_idx}"

    # Use 7x7 for the actual scale degrees
    size = 7

    sections = []
    sections.append("// Auto-generated by scripts/extract_gravity_reference.py")
    sections.append("// Do not edit manually.")
    sections.append("")
    sections.append("#include <cstdint>")
    sections.append("")
    sections.append("namespace gravity_data {")
    sections.append("")

    sections.append(format_2d_array(
        "kHarmonicBigram",
        bigram_norm,
        size,
        size,
        "Source: BWV*_fugue.json -- harmonic degree bigrams (scale degree x scale degree, x10000)",
        row_comment,
    ))
    sections.append("")

    sections.append("}  // namespace gravity_data")
    sections.append("")

    return "\n".join(sections)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    """Entry point: extract gravity reference data and write C++ .inc files."""
    parser = argparse.ArgumentParser(
        description="Extract gravity reference data from Bach corpus for GravityConfig."
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("src/core"),
        help="Output directory for .inc files (default: src/core)",
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=Path("data/reference"),
        help="Directory containing BWV*.json reference files (default: data/reference)",
    )
    parser.add_argument(
        "--category-dir",
        type=Path,
        default=Path("scripts/bach_analyzer/reference/categories"),
        help="Directory containing category JSON files (default: scripts/bach_analyzer/reference/categories)",
    )
    args = parser.parse_args()

    output_dir = args.output_dir
    data_dir = args.data_dir
    category_dir = args.category_dir

    # Validate inputs
    organ_fugue_path = category_dir / "organ_fugue.json"
    if not organ_fugue_path.exists():
        print(f"ERROR: organ_fugue.json not found at {organ_fugue_path}", file=sys.stderr)
        return 1

    if not data_dir.exists():
        print(f"ERROR: Data directory not found: {data_dir}", file=sys.stderr)
        return 1

    # ---------------------------------------------------------------------------
    # Items 1-7: Extract from organ_fugue.json category data
    # ---------------------------------------------------------------------------
    print("Extracting category distributions from organ_fugue.json...", file=sys.stderr)
    category_data = extract_category_distributions(organ_fugue_path)
    print(f"  Work count: {category_data['work_count']}", file=sys.stderr)
    print(f"  Harmony degrees kept: {len(category_data['harmony_labels'])}", file=sys.stderr)

    # ---------------------------------------------------------------------------
    # Items 8-9: Extract from individual BWV*_fugue.json files
    # ---------------------------------------------------------------------------
    reference_files = sorted(data_dir.glob("BWV*_fugue.json"))
    if not reference_files:
        print(f"ERROR: No BWV*_fugue.json files found in {data_dir}", file=sys.stderr)
        return 1

    print(f"\nProcessing {len(reference_files)} fugue reference files...", file=sys.stderr)

    # Load key signatures
    key_signatures = load_key_signatures(data_dir)
    if key_signatures:
        print(f"  Loaded {len(key_signatures)} key signatures", file=sys.stderr)

    # Item 8: Harmonic bigrams
    print("  Extracting harmonic bigrams...", file=sys.stderr)
    bigram_raw = extract_harmonic_bigrams(reference_files, key_signatures)

    # Trim to 7x7 (scale degrees 0-6) and normalize
    bigram_7x7 = [row[:7] for row in bigram_raw[:7]]
    bigram_norm = smooth_and_normalize_2d(bigram_7x7)

    # Item 9: Bass strong-beat intervals
    print("  Extracting bass strong-beat intervals...", file=sys.stderr)
    bass_raw = extract_bass_strong_beat(reference_files, key_signatures)
    bass_norm = smooth_and_normalize_1d(bass_raw)

    # ---------------------------------------------------------------------------
    # Verification
    # ---------------------------------------------------------------------------
    print("\nRow sum verification:", file=sys.stderr)
    all_ok = True

    # Check all 1D arrays
    for name, arr in [
        ("kRhythmTarget", smooth_and_normalize_1d(category_data["rhythm"])),
        ("kHarmonyDegreeTarget", smooth_and_normalize_1d(category_data["harmony_degrees"])),
        ("kNctTypeTarget", smooth_and_normalize_1d(category_data["nct_types"])),
        ("kMotionTarget", smooth_and_normalize_1d(category_data["motion"])),
        ("kIntervalTarget", smooth_and_normalize_1d(category_data["interval"])),
        ("kVerticalTarget", smooth_and_normalize_1d(category_data["vertical"])),
        ("kTextureTarget", smooth_and_normalize_1d(category_data["texture"])),
        ("kBassStrongBeatInterval", bass_norm),
    ]:
        arr_sum = sum(arr)
        status = "OK" if arr_sum == NORM_SUM else "FAIL"
        if arr_sum != NORM_SUM:
            all_ok = False
        print(f"  {name}[{len(arr)}]: sum={arr_sum} {status}", file=sys.stderr)

    # Check 2D bigram
    for row_idx, row in enumerate(bigram_norm):
        row_sum = sum(row)
        if row_sum != NORM_SUM:
            all_ok = False
            print(f"  kHarmonicBigram row {row_idx}: sum={row_sum} FAIL", file=sys.stderr)

    bigram_status = "OK" if all(sum(row) == NORM_SUM for row in bigram_norm) else "FAIL"
    print(f"  kHarmonicBigram[7x7]: {bigram_status}", file=sys.stderr)

    if not all_ok:
        print("\nERROR: Row sum verification failed.", file=sys.stderr)
        return 1

    # ---------------------------------------------------------------------------
    # Write output files
    # ---------------------------------------------------------------------------
    output_dir.mkdir(parents=True, exist_ok=True)

    gravity_path = output_dir / "gravity_reference_data.inc"
    gravity_content = generate_gravity_reference(category_data, bass_norm)
    with open(gravity_path, "w", encoding="utf-8") as out_file:
        out_file.write(gravity_content)
    print(f"\nWrote {gravity_path} ({len(gravity_content)} bytes)", file=sys.stderr)

    bigram_path = output_dir / "harmonic_bigram_data.inc"
    bigram_content = generate_harmonic_bigram(bigram_norm)
    with open(bigram_path, "w", encoding="utf-8") as out_file:
        out_file.write(bigram_content)
    print(f"Wrote {bigram_path} ({len(bigram_content)} bytes)", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
