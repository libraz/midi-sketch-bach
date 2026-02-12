"""Cadence rules: final cadence presence and cadence density."""

from __future__ import annotations

from enum import Enum
from typing import Dict, List, Optional

from ..model import (
    CONSONANCES,
    Note,
    NoteSource,
    OCTAVE,
    PERFECT_5TH,
    PERFECT_CONSONANCES,
    Score,
    TICKS_PER_BAR,
    TICKS_PER_BEAT,
    UNISON,
    interval_class,
    pitch_to_name,
    sounding_note_at,
)
from .base import Category, RuleResult, Severity, Violation

if False:  # TYPE_CHECKING
    from ..form_profile import FormProfile


class CadenceType(Enum):
    """Cadence classification types."""
    PERFECT = "perfect"      # V->I, bass P5 descent
    PLAGAL = "plagal"        # IV->I, bass P4 descent
    HALF = "half"            # ->V, bass arrives on dominant
    DECEPTIVE = "deceptive"  # V->vi, bass P5 descent to 6th degree


def _detect_cadence_at(bass_notes: List[Note], soprano_notes: List[Note],
                       tick: int, score: Score) -> Optional[CadenceType]:
    """Detect a cadence candidate at the given tick.

    Returns a CadenceType if detected, None otherwise.
    Conditions checked:
    1. Strong beat (beat 1 or beat 3)
    2. Various cadence patterns based on bass motion and outer voice intervals.
    """
    beat_in_bar = (tick % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
    if beat_in_bar not in (1, 3):
        return None

    bass = sounding_note_at(bass_notes, tick)
    sop = sounding_note_at(soprano_notes, tick)
    if bass is None or sop is None:
        return None

    iv = interval_class(sop.pitch - bass.pitch)

    # Check bass motion from previous bar's same beat position.
    prev_tick = tick - TICKS_PER_BAR
    if prev_tick < 0:
        return None
    prev_bass = sounding_note_at(bass_notes, prev_tick)
    if prev_bass is None:
        return None

    bass_interval = bass.pitch - prev_bass.pitch
    abs_iv = abs(bass_interval) % 12

    # Provenance-based check: chord_degree 5->1 = perfect cadence.
    if (prev_bass.provenance and prev_bass.provenance.chord_degree == 5
            and bass.provenance and bass.provenance.chord_degree == 1):
        return CadenceType.PERFECT

    # Perfect cadence: V->I (bass P5 descent or P4 ascent) + outer voices perfect consonance.
    if iv in PERFECT_CONSONANCES:
        if abs_iv == 7 and bass_interval < 0:
            return CadenceType.PERFECT  # P5 descent
        if abs_iv == 5 and bass_interval > 0:
            return CadenceType.PERFECT  # P4 ascent

    # Deceptive cadence: V->vi (bass P5 descent or P4 ascent but outer voices imperfect).
    if iv not in PERFECT_CONSONANCES and iv in CONSONANCES:
        if abs_iv == 7 and bass_interval < 0:
            return CadenceType.DECEPTIVE
        if abs_iv == 5 and bass_interval > 0:
            return CadenceType.DECEPTIVE

    # Plagal cadence: IV->I (bass P4 descent) + outer voices perfect consonance.
    if iv in PERFECT_CONSONANCES:
        if abs_iv == 5 and bass_interval < 0:
            return CadenceType.PLAGAL

    # Half cadence: arriving on V (bass P5 ascent or P4 descent) + consonant outer voices.
    if iv in CONSONANCES:
        if abs_iv == 7 and bass_interval > 0:
            return CadenceType.HALF  # ascending P5 to dominant
        if abs_iv == 5 and bass_interval < 0:
            return CadenceType.HALF  # descending P4 to dominant

    return None


def _get_outer_voices(score: Score):
    """Return (soprano_notes, bass_notes) sorted lists."""
    if len(score.tracks) < 2:
        return None, None
    soprano = score.tracks[0].sorted_notes
    bass = score.tracks[-1].sorted_notes
    return soprano, bass


# ---------------------------------------------------------------------------
# FinalCadence
# ---------------------------------------------------------------------------


class FinalCadence:
    """Check that a cadence exists in the last 2 bars."""

    @property
    def name(self) -> str:
        return "final_cadence"

    @property
    def category(self) -> Category:
        return Category.CADENCE

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.cadence_validation and profile.counterpoint_enabled

    def configure(self, profile: FormProfile) -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        soprano, bass = _get_outer_voices(score)
        if soprano is None or bass is None:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="insufficient voices for cadence check",
            )

        total_bars = score.total_bars
        if total_bars < 2:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="score too short",
            )

        # Scan last 2 bars for a cadence.
        found = False
        start_tick = (total_bars - 2) * TICKS_PER_BAR
        end_tick = total_bars * TICKS_PER_BAR
        tick = start_tick
        while tick < end_tick:
            if _detect_cadence_at(bass, soprano, tick, score) is not None:
                found = True
                break
            tick += TICKS_PER_BEAT

        violations = []
        if not found:
            violations.append(Violation(
                rule_name=self.name,
                category=self.category,
                severity=Severity.WARNING,
                bar=total_bars,
                beat=1,
                tick=(total_bars - 1) * TICKS_PER_BAR,
                description="no cadence detected in final 2 bars",
            ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


# ---------------------------------------------------------------------------
# CadenceDensity
# ---------------------------------------------------------------------------


class CadenceDensity:
    """Check that cadences occur at reasonable intervals (at least 1 per 8 bars)."""

    def __init__(self, bars_per_cadence: int = 8):
        self.bars_per_cadence = bars_per_cadence

    @property
    def name(self) -> str:
        return "cadence_density"

    @property
    def category(self) -> Category:
        return Category.CADENCE

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.cadence_validation and profile.counterpoint_enabled

    def configure(self, profile: FormProfile) -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        soprano, bass = _get_outer_voices(score)
        if soprano is None or bass is None:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="insufficient voices for cadence check",
            )

        total_bars = score.total_bars
        if total_bars < self.bars_per_cadence:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="score too short for density check",
            )

        # Find all cadence positions with types.
        cadence_bars = []
        cadence_type_counts: Dict[str, int] = {}
        end_tick = score.total_duration
        tick = 0
        while tick < end_tick:
            ctype = _detect_cadence_at(bass, soprano, tick, score)
            if ctype is not None:
                cadence_bars.append(tick // TICKS_PER_BAR + 1)
                cadence_type_counts[ctype.value] = cadence_type_counts.get(ctype.value, 0) + 1
            tick += TICKS_PER_BEAT

        # Check for gaps exceeding bars_per_cadence.
        violations = []
        prev_bar = 0
        for cb in cadence_bars:
            if cb - prev_bar > self.bars_per_cadence:
                violations.append(Violation(
                    rule_name=self.name,
                    category=self.category,
                    severity=Severity.INFO,
                    bar=prev_bar + 1,
                    description=f"no cadence for {cb - prev_bar} bars (bars {prev_bar + 1}-{cb})",
                ))
            prev_bar = cb
        # Check trailing gap.
        if total_bars - prev_bar > self.bars_per_cadence:
            violations.append(Violation(
                rule_name=self.name,
                category=self.category,
                severity=Severity.INFO,
                bar=prev_bar + 1,
                description=f"no cadence for {total_bars - prev_bar} bars (bars {prev_bar + 1}-{total_bars})",
            ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info=f"cadences at bars: {cadence_bars}, types: {cadence_type_counts}",
        )


ALL_CADENCE_RULES = [FinalCadence, CadenceDensity]
