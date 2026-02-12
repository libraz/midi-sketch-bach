"""Counterpoint rules: parallel perfect, hidden perfect, voice crossing,
cross-relation, augmented leap.

Ported from analyzer.py and extended to match C++ counterpoint_analyzer.h semantics.
"""

from __future__ import annotations

from collections import defaultdict
from typing import Dict, List, Tuple

from ..model import (
    NoteSource,
    OCTAVE,
    PERFECT_5TH,
    PERFECT_CONSONANCES,
    TICKS_PER_BAR,
    TICKS_PER_BEAT,
    TRITONE,
    UNISON,
    Note,
    Score,
    interval_class,
    pitch_to_name,
    sounding_note_at,
)
from .base import Category, RuleResult, Severity, Violation


def _voices_sorted(score: Score) -> Dict[str, List[Note]]:
    """Return voice dict with notes sorted by start_tick."""
    return score.voices_dict


def _beat_map(notes: List[Note]) -> Dict[int, Note]:
    """Map start_tick -> note (last note wins if duplicates)."""
    return {n.start_tick: n for n in notes}


# ---------------------------------------------------------------------------
# ParallelPerfect
# ---------------------------------------------------------------------------


class ParallelPerfect:
    """Detect parallel P5/P8 between all voice pairs."""

    @property
    def name(self) -> str:
        return "parallel_perfect"

    @property
    def category(self) -> Category:
        return Category.COUNTERPOINT

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        voices = _voices_sorted(score)
        names = sorted(voices.keys())
        end_tick = score.total_duration
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                violations.extend(
                    self._check_pair(names[i], voices[names[i]], names[j], voices[names[j]], end_tick)
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )

    def _check_pair(
        self, name_a: str, va: List[Note], name_b: str, vb: List[Note], end_tick: int
    ) -> List[Violation]:
        """Scan beat-by-beat using sounding_note_at to catch sustained-note parallels."""
        violations = []
        prev_na = prev_nb = None
        beat = 0
        while beat < end_tick:
            na = sounding_note_at(va, beat)
            nb = sounding_note_at(vb, beat)
            if na is not None and nb is not None:
                if prev_na is not None and prev_nb is not None:
                    iv1 = interval_class(prev_na.pitch - prev_nb.pitch)
                    iv2 = interval_class(na.pitch - nb.pitch)
                    if iv1 in PERFECT_CONSONANCES and iv1 == iv2:
                        dir_a = na.pitch - prev_na.pitch
                        dir_b = nb.pitch - prev_nb.pitch
                        if dir_a != 0 and dir_b != 0 and (dir_a > 0) == (dir_b > 0):
                            iv_name = "P5" if iv1 == PERFECT_5TH else ("P8" if iv1 in (UNISON, OCTAVE) else f"P{iv1}")
                            bar = beat // TICKS_PER_BAR + 1
                            beat_in_bar = (beat % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
                            violations.append(
                                Violation(
                                    rule_name=self.name,
                                    category=self.category,
                                    severity=Severity.CRITICAL,
                                    bar=bar,
                                    beat=beat_in_bar,
                                    tick=beat,
                                    voice_a=name_a,
                                    voice_b=name_b,
                                    description=f"{iv_name}->{iv_name} {pitch_to_name(prev_na.pitch)}/{pitch_to_name(prev_nb.pitch)} -> {pitch_to_name(na.pitch)}/{pitch_to_name(nb.pitch)}",
                                    source=na.provenance.source if na.provenance else None,
                                )
                            )
                prev_na, prev_nb = na, nb
            else:
                prev_na = prev_nb = None
            beat += TICKS_PER_BEAT
        return violations


# ---------------------------------------------------------------------------
# HiddenPerfect
# ---------------------------------------------------------------------------


class HiddenPerfect:
    """Detect hidden (direct) 5ths/8ths: similar motion arriving at P5/P8
    where both voices leap."""

    @property
    def name(self) -> str:
        return "hidden_perfect"

    @property
    def category(self) -> Category:
        return Category.COUNTERPOINT

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        voices = _voices_sorted(score)
        names = sorted(voices.keys())
        end_tick = score.total_duration
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                violations.extend(
                    self._check_pair(names[i], voices[names[i]], names[j], voices[names[j]], end_tick)
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )

    def _check_pair(
        self, name_a: str, va: List[Note], name_b: str, vb: List[Note], end_tick: int
    ) -> List[Violation]:
        """Scan beat-by-beat using sounding_note_at to catch sustained-note hidden parallels."""
        violations = []
        prev_na = prev_nb = None
        beat = 0
        while beat < end_tick:
            na = sounding_note_at(va, beat)
            nb = sounding_note_at(vb, beat)
            if na is not None and nb is not None:
                if prev_na is not None and prev_nb is not None:
                    iv2 = interval_class(na.pitch - nb.pitch)
                    if iv2 in PERFECT_CONSONANCES:
                        dir_a = na.pitch - prev_na.pitch
                        dir_b = nb.pitch - prev_nb.pitch
                        # Same direction (similar motion), both non-zero
                        if dir_a != 0 and dir_b != 0 and (dir_a > 0) == (dir_b > 0):
                            # Both voices leap (>2 semitones)
                            if abs(dir_a) > 2 and abs(dir_b) > 2:
                                # Not already a parallel perfect (caught separately)
                                iv1 = interval_class(prev_na.pitch - prev_nb.pitch)
                                if iv1 != iv2:
                                    iv_name = "P5" if iv2 == PERFECT_5TH else "P8"
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
                                            voice_a=name_a,
                                            voice_b=name_b,
                                            description=f"hidden {iv_name}: both leap to {pitch_to_name(na.pitch)}/{pitch_to_name(nb.pitch)}",
                                            source=na.provenance.source if na.provenance else None,
                                        )
                                    )
                prev_na, prev_nb = na, nb
            else:
                prev_na = prev_nb = None
            beat += TICKS_PER_BEAT
        return violations


# ---------------------------------------------------------------------------
# VoiceCrossing
# ---------------------------------------------------------------------------


class VoiceCrossing:
    """Detect voice crossing: upper voice pitch below lower voice pitch.

    Scans every beat position and checks the sounding pitch of each voice
    (including sustained notes), matching C++ countVoiceCrossings().
    A 2-beat lookahead suppresses temporary crossings that resolve quickly.
    """

    _LOOKAHEAD_BEATS = 2
    # Crossings <= this many semitones are treated as minor (WARNING).
    _MINOR_CROSSING_LIMIT = 2
    # Sources involved in invertible counterpoint (subject/answer + countersubject).
    _INVERTIBLE_SOURCES = {NoteSource.FUGUE_SUBJECT, NoteSource.FUGUE_ANSWER,
                           NoteSource.COUNTERSUBJECT}

    @property
    def name(self) -> str:
        return "voice_crossing"

    @property
    def category(self) -> Category:
        return Category.COUNTERPOINT

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
            beat = 0
            while beat < end_tick:
                nu = sounding_note_at(notes_upper, beat)
                nl = sounding_note_at(notes_lower, beat)
                if nu is not None and nl is not None and nu.pitch < nl.pitch:
                    # Multi-beat lookahead: skip temporary crossings.
                    resolved = False
                    for ahead in range(1, self._LOOKAHEAD_BEATS + 1):
                        fb = beat + TICKS_PER_BEAT * ahead
                        if fb >= end_tick:
                            break
                        nnu = sounding_note_at(notes_upper, fb)
                        nnl = sounding_note_at(notes_lower, fb)
                        if nnu is not None and nnl is not None and nnu.pitch >= nnl.pitch:
                            resolved = True
                            break
                    if resolved:
                        beat += TICKS_PER_BEAT
                        continue
                    crossing_amount = nl.pitch - nu.pitch
                    # Invertible counterpoint: subject/answer crossing with countersubject
                    # is structurally intentional -> INFO.
                    src_u = nu.provenance.source if nu.provenance else None
                    src_l = nl.provenance.source if nl.provenance else None
                    if (src_u in self._INVERTIBLE_SOURCES and src_l in self._INVERTIBLE_SOURCES
                            and src_u != src_l):
                        sev = Severity.INFO
                    # Small crossings (<=2 semitones) are WARNING, not ERROR.
                    elif crossing_amount <= self._MINOR_CROSSING_LIMIT:
                        sev = Severity.WARNING
                    else:
                        sev = Severity.ERROR
                    bar = beat // TICKS_PER_BAR + 1
                    beat_in_bar = (beat % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=sev,
                            bar=bar,
                            beat=beat_in_bar,
                            tick=beat,
                            voice_a=name_upper,
                            voice_b=name_lower,
                            description=f"{name_upper} {pitch_to_name(nu.pitch)} < {name_lower} {pitch_to_name(nl.pitch)}",
                            source=nu.provenance.source if nu.provenance else None,
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
# CrossRelation
# ---------------------------------------------------------------------------


class CrossRelation:
    """Detect cross-relations: conflicting chromatic alterations of the same
    pitch class between voices within proximity (1 beat default)."""

    def __init__(self, proximity_ticks: int = TICKS_PER_BEAT):
        self.proximity_ticks = proximity_ticks

    @property
    def name(self) -> str:
        return "cross_relation"

    @property
    def category(self) -> Category:
        return Category.COUNTERPOINT

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        voices = _voices_sorted(score)
        names = sorted(voices.keys())
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                violations.extend(
                    self._check_pair(names[i], voices[names[i]], names[j], voices[names[j]])
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )

    # Sources where chromatic pitch conflicts are expected.
    _CHROMATIC_SOURCES = {NoteSource.CHROMATIC_PASSING, NoteSource.ORNAMENT}

    def _check_pair(
        self, name_a: str, va: List[Note], name_b: str, vb: List[Note]
    ) -> List[Violation]:
        violations = []
        natural_classes = {0, 2, 4, 5, 7, 9, 11}
        # Use sorted order + early termination to avoid O(n*m) worst case.
        j_start = 0
        for na in va:
            # Exempt chromatic passing tones and ornaments.
            if na.provenance and na.provenance.source in self._CHROMATIC_SOURCES:
                continue
            # Advance j_start past notes too early.
            while j_start < len(vb) and vb[j_start].start_tick < na.start_tick - self.proximity_ticks:
                j_start += 1
            for j in range(j_start, len(vb)):
                nb = vb[j]
                if nb.start_tick > na.start_tick + self.proximity_ticks:
                    break
                # Exempt chromatic passing tones and ornaments.
                if nb.provenance and nb.provenance.source in self._CHROMATIC_SOURCES:
                    continue
                if na.pitch_class == nb.pitch_class:
                    continue
                # Same letter name, different accidental: e.g., F# vs F natural.
                # Pitch classes differ by exactly 1 semitone.
                diff = abs(na.pitch - nb.pitch) % 12
                if diff == 1 or diff == 11:
                    pc_a, pc_b = na.pitch_class, nb.pitch_class
                    # Cross-relation if one is natural and other is its sharp/flat.
                    if (pc_a in natural_classes) != (pc_b in natural_classes):
                        later = na if na.start_tick >= nb.start_tick else nb
                        violations.append(
                            Violation(
                                rule_name=self.name,
                                category=self.category,
                                severity=Severity.WARNING,
                                bar=later.bar,
                                beat=later.beat,
                                tick=later.start_tick,
                                voice_a=name_a,
                                voice_b=name_b,
                                description=f"cross-relation {pitch_to_name(na.pitch)} vs {pitch_to_name(nb.pitch)}",
                                source=later.provenance.source if later.provenance else None,
                            )
                        )
        return violations


# ---------------------------------------------------------------------------
# AugmentedLeap
# ---------------------------------------------------------------------------


class AugmentedLeap:
    """Detect augmented (tritone = 6 semitones) leaps within individual voices.

    Short notes (<= quarter note) in running passages are exempt because tritone
    intervals arise naturally in free counterpoint figurations.
    """

    _SHORT_NOTE_LIMIT = TICKS_PER_BEAT // 2  # 240 ticks (eighth note)

    @property
    def name(self) -> str:
        return "augmented_leap"

    @property
    def category(self) -> Category:
        return Category.COUNTERPOINT

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        for voice_name, notes in score.voices_dict.items():
            sorted_notes = sorted(notes, key=lambda n: n.start_tick)
            for k in range(len(sorted_notes) - 1):
                n1, n2 = sorted_notes[k], sorted_notes[k + 1]
                leap = abs(n2.pitch - n1.pitch)
                if leap == TRITONE:
                    # Exempt short notes in rapid passages.
                    if n1.duration <= self._SHORT_NOTE_LIMIT and n2.duration <= self._SHORT_NOTE_LIMIT:
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
                            description=f"tritone leap {pitch_to_name(n1.pitch)}->{pitch_to_name(n2.pitch)}",
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
# VoiceInterleaving
# ---------------------------------------------------------------------------


class VoiceInterleaving:
    """Detect sustained register inversion between adjacent voices.

    Computes per-bar average pitch for each voice and flags runs of N or more
    consecutive bars where the expected upper voice has a lower average pitch
    than its lower neighbour.
    """

    def __init__(self, min_bars: int = 3):
        self.min_bars = min_bars

    @property
    def name(self) -> str:
        return "voice_interleaving"

    @property
    def category(self) -> Category:
        return Category.COUNTERPOINT

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        track_order = [(t.name, t.sorted_notes) for t in score.tracks]
        if len(track_order) < 2:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, violations=[],
            )
        total_bars = score.total_bars
        for i in range(len(track_order) - 1):
            name_upper, notes_upper = track_order[i]
            name_lower, notes_lower = track_order[i + 1]
            avg_upper = self._bar_averages(notes_upper, total_bars)
            avg_lower = self._bar_averages(notes_lower, total_bars)
            run_start: int | None = None
            for bar in range(1, total_bars + 1):
                au = avg_upper.get(bar)
                al = avg_lower.get(bar)
                inverted = au is not None and al is not None and au < al
                if inverted:
                    if run_start is None:
                        run_start = bar
                else:
                    if run_start is not None:
                        run_len = bar - run_start
                        if run_len >= self.min_bars:
                            violations.append(
                                Violation(
                                    rule_name=self.name,
                                    category=self.category,
                                    severity=Severity.WARNING,
                                    bar=run_start,
                                    beat=1,
                                    tick=(run_start - 1) * TICKS_PER_BAR,
                                    voice_a=name_upper,
                                    voice_b=name_lower,
                                    description=(
                                        f"register inverted for bars {run_start}-{bar - 1} "
                                        f"({run_len} bars)"
                                    ),
                                )
                            )
                        run_start = None
            # Flush trailing run
            if run_start is not None:
                run_len = total_bars - run_start + 1
                if run_len >= self.min_bars:
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.WARNING,
                            bar=run_start,
                            beat=1,
                            tick=(run_start - 1) * TICKS_PER_BAR,
                            voice_a=name_upper,
                            voice_b=name_lower,
                            description=(
                                f"register inverted for bars {run_start}-{total_bars} "
                                f"({run_len} bars)"
                            ),
                        )
                    )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )

    @staticmethod
    def _bar_averages(notes: List[Note], total_bars: int) -> Dict[int, float]:
        """Return {bar_number: average_pitch} for bars that contain notes.

        A note that sustains across multiple bars contributes its pitch to
        every bar it overlaps.
        """
        bar_pitches: Dict[int, List[int]] = defaultdict(list)
        for n in notes:
            start_bar = n.start_tick // TICKS_PER_BAR + 1
            end_bar = (n.end_tick - 1) // TICKS_PER_BAR + 1 if n.duration > 0 else start_bar
            for b in range(start_bar, end_bar + 1):
                bar_pitches[b].append(n.pitch)
        return {b: sum(ps) / len(ps) for b, ps in bar_pitches.items()}


# Convenience: all counterpoint rules.
ALL_COUNTERPOINT_RULES = [
    ParallelPerfect,
    HiddenPerfect,
    VoiceCrossing,
    CrossRelation,
    AugmentedLeap,
    VoiceInterleaving,
]
