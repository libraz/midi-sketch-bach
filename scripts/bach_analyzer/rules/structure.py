"""Structure rules: exposition completeness.

Migrated from fugue_detector.py with heuristic fallback when provenance
is unavailable.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Set

from ..model import Note, NoteSource, Score, TICKS_PER_BAR
from .base import Category, RuleResult, Severity, Violation


# ---------------------------------------------------------------------------
# ExpositionCompleteness
# ---------------------------------------------------------------------------


class ExpositionCompleteness:
    """Check that all voices enter with subject or answer in the exposition.

    With provenance: uses NoteSource to detect subject/answer entries.
    Without provenance: heuristic based on first note of each voice
    appearing within the first N bars.
    """

    def __init__(self, max_expo_bars: int = 12):
        self.max_expo_bars = max_expo_bars

    @property
    def name(self) -> str:
        return "exposition_completeness"

    @property
    def category(self) -> Category:
        return Category.STRUCTURE

    def check(self, score: Score) -> RuleResult:
        if score.has_provenance:
            return self._check_with_provenance(score)
        return self._check_heuristic(score)

    def _check_with_provenance(self, score: Score) -> RuleResult:
        """Check using provenance data: each voice must have a subject or answer entry."""
        entered_voices: Set[str] = set()
        subject_sources = {NoteSource.FUGUE_SUBJECT, NoteSource.FUGUE_ANSWER}
        for note in score.all_notes:
            if note.provenance and note.provenance.source in subject_sources:
                entered_voices.add(note.voice)
        all_voices = {t.name for t in score.tracks}
        missing = all_voices - entered_voices
        violations = []
        if missing:
            for v in sorted(missing):
                violations.append(
                    Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.ERROR,
                        voice_a=v,
                        description=f"voice '{v}' has no subject/answer entry",
                    )
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info=f"entered: {sorted(entered_voices)}, missing: {sorted(missing)}",
        )

    def _check_heuristic(self, score: Score) -> RuleResult:
        """Without provenance: each voice must have notes within the first N bars."""
        expo_end = self.max_expo_bars * TICKS_PER_BAR
        entered: Set[str] = set()
        for track in score.tracks:
            for note in track.notes:
                if note.start_tick < expo_end:
                    entered.add(track.name)
                    break
        all_voices = {t.name for t in score.tracks}
        missing = all_voices - entered
        violations = []
        if missing:
            for v in sorted(missing):
                violations.append(
                    Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.ERROR,
                        voice_a=v,
                        description=f"voice '{v}' has no notes in first {self.max_expo_bars} bars (heuristic)",
                    )
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info=f"entered (heuristic): {sorted(entered)}, missing: {sorted(missing)}",
        )


ALL_STRUCTURE_RULES = [ExpositionCompleteness]
