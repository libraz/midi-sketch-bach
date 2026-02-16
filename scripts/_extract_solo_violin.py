# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "mcp[cli]>=1.0.0",
# ]
# ///
"""Extract harmonic reference data for solo_violin (sonata + partita)."""

from __future__ import annotations

import json
import statistics
import sys
from collections import Counter
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.bach_reference_server import (
    _estimate_chords_for_work,
    _get_index,
    _get_key_info,
    _load_score,
    _DEGREE_TO_FUNCTION,
    _degree_to_roman,
    sounding_note_at,
    CHORD_TEMPLATES,
    TICKS_PER_BEAT,
    TICKS_PER_BAR,
    _beat_strength,
)


def _count_parallel_perfects_for_score(score, raw_data):
    """Count parallel perfect consonances per 100 beats."""
    if len(score.tracks) < 2:
        return 0.0

    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)
    total_beats = score.total_duration / tpb if tpb > 0 else 0
    if total_beats < 2:
        return 0.0

    STRICT_PERFECTS = {0, 7}
    parallel_count = 0
    sample_step = int(tpb)

    for ti in range(len(score.tracks)):
        for tj in range(ti + 1, len(score.tracks)):
            notes_a = score.tracks[ti].sorted_notes
            notes_b = score.tracks[tj].sorted_notes
            prev_iv = None
            tick = 0
            while tick < score.total_duration:
                na = sounding_note_at(notes_a, tick)
                nb = sounding_note_at(notes_b, tick)
                if na and nb:
                    iv = abs(na.pitch - nb.pitch) % 12
                    if prev_iv is not None and iv in STRICT_PERFECTS and prev_iv == iv:
                        parallel_count += 1
                    prev_iv = iv
                else:
                    prev_iv = None
                tick += sample_step

    return round(parallel_count * 100.0 / total_beats, 2) if total_beats > 0 else 0.0


def _compute_nct_distribution(score, raw_data, chords, sample_interval_beats=1.0):
    """Compute NCT type distribution."""
    tpb = raw_data.get("ticks_per_beat", TICKS_PER_BEAT)
    sample_ticks = int(sample_interval_beats * tpb)

    all_notes = []
    for trk in score.tracks:
        all_notes.extend(trk.sorted_notes)
    all_notes.sort(key=lambda n: (n.start_tick, n.pitch))

    if not all_notes or not chords:
        return {}

    def _chord_at_tick(tick):
        if sample_ticks <= 0:
            return None
        idx = tick // sample_ticks
        if 0 <= idx < len(chords):
            return chords[idx]
        return None

    def _chord_pcs(chord):
        template = CHORD_TEMPLATES.get(chord.quality, frozenset())
        return frozenset((chord.root_pc + pc) % 12 for pc in template)

    nct_types = Counter()
    ct_count = 0
    nct_count = 0

    for note_idx, note_obj in enumerate(all_notes):
        chord = _chord_at_tick(note_obj.start_tick)
        if chord is None or chord.quality == "" or chord.confidence < 0.4:
            continue

        note_pc = note_obj.pitch % 12
        chord_pc_set = _chord_pcs(chord)

        if note_pc in chord_pc_set:
            ct_count += 1
            continue

        nct_count += 1
        prev_note = all_notes[note_idx - 1] if note_idx > 0 else None
        next_note = all_notes[note_idx + 1] if note_idx + 1 < len(all_notes) else None

        if prev_note and next_note:
            if (note_obj.pitch == prev_note.pitch
                    and _beat_strength(note_obj.start_tick) >= 0.5
                    and 1 <= note_obj.pitch - next_note.pitch <= 2):
                nct_types["suspension"] += 1
                continue

        if prev_note and next_note:
            iv_in = note_obj.pitch - prev_note.pitch
            iv_out = next_note.pitch - note_obj.pitch
            is_step_in = 1 <= abs(iv_in) <= 2
            is_step_out = 1 <= abs(iv_out) <= 2

            if is_step_in and is_step_out:
                prev_chord = _chord_at_tick(prev_note.start_tick)
                next_chord = _chord_at_tick(next_note.start_tick)
                prev_is_ct = (prev_chord and prev_chord.quality != ""
                              and prev_note.pitch % 12 in _chord_pcs(prev_chord))
                next_is_ct = (next_chord and next_chord.quality != ""
                              and next_note.pitch % 12 in _chord_pcs(next_chord))

                if prev_is_ct and next_is_ct:
                    if (iv_in > 0 and iv_out > 0) or (iv_in < 0 and iv_out < 0):
                        nct_types["passing"] += 1
                        continue
                    else:
                        nct_types["neighbor"] += 1
                        continue

        nct_types["other"] += 1

    if nct_count == 0:
        return {}

    return {
        ntype: round(nct_types.get(ntype, 0) / nct_count, 3)
        for ntype in ("passing", "neighbor", "suspension", "other")
    }


def _count_cadences_for_work(chords, score, sample_interval_beats=1.0):
    """Count cadences per 8 bars."""
    total_bars = score.total_bars
    if total_bars == 0 or not chords:
        return 0.0

    cadence_count = 0
    tpb = TICKS_PER_BEAT
    sample_ticks = int(sample_interval_beats * tpb)

    for idx in range(1, len(chords)):
        prev = chords[idx - 1]
        curr = chords[idx]
        if prev.confidence < 0.3 or curr.confidence < 0.3:
            continue
        if prev.degree == 4 and curr.degree == 0:
            tick_at = idx * sample_ticks
            beat_str = _beat_strength(tick_at)
            if beat_str >= 0.5:
                cadence_count += 1

    return round(cadence_count * 8.0 / total_bars, 2)


# solo_violin = solo_violin_sonata + solo_violin_partita
idx = _get_index()
sonata_works = idx.filter(category="solo_violin_sonata")
partita_works = idx.filter(category="solo_violin_partita")
all_works = sonata_works + partita_works
print(f"solo_violin total: {len(all_works)} works", file=sys.stderr)

func_accum = Counter()
degree_accum = Counter()
cadence_rates = []
nct_dists = []
pp_rates = []
total_confident = 0
work_count = 0

for w in all_works:
    work_id = w["id"]
    print(f"  {work_id}...", file=sys.stderr, end="", flush=True)

    chords, stats = _estimate_chords_for_work(work_id, 1.0)
    if not chords:
        print(" skip (no chords)", file=sys.stderr)
        continue

    tonic_pc, is_minor, key_conf = _get_key_info(work_id)
    if tonic_pc is None:
        tonic_pc = 0
        is_minor = False

    confident = [c for c in chords if c.confidence > 0.0]
    work_confident = len(confident)

    for chord in confident:
        func = _DEGREE_TO_FUNCTION.get(chord.degree, "?")
        func_accum[func] += 1

    for chord in confident:
        roman = _degree_to_roman(chord.degree, chord.quality, is_minor)
        degree_accum[roman] += 1

    total_confident += work_confident

    score, raw_data, err = _load_score(work_id)
    if err or score is None or raw_data is None:
        print(" skip (load error)", file=sys.stderr)
        continue

    cad_rate = _count_cadences_for_work(chords, score, 1.0)
    cadence_rates.append(cad_rate)

    nct_dist = _compute_nct_distribution(score, raw_data, chords, 1.0)
    if nct_dist:
        nct_dists.append(nct_dist)

    pp_rate = _count_parallel_perfects_for_score(score, raw_data)
    pp_rates.append(pp_rate)

    work_count += 1
    print(f" OK ({work_confident} confident)", file=sys.stderr)

# Aggregate
func_total = sum(func_accum.values())
func_dist = {}
if func_total > 0:
    for func in ["T", "S", "D", "M"]:
        func_dist[func] = round(func_accum.get(func, 0) / func_total, 3)

deg_total = sum(degree_accum.values())
degree_dist = {}
if deg_total > 0:
    for roman, count in degree_accum.most_common():
        ratio = round(count / deg_total, 3)
        if ratio >= 0.005:
            degree_dist[roman] = ratio

avg_nct = {}
if nct_dists:
    for ntype in ("passing", "neighbor", "suspension", "other"):
        vals = [d.get(ntype, 0.0) for d in nct_dists]
        avg_nct[ntype] = round(sum(vals) / len(vals), 3)

cad_mean = round(statistics.mean(cadence_rates), 1) if cadence_rates else 0.0
cad_std = round(statistics.stdev(cadence_rates), 1) if len(cadence_rates) > 1 else 0.5

pp_mean = round(statistics.mean(pp_rates), 2) if pp_rates else 0.0
pp_std = round(statistics.stdev(pp_rates), 2) if len(pp_rates) > 1 else 0.5

result = {
    "function": func_dist,
    "harmony_degrees": degree_dist,
    "nct_types": avg_nct,
    "cadences_per_8_bars": {"mean": cad_mean, "std": cad_std},
    "parallel_perfects_per_100_beats": {"mean": pp_mean, "std": pp_std},
    "work_count": work_count,
}

print(json.dumps(result, indent=2))
