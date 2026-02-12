"""Structure rules: exposition completeness, final bar validation,
tonal answer interval verification, and ground bass repetition.

Migrated from fugue_detector.py with heuristic fallback when provenance
is unavailable.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Set, Tuple

from ..model import Note, NoteSource, Score, TICKS_PER_BAR, TransformStep
from .base import Category, RuleResult, Severity, Violation

if False:  # TYPE_CHECKING
    from ..form_profile import FormProfile


# ---------------------------------------------------------------------------
# ExpositionCompleteness
# ---------------------------------------------------------------------------


class ExpositionCompleteness:
    """Check that all voices enter with subject or answer in the exposition.

    With provenance: uses NoteSource to detect subject/answer entries.
    Without provenance: heuristic based on first note of each voice
    appearing within the first N bars.
    """

    def __init__(self, max_expo_bars: int = 12):
        self.max_expo_bars = max_expo_bars

    @property
    def name(self) -> str:
        return "exposition_completeness"

    @property
    def category(self) -> Category:
        return Category.STRUCTURE

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.exposition_required

    def configure(self, profile: FormProfile) -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        if score.has_provenance:
            return self._check_with_provenance(score)
        return self._check_heuristic(score)

    def _check_with_provenance(self, score: Score) -> RuleResult:
        """Check using provenance data: each voice must have a subject or answer entry,
        and entries should alternate subject/answer."""
        entered_voices: Set[str] = set()
        subject_sources = {NoteSource.FUGUE_SUBJECT, NoteSource.FUGUE_ANSWER}

        # Collect entries with timing for order check.
        entries: List[Tuple[int, int, "NoteSource"]] = []  # (start_tick, entry_number, source)
        for note in score.all_notes:
            if note.provenance and note.provenance.source in subject_sources:
                entered_voices.add(note.voice)
                entries.append((note.start_tick, note.provenance.entry_number,
                                note.provenance.source))

        all_voices = {t.name for t in score.tracks}
        missing = all_voices - entered_voices
        violations = []
        if missing:
            for v in sorted(missing):
                violations.append(
                    Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.ERROR,
                        voice_a=v,
                        description=f"voice '{v}' has no subject/answer entry",
                    )
                )

        # Check subject/answer alternation order.
        if entries:
            # Deduplicate by entry_number (take first note of each entry).
            seen_entries: Dict[int, Tuple[int, "NoteSource"]] = {}
            for tick, entry_num, src in entries:
                if entry_num not in seen_entries or tick < seen_entries[entry_num][0]:
                    seen_entries[entry_num] = (tick, src)
            ordered = sorted(seen_entries.values(), key=lambda x: x[0])
            for k in range(len(ordered) - 1):
                _, src1 = ordered[k]
                _, src2 = ordered[k + 1]
                # Subject should alternate with answer.
                if src1 == src2:
                    tick2 = ordered[k + 1][0]
                    src_name = "subject" if src2 == NoteSource.FUGUE_SUBJECT else "answer"
                    violations.append(
                        Violation(
                            rule_name=self.name,
                            category=self.category,
                            severity=Severity.WARNING,
                            bar=tick2 // TICKS_PER_BAR + 1,
                            tick=tick2,
                            description=f"consecutive {src_name} entries (expected alternation)",
                        )
                    )

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=all(v.severity != Severity.ERROR for v in violations) if violations else True,
            violations=violations,
            info=f"entered: {sorted(entered_voices)}, missing: {sorted(missing)}",
        )

    def _check_heuristic(self, score: Score) -> RuleResult:
        """Without provenance: each voice must have notes within the first N bars."""
        expo_end = self.max_expo_bars * TICKS_PER_BAR
        entered: Set[str] = set()
        for track in score.tracks:
            for note in track.notes:
                if note.start_tick < expo_end:
                    entered.add(track.name)
                    break
        all_voices = {t.name for t in score.tracks}
        missing = all_voices - entered
        violations = []
        if missing:
            for v in sorted(missing):
                violations.append(
                    Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.ERROR,
                        voice_a=v,
                        description=f"voice '{v}' has no notes in first {self.max_expo_bars} bars (heuristic)",
                    )
                )
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info=f"entered (heuristic): {sorted(entered)}, missing: {sorted(missing)}",
        )


# ---------------------------------------------------------------------------
# FinalBarValidation
# ---------------------------------------------------------------------------


class FinalBarValidation:
    """Validate the final bar of the piece.

    Checks:
    1. Final beat: outer voices form perfect consonance (P1/P5/P8).
    2. Final note is longer than the preceding note (fermata effect).
    3. Bass final note: chord_degree == 1 if provenance available.
    """

    @property
    def name(self) -> str:
        return "final_bar_validation"

    @property
    def category(self) -> Category:
        return Category.STRUCTURE

    def applies_to(self, profile: FormProfile) -> bool:
        # All forms benefit from final bar validation.
        return True

    def configure(self, profile: FormProfile) -> None:
        self._is_solo = not profile.counterpoint_enabled

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []
        is_solo = getattr(self, '_is_solo', False)

        if not score.tracks or score.total_notes == 0:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True, info="empty score",
            )

        total_bars = score.total_bars
        last_bar_tick = (total_bars - 1) * TICKS_PER_BAR

        if not is_solo and len(score.tracks) >= 2:
            # Check outer voice consonance on final beat.
            soprano_notes = score.tracks[0].sorted_notes
            bass_notes = score.tracks[-1].sorted_notes
            final_sop = [n for n in soprano_notes if n.bar == total_bars]
            final_bass = [n for n in bass_notes if n.bar == total_bars]

            if final_sop and final_bass:
                from ..model import PERFECT_CONSONANCES, interval_class
                last_s = final_sop[-1]
                last_b = final_bass[-1]
                iv = interval_class(last_s.pitch - last_b.pitch)
                if iv not in PERFECT_CONSONANCES:
                    violations.append(Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.WARNING,
                        bar=total_bars,
                        beat=last_s.beat,
                        tick=last_s.start_tick,
                        voice_a=score.tracks[0].name,
                        voice_b=score.tracks[-1].name,
                        description=f"final bar outer voices: {iv} semitones (not perfect consonance)",
                    ))

            # Bass chord_degree check.
            if final_bass:
                last_b = final_bass[-1]
                if (last_b.provenance and last_b.provenance.chord_degree >= 0
                        and last_b.provenance.chord_degree != 1):
                    violations.append(Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.WARNING,
                        bar=total_bars,
                        tick=last_b.start_tick,
                        voice_a=score.tracks[-1].name,
                        description=f"bass final note chord_degree={last_b.provenance.chord_degree} (expected 1)",
                    ))

        # Final note duration check.
        # Solo string: skip (arpeggio decomposition makes this check unreliable).
        # Multi-voice with 6+ notes: compare last-2 average vs preceding-4 average.
        # Short tracks: fallback to simple last > prev comparison.
        for track in score.tracks:
            if is_solo:
                continue  # Skip duration check for solo string
            sorted_notes = track.sorted_notes
            if len(sorted_notes) < 2:
                continue
            if len(sorted_notes) >= 6:
                final_avg = (sorted_notes[-1].duration + sorted_notes[-2].duration) / 2
                preceding_avg = sum(n.duration for n in sorted_notes[-6:-2]) / 4
                if final_avg <= preceding_avg:
                    violations.append(Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.WARNING,
                        bar=sorted_notes[-1].bar,
                        tick=sorted_notes[-1].start_tick,
                        voice_a=track.name,
                        description=(
                            f"final 2 notes avg ({final_avg:.0f} ticks) not longer "
                            f"than preceding 4 avg ({preceding_avg:.0f} ticks)"
                        ),
                    ))
            else:
                last_note = sorted_notes[-1]
                prev_note = sorted_notes[-2]
                if last_note.duration <= prev_note.duration:
                    violations.append(Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.WARNING,
                        bar=last_note.bar,
                        tick=last_note.start_tick,
                        voice_a=track.name,
                        description=(
                            f"final note ({last_note.duration} ticks) not longer "
                            f"than preceding ({prev_note.duration} ticks)"
                        ),
                    ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


# ---------------------------------------------------------------------------
# TrioSonataVoiceCount
# ---------------------------------------------------------------------------


class TrioSonataVoiceCount:
    """Check that trio_sonata form has exactly 3 voices."""

    @property
    def name(self) -> str:
        return "trio_sonata_voice_count"

    @property
    def category(self) -> Category:
        return Category.STRUCTURE

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.form_name == "trio_sonata"

    def configure(self, profile: FormProfile) -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        violations = []
        if score.num_voices != 3:
            violations.append(Violation(
                rule_name=self.name,
                category=self.category,
                severity=Severity.ERROR,
                description=f"trio_sonata requires 3 voices, found {score.num_voices}",
            ))
        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
        )


# ---------------------------------------------------------------------------
# GroundBassRepetition
# ---------------------------------------------------------------------------


class GroundBassRepetition:
    """Validate ground bass repetition consistency in passacaglia/chaconne.

    Extracts notes with provenance source == GROUND_BASS, groups by
    repetition period, and compares pitch-class sequences (allowing
    transposition).
    """

    def __init__(self):
        self._period_bars: int = 8

    @property
    def name(self) -> str:
        return "ground_bass_repetition"

    @property
    def category(self) -> Category:
        return Category.STRUCTURE

    def applies_to(self, profile: FormProfile) -> bool:
        return profile.ground_bass_expected

    def configure(self, profile: FormProfile) -> None:
        if profile.ground_bass_period:
            self._period_bars = profile.ground_bass_period

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []

        # Collect ground bass notes.
        gb_notes = [
            n for n in score.all_notes
            if n.provenance and n.provenance.source == NoteSource.GROUND_BASS
        ]

        if not gb_notes:
            return RuleResult(
                rule_name=self.name,
                category=self.category,
                passed=True,
                info="no ground_bass notes found (provenance may be absent)",
            )

        # Group by repetition period.
        period_ticks = self._period_bars * TICKS_PER_BAR
        groups: Dict[int, List[Note]] = {}
        for note in gb_notes:
            group_idx = note.start_tick // period_ticks
            if group_idx not in groups:
                groups[group_idx] = []
            groups[group_idx].append(note)

        if len(groups) < 2:
            return RuleResult(
                rule_name=self.name,
                category=self.category,
                passed=True,
                info="only one ground bass statement found",
            )

        # Extract pitch-class interval sequence for each group.
        def interval_sequence(notes: List[Note]) -> List[int]:
            sorted_ns = sorted(notes, key=lambda n: n.start_tick)
            if len(sorted_ns) < 2:
                return []
            return [(sorted_ns[i + 1].pitch - sorted_ns[i].pitch)
                    for i in range(len(sorted_ns) - 1)]

        group_keys = sorted(groups.keys())
        reference = interval_sequence(groups[group_keys[0]])

        for gk in group_keys[1:]:
            current = interval_sequence(groups[gk])
            if current != reference:
                bar_start = gk * self._period_bars + 1
                violations.append(Violation(
                    rule_name=self.name,
                    category=self.category,
                    severity=Severity.ERROR,
                    bar=bar_start,
                    tick=(bar_start - 1) * TICKS_PER_BAR,
                    description=(
                        f"ground bass interval pattern differs at bar {bar_start} "
                        f"(expected {reference[:5]}..., got {current[:5]}...)"
                    ),
                ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info=f"{len(groups)} statements, period={self._period_bars} bars",
        )


# ---------------------------------------------------------------------------
# TonalAnswerInterval
# ---------------------------------------------------------------------------


class TonalAnswerInterval:
    """Verify tonal answer transformation correctness.

    In a tonal answer, the subject's 5th degree (P5) is mutated to a 4th degree
    (P4) at the head or tail of the answer. This rule checks that answer notes
    with tonal_answer in their transform_steps show the expected interval
    mutation compared to the original subject.
    """

    @property
    def name(self) -> str:
        return "tonal_answer_interval"

    @property
    def category(self) -> Category:
        return Category.STRUCTURE

    def applies_to(self, profile: "FormProfile") -> bool:
        return profile.exposition_required

    def configure(self, profile: "FormProfile") -> None:
        pass

    def check(self, score: Score) -> RuleResult:
        violations: List[Violation] = []

        # Collect subject notes (entry_number=1).
        subject_notes = sorted(
            [n for n in score.all_notes
             if n.provenance and n.provenance.source == NoteSource.FUGUE_SUBJECT
             and n.provenance.entry_number == 1],
            key=lambda n: n.start_tick,
        )

        # Collect tonal answer notes.
        tonal_answer_notes = sorted(
            [n for n in score.all_notes
             if n.provenance and n.provenance.source == NoteSource.FUGUE_ANSWER
             and TransformStep.TONAL_ANSWER in n.provenance.transform_steps],
            key=lambda n: n.start_tick,
        )

        if not subject_notes or not tonal_answer_notes:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True,
                info="no subject or tonal answer notes found",
            )

        # Build subject interval sequence.
        subject_intervals = [
            subject_notes[i + 1].pitch - subject_notes[i].pitch
            for i in range(len(subject_notes) - 1)
        ]

        # Build answer interval sequence.
        answer_intervals = [
            tonal_answer_notes[i + 1].pitch - tonal_answer_notes[i].pitch
            for i in range(len(tonal_answer_notes) - 1)
        ]

        if not subject_intervals or not answer_intervals:
            return RuleResult(
                rule_name=self.name, category=self.category,
                passed=True,
                info="insufficient notes for interval comparison",
            )

        # Compare: tonal answer should have at least one P5->P4 mutation.
        # Check the comparable portion (shorter of the two).
        compare_len = min(len(subject_intervals), len(answer_intervals))
        has_mutation = False
        for i in range(compare_len):
            si, ai = subject_intervals[i], answer_intervals[i]
            if si != ai:
                # Expected mutation: P5 (7) -> P4 (5) or -P5 (-7) -> -P4 (-5).
                if (abs(si) == 7 and abs(ai) == 5) or (abs(si) == 5 and abs(ai) == 7):
                    has_mutation = True
                else:
                    # Unexpected interval change.
                    violations.append(Violation(
                        rule_name=self.name,
                        category=self.category,
                        severity=Severity.INFO,
                        bar=tonal_answer_notes[i + 1].bar if i + 1 < len(tonal_answer_notes) else 1,
                        description=(
                            f"interval {i}: subject {si:+d} vs answer {ai:+d} "
                            f"(unexpected mutation)"
                        ),
                    ))

        if not has_mutation and compare_len > 0:
            violations.append(Violation(
                rule_name=self.name,
                category=self.category,
                severity=Severity.INFO,
                description="tonal answer has no P5<->P4 mutation (may be real answer)",
            ))

        return RuleResult(
            rule_name=self.name,
            category=self.category,
            passed=len(violations) == 0,
            violations=violations,
            info=f"subject intervals: {subject_intervals[:8]}, answer intervals: {answer_intervals[:8]}",
        )


ALL_STRUCTURE_RULES = [
    ExpositionCompleteness,
    FinalBarValidation,
    TrioSonataVoiceCount,
    GroundBassRepetition,
    TonalAnswerInterval,
]
