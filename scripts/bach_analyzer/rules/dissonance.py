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

    Suspensions (preparation -> dissonance on strong beat -> stepwise resolution)
    are exempt because they are idiomatic Baroque practice.
    """

    @property
    def name(self) -> str:
        return "strong_beat_dissonance"

    @property
    def category(self) -> Category:
        return Category.DISSONANCE

    def _is_suspension(self, note: Note, partner: Note,
                       all_notes: List[Note], tick: int,
                       num_voices: int) -> bool:
        """Check if a dissonance is a prepared suspension.

        A suspension has three phases:
        1. Preparation: the dissonant pitch sounds as a consonance on the
           previous beat.
        2. Suspension: the note is held (or re-attacked) while the other voice
           moves, creating a dissonance on the strong beat.
        3. Resolution: the suspended voice resolves by step (1-2 semitones),
           typically downward.
        """
        prev_tick = tick - TICKS_PER_BEAT
        next_tick = tick + TICKS_PER_BEAT
        if prev_tick < 0:
            return False
        # Check preparation: note.pitch was sounding at prev_tick.
        prev_sounding = _notes_at_tick(all_notes, prev_tick)
        prepared = any(n.pitch == note.pitch and n.voice == note.voice
                       for n in prev_sounding)
        if not prepared:
            # Also accept appoggiatura: approached by step and resolves by step.
            return self._is_accented_dissonance(note, partner, all_notes, tick, num_voices)
        # Check resolution: note's voice moves by step (1-2 semitones) on next beat
        # to a consonant interval with the partner voice.
        next_sounding = _notes_at_tick(all_notes, next_tick)
        next_note_voice = [n for n in next_sounding if n.voice == note.voice]
        next_partner = [n for n in next_sounding if n.voice == partner.voice]
        for nn in next_note_voice:
            step = abs(nn.pitch - note.pitch)
            if step < 1 or step > 2:
                continue
            # Check resulting interval is consonant.
            for np_ in next_partner:
                iv = interval_class(nn.pitch - np_.pitch)
                if iv == PERFECT_4TH and num_voices >= 3:
                    return True
                if iv in CONSONANCES:
                    return True
            # Partner may have dropped out (rest) -> resolved.
            if not next_partner:
                return True
        return False

    def _is_accented_dissonance(self, note: Note, partner: Note,
                               all_notes: List[Note], tick: int,
                               num_voices: int) -> bool:
        """Check if a dissonance is an accented dissonance (appoggiatura,
        accented passing tone, or accented neighbor tone) that resolves
        by step."""
        next_tick = tick + TICKS_PER_BEAT
        next_sounding = _notes_at_tick(all_notes, next_tick)
        next_note_voice = [n for n in next_sounding if n.voice == note.voice]
        for nn in next_note_voice:
            step = abs(nn.pitch - note.pitch)
            if 1 <= step <= 2:
                next_partner = [n for n in next_sounding if n.voice == partner.voice]
                for np_ in next_partner:
                    iv = interval_class(nn.pitch - np_.pitch)
                    if iv == PERFECT_4TH and num_voices >= 3:
                        return True
                    if iv in CONSONANCES:
                        return True
                if not next_partner:
                    return True
        return False

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
                        # Exempt suspensions and appoggiaturas.
                        if self._is_suspension(na, nb, all_notes, tick, num_voices):
                            continue
                        if self._is_suspension(nb, na, all_notes, tick, num_voices):
                            continue
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

    # Compound intervals (> octave) are perceptually less dissonant.
    _COMPOUND_THRESHOLD = 12
    # Notes shorter than a half-beat are likely passing/neighbor tones.
    _SHORT_NOTE_LIMIT = TICKS_PER_BEAT // 2  # 240 ticks

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
                    # Skip compound intervals (voices > octave apart).
                    actual_dist = abs(na.pitch - nb.pitch)
                    if actual_dist > self._COMPOUND_THRESHOLD:
                        continue
                    iv = interval_class(na.pitch - nb.pitch)
                    if iv == PERFECT_4TH and num_voices >= 3:
                        continue
                    if iv in CONSONANCES:
                        continue
                    # Skip short passing/neighbor tones (duration <= half beat).
                    if na.duration <= self._SHORT_NOTE_LIMIT or nb.duration <= self._SHORT_NOTE_LIMIT:
                        continue
                    # Found a dissonance. Check resolution within 3 beats.
                    # Bach's suspensions and 7th-chord resolutions can span
                    # longer arcs than 2 beats (e.g., chain suspensions).
                    resolved = False
                    for look in range(1, 4):
                        fut_idx = b_idx + look
                        if fut_idx >= len(beats):
                            break
                        fut_sounding = _notes_at_tick(all_notes, beats[fut_idx])
                        if self._check_resolved(na, nb, fut_sounding, num_voices):
                            resolved = True
                            break
                    if not resolved:
                        # Strong beats (1, 3) are ERROR; weak beats are WARNING.
                        beat_in_bar = (tick % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
                        sev = Severity.ERROR if beat_in_bar in (1, 3) else Severity.WARNING
                        violations.append(
                            Violation(
                                rule_name=self.name,
                                category=self.category,
                                severity=sev,
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

    def _check_resolved(self, na: Note, nb: Note, next_sounding: List[Note],
                        num_voices: int) -> bool:
        """Check if the dissonance between na/nb resolves at the next beat.

        Baroque resolution convention: the voice holding the dissonant note
        (typically the upper, suspended voice) resolves by step, preferably
        downward.  We check both voices as potential resolvers, but prioritize
        the one that resolves by descending step (traditional suspension
        resolution).
        """
        next_a = [n for n in next_sounding if n.voice == na.voice]
        next_b = [n for n in next_sounding if n.voice == nb.voice]
        if not next_a or not next_b:
            return True  # Voice drops out -> resolved by rest.
        for nxa in next_a:
            for nxb in next_b:
                new_iv = interval_class(nxa.pitch - nxb.pitch)
                if new_iv == PERFECT_4TH and num_voices >= 3:
                    consonant = True
                else:
                    consonant = new_iv in CONSONANCES
                if not consonant:
                    continue
                # Prefer: the dissonant note resolves by descending step.
                step_a_down = 1 <= (na.pitch - nxa.pitch) <= 2
                step_b_down = 1 <= (nb.pitch - nxb.pitch) <= 2
                if step_a_down or step_b_down:
                    return True
                # Accept ascending step resolution (less common but valid).
                step_a = abs(nxa.pitch - na.pitch) <= 2 and nxa.pitch != na.pitch
                step_b = abs(nxb.pitch - nb.pitch) <= 2 and nxb.pitch != nb.pitch
                if step_a or step_b:
                    return True
        return False


ALL_DISSONANCE_RULES = [StrongBeatDissonance, UnresolvedDissonance]
