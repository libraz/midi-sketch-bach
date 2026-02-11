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
