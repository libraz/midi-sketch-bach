"""Structure rules: exposition completeness.

Migrated from fugue_detector.py with heuristic fallback when provenance
is unavailable.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Set, Tuple

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

    # Forms where an exposition is expected.
    _FUGUE_FORMS = {"fugue", "prelude_and_fugue", "toccata_and_fugue",
                    "fantasia_and_fugue"}

    def check(self, score: Score) -> RuleResult:
        # Skip for non-fugue forms (passacaglia, chaconne, chorale_prelude, etc.).
        form = getattr(score, "form", "")
        if form and form not in self._FUGUE_FORMS:
            return RuleResult(
                rule_name=self.name,
                category=self.category,
                passed=True,
                violations=[],
                info=f"skipped for form '{form}'",
            )
        if score.has_provenance:
            return self._check_with_provenance(score)
        return self._check_heuristic(score)

    def _check_with_provenance(self, score: Score) -> RuleResult:
        """Check using provenance data: each voice must have a subject or answer entry,
        and entries should alternate subject/answer."""
        entered_voices: Set[str] = set()
        subject_sources = {NoteSource.FUGUE_SUBJECT, NoteSource.FUGUE_ANSWER}

        # Collect entries with timing for order check.
        entries: List[Tuple[int, int, "NoteSource"]] = []  # (start_tick, entry_number, source)
        for note in score.all_notes:
            if note.provenance and note.provenance.source in subject_sources:
                entered_voices.add(note.voice)
                entries.append((note.start_tick, note.provenance.entry_number,
                                note.provenance.source))

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

        # Check subject/answer alternation order.
        if entries:
            # Deduplicate by entry_number (take first note of each entry).
            seen_entries: Dict[int, Tuple[int, "NoteSource"]] = {}
            for tick, entry_num, src in entries:
                if entry_num not in seen_entries or tick < seen_entries[entry_num][0]:
                    seen_entries[entry_num] = (tick, src)
            ordered = sorted(seen_entries.values(), key=lambda x: x[0])
            for k in range(len(ordered) - 1):
                _, src1 = ordered[k]
                _, src2 = ordered[k + 1]
                # Subject should alternate with answer.
                if src1 == src2:
                    tick2 = ordered[k + 1][0]
                    src_name = "subject" if src2 == NoteSource.FUGUE_SUBJECT else "answer"
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.WARNING,
                            bar=tick2 // TICKS_PER_BAR + 1,
                            tick=tick2,
                            description=f"consecutive {src_name} entries (expected alternation)",
                        )
                    )

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=all(v.severity != Severity.ERROR for v in violations) if violations else True,
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
