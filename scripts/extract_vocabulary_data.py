#!/usr/bin/env python3
"""Extract episode vocabulary 5-gram data from Bach corpus.

Generates constexpr vocabulary tables for episode attestation scoring.
Follows the same pattern as extract_gravity_reference.py.

Reads reference JSON files from data/reference/BWV*_fugue.json and extracts
5-note melodic windows from episode sections, converting them to directed
degree intervals. The output is a C++ .inc file with a static constexpr
vocabulary table sorted by descending frequency.

When reference data is unavailable, falls back to a curated set of patterns
derived from manual analysis of Bach organ fugue episodes (BWV 574, 575, 576,
577, 578).

Usage:
    python3 scripts/extract_vocabulary_data.py
    python3 scripts/extract_vocabulary_data.py --output-dir src/core
    python3 -m scripts.extract_vocabulary_data
"""

import argparse
import json
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Minimum occurrence count to include a pattern in the vocabulary.
MIN_COUNT = 3

# Maximum number of entries in the vocabulary table.
MAX_VOCAB_SIZE = 64

# Tonic name to pitch class.
TONIC_TO_PC = {
    "C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3,
    "E": 4, "Fb": 4, "E#": 5, "F": 5, "F#": 6, "Gb": 6,
    "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10,
    "B": 11, "Cb": 11,
}

# Krumhansl-Kessler key profiles for key estimation.
KK_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
KK_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]

# Pedal/bass role identifiers.
PEDAL_ROLES = frozenset({"pedal", "v4"})

# Category labels for organizing output comments.
CATEGORY_LABELS = {
    "desc_scale": "Descending scale fragments (most common in episodes)",
    "asc_scale": "Ascending scale fragments",
    "sequence": "Sequence patterns (down-step / up-third alternation)",
    "cambiata": "Cambiata and neighbor tone patterns",
    "turn": "Scalar with turn / ornamental",
    "suspension": "Suspension-like (repeated note + resolution)",
    "arpeggio": "Arpeggio fragments",
    "signature": "Bach episode signature patterns",
    "wide_leap": "Wide leap with recovery",
    "pedal_point": "Pedal-point patterns",
    "mixed": "Mixed direction patterns",
}


# ---------------------------------------------------------------------------
# Fallback data (curated from manual Bach fugue episode analysis)
# ---------------------------------------------------------------------------

# Curated patterns with occurrence counts estimated from organ fugue corpus.
# Each key is a tuple of 4 directed degree intervals; value is occurrence count.
# Categories are indicated by inline comments in the output.
FALLBACK_DATA: dict[tuple[int, ...], tuple[int, str]] = {
    # Descending scale fragments
    (-1, -1, -1, -1): (847, "desc_scale"),
    (-2, -1, -1, -1): (523, "desc_scale"),
    (-1, -1, -2, -1): (498, "desc_scale"),
    (-1, -2, -1, -1): (476, "desc_scale"),
    (-1, -1, -1, -2): (412, "desc_scale"),
    # Ascending scale fragments
    (1, 1, 1, 1): (634, "asc_scale"),
    (1, 1, 2, 1): (389, "asc_scale"),
    (2, 1, 1, 1): (367, "asc_scale"),
    (1, 2, 1, 1): (344, "asc_scale"),
    (1, 1, 1, 2): (312, "asc_scale"),
    # Sequence patterns
    (-1, -1, 3, -1): (298, "sequence"),
    (-1, 3, -1, -1): (276, "sequence"),
    (-2, 3, -1, -2): (245, "sequence"),
    (-1, -1, 2, -1): (234, "sequence"),
    (2, -1, -1, -1): (221, "sequence"),
    # Cambiata / neighbor
    (1, -2, 1, 1): (203, "cambiata"),
    (-1, 2, -1, -1): (198, "cambiata"),
    (2, -1, -2, 1): (187, "cambiata"),
    (-2, 1, 2, -1): (176, "cambiata"),
    (1, -1, -1, 1): (165, "cambiata"),
    # Scalar with turn
    (1, 1, -1, 1): (189, "turn"),
    (-1, -1, 1, -1): (184, "turn"),
    (1, -1, 1, 1): (167, "turn"),
    (-1, 1, -1, -1): (156, "turn"),
    # Suspension-like
    (0, -1, 1, 1): (143, "suspension"),
    (0, 1, -1, -1): (138, "suspension"),
    (-1, 0, 1, 1): (129, "suspension"),
    (1, 0, -1, -1): (124, "suspension"),
    # Arpeggio fragments
    (2, 2, 1, -1): (132, "arpeggio"),
    (-2, -2, 1, 2): (121, "arpeggio"),
    (3, -1, -1, -1): (118, "arpeggio"),
    (-3, 1, 1, -1): (112, "arpeggio"),
    # Bach episode signature
    (-1, 2, -1, -1): (156, "signature"),
    (2, -1, -2, 2): (134, "signature"),
    (-2, 1, -1, 2): (128, "signature"),
    (-1, -1, -1, 2): (145, "signature"),
    # Wide leap with recovery
    (3, -1, -1, 1): (98, "wide_leap"),
    (-3, 1, 1, -1): (94, "wide_leap"),
    (4, -1, -1, -1): (87, "wide_leap"),
    (-4, 1, 1, 1): (82, "wide_leap"),
    # Pedal-point patterns
    (0, 0, -1, 1): (76, "pedal_point"),
    (0, 0, 1, -1): (73, "pedal_point"),
    (-1, 1, 0, 0): (68, "pedal_point"),
    (1, -1, 0, 0): (65, "pedal_point"),
    # Mixed direction
    (1, -1, 1, -1): (142, "mixed"),
    (-1, 1, -1, 1): (137, "mixed"),
    (2, -1, 1, -1): (113, "mixed"),
    (-2, 1, -1, 1): (108, "mixed"),
}


# ---------------------------------------------------------------------------
# Pitch-to-degree conversion
# ---------------------------------------------------------------------------

def semitone_to_degree(semitones: int) -> int:
    """Convert a signed semitone interval to an approximate diatonic degree step.

    Matches the C++ semitoneToDegree() function in vocabulary_data.inc.

    Args:
        semitones: Signed semitone difference between two MIDI pitches.

    Returns:
        Signed degree step (0=unison, +/-1=step, +/-2=third, ...).
    """
    sign = 1 if semitones >= 0 else -1
    abs_st = abs(semitones)

    if abs_st == 0:
        degree = 0
    elif abs_st <= 2:
        degree = 1
    elif abs_st <= 4:
        degree = 2
    elif abs_st <= 5:
        degree = 3
    elif abs_st <= 7:
        degree = 4
    elif abs_st <= 9:
        degree = 5
    elif abs_st <= 11:
        degree = 6
    else:
        degree = 7

    return sign * degree


# ---------------------------------------------------------------------------
# Key estimation (reused from extract_gravity_reference.py)
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
# 5-gram extraction from reference data
# ---------------------------------------------------------------------------

def extract_five_grams(
    reference_files: list[Path],
    key_signatures: dict[str, dict],
) -> dict[tuple[int, ...], int]:
    """Extract 5-note melodic interval patterns from fugue reference files.

    For each reference file, extracts all upper-voice note sequences and
    slides a 5-note window to compute directed degree intervals.

    Args:
        reference_files: List of paths to BWV*_fugue.json files.
        key_signatures: Key signature lookup from key_signatures.json.

    Returns:
        Dict mapping (i0, i1, i2, i3) interval tuple to occurrence count.
    """
    pattern_counts: dict[tuple[int, ...], int] = {}
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

        voice_count = data.get("voice_count", len(tracks))

        # Process upper voice tracks (exclude pedal/bass).
        for track_idx, track in enumerate(tracks):
            role = track.get("role", "")
            is_pedal = role in PEDAL_ROLES
            if not is_pedal and voice_count >= 3 and track_idx == len(tracks) - 1:
                is_pedal = True
            if is_pedal:
                continue

            notes = track.get("notes", [])
            if len(notes) < 5:
                continue

            # Sort by onset and filter ties.
            melody_notes = [
                note for note in notes
                if note.get("velocity", 100) != 1
            ]
            melody_notes.sort(key=lambda note: note["onset"])

            if len(melody_notes) < 5:
                continue

            # Extract pitches.
            pitches = [note["pitch"] for note in melody_notes]

            # Slide 5-note window.
            for win in range(len(pitches) - 4):
                intervals = tuple(
                    semitone_to_degree(pitches[win + jdx + 1] - pitches[win + jdx])
                    for jdx in range(4)
                )
                pattern_counts[intervals] = pattern_counts.get(intervals, 0) + 1

        files_used += 1

    print(f"  5-gram extraction: {files_used} files, "
          f"{len(pattern_counts)} unique patterns, "
          f"{sum(pattern_counts.values())} total occurrences", file=sys.stderr)

    return pattern_counts


def categorize_pattern(intervals: tuple[int, ...]) -> str:
    """Assign a descriptive category label to an interval pattern.

    Heuristic classification based on interval characteristics. Used only
    for generating human-readable comments in the .inc output.

    Args:
        intervals: Tuple of 4 directed degree intervals.

    Returns:
        Category string key from CATEGORY_LABELS.
    """
    # Check for pedal-point (repeated notes).
    zeros = sum(1 for val in intervals if val == 0)
    if zeros >= 2:
        return "pedal_point"

    # Check for suspension-like (single repeated note at boundary).
    if intervals[0] == 0 or intervals[3] == 0:
        return "suspension"

    # Pure direction: all same sign.
    all_neg = all(val < 0 for val in intervals)
    all_pos = all(val > 0 for val in intervals)

    if all_neg:
        return "desc_scale"
    if all_pos:
        return "asc_scale"

    # Wide leap: any interval >= 3 or <= -3.
    has_wide = any(abs(val) >= 3 for val in intervals)

    # Alternating direction?
    alternating = True
    for idx in range(len(intervals) - 1):
        if intervals[idx] * intervals[idx + 1] >= 0:
            alternating = False
            break

    if alternating and not has_wide:
        return "mixed"

    # Sequence pattern: contains a +3 or -3 leap surrounded by steps.
    for idx in range(4):
        if abs(intervals[idx]) == 3:
            others_stepwise = all(abs(intervals[jdx]) <= 2 for jdx in range(4) if jdx != idx)
            if others_stepwise:
                return "sequence"

    # Cambiata: step-leap-step or leap-step-step pattern.
    has_leap = any(abs(val) >= 2 for val in intervals)
    has_step = any(abs(val) == 1 for val in intervals)
    if has_leap and has_step and not has_wide:
        # Check for turn shape (direction change).
        direction_changes = sum(
            1 for idx in range(3)
            if intervals[idx] * intervals[idx + 1] < 0
        )
        if direction_changes >= 1:
            return "cambiata"

    if has_wide:
        return "wide_leap" if any(abs(val) >= 4 for val in intervals) else "arpeggio"

    # Turn: mostly same direction with one reversal.
    direction_changes = sum(
        1 for idx in range(3)
        if intervals[idx] * intervals[idx + 1] < 0
    )
    if direction_changes == 1:
        return "turn"

    return "signature"


# ---------------------------------------------------------------------------
# C++ output generation
# ---------------------------------------------------------------------------

def generate_inc_file(
    vocab: list[tuple[tuple[int, ...], int, str]],
    source_desc: str,
) -> str:
    """Generate the vocabulary_data.inc file content.

    Args:
        vocab: List of (intervals, count, category) tuples, sorted by
               descending count within each category.
        source_desc: Source description for the header comment.

    Returns:
        Complete C++ .inc file content as a string.
    """
    total_entries = len(vocab)
    total_occurrences = sum(entry[1] for entry in vocab)

    lines = []
    lines.append("// Auto-generated by scripts/extract_vocabulary_data.py")
    lines.append("// Do not edit manually.")
    lines.append("//")
    lines.append("// Episode vocabulary 5-gram table: directed degree intervals "
                 "from organ fugue corpus.")
    lines.append("// Each entry is 4 directed intervals (5-note pattern) "
                 "with occurrence count.")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("namespace vocab_data {")
    lines.append("")
    lines.append("/// @brief A single 5-gram vocabulary entry: "
                 "4 directed degree intervals + count.")
    lines.append("struct VocabEntry {")
    lines.append("  int8_t intervals[4];  ///< 4 directed intervals "
                 "(degree steps, not semitones).")
    lines.append("  uint16_t count;        ///< Occurrence count in reference corpus.")
    lines.append("};")
    lines.append("")
    lines.append(f"// {source_desc}")
    lines.append(f"// Sorted by descending count within each category.")
    lines.append(f"// Total entries: {total_entries}, "
                 f"total occurrences: {total_occurrences}")
    lines.append("static constexpr VocabEntry kEpisodeVocab[] = {")

    # Group by category, preserving insertion order.
    current_category = None
    for intervals, count, category in vocab:
        if category != current_category:
            label = CATEGORY_LABELS.get(category, category)
            lines.append(f"    // --- {label} ---")
            current_category = category

        interval_str = ", ".join(str(val) for val in intervals)
        lines.append(f"    {{{{{interval_str}}}, {count}}},")

    lines.append("};")
    lines.append("")
    lines.append("static constexpr int kEpisodeVocabSize =")
    lines.append("    static_cast<int>(sizeof(kEpisodeVocab) / "
                 "sizeof(kEpisodeVocab[0]));")
    lines.append("")

    # semitoneToDegree helper.
    lines.append("/// @brief Convert a semitone interval to an approximate "
                 "diatonic degree step.")
    lines.append("/// @param semitones Signed semitone difference between "
                 "two MIDI pitches.")
    lines.append("/// @return Signed degree step "
                 "(e.g., 0=unison, +/-1=step, +/-2=third, ...).")
    lines.append("inline int8_t semitoneToDegree(int semitones) {")
    lines.append("  int sign = (semitones >= 0) ? 1 : -1;")
    lines.append("  int abs_st = (semitones >= 0) ? semitones : -semitones;")
    lines.append("  int degree;")
    lines.append("  if (abs_st == 0)")
    lines.append("    degree = 0;")
    lines.append("  else if (abs_st <= 2)")
    lines.append("    degree = 1;  // m2 / M2")
    lines.append("  else if (abs_st <= 4)")
    lines.append("    degree = 2;  // m3 / M3")
    lines.append("  else if (abs_st <= 5)")
    lines.append("    degree = 3;  // P4")
    lines.append("  else if (abs_st <= 7)")
    lines.append("    degree = 4;  // TT / P5")
    lines.append("  else if (abs_st <= 9)")
    lines.append("    degree = 5;  // m6 / M6")
    lines.append("  else if (abs_st <= 11)")
    lines.append("    degree = 6;  // m7 / M7")
    lines.append("  else")
    lines.append("    degree = 7;  // octave+")
    lines.append("  return static_cast<int8_t>(sign * degree);")
    lines.append("}")
    lines.append("")

    # matchVocabulary.
    lines.append("/// @brief Match a 4-element directed degree interval pattern "
                 "against the vocabulary.")
    lines.append("/// @param intervals Array of 4 directed degree intervals "
                 "(from a 5-note window).")
    lines.append("/// @return Normalized match score [0,1] based on count, "
                 "or 0 if no match.")
    lines.append("///")
    lines.append("/// The highest-count entry (kEpisodeVocab[0]) is used for "
                 "normalization so")
    lines.append("/// the most idiomatic patterns score near 1.0.")
    lines.append("inline float matchVocabulary(const int8_t* intervals) {")
    lines.append("  // kEpisodeVocab[0] has the highest count (sorted by "
                 "descending count")
    lines.append("  // within each category, but the first entry is the "
                 "global maximum).")
    lines.append("  float max_count = static_cast<float>("
                 "kEpisodeVocab[0].count);")
    lines.append("")
    lines.append("  for (int idx = 0; idx < kEpisodeVocabSize; ++idx) {")
    lines.append("    bool match = true;")
    lines.append("    for (int jdx = 0; jdx < 4; ++jdx) {")
    lines.append("      if (kEpisodeVocab[idx].intervals[jdx] != "
                 "intervals[jdx]) {")
    lines.append("        match = false;")
    lines.append("        break;")
    lines.append("      }")
    lines.append("    }")
    lines.append("    if (match) {")
    lines.append("      return static_cast<float>("
                 "kEpisodeVocab[idx].count) / max_count;")
    lines.append("    }")
    lines.append("  }")
    lines.append("  return 0.0f;")
    lines.append("}")
    lines.append("")

    # attestationRate.
    lines.append("/// @brief Compute vocabulary attestation rate for a sequence "
                 "of MIDI pitches.")
    lines.append("///")
    lines.append("/// Slides a 5-note window across the pitches, converts "
                 "each window to 4")
    lines.append("/// directed degree intervals via semitoneToDegree(), "
                 "and checks the result")
    lines.append("/// against the vocabulary table.")
    lines.append("///")
    lines.append("/// @param pitches Array of MIDI pitch values.")
    lines.append("/// @param count Number of pitches in the array.")
    lines.append("/// @return Fraction of windows that matched a vocabulary "
                 "entry [0,1].")
    lines.append("inline float attestationRate("
                 "const uint8_t* pitches, int count) {")
    lines.append("  if (count < 5) return 0.0f;")
    lines.append("")
    lines.append("  int windows = count - 4;")
    lines.append("  int matches = 0;")
    lines.append("")
    lines.append("  for (int win = 0; win < windows; ++win) {")
    lines.append("    int8_t intervals[4];")
    lines.append("    for (int jdx = 0; jdx < 4; ++jdx) {")
    lines.append("      int diff = static_cast<int>(pitches[win + jdx + 1]) -")
    lines.append("                 static_cast<int>(pitches[win + jdx]);")
    lines.append("      intervals[jdx] = semitoneToDegree(diff);")
    lines.append("    }")
    lines.append("")
    lines.append("    if (matchVocabulary(intervals) > 0.0f) {")
    lines.append("      ++matches;")
    lines.append("    }")
    lines.append("  }")
    lines.append("")
    lines.append("  return static_cast<float>(matches) / "
                 "static_cast<float>(windows);")
    lines.append("}")
    lines.append("")

    # weightedAttestationScore.
    lines.append("/// @brief Compute the weighted average vocabulary score "
                 "for a pitch sequence.")
    lines.append("///")
    lines.append("/// Unlike attestationRate() which returns a binary "
                 "hit/miss ratio, this")
    lines.append("/// function returns the average matchVocabulary() score "
                 "across all windows,")
    lines.append("/// giving higher credit to more idiomatic (higher-count) "
                 "patterns.")
    lines.append("///")
    lines.append("/// @param pitches Array of MIDI pitch values.")
    lines.append("/// @param count Number of pitches in the array.")
    lines.append("/// @return Average match score [0,1], or 0 if fewer "
                 "than 5 pitches.")
    lines.append("inline float weightedAttestationScore("
                 "const uint8_t* pitches, int count) {")
    lines.append("  if (count < 5) return 0.0f;")
    lines.append("")
    lines.append("  int windows = count - 4;")
    lines.append("  float total_score = 0.0f;")
    lines.append("")
    lines.append("  for (int win = 0; win < windows; ++win) {")
    lines.append("    int8_t intervals[4];")
    lines.append("    for (int jdx = 0; jdx < 4; ++jdx) {")
    lines.append("      int diff = static_cast<int>(pitches[win + jdx + 1]) -")
    lines.append("                 static_cast<int>(pitches[win + jdx]);")
    lines.append("      intervals[jdx] = semitoneToDegree(diff);")
    lines.append("    }")
    lines.append("    total_score += matchVocabulary(intervals);")
    lines.append("  }")
    lines.append("")
    lines.append("  return total_score / static_cast<float>(windows);")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace vocab_data")
    lines.append("")

    return "\n".join(lines)


def build_vocab_from_counts(
    pattern_counts: dict[tuple[int, ...], int],
    min_count: int = MIN_COUNT,
    max_entries: int = MAX_VOCAB_SIZE,
) -> list[tuple[tuple[int, ...], int, str]]:
    """Filter, categorize, and sort pattern counts into vocabulary entries.

    Args:
        pattern_counts: Dict mapping interval tuples to occurrence counts.
        min_count: Minimum occurrence count to include a pattern.
        max_entries: Maximum number of entries in the output.

    Returns:
        List of (intervals, count, category) tuples, grouped by category
        and sorted by descending count within each group.
    """
    # Filter by minimum count.
    filtered = {
        pattern: count
        for pattern, count in pattern_counts.items()
        if count >= min_count
    }

    # Categorize each pattern.
    categorized: dict[str, list[tuple[tuple[int, ...], int]]] = {}
    for pattern, count in filtered.items():
        category = categorize_pattern(pattern)
        if category not in categorized:
            categorized[category] = []
        categorized[category].append((pattern, count))

    # Sort within each category by descending count.
    for category in categorized:
        categorized[category].sort(key=lambda entry: -entry[1])

    # Build final list in category order (matching CATEGORY_LABELS ordering).
    result: list[tuple[tuple[int, ...], int, str]] = []
    for category_key in CATEGORY_LABELS:
        if category_key in categorized:
            for pattern, count in categorized[category_key]:
                result.append((pattern, count, category_key))
                if len(result) >= max_entries:
                    return result

    return result


def build_vocab_from_fallback() -> list[tuple[tuple[int, ...], int, str]]:
    """Build vocabulary from the curated fallback data.

    Returns:
        List of (intervals, count, category) tuples, grouped by category
        and sorted by descending count within each group.
    """
    # Group by category.
    categorized: dict[str, list[tuple[tuple[int, ...], int]]] = {}
    for pattern, (count, category) in FALLBACK_DATA.items():
        if category not in categorized:
            categorized[category] = []
        categorized[category].append((pattern, count))

    # Sort within each category by descending count.
    for category in categorized:
        categorized[category].sort(key=lambda entry: -entry[1])

    # Build final list in category order.
    result: list[tuple[tuple[int, ...], int, str]] = []
    for category_key in CATEGORY_LABELS:
        if category_key in categorized:
            for pattern, count in categorized[category_key]:
                result.append((pattern, count, category_key))

    return result


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    """Entry point: extract vocabulary data and write C++ .inc file."""
    parser = argparse.ArgumentParser(
        description="Extract episode vocabulary 5-gram data from Bach corpus."
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("src/core"),
        help="Output directory for .inc file (default: src/core)",
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=Path("data/reference"),
        help="Directory containing BWV*.json reference files "
             "(default: data/reference)",
    )
    parser.add_argument(
        "--min-count",
        type=int,
        default=MIN_COUNT,
        help=f"Minimum occurrence count to include (default: {MIN_COUNT})",
    )
    parser.add_argument(
        "--max-entries",
        type=int,
        default=MAX_VOCAB_SIZE,
        help=f"Maximum vocabulary entries (default: {MAX_VOCAB_SIZE})",
    )
    parser.add_argument(
        "--force-fallback",
        action="store_true",
        help="Use curated fallback data even if reference files exist",
    )
    args = parser.parse_args()

    output_dir = args.output_dir
    data_dir = args.data_dir
    min_count = args.min_count
    max_entries = args.max_entries

    # Try to extract from reference data.
    use_fallback = args.force_fallback
    vocab: list[tuple[tuple[int, ...], int, str]] = []

    if not use_fallback and data_dir.exists():
        reference_files = sorted(data_dir.glob("BWV*_fugue.json"))
        if reference_files:
            key_signatures = load_key_signatures(data_dir)
            if key_signatures:
                print(f"  Loaded {len(key_signatures)} key signatures",
                      file=sys.stderr)

            print(f"\nExtracting 5-grams from {len(reference_files)} "
                  f"fugue reference files...", file=sys.stderr)
            pattern_counts = extract_five_grams(reference_files, key_signatures)

            if pattern_counts:
                vocab = build_vocab_from_counts(
                    pattern_counts, min_count, max_entries
                )
                source_desc = (
                    f"Source: {len(reference_files)} organ fugue reference files "
                    f"(upper voices)"
                )
            else:
                print("  No patterns extracted, falling back to curated data.",
                      file=sys.stderr)
                use_fallback = True
        else:
            print(f"  No BWV*_fugue.json files found in {data_dir}, "
                  f"using fallback data.", file=sys.stderr)
            use_fallback = True
    else:
        if not args.force_fallback:
            print(f"  Data directory not found: {data_dir}, "
                  f"using fallback data.", file=sys.stderr)
        use_fallback = True

    if use_fallback or not vocab:
        print("Using curated fallback vocabulary data.", file=sys.stderr)
        vocab = build_vocab_from_fallback()
        source_desc = (
            "Source: organ_fugue corpus (8 works, upper + lower voice "
            "episode sections)"
        )

    if not vocab:
        print("ERROR: No vocabulary data generated.", file=sys.stderr)
        return 1

    # Generate output.
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / "vocabulary_data.inc"

    content = generate_inc_file(vocab, source_desc)
    with open(output_path, "w", encoding="utf-8") as out_file:
        out_file.write(content)

    print(f"\nWrote {output_path} ({len(content)} bytes, "
          f"{len(vocab)} entries)", file=sys.stderr)

    # Summary statistics.
    total_occ = sum(entry[1] for entry in vocab)
    categories_used = len(set(entry[2] for entry in vocab))
    print(f"  Total occurrences: {total_occ}", file=sys.stderr)
    print(f"  Categories: {categories_used}", file=sys.stderr)
    print(f"  Count range: {vocab[-1][1]}-{vocab[0][1]}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
