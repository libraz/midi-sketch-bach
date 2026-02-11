"""Counterpoint rules: parallel perfect, hidden perfect, voice crossing,
cross-relation, augmented leap.

Ported from analyzer.py and extended to match C++ counterpoint_analyzer.h semantics.
"""

from __future__ import annotations

from collections import defaultdict
from typing import Dict, List, Tuple

from ..model import (
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

    def _check_pair(
        self, name_a: str, va: List[Note], name_b: str, vb: List[Note]
    ) -> List[Violation]:
        violations = []
        beats_a = _beat_map(va)
        beats_b = _beat_map(vb)
        common = sorted(set(beats_a) & set(beats_b))
        for k in range(len(common) - 1):
            t1, t2 = common[k], common[k + 1]
            na1, nb1 = beats_a[t1], beats_b[t1]
            na2, nb2 = beats_a[t2], beats_b[t2]
            iv1 = interval_class(na1.pitch - nb1.pitch)
            iv2 = interval_class(na2.pitch - nb2.pitch)
            if iv1 in PERFECT_CONSONANCES and iv1 == iv2:
                dir_a = na2.pitch - na1.pitch
                dir_b = nb2.pitch - nb1.pitch
                if dir_a != 0 and dir_b != 0 and (dir_a > 0) == (dir_b > 0):
                    iv_name = "P5" if iv1 == PERFECT_5TH else ("P8" if iv1 in (UNISON, OCTAVE) else f"P{iv1}")
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.CRITICAL,
                            bar=na2.bar,
                            beat=na2.beat,
                            tick=t2,
                            voice_a=name_a,
                            voice_b=name_b,
                            description=f"{iv_name}->{iv_name} {pitch_to_name(na1.pitch)}/{pitch_to_name(nb1.pitch)} -> {pitch_to_name(na2.pitch)}/{pitch_to_name(nb2.pitch)}",
                            source=na2.provenance.source if na2.provenance else None,
                        )
                    )
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

    def _check_pair(
        self, name_a: str, va: List[Note], name_b: str, vb: List[Note]
    ) -> List[Violation]:
        violations = []
        beats_a = _beat_map(va)
        beats_b = _beat_map(vb)
        common = sorted(set(beats_a) & set(beats_b))
        for k in range(len(common) - 1):
            t1, t2 = common[k], common[k + 1]
            na1, nb1 = beats_a[t1], beats_b[t1]
            na2, nb2 = beats_a[t2], beats_b[t2]
            iv2 = interval_class(na2.pitch - nb2.pitch)
            if iv2 not in PERFECT_CONSONANCES:
                continue
            dir_a = na2.pitch - na1.pitch
            dir_b = nb2.pitch - nb1.pitch
            # Same direction (similar motion)
            if dir_a == 0 or dir_b == 0:
                continue
            if (dir_a > 0) != (dir_b > 0):
                continue
            # Both voices leap (>2 semitones)
            if abs(dir_a) <= 2 or abs(dir_b) <= 2:
                continue
            # Not already a parallel perfect (those are caught separately)
            iv1 = interval_class(na1.pitch - nb1.pitch)
            if iv1 == iv2:
                continue
            iv_name = "P5" if iv2 == PERFECT_5TH else "P8"
            violations.append(
                Violation(
                    rule_name=self.name,
                    category=self.category,
                    severity=Severity.WARNING,
                    bar=na2.bar,
                    beat=na2.beat,
                    tick=t2,
                    voice_a=name_a,
                    voice_b=name_b,
                    description=f"hidden {iv_name}: both leap to {pitch_to_name(na2.pitch)}/{pitch_to_name(nb2.pitch)}",
                    source=na2.provenance.source if na2.provenance else None,
                )
            )
        return violations


# ---------------------------------------------------------------------------
# VoiceCrossing
# ---------------------------------------------------------------------------


class VoiceCrossing:
    """Detect voice crossing: upper voice pitch below lower voice pitch.

    Scans every beat position and checks the sounding pitch of each voice
    (including sustained notes), matching C++ countVoiceCrossings().
    A 1-beat lookahead suppresses temporary crossings that resolve immediately.
    """

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
                    # 1-beat lookahead: skip temporary crossings
                    next_beat = beat + TICKS_PER_BEAT
                    if next_beat < end_tick:
                        nnu = sounding_note_at(notes_upper, next_beat)
                        nnl = sounding_note_at(notes_lower, next_beat)
                        if nnu is not None and nnl is not None and nnu.pitch >= nnl.pitch:
                            beat += TICKS_PER_BEAT
                            continue
                    bar = beat // TICKS_PER_BAR + 1
                    beat_in_bar = (beat % TICKS_PER_BAR) // TICKS_PER_BEAT + 1
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.ERROR,
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

    def _check_pair(
        self, name_a: str, va: List[Note], name_b: str, vb: List[Note]
    ) -> List[Violation]:
        violations = []
        for na in va:
            for nb in vb:
                if abs(na.start_tick - nb.start_tick) > self.proximity_ticks:
                    continue
                if na.pitch_class == nb.pitch_class:
                    continue
                # Same letter name, different accidental: e.g., F# vs F natural
                # Simplification: pitch classes differ by exactly 1 semitone and
                # share the same "base" (approximated by checking if one is the
                # sharp version of the other).
                diff = abs(na.pitch - nb.pitch) % 12
                if diff == 1 or diff == 11:
                    # Check they share a letter-name base (heuristic: same pitch class
                    # differs by 1 semitone and they are in different voices)
                    # More precise: the "natural" pitch classes C,D,E,F,G,A,B
                    # C=0,D=2,E=4,F=5,G=7,A=9,B=11; accidentals are 1,3,6,8,10
                    natural_classes = {0, 2, 4, 5, 7, 9, 11}
                    pc_a, pc_b = na.pitch_class, nb.pitch_class
                    # Cross-relation if one is natural and other is its sharp/flat
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
    """Detect augmented (tritone = 6 semitones) leaps within individual voices."""

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
