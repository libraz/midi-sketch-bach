"""Diagnosis: map NoteSource to responsible source file and group violations."""

from __future__ import annotations

from collections import Counter, defaultdict
from typing import Dict, List, Optional, Tuple

from .model import NoteSource
from .rules.base import Violation

# NoteSource -> responsible C++ source file (mirrors CLAUDE.md provenance table).
SOURCE_FILE_MAP: Dict[NoteSource, str] = {
    NoteSource.FUGUE_SUBJECT: "src/fugue/subject.cpp",
    NoteSource.FUGUE_ANSWER: "src/fugue/answer.cpp",
    NoteSource.COUNTERSUBJECT: "src/fugue/countersubject.cpp",
    NoteSource.EPISODE_MATERIAL: "src/fugue/episode.cpp",
    NoteSource.FREE_COUNTERPOINT: "src/counterpoint/",
    NoteSource.CANTUS_FIXED: "src/counterpoint/cantus_firmus.cpp",
    NoteSource.ORNAMENT: "src/ornament/",
    NoteSource.PEDAL_POINT: "src/fugue/",
    NoteSource.ARPEGGIO_FLOW: "src/solo_string/flow/",
    NoteSource.TEXTURE_NOTE: "src/solo_string/arch/texture_generator.cpp",
    NoteSource.GROUND_BASS: "src/solo_string/arch/ground_bass.cpp",
    NoteSource.COLLISION_AVOID: "src/counterpoint/collision_resolver.cpp",
    NoteSource.POST_PROCESS: "varies",
    NoteSource.CHROMATIC_PASSING: "src/counterpoint/",
    NoteSource.FALSE_ENTRY: "src/fugue/",
    NoteSource.CODA: "src/fugue/",
    NoteSource.SEQUENCE_NOTE: "src/fugue/",
    NoteSource.CANON_DUX: "src/forms/goldberg/canon/canon_generator.cpp",
    NoteSource.CANON_COMES: "src/forms/goldberg/canon/canon_generator.cpp",
    NoteSource.CANON_FREE_BASS: "src/forms/goldberg/canon/canon_generator.cpp",
    NoteSource.GOLDBERG_ARIA: "src/forms/goldberg/goldberg_aria.cpp",
    NoteSource.GOLDBERG_BASS: "src/forms/goldberg/",
    NoteSource.GOLDBERG_FIGURA: "src/forms/goldberg/goldberg_figuren.cpp",
    NoteSource.GOLDBERG_SOGGETTO: "src/forms/goldberg/",
    NoteSource.GOLDBERG_DANCE: "src/forms/goldberg/variations/goldberg_dance.cpp",
    NoteSource.GOLDBERG_FUGHETTA: "src/forms/goldberg/",
    NoteSource.GOLDBERG_INVENTION: "src/forms/goldberg/",
    NoteSource.QUODLIBET_MELODY: "src/forms/goldberg/",
    NoteSource.GOLDBERG_OVERTURE: "src/forms/goldberg/",
    NoteSource.GOLDBERG_SUSPENSION: "src/forms/goldberg/",
}


def source_file_for(source: Optional[NoteSource]) -> Optional[str]:
    """Return the responsible source file for a NoteSource, or None."""
    if source is None:
        return None
    return SOURCE_FILE_MAP.get(source)


def group_by_source(violations: List[Violation]) -> Dict[str, List[Violation]]:
    """Group violations by their responsible source file.

    Violations without provenance are grouped under "unknown".
    """
    groups: Dict[str, List[Violation]] = defaultdict(list)
    for v in violations:
        path = source_file_for(v.source) or "unknown"
        groups[path].append(v)
    return dict(groups)


def hotspot_ranking(violations: List[Violation]) -> List[Tuple[str, int, float]]:
    """Rank source files by violation count.

    Returns:
        List of (source_file, count, percentage) tuples sorted descending.
    """
    groups = group_by_source(violations)
    total = len(violations)
    if total == 0:
        return []
    ranked = [(path, len(vios), len(vios) / total) for path, vios in groups.items()]
    ranked.sort(key=lambda x: x[1], reverse=True)
    return ranked


# ---------------------------------------------------------------------------
# Dissonance diagnostics (Phase 5-A)
# ---------------------------------------------------------------------------

INTERVAL_NAMES = {
    0: "P1/P8", 1: "m2", 2: "M2", 3: "m3", 4: "M3",
    5: "P4", 6: "TT", 7: "P5", 8: "m6", 9: "M6",
    10: "m7", 11: "M7",
}


def _primary_modified_by(flags: int) -> str:
    """Return the name of the highest-priority modification bit."""
    if flags == 0:
        return "none"
    from .model import NoteModifiedBy
    for bit_val in sorted(NoteModifiedBy.__members__.values(), reverse=True):
        if bit_val > 0 and flags & bit_val:
            return bit_val.name.lower()
    return "none"


def compute_dissonance_diagnostics(
    batch_results: List[Dict],
    target_rule: str = "strong_beat_dissonance",
) -> Dict:
    """Compute detailed diagnostics for a specific dissonance rule.

    Args:
        batch_results: List of per-seed result dicts from run_batch (with diagnostics=True).
        target_rule: The rule name to analyze.

    Returns:
        Dict with source/interval/beat/voice_pair/modified_by breakdowns.
    """
    from .model import NoteModifiedBy

    all_details = []
    seeds_analyzed = 0
    for r in batch_results:
        if r.get("error"):
            continue
        seeds_analyzed += 1
        for d in r.get("violation_details", []):
            if d["rule"] == target_rule:
                all_details.append(d)

    total = len(all_details)
    if total == 0:
        return {
            "rule": target_rule,
            "total_violations": 0,
            "seeds_analyzed": seeds_analyzed,
            "avg_per_seed": 0.0,
            "unknown_source_rate": 0.0,
            "source_breakdown": {},
            "source_pair_breakdown": {},
            "interval_distribution": {},
            "beat_distribution": {},
            "voice_pair_breakdown": {},
            "modified_by_bits": {},
            "modified_by_primary": {},
        }

    avg = total / seeds_analyzed if seeds_analyzed > 0 else 0.0

    # Unknown source rate
    unknown_count = sum(1 for d in all_details if d["source_a"] == "unknown")
    unknown_rate = unknown_count / total

    # Source breakdown (source_a only)
    source_counter: Counter = Counter()
    for d in all_details:
        source_counter[d["source_a"]] += 1
    source_breakdown = {
        k: {"count": v, "pct": round(v / total * 100, 1)}
        for k, v in source_counter.most_common()
    }

    # Source pair breakdown
    pair_counter: Counter = Counter()
    for d in all_details:
        pair = "+".join(sorted([d["source_a"], d["source_b"]]))
        pair_counter[pair] += 1
    source_pair_breakdown = {
        k: {"count": v, "pct": round(v / total * 100, 1)}
        for k, v in pair_counter.most_common()
    }

    # Interval distribution
    iv_counter: Counter = Counter()
    for d in all_details:
        if d["interval"] is not None:
            iv_counter[d["interval"]] += 1
    interval_distribution = {}
    for iv in sorted(iv_counter.keys()):
        count = iv_counter[iv]
        interval_distribution[str(iv)] = {
            "name": INTERVAL_NAMES.get(iv, f"?{iv}"),
            "count": count,
            "pct": round(count / total * 100, 1),
        }

    # Beat distribution
    beat_counter: Counter = Counter()
    for d in all_details:
        beat_counter[d["beat"]] += 1
    beat_distribution = {
        str(b): {"count": c, "pct": round(c / total * 100, 1)}
        for b, c in sorted(beat_counter.items())
    }

    # Voice pair breakdown
    vp_counter: Counter = Counter()
    for d in all_details:
        pair = "<>".join(sorted([d["voice_a"], d["voice_b"]]))
        vp_counter[pair] += 1
    voice_pair_breakdown = {
        k: {"count": v, "pct": round(v / total * 100, 1)}
        for k, v in vp_counter.most_common()
    }

    # Modified-by bits (each bit counted independently, sum > 100% possible)
    bit_counter: Counter = Counter()
    for d in all_details:
        for flags in (d["modified_by_a"], d["modified_by_b"]):
            if flags == 0:
                bit_counter["none"] += 1
            else:
                for member in NoteModifiedBy.__members__.values():
                    if member > 0 and flags & member:
                        bit_counter[member.name.lower()] += 1
    total_flag_slots = total * 2  # two notes per violation
    modified_by_bits = {
        k: {"count": v, "pct": round(v / total_flag_slots * 100, 1)}
        for k, v in bit_counter.most_common()
    }

    # Modified-by primary (highest bit per note, sum = 100%)
    primary_counter: Counter = Counter()
    for d in all_details:
        primary_counter[_primary_modified_by(d["modified_by_a"])] += 1
        primary_counter[_primary_modified_by(d["modified_by_b"])] += 1
    modified_by_primary = {
        k: {"count": v, "pct": round(v / total_flag_slots * 100, 1)}
        for k, v in primary_counter.most_common()
    }

    return {
        "rule": target_rule,
        "total_violations": total,
        "seeds_analyzed": seeds_analyzed,
        "avg_per_seed": round(avg, 1),
        "unknown_source_rate": round(unknown_rate, 4),
        "source_breakdown": source_breakdown,
        "source_pair_breakdown": source_pair_breakdown,
        "interval_distribution": interval_distribution,
        "beat_distribution": beat_distribution,
        "voice_pair_breakdown": voice_pair_breakdown,
        "modified_by_bits": modified_by_bits,
        "modified_by_primary": modified_by_primary,
    }
