"""Overlap rules: within-voice overlap and voice spacing."""

from __future__ import annotations

from typing import Dict, List

from ..model import TICKS_PER_BAR, TICKS_PER_BEAT, Note, Score, pitch_to_name, sounding_note_at
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
    """Detect excessive spacing between adjacent voices.

    Default threshold is 12 semitones (one octave) between manual voices.
    When one voice is a pedal voice (organ pedal), the threshold is relaxed
    to 24 semitones (two octaves) because large pedal-manual gaps are
    idiomatic in Bach's organ music.

    Scans every beat position and checks sounding pitches (including sustained
    notes), matching the sustained-note-aware approach used by C++ analysis.
    """

    _PEDAL_NAMES = {"pedal", "ped", "ped."}
    _PEDAL_CHANNEL = 3

    def __init__(self, max_semitones: int = 12, pedal_max_semitones: int = 24):
        self.max_semitones = max_semitones
        self.pedal_max_semitones = pedal_max_semitones

    def _is_pedal(self, name: str, track_list: list) -> bool:
        """Check if a voice is a pedal voice by name or channel."""
        if name.lower() in self._PEDAL_NAMES:
            return True
        for t in track_list:
            if t.name == name and t.channel == self._PEDAL_CHANNEL:
                return True
        return False

    @property
    def name(self) -> str:
        return "voice_spacing"

    @property
    def category(self) -> Category:
        return Category.OVERLAP

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        track_order = [(t.name, t.sorted_notes) for t in score.tracks]
        if len(track_order) < 2:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, violations=[],
            )
        end_tick = score.total_duration
        for i in range(len(track_order) - 1):
            name_upper, notes_upper = track_order[i]
            name_lower, notes_lower = track_order[i + 1]
            # Use relaxed threshold if either voice is a pedal voice.
            involves_pedal = (self._is_pedal(name_upper, score.tracks)
                              or self._is_pedal(name_lower, score.tracks))
            threshold = self.pedal_max_semitones if involves_pedal else self.max_semitones
            beat = 0
            while beat < end_tick:
                nu = sounding_note_at(notes_upper, beat)
                nl = sounding_note_at(notes_lower, beat)
                if nu is not None and nl is not None:
                    gap = abs(nu.pitch - nl.pitch)
                    if gap > threshold:
                        bar = beat // TICKS_PER_BAR + 1
                        beat_in_bar = (beat % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
                        violations.append(
                            Violation(
                                rule_name=self.name,
                                category=self.category,
                                severity=Severity.WARNING,
                                bar=bar,
                                beat=beat_in_bar,
                                tick=beat,
                                voice_a=name_upper,
                                voice_b=name_lower,
                                description=f"{gap} semitones apart: {pitch_to_name(nu.pitch)} / {pitch_to_name(nl.pitch)}",
                            )
                        )
                beat += TICKS_PER_BEAT
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


ALL_OVERLAP_RULES = [WithinVoiceOverlap, VoiceSpacing]
