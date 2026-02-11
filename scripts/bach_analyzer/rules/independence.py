"""Independence rules: voice independence and rhythm diversity.

Migrated from analyzer.py with extensions.
"""

from __future__ import annotations

from collections import Counter
from typing import Dict, List

from ..model import Note, Score
from .base import Category, RuleResult, Severity, Violation


# ---------------------------------------------------------------------------
# VoiceIndependence
# ---------------------------------------------------------------------------


class VoiceIndependence:
    """Composite voice independence score (rhythm/contour/register).
    Flag if below threshold (default 0.6)."""

    def __init__(self, min_composite: float = 0.6):
        self.min_composite = min_composite

    @property
    def name(self) -> str:
        return "voice_independence"

    @property
    def category(self) -> Category:
        return Category.INDEPENDENCE

    def check(self, score: Score) -> RuleResult:
        voices = score.voices_dict
        names = sorted(voices.keys())
        if len(names) < 2:
            return RuleResult(
                rule_name=self.name,
                category=self.category,
                passed=True,
                info="single voice, independence N/A",
            )
        rhythm_scores = []
        contour_scores = []
        register_scores = []
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                va = sorted(voices[names[i]], key=lambda n: n.start_tick)
                vb = sorted(voices[names[j]], key=lambda n: n.start_tick)
                rhythm_scores.append(self._rhythm(va, vb))
                contour_scores.append(self._contour(va, vb))
                register_scores.append(self._register(va, vb))
        rhythm = min(rhythm_scores) if rhythm_scores else 0.0
        contour = min(contour_scores) if contour_scores else 0.0
        register = min(register_scores) if register_scores else 0.0
        composite = rhythm * 0.4 + contour * 0.3 + register * 0.3
        info = f"rhythm={rhythm:.2f} contour={contour:.2f} register={register:.2f} composite={composite:.2f}"
        violations = []
        if composite < self.min_composite:
            violations.append(
                Violation(
                    rule_name=self.name,
                    category=self.category,
                    severity=Severity.INFO,
                    description=f"composite {composite:.2f} < {self.min_composite}",
                )
            )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info=info,
        )

    @staticmethod
    def _rhythm(va: List[Note], vb: List[Note]) -> float:
        ticks_a = {n.start_tick for n in va}
        ticks_b = {n.start_tick for n in vb}
        if not ticks_a and not ticks_b:
            return 0.0
        total = len(ticks_a | ticks_b)
        simultaneous = len(ticks_a & ticks_b)
        return 1.0 - (simultaneous / total) if total > 0 else 0.0

    @staticmethod
    def _contour(va: List[Note], vb: List[Note]) -> float:
        if len(va) < 2 or len(vb) < 2:
            return 0.0
        dirs_a = [
            1 if va[k + 1].pitch > va[k].pitch else (-1 if va[k + 1].pitch < va[k].pitch else 0)
            for k in range(len(va) - 1)
        ]
        dirs_b = [
            1 if vb[k + 1].pitch > vb[k].pitch else (-1 if vb[k + 1].pitch < vb[k].pitch else 0)
            for k in range(len(vb) - 1)
        ]
        min_len = min(len(dirs_a), len(dirs_b))
        if min_len == 0:
            return 0.0
        opposite = sum(1 for k in range(min_len) if dirs_a[k] * dirs_b[k] < 0)
        return opposite / min_len

    @staticmethod
    def _register(va: List[Note], vb: List[Note]) -> float:
        if not va or not vb:
            return 0.0
        min_a, max_a = min(n.pitch for n in va), max(n.pitch for n in va)
        min_b, max_b = min(n.pitch for n in vb), max(n.pitch for n in vb)
        overlap = max(0, min(max_a, max_b) - max(min_a, min_b))
        total_range = max(max_a, max_b) - min(min_a, min_b)
        return 1.0 - (overlap / total_range) if total_range > 0 else 0.0


# ---------------------------------------------------------------------------
# RhythmDiversity
# ---------------------------------------------------------------------------


class RhythmDiversity:
    """Check duration variety per voice. Flag if a single duration dominates."""

    def __init__(self, min_diversity: float = 0.3):
        self.min_diversity = min_diversity

    @property
    def name(self) -> str:
        return "rhythm_diversity"

    @property
    def category(self) -> Category:
        return Category.INDEPENDENCE

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        info_parts = []
        for voice_name, notes in score.voices_dict.items():
            if len(notes) < 2:
                continue
            durations = [n.duration for n in notes]
            counter = Counter(durations)
            max_ratio = counter.most_common(1)[0][1] / len(durations)
            # Score: 1.0 if max_ratio <= 0.3, 0.0 if max_ratio = 1.0
            diversity = 1.0 - max(0.0, (max_ratio - 0.3) / 0.7)
            info_parts.append(f"{voice_name}: {diversity:.2f}")
            if diversity < self.min_diversity:
                violations.append(
                    Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.INFO,
                        voice_a=voice_name,
                        description=f"rhythm diversity {diversity:.2f} (dominant duration {counter.most_common(1)[0][0]} ticks, {max_ratio:.0%})",
                    )
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info="; ".join(info_parts),
        )


ALL_INDEPENDENCE_RULES = [VoiceIndependence, RhythmDiversity]
