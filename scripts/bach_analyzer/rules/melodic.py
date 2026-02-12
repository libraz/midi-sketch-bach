"""Melodic rules: consecutive repeated notes, excessive leap, leap resolution,
stepwise motion ratio."""

from __future__ import annotations

from typing import List

from ..model import Note, NoteSource, Score, TICKS_PER_BAR, pitch_to_name
from .base import Category, RuleResult, Severity, Violation


# ---------------------------------------------------------------------------
# ConsecutiveRepeatedNotes
# ---------------------------------------------------------------------------


class ConsecutiveRepeatedNotes:
    """Detect >3 consecutive repeated pitches within a voice.

    Notes with pedal_point or ground_bass provenance are exempt because
    structural repetition is inherent to these musical functions.
    """

    _EXEMPT_SOURCES = {NoteSource.PEDAL_POINT, NoteSource.GROUND_BASS}
    _MAX_GAP_TICKS = 2 * TICKS_PER_BAR  # 2 bars

    def __init__(self, max_repeats: int = 3):
        self.max_repeats = max_repeats

    @property
    def name(self) -> str:
        return "consecutive_repeated_notes"

    @property
    def category(self) -> Category:
        return Category.MELODIC

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        for voice_name, notes in score.voices_dict.items():
            # Exclude pedal_point and ground_bass notes from repetition checks.
            sorted_notes = sorted(
                [n for n in notes
                 if not (n.provenance and n.provenance.source in self._EXEMPT_SOURCES)],
                key=lambda n: n.start_tick,
            )
            run_start = 0
            for k in range(1, len(sorted_notes)):
                # Gap too large -> flush and reset run.
                gap = sorted_notes[k].start_tick - sorted_notes[k - 1].start_tick
                if gap > self._MAX_GAP_TICKS:
                    run_len = k - run_start
                    if run_len > self.max_repeats:
                        n = sorted_notes[run_start]
                        violations.append(
                            Violation(
                                rule_name=self.name,
                                category=self.category,
                                severity=Severity.WARNING,
                                bar=n.bar,
                                beat=n.beat,
                                tick=n.start_tick,
                                voice_a=voice_name,
                                description=f"{run_len}x {pitch_to_name(n.pitch)}",
                                source=n.provenance.source if n.provenance else None,
                            )
                        )
                    run_start = k
                    continue
                if sorted_notes[k].pitch != sorted_notes[run_start].pitch:
                    run_len = k - run_start
                    if run_len > self.max_repeats:
                        n = sorted_notes[run_start]
                        violations.append(
                            Violation(
                                rule_name=self.name,
                                category=self.category,
                                severity=Severity.WARNING,
                                bar=n.bar,
                                beat=n.beat,
                                tick=n.start_tick,
                                voice_a=voice_name,
                                description=f"{run_len}x {pitch_to_name(n.pitch)}",
                                source=n.provenance.source if n.provenance else None,
                            )
                        )
                    run_start = k
            # Check final run.
            run_len = len(sorted_notes) - run_start
            if run_len > self.max_repeats:
                n = sorted_notes[run_start]
                violations.append(
                    Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.WARNING,
                        bar=n.bar,
                        beat=n.beat,
                        tick=n.start_tick,
                        voice_a=voice_name,
                        description=f"{run_len}x {pitch_to_name(n.pitch)}",
                        source=n.provenance.source if n.provenance else None,
                    )
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


# ---------------------------------------------------------------------------
# ExcessiveLeap
# ---------------------------------------------------------------------------


class ExcessiveLeap:
    """Detect leaps exceeding a threshold within a voice.

    Allows up to a minor 9th (13 semitones) for normal melodic motion,
    matching Bach's actual practice where leaps beyond an octave are rare.
    Leaps across source boundaries (e.g. fugue_subject -> pedal_point)
    are exempt because they represent voice-role transitions, not melodic jumps.
    """

    def __init__(self, max_semitones: int = 13):
        self.max_semitones = max_semitones

    @property
    def name(self) -> str:
        return "excessive_leap"

    @property
    def category(self) -> Category:
        return Category.MELODIC

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        for voice_name, notes in score.voices_dict.items():
            sorted_notes = sorted(notes, key=lambda n: n.start_tick)
            for k in range(len(sorted_notes) - 1):
                n1, n2 = sorted_notes[k], sorted_notes[k + 1]
                leap = abs(n2.pitch - n1.pitch)
                if leap > self.max_semitones:
                    # Exempt source transitions (voice role changes).
                    src1 = n1.provenance.source if n1.provenance else None
                    src2 = n2.provenance.source if n2.provenance else None
                    if src1 and src2 and src1 != src2:
                        continue
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.ERROR,
                            bar=n2.bar,
                            beat=n2.beat,
                            tick=n2.start_tick,
                            voice_a=voice_name,
                            description=f"{leap} semitones {pitch_to_name(n1.pitch)}->{pitch_to_name(n2.pitch)}",
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
# LeapResolution
# ---------------------------------------------------------------------------


class LeapResolution:
    """Detect leaps >=5 semitones not followed by step in opposite direction.

    Arpeggiated figures (episode_material, arpeggio_flow) are exempt because
    consecutive leaps are idiomatic in sequential and figurative passages.
    """

    _ARPEGGIO_SOURCES = {NoteSource.EPISODE_MATERIAL, NoteSource.ARPEGGIO_FLOW}

    def __init__(self, leap_threshold: int = 5):
        self.leap_threshold = leap_threshold

    @property
    def name(self) -> str:
        return "leap_resolution"

    @property
    def category(self) -> Category:
        return Category.MELODIC

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        for voice_name, notes in score.voices_dict.items():
            sorted_notes = sorted(notes, key=lambda n: n.start_tick)
            for k in range(len(sorted_notes) - 2):
                n1, n2, n3 = sorted_notes[k], sorted_notes[k + 1], sorted_notes[k + 2]
                leap = n2.pitch - n1.pitch
                if abs(leap) < self.leap_threshold:
                    continue
                # Exempt arpeggiated figures (episodes, flow patterns).
                src1 = n1.provenance.source if n1.provenance else None
                src2 = n2.provenance.source if n2.provenance else None
                if (src1 and src1 in self._ARPEGGIO_SOURCES) or (src2 and src2 in self._ARPEGGIO_SOURCES):
                    continue
                # Resolution should step (1-2 semitones) in opposite direction.
                resolution = n3.pitch - n2.pitch
                resolved = (
                    abs(resolution) <= 2
                    and resolution != 0
                    and (leap > 0) != (resolution > 0)
                )
                if not resolved:
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.WARNING,
                            bar=n2.bar,
                            beat=n2.beat,
                            tick=n2.start_tick,
                            voice_a=voice_name,
                            description=f"leap {leap:+d} st not resolved (next {resolution:+d} st)",
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
# StepwiseMotionRatio
# ---------------------------------------------------------------------------


def _is_pedal_voice(voice_name: str, notes: list) -> bool:
    """Detect pedal voice by name or provenance (>80% pedal/ground_bass sources)."""
    name_lower = voice_name.lower()
    if name_lower in ("pedal", "ped"):
        return True
    if not notes:
        return False
    pedal_sources = {NoteSource.PEDAL_POINT, NoteSource.GROUND_BASS}
    pedal_count = sum(1 for n in notes
                      if n.provenance and n.provenance.source in pedal_sources)
    return pedal_count / len(notes) > 0.8


class StepwiseMotionRatio:
    """Report the fraction of stepwise motion (<=2 semitones) per voice.
    Flag voices below threshold (default 0.4)."""

    _PEDAL_MIN_RATIO = 0.2  # Relaxed threshold for pedal voices.

    def __init__(self, min_ratio: float = 0.4):
        self.min_ratio = min_ratio

    @property
    def name(self) -> str:
        return "stepwise_motion_ratio"

    @property
    def category(self) -> Category:
        return Category.MELODIC

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        info_parts = []
        for voice_name, notes in score.voices_dict.items():
            sorted_notes = sorted(notes, key=lambda n: n.start_tick)
            if len(sorted_notes) < 2:
                continue
            steps = sum(
                1
                for k in range(len(sorted_notes) - 1)
                if abs(sorted_notes[k + 1].pitch - sorted_notes[k].pitch) <= 2
            )
            total = len(sorted_notes) - 1
            ratio = steps / total if total > 0 else 1.0
            info_parts.append(f"{voice_name}: {ratio:.2f}")
            threshold = self._PEDAL_MIN_RATIO if _is_pedal_voice(voice_name, sorted_notes) else self.min_ratio
            if ratio < threshold:
                violations.append(
                    Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.INFO,
                        voice_a=voice_name,
                        description=f"stepwise ratio {ratio:.2f} < {self.min_ratio}",
                    )
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info="; ".join(info_parts),
        )


ALL_MELODIC_RULES = [
    ConsecutiveRepeatedNotes,
    ExcessiveLeap,
    LeapResolution,
    StepwiseMotionRatio,
]
