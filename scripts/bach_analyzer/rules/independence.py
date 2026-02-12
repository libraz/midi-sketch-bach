"""Independence rules: voice independence and rhythm diversity.

Migrated from analyzer.py with extensions.
"""

from __future__ import annotations

from collections import Counter
from typing import Dict, List

from ..model import Note, NoteSource, Score, TICKS_PER_BEAT
from .base import Category, RuleResult, Severity, Violation

if False:  # TYPE_CHECKING
    from ..form_profile import FormProfile


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

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.independence_enabled

    def configure(self, profile: FormProfile) -> None:
        pass

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
        """Compare melodic contour between two voices using time-aligned beats.

        Instead of comparing notes by sequential index (which is meaningless
        when voices have different note counts), we sample the melodic direction
        at each beat position where both voices have notes, producing a
        time-aligned contour comparison.
        """
        if len(va) < 2 or len(vb) < 2:
            return 0.0
        # Build per-beat pitch maps for each voice.
        def beat_pitches(notes: List[Note]) -> Dict[int, int]:
            return {n.start_tick // TICKS_PER_BEAT: n.pitch for n in notes}
        bp_a = beat_pitches(va)
        bp_b = beat_pitches(vb)
        # Find beats where both voices have attacks.
        common_beats = sorted(set(bp_a) & set(bp_b))
        if len(common_beats) < 2:
            return 0.0
        opposite = 0
        comparisons = 0
        for k in range(len(common_beats) - 1):
            b1, b2 = common_beats[k], common_beats[k + 1]
            dir_a = bp_a[b2] - bp_a[b1]
            dir_b = bp_b[b2] - bp_b[b1]
            if dir_a != 0 and dir_b != 0:
                comparisons += 1
                if (dir_a > 0) != (dir_b > 0):
                    opposite += 1
        return opposite / comparisons if comparisons > 0 else 0.0

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


def _is_pedal_voice(voice_name: str, notes: List[Note]) -> bool:
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

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.independence_enabled

    def configure(self, profile: FormProfile) -> None:
        pass

    _PEDAL_MIN_DIVERSITY = 0.0  # Pedal voices may have completely uniform rhythm.

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
            threshold = self._PEDAL_MIN_DIVERSITY if _is_pedal_voice(voice_name, notes) else self.min_diversity
            if diversity < threshold:
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


# ---------------------------------------------------------------------------
# ContraryMotionRatio
# ---------------------------------------------------------------------------


class ContraryMotionRatio:
    """Check the ratio of contrary motion between voice pairs.

    Bach's counterpoint typically shows 40-60% contrary motion.
    Flag if below 25%.
    """

    def __init__(self, min_ratio: float = 0.25):
        self.min_ratio = min_ratio

    @property
    def name(self) -> str:
        return "contrary_motion_ratio"

    @property
    def category(self) -> Category:
        return Category.INDEPENDENCE

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.independence_enabled

    def configure(self, profile: FormProfile) -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        voices = score.voices_dict
        names = sorted(voices.keys())
        if len(names) < 2:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="single voice, contrary motion N/A",
            )

        info_parts = []
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                va = sorted(voices[names[i]], key=lambda n: n.start_tick)
                vb = sorted(voices[names[j]], key=lambda n: n.start_tick)
                ratio = self._compute_ratio(va, vb)
                info_parts.append(f"{names[i]}<>{names[j]}: {ratio:.2f}")
                if ratio < self.min_ratio:
                    violations.append(Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.WARNING,
                        voice_a=names[i],
                        voice_b=names[j],
                        description=f"contrary motion ratio {ratio:.2f} < {self.min_ratio}",
                    ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info="; ".join(info_parts),
        )

    @staticmethod
    def _compute_ratio(va: List[Note], vb: List[Note]) -> float:
        """Compute contrary motion ratio using time-aligned beats."""
        if len(va) < 2 or len(vb) < 2:
            return 0.0
        bp_a = {n.start_tick // TICKS_PER_BEAT: n.pitch for n in va}
        bp_b = {n.start_tick // TICKS_PER_BEAT: n.pitch for n in vb}
        common_beats = sorted(set(bp_a) & set(bp_b))
        if len(common_beats) < 2:
            return 0.0
        contrary = 0
        comparisons = 0
        for k in range(len(common_beats) - 1):
            b1, b2 = common_beats[k], common_beats[k + 1]
            dir_a = bp_a[b2] - bp_a[b1]
            dir_b = bp_b[b2] - bp_b[b1]
            if dir_a != 0 and dir_b != 0:
                comparisons += 1
                if (dir_a > 0) != (dir_b > 0):
                    contrary += 1
        return contrary / comparisons if comparisons > 0 else 0.0


# ---------------------------------------------------------------------------
# OnsetAsynchrony
# ---------------------------------------------------------------------------


class OnsetAsynchrony:
    """Measure onset synchrony between voices.

    Bach's counterpoint shows rhythmic independence via staggered note onsets.
    If >80% of beats have all voices attacking simultaneously, the texture
    is too homophonic.
    """

    def __init__(self, max_sync_ratio: float = 0.8):
        self.max_sync_ratio = max_sync_ratio

    @property
    def name(self) -> str:
        return "onset_asynchrony"

    @property
    def category(self) -> Category:
        return Category.INDEPENDENCE

    def applies_to(self, profile: "FormProfile") -> bool:
        return (profile.independence_enabled
                and profile.form_name != "chorale_prelude")

    def configure(self, profile: "FormProfile") -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        voices = score.voices_dict
        names = sorted(voices.keys())
        if len(names) < 2:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="single voice, onset asynchrony N/A",
            )

        # Collect all onset ticks per voice.
        onset_sets = {name: {n.start_tick for n in notes} for name, notes in voices.items()}
        num_voices = len(names)

        # Count beats where all voices have an onset.
        total_beats = score.total_duration // TICKS_PER_BEAT
        if total_beats == 0:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="empty score",
            )

        fully_sync = 0
        active_beats = 0
        beat = 0
        while beat < score.total_duration:
            # Count how many voices have an onset at this beat.
            attacking = sum(1 for name in names if beat in onset_sets[name])
            if attacking > 0:
                active_beats += 1
                if attacking == num_voices:
                    fully_sync += 1
            beat += TICKS_PER_BEAT

        sync_ratio = fully_sync / active_beats if active_beats > 0 else 0.0

        violations = []
        if sync_ratio > self.max_sync_ratio:
            violations.append(Violation(
                rule_name=self.name,
                category=self.category,
                severity=Severity.WARNING,
                description=(
                    f"onset synchrony {sync_ratio:.2f} > {self.max_sync_ratio} "
                    f"({fully_sync}/{active_beats} beats fully synchronous)"
                ),
            ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info=f"sync_ratio={sync_ratio:.2f}",
        )


ALL_INDEPENDENCE_RULES = [VoiceIndependence, RhythmDiversity, ContraryMotionRatio, OnsetAsynchrony]
