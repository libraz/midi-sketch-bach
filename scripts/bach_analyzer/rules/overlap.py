"""Overlap rules: within-voice overlap and voice spacing."""

from __future__ import annotations

from typing import Dict, List

from ..model import TICKS_PER_BEAT, Note, Score, pitch_to_name
from .base import Category, RuleResult, Severity, Violation


# ---------------------------------------------------------------------------
# WithinVoiceOverlap
# ---------------------------------------------------------------------------


class WithinVoiceOverlap:
    """Detect overlapping notes within the same voice (critical bug indicator)."""

    @property
    def name(self) -> str:
        return "within_voice_overlap"

    @property
    def category(self) -> Category:
        return Category.OVERLAP

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        for voice_name, notes in score.voices_dict.items():
            sorted_notes = sorted(notes, key=lambda n: n.start_tick)
            for k in range(len(sorted_notes) - 1):
                n1, n2 = sorted_notes[k], sorted_notes[k + 1]
                if n1.end_tick > n2.start_tick:
                    overlap = n1.end_tick - n2.start_tick
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.CRITICAL,
                            bar=n2.bar,
                            beat=n2.beat,
                            tick=n2.start_tick,
                            voice_a=voice_name,
                            description=(
                                f"{pitch_to_name(n1.pitch)} ends {n1.end_tick} overlaps "
                                f"{pitch_to_name(n2.pitch)} starts {n2.start_tick} "
                                f"(overlap {overlap} ticks)"
                            ),
                            source=n2.provenance.source if n2.provenance else None,
                        )
                    )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


# ---------------------------------------------------------------------------
# VoiceSpacing
# ---------------------------------------------------------------------------


class VoiceSpacing:
    """Detect excessive spacing (>12 semitones) between adjacent voices on
    the same beat."""

    def __init__(self, max_semitones: int = 12):
        self.max_semitones = max_semitones

    @property
    def name(self) -> str:
        return "voice_spacing"

    @property
    def category(self) -> Category:
        return Category.OVERLAP

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        track_order = [(t.name, t.notes) for t in score.tracks]
        for i in range(len(track_order) - 1):
            name_upper, notes_upper = track_order[i]
            name_lower, notes_lower = track_order[i + 1]
            beats_u: Dict[int, Note] = {n.start_tick: n for n in notes_upper}
            beats_l: Dict[int, Note] = {n.start_tick: n for n in notes_lower}
            for tick in sorted(set(beats_u) & set(beats_l)):
                nu, nl = beats_u[tick], beats_l[tick]
                gap = abs(nu.pitch - nl.pitch)
                if gap > self.max_semitones:
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.WARNING,
                            bar=nu.bar,
                            beat=nu.beat,
                            tick=tick,
                            voice_a=name_upper,
                            voice_b=name_lower,
                            description=f"{gap} semitones apart: {pitch_to_name(nu.pitch)} / {pitch_to_name(nl.pitch)}",
                        )
                    )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


ALL_OVERLAP_RULES = [WithinVoiceOverlap, VoiceSpacing]
