"""Dissonance rules: strong-beat dissonance and unresolved dissonance."""

from __future__ import annotations

from typing import Dict, List, Tuple

from ..model import (
    CONSONANCES,
    NoteSource,
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

if False:  # TYPE_CHECKING
    from ..form_profile import FormProfile


def _notes_at_tick(all_notes: List[Note], tick: int) -> List[Note]:
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

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.counterpoint_enabled

    def configure(self, profile: FormProfile) -> None:
        pass

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
        next_tick = tick + TICKS_PER_BEAT
        # Check preparation: note.pitch was sounding at the previous beat.
        # Extended to 2-beat lookback to recognize chain suspensions where the
        # previous beat's dissonance resolution serves as preparation for the
        # next suspension (standard Baroque practice).
        prepared = False
        for lookback in (1, 2):
            prev_tick = tick - TICKS_PER_BEAT * lookback
            if prev_tick < 0:
                continue
            prev_sounding = _notes_at_tick(all_notes, prev_tick)
            if any(n.pitch == note.pitch and n.voice == note.voice
                   for n in prev_sounding):
                prepared = True
                break
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

    @staticmethod
    def _is_dominant_seventh_context(sounding: List[Note]) -> bool:
        """Check if sounding notes form a dominant 7th chord context.

        A dominant 7th chord consists of root, M3, P5, m7 (pitch class
        intervals 0, 4, 7, 10).  If the sounding notes are a subset of
        such a chord, the m7 and tritone (3rd-7th) dissonances are
        structurally expected and should not be flagged.
        """
        if len(sounding) < 3:
            return False
        pitch_classes = sorted(set(n.pitch % 12 for n in sounding))
        for base in pitch_classes:
            v7 = {base, (base + 4) % 12, (base + 7) % 12, (base + 10) % 12}
            if set(pitch_classes).issubset(v7):
                return True
        return False

    @staticmethod
    def _is_diminished_seventh_context(sounding: List[Note]) -> bool:
        """Check if sounding notes form a diminished 7th chord context.

        A diminished 7th chord consists of notes spaced 3 semitones apart
        (e.g., B-D-F-Ab = pitch classes 11,2,5,8). If the sounding notes
        are a subset of such a chord, dissonances are structurally expected.
        """
        if len(sounding) < 3:
            return False
        pitch_classes = sorted(set(n.pitch % 12 for n in sounding))
        for base in pitch_classes:
            dim7 = {base, (base + 3) % 12, (base + 6) % 12, (base + 9) % 12}
            if set(pitch_classes).issubset(dim7):
                return True
        return False

    @staticmethod
    def _is_diatonic_seventh_context(sounding: List[Note]) -> bool:
        """Check if sounding notes form a diatonic seventh chord context.

        Recognizes minor 7th (ii7, iii7, vi7) and half-diminished 7th (viiø7)
        chords. Major 7th is excluded as Bach treats it as decorative rather
        than a stable harmonic sonority.

        Only activates with 3+ simultaneous notes to ensure sufficient harmonic
        density for reliable chord recognition.
        """
        if len(sounding) < 3:
            return False
        pitch_classes = sorted(set(n.pitch % 12 for n in sounding))
        # Minor 7th: (0, 3, 7, 10), Half-diminished 7th: (0, 3, 6, 10)
        templates = [
            (0, 3, 7, 10),
            (0, 3, 6, 10),
        ]
        for base in pitch_classes:
            for template in templates:
                chord = {(base + iv) % 12 for iv in template}
                if set(pitch_classes).issubset(chord):
                    return True
        return False

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        all_notes = score.all_notes
        num_voices = score.num_internal_voices
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
            # Check chord context once per beat.
            dim7_context = self._is_diminished_seventh_context(sounding)
            v7_context = self._is_dominant_seventh_context(sounding)
            seventh_context = self._is_diatonic_seventh_context(sounding)
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
                        # Diminished 7th / dominant 7th context: downgrade to INFO.
                        sev = Severity.INFO if (dim7_context or v7_context or seventh_context) else Severity.WARNING
                        violations.append(
                            Violation(
                                rule_name=self.name,
                                category=self.category,
                                severity=sev,
                                bar=tick // TICKS_PER_BAR + 1,
                                beat=beat_in_bar,
                                tick=tick,
                                voice_a=na.voice,
                                voice_b=nb.voice,
                                description=f"dissonant interval {iv} st on beat {beat_in_bar}: {pitch_to_name(na.pitch)}/{pitch_to_name(nb.pitch)}",
                                source=na.provenance.source if na.provenance else None,
                                source_b=nb.provenance.source if nb.provenance else None,
                                interval_semitones=iv,
                                modified_by_a=na.modified_by,
                                modified_by_b=nb.modified_by,
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

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.counterpoint_enabled

    def configure(self, profile: FormProfile) -> None:
        pass

    # Compound intervals (> octave) are perceptually less dissonant.
    _COMPOUND_THRESHOLD = 12
    # Notes shorter than a half-beat are likely passing/neighbor tones.
    _SHORT_NOTE_LIMIT = TICKS_PER_BEAT // 2  # 240 ticks

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        all_notes = score.all_notes
        num_voices = score.num_internal_voices
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
                        # Ground bass on weak (non-accent) beats: structural
                        # dissonance inherent to passacaglia form.
                        is_ground_bass_pair = (
                            (na.provenance and na.provenance.source == NoteSource.GROUND_BASS)
                            or (nb.provenance and nb.provenance.source == NoteSource.GROUND_BASS)
                        )
                        # Use accent-based check (beats 1, 3 = strong in 4/4).
                        beat_in_bar = (tick % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
                        is_accent = beat_in_bar in (1, 3)

                        if is_accent:
                            # Downgrade to WARNING when a diatonic 7th chord
                            # context is present and resolution motion (step
                            # movement or chord change) occurs within 3 beats.
                            is_seventh = (
                                StrongBeatDissonance._is_dominant_seventh_context(sounding)
                                or StrongBeatDissonance._is_diminished_seventh_context(sounding)
                                or StrongBeatDissonance._is_diatonic_seventh_context(sounding)
                            )
                            if is_seventh:
                                has_resolution = False
                                for look in range(1, 4):
                                    fut_idx = b_idx + look
                                    if fut_idx >= len(beats):
                                        break
                                    fut_sounding = _notes_at_tick(all_notes, beats[fut_idx])
                                    # Either voice resolves by step.
                                    for resolver in (na, nb):
                                        fut_voice = [n for n in fut_sounding
                                                     if n.voice == resolver.voice]
                                        for fnt in fut_voice:
                                            if 1 <= abs(fnt.pitch - resolver.pitch) <= 2:
                                                has_resolution = True
                                                break
                                        if has_resolution:
                                            break
                                    # Chord change also counts as resolution.
                                    if not has_resolution and fut_sounding:
                                        fut_pcs = {n.pitch % 12 for n in fut_sounding}
                                        curr_pcs = {n.pitch % 12 for n in sounding}
                                        if fut_pcs != curr_pcs:
                                            has_resolution = True
                                    if has_resolution:
                                        break
                                sev = Severity.WARNING if has_resolution else Severity.ERROR
                            else:
                                sev = Severity.ERROR
                        elif is_ground_bass_pair:
                            sev = Severity.INFO
                        else:
                            sev = Severity.WARNING
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
                                source_b=nb.provenance.source if nb.provenance else None,
                                interval_semitones=iv,
                                modified_by_a=na.modified_by,
                                modified_by_b=nb.modified_by,
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
