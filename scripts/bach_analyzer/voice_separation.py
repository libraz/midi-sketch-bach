"""Voice separation for multi-voice Bach reference works.

Separates monophonic MIDI tracks (track_type="manual") into individual
voice streams using sequential cost-based assignment. Works with the
bach_analyzer data model (Note, Track, Score).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from itertools import permutations
from typing import Dict, List, Optional, Tuple

from .model import Note, Score, TICKS_PER_BAR, TICKS_PER_BEAT, Track

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Duration threshold for ornament detection (less than half a 32nd note).
_ORNAMENT_THRESHOLD_TICKS = TICKS_PER_BEAT // 8  # 60 ticks at 480 TPB

# Cost function weights.
_OVERLAP_PENALTY = 1000.0
_REGISTER_WEIGHT = 0.5
_CROSSING_INSTANT_PENALTY = 3.0
_CROSSING_CONTINUOUS_PENALTY = 6.0
_ORNAMENT_DISTANCE_FACTOR = 0.3
_CROSSING_MARGIN = 3  # semitones margin before crossing penalty applies

# EMA smoothing for register center.
_EMA_ALPHA = 0.15

# Voice count clamp.
_MIN_VOICES = 1
_MAX_VOICES = 6


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class VoiceState:
    """Mutable state for one voice during separation."""
    voice_id: int
    last_pitch: int = 0
    last_end_tick: int = 0
    register_center: float = 60.0
    note_count: int = 0
    crossing_with: set = field(default_factory=set)


@dataclass
class SeparationResult:
    """Output of voice separation."""
    voices: List[List[Note]]
    ornaments: List[Note]
    num_voices: int
    arpeggio_like: bool = False

    @property
    def unassigned_rate(self) -> float:
        total = sum(len(v) for v in self.voices) + len(self.ornaments)
        if total == 0:
            return 0.0
        return len(self.ornaments) / total


# ---------------------------------------------------------------------------
# 1a. Voice count detection
# ---------------------------------------------------------------------------


def detect_voice_count(
    notes: List[Note],
    tpb: int = TICKS_PER_BEAT,
    pedal_notes: Optional[List[Note]] = None,
) -> Tuple[int, bool]:
    """Detect the number of voices from sounding/onset polyphony.

    Args:
        notes: All notes from manual tracks (sorted by start_tick).
        tpb: Ticks per beat.
        pedal_notes: Notes from a separate pedal track, excluded from
            manual polyphony counting.

    Returns:
        (num_voices, arpeggio_like) — clamped to [1, 6].
    """
    if not notes:
        return 1, False

    # Build pedal exclusion set: (start_tick, pitch) pairs from pedal.
    pedal_set: set = set()
    if pedal_notes:
        for n in pedal_notes:
            pedal_set.add((n.start_tick, n.pitch))

    # Filter notes: exclude exact pedal duplicates.
    filtered = [
        n for n in notes
        if (n.start_tick, n.pitch) not in pedal_set
    ]
    if not filtered:
        return 1, False

    sorted_notes = sorted(filtered, key=lambda n: n.start_tick)
    total_dur = max(n.start_tick + n.duration for n in sorted_notes)
    if total_dur == 0:
        return 1, False

    # Sounding polyphony: sample at 16th-note resolution.
    sample_step = max(1, tpb // 4)
    sounding_samples: List[int] = []
    tick = 0
    while tick < total_dur:
        count = 0
        for n in sorted_notes:
            if n.start_tick > tick:
                break
            if n.start_tick <= tick < n.start_tick + n.duration:
                count += 1
        sounding_samples.append(count)
        tick += sample_step

    # Onset polyphony: group by start_tick.
    onset_groups: Dict[int, int] = {}
    for n in sorted_notes:
        onset_groups[n.start_tick] = onset_groups.get(n.start_tick, 0) + 1
    onset_samples = sorted(onset_groups.values())

    def percentile(data: List[int], p: float) -> int:
        if not data:
            return 0
        k = (len(data) - 1) * p / 100.0
        f = int(k)
        c = f + 1
        if c >= len(data):
            return data[-1]
        return round(data[f] + (k - f) * (data[c] - data[f]))

    sounding_sorted = sorted(sounding_samples)
    s_p80 = percentile(sounding_sorted, 80)
    s_p90 = percentile(sounding_sorted, 90)
    s_p95 = percentile(sounding_sorted, 95)

    o_p90 = percentile(onset_samples, 90)

    # Primary estimate from sounding P90.
    primary = s_p90

    # If P95-P80 spread > 2, there are resonance/ornament outliers — use P80.
    if s_p95 - s_p80 > 2:
        primary = s_p80

    # Arpeggio detection.
    arpeggio_like = False
    if s_p90 >= 3 and o_p90 <= s_p90 - 1:
        arpeggio_like = True

    # Clamp.
    primary = max(_MIN_VOICES, min(_MAX_VOICES, primary))
    return primary, arpeggio_like


# ---------------------------------------------------------------------------
# 1b. Note normalization
# ---------------------------------------------------------------------------


@dataclass
class _NormNote:
    """Internal note wrapper with ornament flag."""
    note: Note
    is_ornament: bool = False


def normalize_notes(notes: List[Note], tpb: int = TICKS_PER_BEAT) -> List[_NormNote]:
    """Remove duplicates and mark ornamental short notes.

    - Duplicate: same start_tick + same pitch → keep longest.
    - Ornament: duration < half a 32nd note.
    """
    # Group by (start_tick, pitch).
    groups: Dict[Tuple[int, int], List[Note]] = {}
    for n in notes:
        key = (n.start_tick, n.pitch)
        groups.setdefault(key, []).append(n)

    deduped: List[Note] = []
    for key, group in groups.items():
        # Keep the longest.
        best = max(group, key=lambda n: n.duration)
        deduped.append(best)

    # Sort by start_tick, then pitch descending (soprano first).
    deduped.sort(key=lambda n: (n.start_tick, -n.pitch))

    threshold = max(1, tpb // 8)  # half a 32nd note
    result: List[_NormNote] = []
    for n in deduped:
        is_orn = n.duration < threshold
        result.append(_NormNote(note=n, is_ornament=is_orn))
    return result


# ---------------------------------------------------------------------------
# 1c. Sequential voice separation
# ---------------------------------------------------------------------------


def _assignment_cost(
    note: _NormNote,
    voice: VoiceState,
    tpb: int,
    all_voices: List[VoiceState],
) -> float:
    """Compute the cost of assigning a note to a voice."""
    n = note.note
    cost = 0.0

    # (A) Overlap penalty: one voice = one line at a time.
    if voice.note_count > 0 and n.start_tick < voice.last_end_tick:
        cost += _OVERLAP_PENALTY

    # (B) Pitch distance with gap decay and ornament factor.
    if voice.note_count == 0:
        # Unvoiced: only register matters.
        pass
    else:
        base = abs(n.pitch - voice.last_pitch)
        gap_ticks = max(0, n.start_tick - voice.last_end_tick)
        gap_decay = math.exp(-gap_ticks / (tpb * 8))
        orn_factor = _ORNAMENT_DISTANCE_FACTOR if note.is_ornament else 1.0
        cost += base * gap_decay * orn_factor

    # (C) Register deviation (stronger when gap is large — helps re-acquire voice).
    gap_ticks_for_reg = (
        max(0, n.start_tick - voice.last_end_tick)
        if voice.note_count > 0 else tpb * 4
    )
    gap_factor = 1.0 + 2.0 * (1.0 - math.exp(-gap_ticks_for_reg / (tpb * 4)))
    register_dev = abs(n.pitch - voice.register_center) / 12.0
    cost += _REGISTER_WEIGHT * gap_factor * register_dev

    # (D) Crossing penalty (based on register_center, not last_pitch).
    for other in all_voices:
        if other.voice_id == voice.voice_id:
            continue
        if other.note_count == 0:
            continue
        # Check if register centers would invert.
        if voice.voice_id < other.voice_id:
            # Voice should be higher or equal.
            if n.pitch < other.register_center - _CROSSING_MARGIN:
                if other.voice_id in voice.crossing_with:
                    cost += _CROSSING_CONTINUOUS_PENALTY
                else:
                    cost += _CROSSING_INSTANT_PENALTY
        else:
            # Voice should be lower or equal.
            if n.pitch > other.register_center + _CROSSING_MARGIN:
                if other.voice_id in voice.crossing_with:
                    cost += _CROSSING_CONTINUOUS_PENALTY
                else:
                    cost += _CROSSING_INSTANT_PENALTY

    return cost


def _optimal_assignment(
    norm_notes: List[_NormNote],
    voices: List[VoiceState],
    tpb: int,
) -> List[Tuple[int, int]]:
    """Find optimal note-to-voice assignment for a group.

    Returns list of (note_index, voice_id) pairs.
    For N <= M: exhaustive permutation search.
    """
    n = len(norm_notes)
    m = len(voices)

    if n == 0:
        return []

    if n == 1:
        # Single note: pick best voice.
        best_v = min(range(m), key=lambda vi: _assignment_cost(
            norm_notes[0], voices[vi], tpb, voices))
        return [(0, voices[best_v].voice_id)]

    # Build cost matrix.
    cost_matrix: List[List[float]] = []
    for ni in range(n):
        row = []
        for vi in range(m):
            row.append(_assignment_cost(norm_notes[ni], voices[vi], tpb, voices))
        cost_matrix.append(row)

    # For small N, M: exhaustive search over voice permutations.
    # We pick n voices from m available.
    best_cost = float('inf')
    best_assign: List[Tuple[int, int]] = []

    if n <= m:
        # Choose n voices from m and try all permutations.
        from itertools import combinations
        for voice_combo in combinations(range(m), n):
            for perm in permutations(voice_combo):
                total = sum(cost_matrix[ni][perm[ni]] for ni in range(n))
                if total < best_cost:
                    best_cost = total
                    best_assign = [
                        (ni, voices[perm[ni]].voice_id) for ni in range(n)
                    ]
    else:
        # N > M: should not reach here (handled by caller).
        # Fallback: greedy assignment.
        used_voices: set = set()
        for ni in range(min(n, m)):
            best_vi = min(
                (vi for vi in range(m) if vi not in used_voices),
                key=lambda vi: cost_matrix[ni][vi],
            )
            best_assign.append((ni, voices[best_vi].voice_id))
            used_voices.add(best_vi)

    return best_assign


def separate_voices(
    notes: List[Note],
    num_voices: int,
    tpb: int = TICKS_PER_BEAT,
) -> SeparationResult:
    """Separate notes into voice streams by sequential cost-based assignment.

    Args:
        notes: All notes (will be normalized internally).
        num_voices: Target number of voices.
        tpb: Ticks per beat.

    Returns:
        SeparationResult with voices, ornaments, and metadata.
    """
    if not notes or num_voices < 1:
        return SeparationResult(
            voices=[[] for _ in range(max(1, num_voices))],
            ornaments=[],
            num_voices=max(1, num_voices),
        )

    norm = normalize_notes(notes, tpb)
    if not norm:
        return SeparationResult(
            voices=[[] for _ in range(num_voices)],
            ornaments=[],
            num_voices=num_voices,
        )

    # Initialize voice states with data-driven register centers.
    all_pitches = sorted(n.note.pitch for n in norm)
    initial_centers: List[float] = []
    for i in range(num_voices):
        # Evenly spaced quantiles: voice 0 = highest, voice N-1 = lowest.
        q = (i + 0.5) / num_voices
        # Invert: voice 0 gets high pitches.
        idx = int((1.0 - q) * (len(all_pitches) - 1))
        initial_centers.append(float(all_pitches[idx]))

    voices = [
        VoiceState(voice_id=i, register_center=initial_centers[i])
        for i in range(num_voices)
    ]

    voice_notes: List[List[Note]] = [[] for _ in range(num_voices)]
    ornaments: List[Note] = []

    # Group notes by start_tick.
    groups: Dict[int, List[_NormNote]] = {}
    for nn in norm:
        groups.setdefault(nn.note.start_tick, []).append(nn)

    for tick in sorted(groups.keys()):
        group = groups[tick]
        m = num_voices

        if len(group) <= m:
            # N <= M: assign all notes.
            assignment = _optimal_assignment(group, voices, tpb)
            for ni, vid in assignment:
                n = group[ni].note
                voice_notes[vid].append(n)
                v = voices[vid]
                v.last_pitch = n.pitch
                v.last_end_tick = n.start_tick + n.duration
                v.register_center = (
                    (1 - _EMA_ALPHA) * v.register_center + _EMA_ALPHA * n.pitch
                )
                v.note_count += 1
        else:
            # N > M: separate into main candidates and ornaments.
            main_candidates = [nn for nn in group if not nn.is_ornament]
            orn_candidates = [nn for nn in group if nn.is_ornament]

            if len(main_candidates) <= m:
                # Enough room for main notes.
                selected = main_candidates
                overflow = orn_candidates
            else:
                # Too many main notes: select M with max pitch spread.
                selected = _select_spread(main_candidates, m)
                overflow = [
                    nn for nn in main_candidates if nn not in selected
                ] + orn_candidates

            # Assign selected notes.
            assignment = _optimal_assignment(selected, voices, tpb)
            for ni, vid in assignment:
                n = selected[ni].note
                voice_notes[vid].append(n)
                v = voices[vid]
                v.last_pitch = n.pitch
                v.last_end_tick = n.start_tick + n.duration
                v.register_center = (
                    (1 - _EMA_ALPHA) * v.register_center + _EMA_ALPHA * n.pitch
                )
                v.note_count += 1

            # Overflow → ornaments.
            for nn in overflow:
                ornaments.append(nn.note)

        # Update crossing_with for all voice pairs.
        _update_crossings(voices)

    return SeparationResult(
        voices=voice_notes,
        ornaments=ornaments,
        num_voices=num_voices,
    )


def _select_spread(candidates: List[_NormNote], m: int) -> List[_NormNote]:
    """Select M candidates maximizing pitch spread."""
    if len(candidates) <= m:
        return candidates

    sorted_by_pitch = sorted(candidates, key=lambda nn: nn.note.pitch)

    if m == 1:
        return [sorted_by_pitch[len(sorted_by_pitch) // 2]]

    # Greedy: always include highest and lowest, then fill by max gap.
    selected = [sorted_by_pitch[0], sorted_by_pitch[-1]]
    remaining = sorted_by_pitch[1:-1]

    while len(selected) < m and remaining:
        selected_pitches = sorted(nn.note.pitch for nn in selected)
        # Find the largest gap.
        best_gap = 0
        best_nn = remaining[0]
        for nn in remaining:
            p = nn.note.pitch
            # Find which gap this falls into.
            for i in range(len(selected_pitches) - 1):
                if selected_pitches[i] <= p <= selected_pitches[i + 1]:
                    gap = selected_pitches[i + 1] - selected_pitches[i]
                    if gap > best_gap:
                        best_gap = gap
                        best_nn = nn
                    break
        selected.append(best_nn)
        remaining.remove(best_nn)

    return selected


def _update_crossings(voices: List[VoiceState]) -> None:
    """Update crossing_with sets based on current register_centers."""
    for v in voices:
        v.crossing_with.clear()
    for i in range(len(voices)):
        for j in range(i + 1, len(voices)):
            # Voice i should be higher (or equal) than voice j.
            if voices[i].register_center < voices[j].register_center - 2:
                voices[i].crossing_with.add(voices[j].voice_id)
                voices[j].crossing_with.add(voices[i].voice_id)


# ---------------------------------------------------------------------------
# 1d. Score-level separation
# ---------------------------------------------------------------------------


def separate_score(
    score: Score,
    track_type: str,
    num_voices: Optional[int] = None,
    tpb: int = TICKS_PER_BEAT,
) -> Tuple[Score, Optional[SeparationResult]]:
    """Separate a score's manual tracks into individual voices.

    Args:
        score: Input score.
        track_type: The work's track_type ("voice", "solo_string", "manual").
        num_voices: Override voice count (auto-detect if None).
        tpb: Ticks per beat.

    Returns:
        (separated_score, separation_result). If track_type is not "manual",
        returns the original score unchanged with result=None.
    """
    if track_type != "manual":
        return score, None

    # Identify pedal and manual tracks.
    pedal_track: Optional[Track] = None
    manual_notes: List[Note] = []

    for tr in score.tracks:
        if tr.name.lower() in ("pedal", "ped"):
            pedal_track = tr
        else:
            manual_notes.extend(tr.notes)

    if not manual_notes:
        return score, None

    pedal_notes = pedal_track.notes if pedal_track else None

    # Detect or override voice count.
    if num_voices is not None:
        # Explicit count includes pedal — subtract it for manual separation.
        n_voices = num_voices
        if pedal_track:
            n_voices = max(1, n_voices - 1)
        arpeggio_like = False
    else:
        # Auto-detect from manual notes only (pedal already excluded).
        detected, arpeggio_like = detect_voice_count(
            manual_notes, tpb, pedal_notes
        )
        n_voices = detected

    # Separate manual notes.
    result = separate_voices(manual_notes, n_voices, tpb)
    result.arpeggio_like = arpeggio_like

    # Build new Score with separated tracks.
    new_tracks: List[Track] = []

    # Sort voices by median pitch (highest first → v1).
    voice_medians: List[Tuple[int, float]] = []
    for i, voice in enumerate(result.voices):
        if voice:
            pitches = sorted(n.pitch for n in voice)
            median = pitches[len(pitches) // 2]
            voice_medians.append((i, float(median)))
        else:
            voice_medians.append((i, 0.0))
    voice_medians.sort(key=lambda x: -x[1])

    # Create tracks in sorted order.
    voice_id_to_name: Dict[int, str] = {}
    for rank, (orig_idx, _) in enumerate(voice_medians):
        name = f"v{rank + 1}"
        voice_id_to_name[orig_idx] = name
        track_notes = []
        for n in result.voices[orig_idx]:
            track_notes.append(Note(
                pitch=n.pitch,
                velocity=n.velocity,
                start_tick=n.start_tick,
                duration=n.duration,
                voice=name,
                voice_id=rank,
                channel=n.channel,
                provenance=n.provenance,
                modified_by=n.modified_by,
            ))
        new_tracks.append(Track(name=name, notes=track_notes))

    # Add pedal track.
    if pedal_track:
        new_tracks.append(Track(
            name="pedal",
            notes=pedal_track.notes,
            channel=pedal_track.channel,
        ))

    separated_score = Score(
        tracks=new_tracks,
        seed=score.seed,
        form=score.form,
        key=score.key,
        voices=n_voices + (1 if pedal_track else 0),
        source_file=score.source_file,
    )

    return separated_score, result


# ---------------------------------------------------------------------------
# 1e. Quality evaluation
# ---------------------------------------------------------------------------


def evaluate_separation(result: SeparationResult) -> Dict:
    """Compute quality metrics for a voice separation result.

    Returns dict with crossing_rate, avg_interval, gap_rate, overlap_rate,
    register_overlap, voice_swap_events, unassigned_rate.
    """
    metrics: Dict = {}
    n_voices = result.num_voices

    # Per-voice metrics.
    per_voice_intervals: List[float] = []
    per_voice_gap_rates: List[float] = []
    per_voice_overlap_counts: List[int] = []

    for vi, voice in enumerate(result.voices):
        sorted_v = sorted(voice, key=lambda n: n.start_tick)
        if len(sorted_v) < 2:
            per_voice_intervals.append(0.0)
            per_voice_gap_rates.append(0.0)
            per_voice_overlap_counts.append(0)
            continue

        # Average melodic interval.
        intervals = [
            abs(sorted_v[i + 1].pitch - sorted_v[i].pitch)
            for i in range(len(sorted_v) - 1)
        ]
        per_voice_intervals.append(
            sum(intervals) / len(intervals) if intervals else 0.0
        )

        # Gap rate: fraction of consecutive pairs with gap.
        gaps = sum(
            1 for i in range(len(sorted_v) - 1)
            if sorted_v[i + 1].start_tick > sorted_v[i].start_tick + sorted_v[i].duration
        )
        per_voice_gap_rates.append(gaps / (len(sorted_v) - 1))

        # Overlap count: same voice notes overlapping.
        overlaps = sum(
            1 for i in range(len(sorted_v) - 1)
            if sorted_v[i + 1].start_tick < sorted_v[i].start_tick + sorted_v[i].duration
        )
        per_voice_overlap_counts.append(overlaps)

    metrics["avg_interval"] = [round(x, 2) for x in per_voice_intervals]
    metrics["gap_rate"] = [round(x, 4) for x in per_voice_gap_rates]
    metrics["overlap_rate"] = [round(x, 4) for x in [
        c / max(1, len(result.voices[i]) - 1)
        for i, c in enumerate(per_voice_overlap_counts)
    ]]

    # Crossing rate: adjacent voice pairs where register_center inverts.
    if n_voices >= 2:
        crossing_events = 0
        total_pairs = 0
        for vi in range(n_voices - 1):
            v_upper = sorted(result.voices[vi], key=lambda n: n.start_tick)
            v_lower = sorted(result.voices[vi + 1], key=lambda n: n.start_tick)
            if not v_upper or not v_lower:
                continue
            # Sample at each onset in either voice.
            all_ticks = sorted(set(
                [n.start_tick for n in v_upper] + [n.start_tick for n in v_lower]
            ))
            for tick in all_ticks:
                total_pairs += 1
                # Find sounding notes.
                upper_n = _sounding_at(v_upper, tick)
                lower_n = _sounding_at(v_lower, tick)
                if upper_n and lower_n and upper_n.pitch < lower_n.pitch:
                    crossing_events += 1
        metrics["crossing_rate"] = round(
            crossing_events / total_pairs, 4
        ) if total_pairs > 0 else 0.0
    else:
        metrics["crossing_rate"] = 0.0

    # Register overlap: Jaccard index of pitch ranges for adjacent voices.
    register_overlaps: List[float] = []
    for vi in range(n_voices - 1):
        pitches_a = [n.pitch for n in result.voices[vi]]
        pitches_b = [n.pitch for n in result.voices[vi + 1]]
        if not pitches_a or not pitches_b:
            register_overlaps.append(0.0)
            continue
        lo_a, hi_a = min(pitches_a), max(pitches_a)
        lo_b, hi_b = min(pitches_b), max(pitches_b)
        overlap = max(0, min(hi_a, hi_b) - max(lo_a, lo_b))
        union = max(hi_a, hi_b) - min(lo_a, lo_b)
        register_overlaps.append(round(overlap / max(1, union), 4))
    metrics["register_overlap"] = register_overlaps

    # Voice swap events: times when adjacent voice median pitches invert
    # within a 4-bar window.
    swap_events = 0
    if n_voices >= 2:
        window = 4 * TICKS_PER_BAR
        for vi in range(n_voices - 1):
            va = sorted(result.voices[vi], key=lambda n: n.start_tick)
            vb = sorted(result.voices[vi + 1], key=lambda n: n.start_tick)
            if not va or not vb:
                continue
            max_tick = max(
                va[-1].start_tick if va else 0,
                vb[-1].start_tick if vb else 0,
            )
            prev_higher = None
            tick = 0
            while tick <= max_tick:
                pa = [n.pitch for n in va if tick <= n.start_tick < tick + window]
                pb = [n.pitch for n in vb if tick <= n.start_tick < tick + window]
                if pa and pb:
                    med_a = sorted(pa)[len(pa) // 2]
                    med_b = sorted(pb)[len(pb) // 2]
                    higher = med_a >= med_b
                    if prev_higher is not None and higher != prev_higher:
                        swap_events += 1
                    prev_higher = higher
                tick += window
    metrics["voice_swap_events"] = swap_events

    metrics["unassigned_rate"] = round(result.unassigned_rate, 4)

    return metrics


def _sounding_at(sorted_notes: List[Note], tick: int) -> Optional[Note]:
    """Find the note sounding at tick in a sorted note list."""
    for n in reversed(sorted_notes):
        if n.start_tick <= tick:
            if tick < n.start_tick + n.duration:
                return n
            break
    return None


# ---------------------------------------------------------------------------
# 2. Voice relationship analysis helpers
# ---------------------------------------------------------------------------


def compute_independence(
    voices: List[List[Note]],
    tpb: int = TICKS_PER_BEAT,
) -> Dict:
    """Compute rhythmic independence metrics for all voice pairs.

    Args:
        voices: List of note lists, voices[0] = highest (v1).
        tpb: Ticks per beat.

    Returns:
        Dict with pair_independence and overall metrics.
    """
    n_voices = len(voices)
    if n_voices < 2:
        return {"pair_independence": {}, "overall": {
            "avg_simultaneous_onset_ratio": 0.0,
            "avg_rhythmic_divergence": 0.0,
        }}

    # Pre-compute onset sets and per-beat rhythm patterns per voice.
    onset_sets: List[set] = []
    beat_patterns: List[Dict[int, List[float]]] = []  # beat_idx -> durations
    for voice in voices:
        onsets = set(n.start_tick for n in voice)
        onset_sets.append(onsets)
        patterns: Dict[int, List[float]] = {}
        for n in voice:
            beat_idx = n.start_tick // tpb
            patterns.setdefault(beat_idx, []).append(n.duration / tpb)
        beat_patterns.append(patterns)

    # Find total duration for activity correlation.
    total_dur = 0
    for voice in voices:
        for n in voice:
            total_dur = max(total_dur, n.start_tick + n.duration)
    sample_step = max(1, tpb // 4)  # 16th-note resolution

    pair_results: Dict[str, Dict] = {}
    all_sim_ratios: List[float] = []
    all_div: List[float] = []

    for i in range(n_voices):
        for j in range(i + 1, n_voices):
            pair_key = f"v{i+1}-v{j+1}"

            # (A) Simultaneous onset ratio.
            union = onset_sets[i] | onset_sets[j]
            intersect = onset_sets[i] & onset_sets[j]
            sim_ratio = len(intersect) / len(union) if union else 0.0

            # (B) Contrary motion ratio (using sorted notes).
            sorted_i = sorted(voices[i], key=lambda n: n.start_tick)
            sorted_j = sorted(voices[j], key=lambda n: n.start_tick)
            contrary = 0
            motion_total = 0
            prev_ni: Optional[Note] = None
            prev_nj: Optional[Note] = None
            tick = 0
            while tick < total_dur:
                ni = _sounding_at(sorted_i, tick)
                nj = _sounding_at(sorted_j, tick)
                if (ni is not None and nj is not None
                        and prev_ni is not None and prev_nj is not None):
                    di = ni.pitch - prev_ni.pitch
                    dj = nj.pitch - prev_nj.pitch
                    if di != 0 or dj != 0:
                        motion_total += 1
                        if (di > 0 and dj < 0) or (di < 0 and dj > 0):
                            contrary += 1
                prev_ni = ni
                prev_nj = nj
                tick += tpb

            # (C) Rhythmic divergence: compare per-beat duration patterns.
            all_beats = set(beat_patterns[i].keys()) | set(beat_patterns[j].keys())
            divergent_beats = 0
            for b in all_beats:
                pi = tuple(sorted(beat_patterns[i].get(b, [])))
                pj = tuple(sorted(beat_patterns[j].get(b, [])))
                if pi != pj:
                    divergent_beats += 1
            rhythmic_div = divergent_beats / len(all_beats) if all_beats else 0.0

            # (D) Activity correlation: inverse of co-activity.
            both_active = 0
            one_active = 0
            tick = 0
            while tick < total_dur:
                ai = _sounding_at(sorted_i, tick) is not None
                aj = _sounding_at(sorted_j, tick) is not None
                if ai and aj:
                    both_active += 1
                elif ai or aj:
                    one_active += 1
                tick += sample_step
            total_active = both_active + one_active
            activity_corr = both_active / total_active if total_active else 0.0

            pair_results[pair_key] = {
                "simultaneous_onset_ratio": round(sim_ratio, 4),
                "contrary_motion_ratio": round(
                    contrary / motion_total, 4
                ) if motion_total else 0.0,
                "rhythmic_divergence": round(rhythmic_div, 4),
                "activity_correlation": round(activity_corr, 4),
            }
            all_sim_ratios.append(sim_ratio)
            all_div.append(rhythmic_div)

    return {
        "pair_independence": pair_results,
        "overall": {
            "avg_simultaneous_onset_ratio": round(
                sum(all_sim_ratios) / len(all_sim_ratios), 4
            ) if all_sim_ratios else 0.0,
            "avg_rhythmic_divergence": round(
                sum(all_div) / len(all_div), 4
            ) if all_div else 0.0,
        },
    }


def compute_spacing(
    voices: List[List[Note]],
    tpb: int = TICKS_PER_BEAT,
) -> Dict:
    """Compute spacing (pitch gap) between adjacent voice pairs.

    Args:
        voices: List of note lists, voices[0] = highest (v1).
        tpb: Ticks per beat.

    Returns:
        Dict with pair_spacing and avg_adjacent_gap.
    """
    n_voices = len(voices)
    if n_voices < 2:
        return {"pair_spacing": {}, "avg_adjacent_gap": 0.0}

    total_dur = 0
    sorted_voices: List[List[Note]] = []
    for voice in voices:
        sv = sorted(voice, key=lambda n: n.start_tick)
        sorted_voices.append(sv)
        for n in voice:
            total_dur = max(total_dur, n.start_tick + n.duration)

    pair_results: Dict[str, Dict] = {}
    all_avg_gaps: List[float] = []

    for i in range(n_voices - 1):
        pair_key = f"v{i+1}-v{i+2}"
        gaps: List[int] = []

        tick = 0
        while tick < total_dur:
            ni = _sounding_at(sorted_voices[i], tick)
            nj = _sounding_at(sorted_voices[i + 1], tick)
            if ni is not None and nj is not None:
                gap = abs(ni.pitch - nj.pitch)
                gaps.append(gap)
            tick += tpb

        if gaps:
            avg_gap = sum(gaps) / len(gaps)
            # Build distribution buckets.
            buckets = {"0-2": 0, "3-5": 0, "6-8": 0, "9-12": 0, "13+": 0}
            for g in gaps:
                if g <= 2:
                    buckets["0-2"] += 1
                elif g <= 5:
                    buckets["3-5"] += 1
                elif g <= 8:
                    buckets["6-8"] += 1
                elif g <= 12:
                    buckets["9-12"] += 1
                else:
                    buckets["13+"] += 1
            total_g = len(gaps)
            dist = {k: round(v / total_g, 4) for k, v in buckets.items()}
            close_rate = round(buckets["0-2"] / total_g, 4)
        else:
            avg_gap = 0.0
            dist = {}
            close_rate = 0.0

        pair_results[pair_key] = {
            "avg_gap_semitones": round(avg_gap, 1),
            "min_gap": min(gaps) if gaps else 0,
            "max_gap": max(gaps) if gaps else 0,
            "gap_distribution": dist,
            "close_rate": close_rate,
        }
        all_avg_gaps.append(avg_gap)

    return {
        "pair_spacing": pair_results,
        "avg_adjacent_gap": round(
            sum(all_avg_gaps) / len(all_avg_gaps), 1
        ) if all_avg_gaps else 0.0,
    }


def compute_crossing_detail(
    voices: List[List[Note]],
    tpb: int = TICKS_PER_BEAT,
) -> Dict:
    """Compute detailed voice crossing events between adjacent pairs.

    Args:
        voices: List of note lists, voices[0] = highest (v1).
        tpb: Ticks per beat.

    Returns:
        Dict with crossing_events list and summary.
    """
    n_voices = len(voices)
    if n_voices < 2:
        return {
            "crossing_events": [],
            "summary": {
                "total_crossings": 0,
                "avg_duration_beats": 0.0,
                "resolution_rate": 0.0,
                "quick_resolution_rate": 0.0,
                "per_pair": {},
            },
        }

    total_dur = 0
    sorted_voices: List[List[Note]] = []
    for voice in voices:
        sv = sorted(voice, key=lambda n: n.start_tick)
        sorted_voices.append(sv)
        for n in voice:
            total_dur = max(total_dur, n.start_tick + n.duration)

    events: List[Dict] = []

    for i in range(n_voices - 1):
        pair_key = f"v{i+1}-v{i+2}"
        in_crossing = False
        crossing_start = 0
        max_inversion = 0

        tick = 0
        while tick < total_dur:
            ni = _sounding_at(sorted_voices[i], tick)
            nj = _sounding_at(sorted_voices[i + 1], tick)

            if ni is not None and nj is not None:
                # Voice i (higher rank) should have higher pitch.
                crossed = ni.pitch < nj.pitch
                if crossed:
                    inversion = nj.pitch - ni.pitch
                    if not in_crossing:
                        in_crossing = True
                        crossing_start = tick
                        max_inversion = inversion
                    else:
                        max_inversion = max(max_inversion, inversion)
                elif in_crossing:
                    # Crossing resolved.
                    dur_beats = (tick - crossing_start) / tpb
                    bar = crossing_start // TICKS_PER_BAR + 1
                    events.append({
                        "pair": pair_key,
                        "start_bar": bar,
                        "duration_beats": round(dur_beats, 2),
                        "max_inversion_semitones": max_inversion,
                        "resolved": True,
                    })
                    in_crossing = False
            elif in_crossing:
                # One voice silent — crossing ends.
                dur_beats = (tick - crossing_start) / tpb
                bar = crossing_start // TICKS_PER_BAR + 1
                events.append({
                    "pair": pair_key,
                    "start_bar": bar,
                    "duration_beats": round(dur_beats, 2),
                    "max_inversion_semitones": max_inversion,
                    "resolved": True,
                })
                in_crossing = False

            tick += tpb

        # Handle crossing open at end of piece.
        if in_crossing:
            dur_beats = (total_dur - crossing_start) / tpb
            bar = crossing_start // TICKS_PER_BAR + 1
            events.append({
                "pair": pair_key,
                "start_bar": bar,
                "duration_beats": round(dur_beats, 2),
                "max_inversion_semitones": max_inversion,
                "resolved": False,
            })

    # Summary.
    total_crossings = len(events)
    if total_crossings > 0:
        avg_dur = sum(e["duration_beats"] for e in events) / total_crossings
        resolved = sum(1 for e in events if e["resolved"])
        quick = sum(1 for e in events if e["duration_beats"] <= 2.0)
        per_pair: Dict[str, int] = {}
        for e in events:
            per_pair[e["pair"]] = per_pair.get(e["pair"], 0) + 1
    else:
        avg_dur = 0.0
        resolved = 0
        quick = 0
        per_pair = {}

    return {
        "crossing_events": events,
        "summary": {
            "total_crossings": total_crossings,
            "avg_duration_beats": round(avg_dur, 2),
            "resolution_rate": round(
                resolved / total_crossings, 4
            ) if total_crossings else 0.0,
            "quick_resolution_rate": round(
                quick / total_crossings, 4
            ) if total_crossings else 0.0,
            "per_pair": per_pair,
        },
    }


def compute_activity(
    voices: List[List[Note]],
    total_dur: int,
    tpb: int = TICKS_PER_BEAT,
) -> Dict:
    """Compute per-voice activity patterns.

    Args:
        voices: List of note lists, voices[0] = highest (v1).
        total_dur: Total duration in ticks.
        tpb: Ticks per beat.

    Returns:
        Dict with per_voice stats, entry_order, and simultaneous_activity.
    """
    n_voices = len(voices)
    if n_voices == 0 or total_dur <= 0:
        return {
            "per_voice": {},
            "entry_order": [],
            "simultaneous_activity": {},
        }

    sorted_voices: List[List[Note]] = []
    for voice in voices:
        sorted_voices.append(sorted(voice, key=lambda n: n.start_tick))

    sample_step = max(1, tpb // 4)  # 16th-note resolution

    # Per-voice stats.
    per_voice: Dict[str, Dict] = {}
    entry_times: List[Tuple[str, int]] = []

    for vi, sv in enumerate(sorted_voices):
        vname = f"v{vi+1}"

        if not sv:
            per_voice[vname] = {
                "activity_rate": 0.0,
                "avg_rest_duration_beats": 0.0,
                "longest_rest_beats": 0.0,
            }
            continue

        # Entry time.
        entry_times.append((vname, sv[0].start_tick))

        # Compute activity by sampling.
        active_samples = 0
        total_samples = 0
        tick = 0
        while tick < total_dur:
            if _sounding_at(sv, tick) is not None:
                active_samples += 1
            total_samples += 1
            tick += sample_step

        activity_rate = active_samples / total_samples if total_samples else 0.0

        # Compute rest durations between notes.
        rest_durs: List[float] = []
        # Rest before first note.
        if sv[0].start_tick > 0:
            rest_durs.append(sv[0].start_tick / tpb)
        # Rests between consecutive notes.
        for ni in range(len(sv) - 1):
            end_tick = sv[ni].start_tick + sv[ni].duration
            next_start = sv[ni + 1].start_tick
            if next_start > end_tick:
                rest_durs.append((next_start - end_tick) / tpb)

        per_voice[vname] = {
            "activity_rate": round(activity_rate, 4),
            "avg_rest_duration_beats": round(
                sum(rest_durs) / len(rest_durs), 2
            ) if rest_durs else 0.0,
            "longest_rest_beats": round(
                max(rest_durs), 2
            ) if rest_durs else 0.0,
        }

    # Entry order.
    entry_times.sort(key=lambda x: x[1])
    entry_order = [et[0] for et in entry_times]

    # Simultaneous activity distribution.
    density_counts: Dict[int, int] = {}
    total_samples = 0
    tick = 0
    while tick < total_dur:
        active = sum(
            1 for sv in sorted_voices
            if _sounding_at(sv, tick) is not None
        )
        density_counts[active] = density_counts.get(active, 0) + 1
        total_samples += 1
        tick += sample_step

    simultaneous: Dict[str, float] = {}
    for k in range(n_voices + 1):
        if k == 0:
            continue
        label = f"{k}_voice{'s' if k > 1 else ''}"
        simultaneous[label] = round(
            density_counts.get(k, 0) / total_samples, 4
        ) if total_samples else 0.0

    return {
        "per_voice": per_voice,
        "entry_order": entry_order,
        "simultaneous_activity": simultaneous,
    }


def compute_imitation(
    voices: List[List[Note]],
    tpb: int = TICKS_PER_BEAT,
    min_len: int = 4,
) -> Dict:
    """Detect imitation (repeated interval patterns) between voices.

    Looks for melodic interval patterns of length >= min_len that appear
    in one voice and then in another voice (possibly transposed).

    Args:
        voices: List of note lists, voices[0] = highest (v1).
        tpb: Ticks per beat.
        min_len: Minimum pattern length in intervals.

    Returns:
        Dict with imitation_events and summary.
    """
    n_voices = len(voices)
    if n_voices < 2:
        return {
            "imitation_events": [],
            "summary": {
                "total_imitations": 0,
                "avg_lag_beats": 0.0,
                "most_common_interval": 0,
                "imitative_density": 0.0,
                "voice_pair_frequency": {},
            },
        }

    # Build interval sequences per voice.
    voice_intervals: List[List[Tuple[int, int, int]]] = []  # (interval, start_tick, pitch)
    for vi, voice in enumerate(voices):
        sv = sorted(voice, key=lambda n: n.start_tick)
        intervals: List[Tuple[int, int, int]] = []
        for ni in range(len(sv) - 1):
            iv = sv[ni + 1].pitch - sv[ni].pitch
            intervals.append((iv, sv[ni].start_tick, sv[ni].pitch))
        voice_intervals.append(intervals)

    events: List[Dict] = []
    max_events = 100  # Cap for performance.
    total_dur = 0
    for voice in voices:
        for n in voice:
            total_dur = max(total_dur, n.start_tick + n.duration)

    # For each pair of voices, search for matching interval patterns.
    for li in range(n_voices):
        leader_iv = voice_intervals[li]
        if len(leader_iv) < min_len:
            continue

        for fi in range(n_voices):
            if fi == li:
                continue
            follower_iv = voice_intervals[fi]
            if len(follower_iv) < min_len:
                continue

            # Try each starting position in the leader.
            for l_start in range(len(leader_iv) - min_len + 1):
                pattern = [x[0] for x in leader_iv[l_start:l_start + min_len]]
                l_tick = leader_iv[l_start][1]
                l_pitch = leader_iv[l_start][2]

                # Search in follower (only after leader onset for true imitation).
                for f_start in range(len(follower_iv) - min_len + 1):
                    f_tick = follower_iv[f_start][1]
                    if f_tick <= l_tick:
                        continue  # Follower must come after leader.

                    # Check if intervals match.
                    f_pattern = [
                        x[0] for x in follower_iv[f_start:f_start + min_len]
                    ]
                    if pattern == f_pattern:
                        f_pitch = follower_iv[f_start][2]
                        transposition = f_pitch - l_pitch
                        lag = (f_tick - l_tick) / tpb
                        bar = l_tick // TICKS_PER_BAR + 1
                        events.append({
                            "pattern": pattern,
                            "leader": f"v{li+1}",
                            "follower": f"v{fi+1}",
                            "lag_beats": round(lag, 2),
                            "transposition": transposition,
                            "bar": bar,
                        })
                        if len(events) >= max_events:
                            break
                if len(events) >= max_events:
                    break
            if len(events) >= max_events:
                break
        if len(events) >= max_events:
            break

    # Summary.
    total_imitations = len(events)
    if total_imitations > 0:
        avg_lag = sum(e["lag_beats"] for e in events) / total_imitations
        trans_counts: Dict[int, int] = {}
        for e in events:
            t = abs(e["transposition"]) % 12
            trans_counts[t] = trans_counts.get(t, 0) + 1
        most_common = max(trans_counts, key=trans_counts.get) if trans_counts else 0

        pair_freq: Dict[str, int] = {}
        for e in events:
            pk = f"{e['leader']}-{e['follower']}"
            pair_freq[pk] = pair_freq.get(pk, 0) + 1

        # Imitative density: approximate by counting bars with imitations.
        total_bars = max(1, total_dur // TICKS_PER_BAR)
        imitation_bars = len(set(e["bar"] for e in events))
        density = imitation_bars / total_bars
    else:
        avg_lag = 0.0
        most_common = 0
        pair_freq = {}
        density = 0.0

    return {
        "imitation_events": events,
        "summary": {
            "total_imitations": total_imitations,
            "avg_lag_beats": round(avg_lag, 2),
            "most_common_interval": most_common,
            "imitative_density": round(density, 4),
            "voice_pair_frequency": pair_freq,
        },
    }
