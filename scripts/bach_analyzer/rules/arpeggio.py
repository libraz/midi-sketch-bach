"""Arpeggio rules: solo string leap feasibility and pattern consistency."""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from ..model import (
    Note,
    NoteSource,
    Score,
    TICKS_PER_BAR,
    pitch_to_name,
)
from .base import Category, RuleResult, Severity, Violation

if False:  # TYPE_CHECKING
    from ..form_profile import FormProfile


# ---------------------------------------------------------------------------
# SoloStringLeapFeasibility
# ---------------------------------------------------------------------------


class SoloStringLeapFeasibility:
    """Check for consecutive large leaps in solo string parts.

    Primary indicator: >12 semitones occurring 3+ times consecutively -> WARNING.
    Physical feasibility is not the concern (21 semitone leaps are possible);
    stylistic naturalness is.
    """

    def __init__(self, large_leap_threshold: int = 12, max_consecutive: int = 3):
        self.large_leap_threshold = large_leap_threshold
        self.max_consecutive = max_consecutive

    @property
    def name(self) -> str:
        return "solo_string_leap_feasibility"

    @property
    def category(self) -> Category:
        return Category.ARPEGGIO

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.style_family.startswith("solo_string")

    def configure(self, profile: FormProfile) -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []

        for voice_name, notes in score.voices_dict.items():
            sorted_notes = sorted(notes, key=lambda n: n.start_tick)
            if len(sorted_notes) < 2:
                continue

            consecutive_large = 0
            run_start_note: Optional[Note] = None

            for k in range(len(sorted_notes) - 1):
                n1, n2 = sorted_notes[k], sorted_notes[k + 1]
                leap = abs(n2.pitch - n1.pitch)

                if leap > self.large_leap_threshold:
                    if consecutive_large == 0:
                        run_start_note = n1
                    consecutive_large += 1
                else:
                    if consecutive_large >= self.max_consecutive and run_start_note:
                        violations.append(Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.WARNING,
                            bar=run_start_note.bar,
                            beat=run_start_note.beat,
                            tick=run_start_note.start_tick,
                            voice_a=voice_name,
                            description=(
                                f"{consecutive_large} consecutive leaps >{self.large_leap_threshold}st "
                                f"starting at {pitch_to_name(run_start_note.pitch)}"
                            ),
                        ))
                    consecutive_large = 0
                    run_start_note = None

            # Flush trailing run.
            if consecutive_large >= self.max_consecutive and run_start_note:
                violations.append(Violation(
                    rule_name=self.name,
                    category=self.category,
                    severity=Severity.WARNING,
                    bar=run_start_note.bar,
                    beat=run_start_note.beat,
                    tick=run_start_note.start_tick,
                    voice_a=voice_name,
                    description=(
                        f"{consecutive_large} consecutive leaps >{self.large_leap_threshold}st "
                        f"starting at {pitch_to_name(run_start_note.pitch)}"
                    ),
                ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


# ---------------------------------------------------------------------------
# ArpeggioPatternConsistency
# ---------------------------------------------------------------------------


class ArpeggioPatternConsistency:
    """Check for abrupt range changes within arpeggio sections.

    Groups notes by source == ARPEGGIO_FLOW into bar-level sections and
    detects sudden range shifts -> INFO.
    """

    def __init__(self, max_range_change: int = 12):
        self.max_range_change = max_range_change

    @property
    def name(self) -> str:
        return "arpeggio_pattern_consistency"

    @property
    def category(self) -> Category:
        return Category.ARPEGGIO

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.style_family.startswith("solo_string")

    def configure(self, profile: FormProfile) -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []

        # Collect arpeggio notes grouped by bar.
        arpeggio_notes: Dict[int, List[Note]] = {}
        for note in score.all_notes:
            if note.provenance and note.provenance.source == NoteSource.ARPEGGIO_FLOW:
                bar = note.bar
                if bar not in arpeggio_notes:
                    arpeggio_notes[bar] = []
                arpeggio_notes[bar].append(note)

        if not arpeggio_notes:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="no arpeggio_flow notes found",
            )

        # Compare consecutive bars.
        sorted_bars = sorted(arpeggio_notes.keys())
        prev_range: Optional[Tuple[int, int]] = None

        for bar in sorted_bars:
            notes = arpeggio_notes[bar]
            pitches = [n.pitch for n in notes]
            cur_range = (min(pitches), max(pitches))

            if prev_range is not None:
                low_shift = abs(cur_range[0] - prev_range[0])
                high_shift = abs(cur_range[1] - prev_range[1])
                if low_shift > self.max_range_change or high_shift > self.max_range_change:
                    violations.append(Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.INFO,
                        bar=bar,
                        beat=1,
                        tick=(bar - 1) * TICKS_PER_BAR,
                        description=(
                            f"arpeggio range shift: "
                            f"{pitch_to_name(prev_range[0])}-{pitch_to_name(prev_range[1])} -> "
                            f"{pitch_to_name(cur_range[0])}-{pitch_to_name(cur_range[1])}"
                        ),
                    ))

            prev_range = cur_range

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


# ---------------------------------------------------------------------------
# ImpliedPolyphony
# ---------------------------------------------------------------------------


class ImpliedPolyphony:
    """Estimate implied voice count in solo string pieces.

    Single-line solo string works (like the Chaconne BWV 1004) imply multiple
    voices through register jumps and alternating melodic strands. This rule
    estimates the number of implied voices by detecting large leaps (>P5) that
    signal voice-strand switching.

    Expected range for chaconne-style pieces: 2.0-3.0 implied voices.
    """

    _VOICE_SWITCH_THRESHOLD = 7  # P5 = 7 semitones

    def __init__(self, min_implied: float = 2.0, max_implied: float = 3.0):
        self.min_implied = min_implied
        self.max_implied = max_implied

    @property
    def name(self) -> str:
        return "implied_polyphony"

    @property
    def category(self) -> Category:
        return Category.ARPEGGIO

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.style_family == "solo_string_arch"

    def configure(self, profile: FormProfile) -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []

        for voice_name, notes in score.voices_dict.items():
            sorted_notes = sorted(notes, key=lambda n: n.start_tick)
            if len(sorted_notes) < 4:
                continue

            implied_count = self._estimate_implied_voices(sorted_notes)

            if implied_count < self.min_implied or implied_count > self.max_implied:
                violations.append(Violation(
                    rule_name=self.name,
                    category=self.category,
                    severity=Severity.INFO,
                    voice_a=voice_name,
                    description=(
                        f"implied voice count {implied_count:.1f} "
                        f"outside [{self.min_implied}-{self.max_implied}]"
                    ),
                ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )

    def _estimate_implied_voices(self, sorted_notes: List[Note]) -> float:
        """Estimate implied voice count via register clustering.

        Groups notes into pitch-range clusters based on large leaps.
        The number of active clusters approximates implied voices.
        """
        if len(sorted_notes) < 2:
            return 1.0

        # Track active pitch "strands" by their last pitch.
        strands: List[int] = [sorted_notes[0].pitch]

        for i in range(1, len(sorted_notes)):
            cur_pitch = sorted_notes[i].pitch
            prev_pitch = sorted_notes[i - 1].pitch
            leap = abs(cur_pitch - prev_pitch)

            if leap > self._VOICE_SWITCH_THRESHOLD:
                # Check if this pitch is close to an existing strand.
                closest_strand = min(range(len(strands)),
                                     key=lambda s: abs(strands[s] - cur_pitch))
                if abs(strands[closest_strand] - cur_pitch) <= self._VOICE_SWITCH_THRESHOLD:
                    strands[closest_strand] = cur_pitch
                else:
                    strands.append(cur_pitch)
            else:
                # Update the most recent strand (sequential motion).
                if strands:
                    closest_strand = min(range(len(strands)),
                                         key=lambda s: abs(strands[s] - cur_pitch))
                    strands[closest_strand] = cur_pitch

        # Merge strands that have converged (within threshold).
        merged = []
        for s in sorted(strands):
            if not merged or abs(s - merged[-1]) > self._VOICE_SWITCH_THRESHOLD:
                merged.append(s)
            else:
                merged[-1] = (merged[-1] + s) // 2

        return float(len(merged))


ALL_ARPEGGIO_RULES = [SoloStringLeapFeasibility, ArpeggioPatternConsistency, ImpliedPolyphony]
