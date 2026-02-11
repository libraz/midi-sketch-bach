"""Dissonance rules: strong-beat dissonance and unresolved dissonance."""

from __future__ import annotations

from typing import Dict, List, Tuple

from ..model import (
    CONSONANCES,
    PERFECT_4TH,
    TICKS_PER_BEAT,
    TICKS_PER_BAR,
    Note,
    Score,
    interval_class,
    is_consonant,
    pitch_to_name,
)
from .base import Category, RuleResult, Severity, Violation


def _notes_at_tick(all_notes: List[Note], tick: int, tolerance: int = 0) -> List[Note]:
    """Find notes sounding at a given tick (start_tick <= tick < end_tick)."""
    return [n for n in all_notes if n.start_tick <= tick < n.end_tick]


def _beat_ticks(score: Score) -> List[int]:
    """Return all beat positions (ticks) in the score."""
    total = score.total_duration
    ticks = []
    t = 0
    while t < total:
        ticks.append(t)
        t += TICKS_PER_BEAT
    return ticks


# ---------------------------------------------------------------------------
# StrongBeatDissonance
# ---------------------------------------------------------------------------


class StrongBeatDissonance:
    """Detect dissonant intervals on strong beats (1 and 3 in 4/4).

    In 3+ voices, P4 is treated as consonant (contextually acceptable when
    a lower voice provides a bass foundation).
    """

    @property
    def name(self) -> str:
        return "strong_beat_dissonance"

    @property
    def category(self) -> Category:
        return Category.DISSONANCE

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        all_notes = score.all_notes
        num_voices = score.num_voices
        total = score.total_duration

        tick = 0
        while tick < total:
            beat_in_bar = (tick % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
            if beat_in_bar not in (1, 3):
                tick += TICKS_PER_BEAT
                continue
            sounding = _notes_at_tick(all_notes, tick)
            if len(sounding) < 2:
                tick += TICKS_PER_BEAT
                continue
            # Check all pairs.
            for i in range(len(sounding)):
                for j in range(i + 1, len(sounding)):
                    na, nb = sounding[i], sounding[j]
                    iv = interval_class(na.pitch - nb.pitch)
                    # P4 consonant in 3+ voices
                    if iv == PERFECT_4TH and num_voices >= 3:
                        continue
                    if iv not in CONSONANCES:
                        violations.append(
                            Violation(
                                rule_name=self.name,
                                category=self.category,
                                severity=Severity.WARNING,
                                bar=tick // TICKS_PER_BAR + 1,
                                beat=beat_in_bar,
                                tick=tick,
                                voice_a=na.voice,
                                voice_b=nb.voice,
                                description=f"dissonant interval {iv} st on beat {beat_in_bar}: {pitch_to_name(na.pitch)}/{pitch_to_name(nb.pitch)}",
                                source=na.provenance.source if na.provenance else None,
                            )
                        )
            tick += TICKS_PER_BEAT
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


# ---------------------------------------------------------------------------
# UnresolvedDissonance
# ---------------------------------------------------------------------------


class UnresolvedDissonance:
    """Detect dissonances that are not resolved by step to consonance.

    Checks whether a dissonant note on any beat resolves by stepwise motion
    (1-2 semitones) to a consonant interval in the next beat.
    """

    @property
    def name(self) -> str:
        return "unresolved_dissonance"

    @property
    def category(self) -> Category:
        return Category.DISSONANCE

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        all_notes = score.all_notes
        num_voices = score.num_voices
        beats = _beat_ticks(score)

        for b_idx in range(len(beats) - 1):
            tick = beats[b_idx]
            next_tick = beats[b_idx + 1]
            sounding = _notes_at_tick(all_notes, tick)
            if len(sounding) < 2:
                continue
            for i in range(len(sounding)):
                for j in range(i + 1, len(sounding)):
                    na, nb = sounding[i], sounding[j]
                    iv = interval_class(na.pitch - nb.pitch)
                    if iv == PERFECT_4TH and num_voices >= 3:
                        continue
                    if iv in CONSONANCES:
                        continue
                    # Found a dissonance. Check resolution at next beat.
                    next_sounding = _notes_at_tick(all_notes, next_tick)
                    resolved = self._check_resolved(na, nb, next_sounding)
                    if not resolved:
                        violations.append(
                            Violation(
                                rule_name=self.name,
                                category=self.category,
                                severity=Severity.ERROR,
                                bar=tick // TICKS_PER_BAR + 1,
                                beat=(tick % TICKS_PER_BAR) // TICKS_PER_BEAT + 1,
                                tick=tick,
                                voice_a=na.voice,
                                voice_b=nb.voice,
                                description=f"unresolved dissonance {iv} st: {pitch_to_name(na.pitch)}/{pitch_to_name(nb.pitch)}",
                                source=na.provenance.source if na.provenance else None,
                            )
                        )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )

    def _check_resolved(self, na: Note, nb: Note, next_sounding: List[Note]) -> bool:
        """Check if the dissonance between na/nb resolves at the next beat."""
        # Find the continuation notes for both voices.
        next_a = [n for n in next_sounding if n.voice == na.voice]
        next_b = [n for n in next_sounding if n.voice == nb.voice]
        if not next_a or not next_b:
            return True  # Voice drops out -> resolved by rest
        # Check if at least one voice moves by step and the resulting interval is consonant.
        for nxa in next_a:
            for nxb in next_b:
                step_a = abs(nxa.pitch - na.pitch) <= 2 and nxa.pitch != na.pitch
                step_b = abs(nxb.pitch - nb.pitch) <= 2 and nxb.pitch != nb.pitch
                new_iv = interval_class(nxa.pitch - nxb.pitch)
                if (step_a or step_b) and new_iv in CONSONANCES:
                    return True
        return False


ALL_DISSONANCE_RULES = [StrongBeatDissonance, UnresolvedDissonance]
