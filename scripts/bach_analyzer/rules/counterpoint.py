"""Counterpoint rules: parallel perfect, hidden perfect, voice crossing,
cross-relation, augmented leap.

Ported from analyzer.py and extended to match C++ counterpoint_analyzer.h semantics.
"""

from __future__ import annotations

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
    """Detect voice crossing: upper voice pitch below lower voice pitch."""

    @property
    def name(self) -> str:
        return "voice_crossing"

    @property
    def category(self) -> Category:
        return Category.COUNTERPOINT

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        voices = _voices_sorted(score)
        names = sorted(voices.keys())
        # Sorted alphabetically: alto < bass < soprano -> expect soprano > alto > bass
        # Use voice_id order instead (0 = highest)
        track_order = [(t.name, t.notes) for t in score.tracks]
        for i in range(len(track_order) - 1):
            name_upper, notes_upper = track_order[i]
            name_lower, notes_lower = track_order[i + 1]
            beats_u = _beat_map(notes_upper)
            beats_l = _beat_map(notes_lower)
            for tick in sorted(set(beats_u) & set(beats_l)):
                nu, nl = beats_u[tick], beats_l[tick]
                if nu.pitch < nl.pitch:
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.ERROR,
                            bar=nu.bar,
                            beat=nu.beat,
                            tick=tick,
                            voice_a=name_upper,
                            voice_b=name_lower,
                            description=f"{name_upper} {pitch_to_name(nu.pitch)} < {name_lower} {pitch_to_name(nl.pitch)}",
                            source=nu.provenance.source if nu.provenance else None,
                        )
                    )
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


# Convenience: all counterpoint rules.
ALL_COUNTERPOINT_RULES = [
    ParallelPerfect,
    HiddenPerfect,
    VoiceCrossing,
    CrossRelation,
    AugmentedLeap,
]
