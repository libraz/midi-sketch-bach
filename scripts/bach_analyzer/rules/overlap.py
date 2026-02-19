"""Overlap rules: within-voice overlap and voice spacing."""

from __future__ import annotations

from typing import Dict, List

from ..model import TICKS_PER_BAR, TICKS_PER_BEAT, Note, Score, pitch_to_name, sounding_note_at
from .base import Category, RuleResult, Severity, Violation

if False:  # TYPE_CHECKING
    from ..form_profile import FormProfile


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
    to 36 semitones (three octaves) because large pedal-manual gaps are
    idiomatic in Bach's organ music (pedal range 24-50, manual up to 96).

    Scans every beat position and checks sounding pitches (including sustained
    notes), matching the sustained-note-aware approach used by C++ analysis.
    """

    _PEDAL_NAMES = {"pedal", "ped", "ped."}
    _PEDAL_CHANNEL = 3
    _ORGAN_FORMS = {
        "fugue", "prelude_and_fugue", "trio_sonata", "chorale_prelude",
        "toccata_and_fugue", "passacaglia", "fantasia_and_fugue",
    }

    def __init__(self, max_semitones: int = 12, pedal_max_semitones: int = 36):
        self.max_semitones = max_semitones
        self.pedal_max_semitones = pedal_max_semitones

    def applies_to(self, profile: FormProfile) -> bool:
        return (profile.counterpoint_enabled
                and profile.expected_voices is not None
                and profile.expected_voices[0] >= 2)

    def configure(self, profile: FormProfile) -> None:
        if profile.voice_spacing_max != 12:
            self.max_semitones = profile.voice_spacing_max
        pedal_override = getattr(profile, "voice_spacing_pedal_max", None)
        if pedal_override is not None:
            self.pedal_max_semitones = pedal_override

    def _is_pedal(self, name: str, track_list: list, score: Score) -> bool:
        """Check if a voice is a pedal voice by name, channel, or position.

        In organ forms with 3+ voices, the lowest voice (last track) functions
        as a pedal voice even when not explicitly named "Pedal" or on channel 3.
        BWV 578/543 etc. use the lowest voice for pedal keyboard patterns where
        a tenor-pedal gap exceeding 12 semitones is idiomatic.
        """
        if name.lower() in self._PEDAL_NAMES:
            return True
        for t in track_list:
            if t.name == name and t.channel == self._PEDAL_CHANNEL:
                return True
        # In organ forms with 3+ voices, treat the lowest voice as pedal.
        if (score.form in self._ORGAN_FORMS
                and len(track_list) >= 3
                and track_list[-1].name == name):
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
            involves_pedal = (self._is_pedal(name_upper, score.tracks, score)
                              or self._is_pedal(name_lower, score.tracks, score))
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


# ---------------------------------------------------------------------------
# InstrumentRange
# ---------------------------------------------------------------------------


class InstrumentRange:
    """Check that all notes fall within the instrument's physical range.

    Range is defined by the form profile's instrument_range field.
    Organ forms use channel-based ranges (Ch0-2: 36-96, Ch3: 24-50).
    """

    # Organ channel ranges (fallback when no profile instrument_range).
    _ORGAN_RANGES = {
        0: (36, 96),  # Manual I (Great)
        1: (36, 96),  # Manual II (Swell)
        2: (48, 96),  # Manual III (Positiv)
        3: (24, 50),  # Pedal
    }

    def __init__(self):
        self._profile_range: tuple | None = None
        self._is_organ = False

    @property
    def name(self) -> str:
        return "instrument_range"

    @property
    def category(self) -> Category:
        return Category.OVERLAP

    def applies_to(self, profile: FormProfile) -> bool:
        return (profile.instrument_range is not None
                or profile.style_family == "organ")

    def configure(self, profile: FormProfile) -> None:
        self._profile_range = profile.instrument_range
        self._is_organ = profile.style_family == "organ"

    def check(self, score: Score) -> RuleResult:
        violations: list[Violation] = []

        for track in score.tracks:
            if self._is_organ:
                lo, hi = self._ORGAN_RANGES.get(track.channel, (36, 96))
            elif self._profile_range:
                lo, hi = self._profile_range
            else:
                continue

            for note in track.sorted_notes:
                if note.pitch < lo or note.pitch > hi:
                    violations.append(Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.WARNING,
                        bar=note.bar,
                        beat=note.beat,
                        tick=note.start_tick,
                        voice_a=track.name,
                        description=(
                            f"{pitch_to_name(note.pitch)} (MIDI {note.pitch}) "
                            f"outside range {pitch_to_name(lo)}-{pitch_to_name(hi)} "
                            f"({lo}-{hi})"
                        ),
                        source=note.provenance.source if note.provenance else None,
                    ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


ALL_OVERLAP_RULES = [WithinVoiceOverlap, VoiceSpacing, InstrumentRange]
