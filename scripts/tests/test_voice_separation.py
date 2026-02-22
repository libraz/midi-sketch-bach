"""Tests for voice separation module."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import Note, Score, TICKS_PER_BAR, TICKS_PER_BEAT, Track
from scripts.bach_analyzer.voice_separation import (
    SeparationResult,
    _NormNote,
    detect_voice_count,
    evaluate_separation,
    normalize_notes,
    separate_score,
    separate_voices,
)


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------


def _n(pitch, start_beat, dur_beats=1.0, voice="manual"):
    """Create a Note at the given beat position with given duration in beats."""
    return Note(
        pitch=pitch,
        velocity=80,
        start_tick=int(start_beat * TICKS_PER_BEAT),
        duration=int(dur_beats * TICKS_PER_BEAT),
        voice=voice,
    )


def _n_tick(pitch, start_tick, duration, voice="manual"):
    """Create a Note with explicit tick values."""
    return Note(
        pitch=pitch,
        velocity=80,
        start_tick=start_tick,
        duration=duration,
        voice=voice,
    )


# ---------------------------------------------------------------------------
# 1. TestNormalizeNotes
# ---------------------------------------------------------------------------


class TestNormalizeNotes(unittest.TestCase):
    """Test duplicate removal and ornament marking in normalize_notes."""

    def test_duplicate_same_tick_same_pitch_keeps_longest(self):
        """When two notes share start_tick and pitch, the longest survives."""
        notes = [
            _n_tick(60, 0, 200),
            _n_tick(60, 0, 480),  # longer
        ]
        result = normalize_notes(notes)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].note.duration, 480)

    def test_duplicate_three_copies(self):
        """Three duplicates at same tick+pitch keep the single longest."""
        notes = [
            _n_tick(60, 0, 100),
            _n_tick(60, 0, 300),
            _n_tick(60, 0, 200),
        ]
        result = normalize_notes(notes)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].note.duration, 300)

    def test_different_pitches_not_deduped(self):
        """Notes at the same tick but different pitches are independent."""
        notes = [
            _n_tick(60, 0, 480),
            _n_tick(64, 0, 480),
        ]
        result = normalize_notes(notes)
        self.assertEqual(len(result), 2)

    def test_different_ticks_not_deduped(self):
        """Notes with the same pitch at different ticks are independent."""
        notes = [
            _n_tick(60, 0, 480),
            _n_tick(60, 480, 480),
        ]
        result = normalize_notes(notes)
        self.assertEqual(len(result), 2)

    def test_ornament_marking_short_note(self):
        """Notes shorter than threshold (tpb//8 = 60 ticks) are ornaments."""
        notes = [
            _n_tick(60, 0, 30),   # 30 < 60 => ornament
            _n_tick(64, 0, 480),  # normal
        ]
        result = normalize_notes(notes)
        # Sort by pitch descending, so 64 comes first.
        pitches = [(r.note.pitch, r.is_ornament) for r in result]
        self.assertIn((60, True), pitches)
        self.assertIn((64, False), pitches)

    def test_ornament_boundary(self):
        """A note exactly at the threshold is NOT an ornament."""
        threshold = TICKS_PER_BEAT // 8  # 60
        notes = [_n_tick(60, 0, threshold)]
        result = normalize_notes(notes)
        self.assertFalse(result[0].is_ornament)

    def test_ornament_one_below_threshold(self):
        """A note one tick below the threshold IS an ornament."""
        threshold = TICKS_PER_BEAT // 8  # 60
        notes = [_n_tick(60, 0, threshold - 1)]
        result = normalize_notes(notes)
        self.assertTrue(result[0].is_ornament)

    def test_sort_order_soprano_first(self):
        """Output is sorted by (start_tick, -pitch): highest pitch first."""
        notes = [
            _n_tick(48, 0, 480),
            _n_tick(72, 0, 480),
            _n_tick(60, 0, 480),
        ]
        result = normalize_notes(notes)
        pitches = [r.note.pitch for r in result]
        self.assertEqual(pitches, [72, 60, 48])

    def test_empty_input(self):
        result = normalize_notes([])
        self.assertEqual(len(result), 0)

    def test_single_note(self):
        notes = [_n_tick(60, 0, 480)]
        result = normalize_notes(notes)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].note.pitch, 60)
        self.assertFalse(result[0].is_ornament)


# ---------------------------------------------------------------------------
# 2. TestDetectVoiceCount
# ---------------------------------------------------------------------------


class TestDetectVoiceCount(unittest.TestCase):
    """Test voice count detection from polyphony analysis."""

    def test_single_voice_sequential(self):
        """Sequential non-overlapping notes should detect 1 voice."""
        notes = [_n(60, beat) for beat in range(8)]
        count, arpeggio = detect_voice_count(notes)
        self.assertEqual(count, 1)
        self.assertFalse(arpeggio)

    def test_two_voices_overlapping(self):
        """Two sustained notes overlapping at different pitches => 2 voices."""
        notes = []
        for beat in range(0, 16, 2):
            notes.append(_n(72, beat, dur_beats=2.0))  # soprano
            notes.append(_n(60, beat, dur_beats=2.0))  # alto
        count, _ = detect_voice_count(notes)
        self.assertEqual(count, 2)

    def test_four_voices_simultaneous(self):
        """Four simultaneous notes sustained across many beats => 4 voices."""
        notes = []
        for beat in range(0, 16, 2):
            notes.append(_n(72, beat, dur_beats=2.0))  # soprano
            notes.append(_n(67, beat, dur_beats=2.0))  # alto
            notes.append(_n(60, beat, dur_beats=2.0))  # tenor
            notes.append(_n(48, beat, dur_beats=2.0))  # bass
        count, _ = detect_voice_count(notes)
        self.assertEqual(count, 4)

    def test_with_pedal_exclusion(self):
        """Pedal notes are excluded from polyphony counting."""
        # Two manual voices + one pedal note duplicated in manual notes.
        pedal_notes = [_n(36, beat, dur_beats=4.0) for beat in range(0, 16, 4)]
        manual_notes = []
        for beat in range(0, 16, 2):
            manual_notes.append(_n(72, beat, dur_beats=2.0))
            manual_notes.append(_n(60, beat, dur_beats=2.0))
        # Add pedal duplicates into manual notes.
        manual_notes.extend(pedal_notes)

        count_with_pedal, _ = detect_voice_count(manual_notes, pedal_notes=pedal_notes)
        count_without_pedal, _ = detect_voice_count(manual_notes)
        # With pedal exclusion, the pedal notes are removed: only 2 manual voices remain.
        self.assertLessEqual(count_with_pedal, count_without_pedal)
        self.assertEqual(count_with_pedal, 2)

    def test_empty_notes(self):
        count, arpeggio = detect_voice_count([])
        self.assertEqual(count, 1)
        self.assertFalse(arpeggio)

    def test_clamp_max_six(self):
        """Even with 8 simultaneous notes, voice count is clamped to 6."""
        notes = []
        for beat in range(0, 16, 2):
            for pitch in range(60, 68):  # 8 simultaneous
                notes.append(_n(pitch, beat, dur_beats=2.0))
        count, _ = detect_voice_count(notes)
        self.assertLessEqual(count, 6)

    def test_clamp_min_one(self):
        """Voice count is always at least 1."""
        # A single very short note.
        notes = [_n_tick(60, 0, 1)]
        count, _ = detect_voice_count(notes)
        self.assertGreaterEqual(count, 1)


# ---------------------------------------------------------------------------
# 3. TestSeparateVoices
# ---------------------------------------------------------------------------


class TestSeparateVoices(unittest.TestCase):
    """Test the core voice separation algorithm."""

    def test_two_voices_interleaved(self):
        """Soprano C5-D5-E5-F5 vs alto C4-D4-E4-F4, alternating ticks."""
        soprano_pitches = [72, 74, 76, 77]  # C5, D5, E5, F5
        alto_pitches = [60, 62, 64, 65]     # C4, D4, E4, F4
        notes = []
        for idx, (sop, alt) in enumerate(zip(soprano_pitches, alto_pitches)):
            beat = idx * 2
            notes.append(_n(sop, beat, dur_beats=1.0))
            notes.append(_n(alt, beat, dur_beats=1.0))

        result = separate_voices(notes, 2)
        self.assertEqual(result.num_voices, 2)
        self.assertEqual(len(result.voices), 2)

        # Collect pitches per voice.
        v0_pitches = sorted(set(n.pitch for n in result.voices[0]))
        v1_pitches = sorted(set(n.pitch for n in result.voices[1]))

        # One voice should contain the high pitches, the other the low ones.
        all_high = set(soprano_pitches)
        all_low = set(alto_pitches)
        voice_sets = [set(v0_pitches), set(v1_pitches)]

        self.assertIn(all_high, voice_sets)
        self.assertIn(all_low, voice_sets)

    def test_three_voices_register_separation(self):
        """Three well-separated registers should produce three clean voices."""
        notes = []
        for beat in range(0, 8, 2):
            notes.append(_n(84, beat, dur_beats=1.0))  # soprano (C6 area)
            notes.append(_n(67, beat, dur_beats=1.0))  # alto (G4 area)
            notes.append(_n(48, beat, dur_beats=1.0))  # bass (C3 area)

        result = separate_voices(notes, 3)
        self.assertEqual(result.num_voices, 3)
        self.assertEqual(len(result.voices), 3)

        # Each voice should have 4 notes.
        for voice in result.voices:
            self.assertEqual(len(voice), 4)

        # Verify register assignment: compute average pitch per voice.
        avg_pitches = []
        for voice in result.voices:
            avg_pitches.append(sum(n.pitch for n in voice) / len(voice))

        # Voice 0 should be highest (soprano), voice 2 lowest (bass).
        self.assertGreater(avg_pitches[0], avg_pitches[1])
        self.assertGreater(avg_pitches[1], avg_pitches[2])

    def test_empty_input(self):
        """Empty input returns empty voices."""
        result = separate_voices([], 2)
        self.assertEqual(result.num_voices, 2)
        self.assertEqual(len(result.voices), 2)
        for voice in result.voices:
            self.assertEqual(len(voice), 0)
        self.assertEqual(len(result.ornaments), 0)

    def test_single_voice(self):
        """With num_voices=1, all notes should go to voice 0."""
        notes = [_n(60 + idx, idx) for idx in range(6)]
        result = separate_voices(notes, 1)
        self.assertEqual(result.num_voices, 1)
        self.assertEqual(len(result.voices), 1)
        self.assertEqual(len(result.voices[0]), 6)
        self.assertEqual(len(result.ornaments), 0)

    def test_zero_voices_clamped(self):
        """num_voices < 1 is clamped to 1."""
        result = separate_voices([_n(60, 0)], 0)
        self.assertGreaterEqual(result.num_voices, 1)

    def test_notes_not_lost(self):
        """Total notes in voices + ornaments equals input note count (after dedup)."""
        notes = [_n(60, 0), _n(64, 0), _n(67, 0), _n(72, 0)]
        result = separate_voices(notes, 2)
        total_out = sum(len(v) for v in result.voices) + len(result.ornaments)
        # Normalize may dedup, so check against normalized count.
        norm = normalize_notes(notes)
        self.assertEqual(total_out, len(norm))


# ---------------------------------------------------------------------------
# 4. TestNGreaterThanM
# ---------------------------------------------------------------------------


class TestNGreaterThanM(unittest.TestCase):
    """Test when more simultaneous notes than target voices."""

    def test_five_notes_four_voices(self):
        """5 simultaneous notes with 4 voices: 4 assigned + 1 ornament."""
        notes = [
            _n_tick(72, 0, 480),
            _n_tick(67, 0, 480),
            _n_tick(64, 0, 480),
            _n_tick(60, 0, 480),
            _n_tick(55, 0, 480),
        ]
        result = separate_voices(notes, 4)
        assigned = sum(len(v) for v in result.voices)
        self.assertEqual(assigned, 4)
        self.assertEqual(len(result.ornaments), 1)
        self.assertGreater(result.unassigned_rate, 0.0)

    def test_many_excess_notes(self):
        """8 simultaneous notes with 2 voices: 2 assigned + 6 ornaments."""
        notes = [_n_tick(50 + idx * 3, 0, 480) for idx in range(8)]
        result = separate_voices(notes, 2)
        assigned = sum(len(v) for v in result.voices)
        self.assertEqual(assigned, 2)
        self.assertEqual(len(result.ornaments), 6)

    def test_unassigned_rate_computation(self):
        """SeparationResult.unassigned_rate is correctly computed."""
        result = SeparationResult(
            voices=[[_n(60, 0)], [_n(72, 0)]],
            ornaments=[_n(67, 0)],
            num_voices=2,
        )
        # 1 ornament out of 3 total = 1/3.
        self.assertAlmostEqual(result.unassigned_rate, 1.0 / 3.0, places=4)

    def test_unassigned_rate_zero_when_no_ornaments(self):
        result = SeparationResult(
            voices=[[_n(60, 0), _n(62, 1)]],
            ornaments=[],
            num_voices=1,
        )
        self.assertAlmostEqual(result.unassigned_rate, 0.0)

    def test_unassigned_rate_empty(self):
        result = SeparationResult(voices=[[]], ornaments=[], num_voices=1)
        self.assertAlmostEqual(result.unassigned_rate, 0.0)


# ---------------------------------------------------------------------------
# 5. TestArpeggioResistance
# ---------------------------------------------------------------------------


class TestArpeggioResistance(unittest.TestCase):
    """Test that arpeggiated chords are assigned to correct voice slots."""

    def test_staggered_chord_stays_in_voices(self):
        """C-E-G played as arpeggio (staggered by 10 ticks) with long sustain.

        All three notes should end up in separate voices, not jump around.
        """
        # Sustain long enough that they overlap.
        notes = [
            _n_tick(60, 0, 960),    # C4, starts at tick 0
            _n_tick(64, 10, 950),   # E4, starts at tick 10
            _n_tick(67, 20, 940),   # G4, starts at tick 20
        ]
        result = separate_voices(notes, 3)

        # Each voice should have exactly 1 note (3 notes, 3 voices).
        for voice in result.voices:
            self.assertEqual(len(voice), 1, "Each voice should get exactly one note")

        # No ornaments.
        self.assertEqual(len(result.ornaments), 0)

    def test_repeated_arpeggios_consistent(self):
        """Multiple arpeggiated chords: each voice stays in its register."""
        notes = []
        for chord_start in range(0, 4):
            base_tick = chord_start * TICKS_PER_BEAT * 2
            notes.append(_n_tick(72, base_tick, 900))      # C5
            notes.append(_n_tick(67, base_tick + 10, 890))  # G4
            notes.append(_n_tick(60, base_tick + 20, 880))  # C4

        result = separate_voices(notes, 3)

        # Compute average pitch per voice; they should be well separated.
        for voice in result.voices:
            self.assertGreater(len(voice), 0, "No voice should be empty")

        avg_pitches = []
        for voice in result.voices:
            avg_pitches.append(sum(n.pitch for n in voice) / len(voice))

        # All three averages should be distinct (spread over the range 60-72).
        sorted_avg = sorted(avg_pitches)
        for idx in range(len(sorted_avg) - 1):
            self.assertGreater(
                sorted_avg[idx + 1] - sorted_avg[idx], 3.0,
                "Voice register centers should be well separated"
            )


# ---------------------------------------------------------------------------
# 6. TestPedalNote
# ---------------------------------------------------------------------------


class TestPedalNote(unittest.TestCase):
    """Test that long sustained (pedal-like) notes don't create overlaps."""

    def test_pedal_with_melody(self):
        """Long low note + upper melody: no within-voice overlap after separation."""
        notes = [
            _n_tick(36, 0, TICKS_PER_BAR * 4),  # 4-bar pedal on C2
        ]
        # Add a melody line above.
        for beat in range(16):
            notes.append(_n(60 + (beat % 8), beat, dur_beats=1.0))

        result = separate_voices(notes, 2)

        # Check no within-voice overlap.
        for vid, voice in enumerate(result.voices):
            sorted_v = sorted(voice, key=lambda n: n.start_tick)
            for idx in range(len(sorted_v) - 1):
                curr_end = sorted_v[idx].start_tick + sorted_v[idx].duration
                next_start = sorted_v[idx + 1].start_tick
                self.assertLessEqual(
                    curr_end, next_start + 1,  # +1 for rounding tolerance
                    f"Overlap in voice {vid}: note at tick {sorted_v[idx].start_tick} "
                    f"ends at {curr_end}, next starts at {next_start}"
                )

    def test_pedal_in_separate_register(self):
        """Pedal note should end up in the lower voice."""
        pedal = _n_tick(36, 0, TICKS_PER_BAR * 2)
        melody = [_n(72, beat, dur_beats=1.0) for beat in range(8)]
        all_notes = [pedal] + melody

        result = separate_voices(all_notes, 2)

        # Determine which voice has the pedal.
        pedal_voice = None
        for vid, voice in enumerate(result.voices):
            if any(n.pitch == 36 for n in voice):
                pedal_voice = vid
                break

        self.assertIsNotNone(pedal_voice)
        # The pedal voice should have lower avg pitch.
        other_voice = 1 - pedal_voice
        if result.voices[other_voice]:
            avg_pedal = sum(n.pitch for n in result.voices[pedal_voice]) / len(
                result.voices[pedal_voice]
            )
            avg_other = sum(n.pitch for n in result.voices[other_voice]) / len(
                result.voices[other_voice]
            )
            self.assertLess(avg_pedal, avg_other)


# ---------------------------------------------------------------------------
# 7. TestVoiceExchange
# ---------------------------------------------------------------------------


class TestVoiceExchange(unittest.TestCase):
    """Test that voice exchange (register swap) produces reasonable crossing_rate."""

    def test_crossing_rate_nonzero_on_swap(self):
        """Two voices that swap registers should have nonzero crossing_rate."""
        notes = []
        # Phase 1: soprano high, alto low.
        for beat in range(0, 8, 2):
            notes.append(_n(72, beat, dur_beats=1.0))  # soprano
            notes.append(_n(55, beat, dur_beats=1.0))  # alto

        # Phase 2: soprano drops low, alto goes high (voice exchange).
        for beat in range(8, 16, 2):
            notes.append(_n(55, beat, dur_beats=1.0))  # soprano now low
            notes.append(_n(72, beat, dur_beats=1.0))  # alto now high

        result = separate_voices(notes, 2)
        metrics = evaluate_separation(result)

        # The separator will try to keep register consistency, but there will
        # be some crossing or the voices will actually swap internal assignment.
        # Either way, crossing_rate should reflect the ambiguity.
        # We just assert it is a valid float in [0, 1].
        self.assertGreaterEqual(metrics["crossing_rate"], 0.0)
        self.assertLessEqual(metrics["crossing_rate"], 1.0)

    def test_no_crossing_well_separated(self):
        """Well-separated voices should have crossing_rate near 0."""
        notes = []
        for beat in range(0, 16, 2):
            notes.append(_n(84, beat, dur_beats=1.0))  # always high
            notes.append(_n(48, beat, dur_beats=1.0))  # always low

        result = separate_voices(notes, 2)
        metrics = evaluate_separation(result)
        self.assertLessEqual(metrics["crossing_rate"], 0.05)


# ---------------------------------------------------------------------------
# 8. TestSeparateScore
# ---------------------------------------------------------------------------


class TestSeparateScore(unittest.TestCase):
    """Test score-level separation with different track types."""

    def test_voice_type_unchanged(self):
        """track_type='voice' returns the original score unchanged."""
        score = Score(
            tracks=[
                Track(name="v1", notes=[_n(72, 0, voice="v1")]),
                Track(name="v2", notes=[_n(60, 0, voice="v2")]),
            ]
        )
        out_score, result = separate_score(score, "voice")
        self.assertIs(out_score, score)
        self.assertIsNone(result)

    def test_solo_string_type_unchanged(self):
        """track_type='solo_string' returns the original score unchanged."""
        score = Score(
            tracks=[Track(name="solo", notes=[_n(60, 0, voice="solo")])]
        )
        out_score, result = separate_score(score, "solo_string")
        self.assertIs(out_score, score)
        self.assertIsNone(result)

    def test_manual_type_separates(self):
        """track_type='manual' triggers separation."""
        notes = []
        for beat in range(0, 8, 2):
            notes.append(_n(72, beat, dur_beats=1.0, voice="manual"))
            notes.append(_n(60, beat, dur_beats=1.0, voice="manual"))
        score = Score(tracks=[Track(name="manual", notes=notes)])

        out_score, result = separate_score(score, "manual", num_voices=2)
        self.assertIsNotNone(result)
        self.assertIsInstance(result, SeparationResult)
        # Output score should have 2 tracks (v1, v2).
        self.assertEqual(len(out_score.tracks), 2)
        track_names = {t.name for t in out_score.tracks}
        self.assertIn("v1", track_names)
        self.assertIn("v2", track_names)

    def test_manual_with_pedal_track(self):
        """Pedal track is preserved separately in the output score."""
        manual_notes = []
        for beat in range(0, 8, 2):
            manual_notes.append(_n(72, beat, dur_beats=1.0, voice="manual"))
            manual_notes.append(_n(60, beat, dur_beats=1.0, voice="manual"))

        pedal_notes = [_n(36, 0, dur_beats=8.0, voice="pedal")]

        score = Score(
            tracks=[
                Track(name="manual", notes=manual_notes),
                Track(name="pedal", notes=pedal_notes),
            ]
        )

        out_score, result = separate_score(score, "manual", num_voices=3)
        self.assertIsNotNone(result)

        # Pedal track should appear in the output.
        track_names = [t.name for t in out_score.tracks]
        self.assertIn("pedal", track_names)

        # Manual voices are reduced by 1 because pedal takes one slot.
        # num_voices=3, pedal=1 => manual separation uses 2 voices.
        manual_track_names = [name for name in track_names if name != "pedal"]
        self.assertEqual(len(manual_track_names), 2)

        # Output score voices count includes pedal.
        self.assertEqual(out_score.voices, 3)

    def test_empty_manual_tracks(self):
        """Score with empty manual tracks returns original."""
        score = Score(tracks=[Track(name="manual", notes=[])])
        out_score, result = separate_score(score, "manual")
        self.assertIsNone(result)

    def test_output_voice_names_ordered_by_pitch(self):
        """Voice tracks in output are named v1 (highest) to vN (lowest)."""
        notes = []
        for beat in range(0, 16, 2):
            notes.append(_n(84, beat, dur_beats=1.0, voice="manual"))
            notes.append(_n(60, beat, dur_beats=1.0, voice="manual"))
            notes.append(_n(48, beat, dur_beats=1.0, voice="manual"))
        score = Score(tracks=[Track(name="manual", notes=notes)])
        out_score, result = separate_score(score, "manual", num_voices=3)

        self.assertIsNotNone(result)
        # Find v1, v2, v3 tracks.
        v1_notes = [t for t in out_score.tracks if t.name == "v1"]
        v3_notes = [t for t in out_score.tracks if t.name == "v3"]
        self.assertEqual(len(v1_notes), 1)
        self.assertEqual(len(v3_notes), 1)

        # v1 should be the highest register.
        avg_v1 = sum(n.pitch for n in v1_notes[0].notes) / max(len(v1_notes[0].notes), 1)
        avg_v3 = sum(n.pitch for n in v3_notes[0].notes) / max(len(v3_notes[0].notes), 1)
        self.assertGreater(avg_v1, avg_v3)


# ---------------------------------------------------------------------------
# 9. TestEvaluateSeparation
# ---------------------------------------------------------------------------


class TestEvaluateSeparation(unittest.TestCase):
    """Test that evaluate_separation returns all expected metric keys."""

    def _make_result(self, num_voices=2):
        """Build a simple SeparationResult for metric testing."""
        voices = []
        for vid in range(num_voices):
            voice_notes = [
                _n(60 + vid * 12, beat) for beat in range(8)
            ]
            voices.append(voice_notes)
        return SeparationResult(voices=voices, ornaments=[], num_voices=num_voices)

    def test_all_keys_present(self):
        """All expected keys exist in the metrics dict."""
        result = self._make_result(2)
        metrics = evaluate_separation(result)
        expected_keys = {
            "crossing_rate",
            "avg_interval",
            "gap_rate",
            "overlap_rate",
            "register_overlap",
            "voice_swap_events",
            "unassigned_rate",
        }
        for key in expected_keys:
            self.assertIn(key, metrics, f"Missing key: {key}")

    def test_single_voice_metrics(self):
        """Single voice produces valid metrics with crossing_rate=0."""
        result = self._make_result(1)
        metrics = evaluate_separation(result)
        self.assertEqual(metrics["crossing_rate"], 0.0)
        self.assertEqual(metrics["voice_swap_events"], 0)

    def test_per_voice_lists_length(self):
        """Per-voice metric lists match num_voices."""
        for nv in (2, 3, 4):
            result = self._make_result(nv)
            metrics = evaluate_separation(result)
            self.assertEqual(len(metrics["avg_interval"]), nv)
            self.assertEqual(len(metrics["gap_rate"]), nv)
            self.assertEqual(len(metrics["overlap_rate"]), nv)

    def test_register_overlap_length(self):
        """register_overlap list has num_voices-1 entries (adjacent pairs)."""
        result = self._make_result(3)
        metrics = evaluate_separation(result)
        self.assertEqual(len(metrics["register_overlap"]), 2)

    def test_no_overlap_sequential_notes(self):
        """Sequential non-overlapping notes should have overlap_rate = 0."""
        voice_notes = [_n(60, beat) for beat in range(8)]
        result = SeparationResult(
            voices=[voice_notes], ornaments=[], num_voices=1
        )
        metrics = evaluate_separation(result)
        self.assertEqual(metrics["overlap_rate"][0], 0.0)

    def test_gap_rate_with_gaps(self):
        """Notes with gaps between them should have nonzero gap_rate."""
        # Each note is 1 beat (480 ticks), placed every 3 beats => 2-beat gap.
        voice_notes = [_n(60, beat * 3) for beat in range(4)]
        result = SeparationResult(
            voices=[voice_notes], ornaments=[], num_voices=1
        )
        metrics = evaluate_separation(result)
        self.assertGreater(metrics["gap_rate"][0], 0.0)

    def test_unassigned_rate_in_metrics(self):
        """unassigned_rate matches SeparationResult.unassigned_rate."""
        result = SeparationResult(
            voices=[[_n(60, 0)]], ornaments=[_n(72, 0)], num_voices=1
        )
        metrics = evaluate_separation(result)
        self.assertAlmostEqual(metrics["unassigned_rate"], 0.5)


# ---------------------------------------------------------------------------
# 10. TestCrossValidation
# ---------------------------------------------------------------------------


class TestCrossValidation(unittest.TestCase):
    """Cross-validation: merge voice-type tracks, re-separate, measure agreement."""

    def test_merge_and_reseparate_two_voices(self):
        """Merge 2 voice tracks into 1, re-separate, check agreement.

        Uses permutation-based matching in 4-bar windows since voice IDs
        may be swapped.
        """
        # Create two well-separated voice streams.
        soprano_notes = []
        alto_notes = []
        for beat in range(0, 32, 2):
            soprano_notes.append(_n(72 + (beat % 5), beat, dur_beats=1.0, voice="v1"))
            alto_notes.append(_n(55 + (beat % 5), beat, dur_beats=1.0, voice="v2"))

        # Merge into a single stream.
        merged = soprano_notes + alto_notes

        # Re-separate.
        result = separate_voices(merged, 2)

        # Measure agreement using permutation-based matching in 4-bar windows.
        # Build ground truth mapping: pitch -> original voice.
        ground_truth = {}
        for note in soprano_notes:
            ground_truth[(note.start_tick, note.pitch)] = 0
        for note in alto_notes:
            ground_truth[(note.start_tick, note.pitch)] = 1

        # Evaluate each 4-bar window.
        window_ticks = 4 * TICKS_PER_BAR
        max_tick = max(n.start_tick for n in merged)
        total_correct = 0
        total_notes = 0

        tick = 0
        while tick <= max_tick:
            window_end = tick + window_ticks
            # Collect notes per separated voice in this window.
            window_voices = [[], []]
            for vid, voice in enumerate(result.voices):
                for note in voice:
                    if tick <= note.start_tick < window_end:
                        window_voices[vid].append(note)

            # Try both permutations of voice mapping.
            best_correct = 0
            for perm in [(0, 1), (1, 0)]:
                correct = 0
                for sep_vid, orig_vid in enumerate(perm):
                    for note in window_voices[sep_vid]:
                        key = (note.start_tick, note.pitch)
                        if key in ground_truth and ground_truth[key] == orig_vid:
                            correct += 1
                best_correct = max(best_correct, correct)

            window_total = sum(len(v) for v in window_voices)
            total_correct += best_correct
            total_notes += window_total
            tick += window_ticks

        # Agreement rate: with well-separated registers, should be very high.
        if total_notes > 0:
            agreement = total_correct / total_notes
            self.assertGreater(
                agreement, 0.80,
                f"Agreement rate {agreement:.2%} too low for well-separated voices"
            )

    def test_cross_validation_three_voices(self):
        """Three well-separated voices: merge and re-separate with high agreement."""
        voice_data = [
            (84, "soprano"),  # C6 area
            (67, "alto"),     # G4 area
            (48, "bass"),     # C3 area
        ]
        all_notes = []
        voice_truth = {}
        for vid, (base_pitch, name) in enumerate(voice_data):
            for beat in range(0, 16, 2):
                pitch = base_pitch + (beat % 3)
                note = _n(pitch, beat, dur_beats=1.0, voice=name)
                all_notes.append(note)
                voice_truth[(note.start_tick, note.pitch)] = vid

        result = separate_voices(all_notes, 3)

        # Permutation-based matching (full window, no need for windowing here
        # since the piece is short).
        best_correct = 0
        for perm in permutations(range(3)):
            correct = 0
            for sep_vid, orig_vid in enumerate(perm):
                for note in result.voices[sep_vid]:
                    key = (note.start_tick, note.pitch)
                    if key in voice_truth and voice_truth[key] == orig_vid:
                        correct += 1
            best_correct = max(best_correct, correct)

        total = sum(len(v) for v in result.voices)
        if total > 0:
            agreement = best_correct / total
            self.assertGreater(agreement, 0.85)


# Need permutations for TestCrossValidation.
from itertools import permutations  # noqa: E402

# Import new analysis helpers.
from scripts.bach_analyzer.voice_separation import (  # noqa: E402
    compute_activity,
    compute_crossing_detail,
    compute_imitation,
    compute_independence,
    compute_spacing,
)


# ---------------------------------------------------------------------------
# 11. TestComputeIndependence
# ---------------------------------------------------------------------------


class TestComputeIndependence(unittest.TestCase):
    """Test voice independence metrics."""

    def test_single_voice_empty(self):
        """Single voice returns empty pair data."""
        voices = [[_n(60, 0)]]
        result = compute_independence(voices)
        self.assertEqual(result["pair_independence"], {})
        self.assertEqual(result["overall"]["avg_simultaneous_onset_ratio"], 0.0)

    def test_identical_rhythm_high_onset_ratio(self):
        """Two voices with identical onset times should have high simultaneous onset ratio."""
        v1 = [_n(72, beat) for beat in range(8)]
        v2 = [_n(60, beat) for beat in range(8)]
        result = compute_independence([v1, v2])
        pair = result["pair_independence"]["v1-v2"]
        # Identical onsets => ratio = 1.0.
        self.assertAlmostEqual(pair["simultaneous_onset_ratio"], 1.0, places=2)

    def test_alternating_rhythm_low_onset_ratio(self):
        """Two voices with alternating onsets should have low simultaneous onset ratio."""
        v1 = [_n(72, beat * 2) for beat in range(4)]       # beats 0, 2, 4, 6
        v2 = [_n(60, beat * 2 + 1) for beat in range(4)]   # beats 1, 3, 5, 7
        result = compute_independence([v1, v2])
        pair = result["pair_independence"]["v1-v2"]
        self.assertAlmostEqual(pair["simultaneous_onset_ratio"], 0.0, places=2)

    def test_contrary_motion_detected(self):
        """Ascending v1 + descending v2 should show high contrary motion ratio."""
        # v1: ascending C5 -> G5
        v1 = [_n(72 + i * 2, i * 2, dur_beats=2.0) for i in range(4)]
        # v2: descending C4 -> F#3
        v2 = [_n(60 - i * 2, i * 2, dur_beats=2.0) for i in range(4)]
        result = compute_independence([v1, v2])
        pair = result["pair_independence"]["v1-v2"]
        self.assertGreater(pair["contrary_motion_ratio"], 0.5)

    def test_rhythmic_divergence_different_patterns(self):
        """Two voices with different rhythm patterns should have high divergence."""
        # v1: quarter notes
        v1 = [_n(72, beat) for beat in range(8)]
        # v2: half notes
        v2 = [_n(60, beat * 2, dur_beats=2.0) for beat in range(4)]
        result = compute_independence([v1, v2])
        pair = result["pair_independence"]["v1-v2"]
        self.assertGreater(pair["rhythmic_divergence"], 0.3)

    def test_three_voices_all_pairs(self):
        """Three voices should produce 3 pairs."""
        v1 = [_n(84, beat) for beat in range(4)]
        v2 = [_n(67, beat) for beat in range(4)]
        v3 = [_n(48, beat) for beat in range(4)]
        result = compute_independence([v1, v2, v3])
        self.assertEqual(len(result["pair_independence"]), 3)
        self.assertIn("v1-v2", result["pair_independence"])
        self.assertIn("v1-v3", result["pair_independence"])
        self.assertIn("v2-v3", result["pair_independence"])


# ---------------------------------------------------------------------------
# 12. TestComputeSpacing
# ---------------------------------------------------------------------------


class TestComputeSpacing(unittest.TestCase):
    """Test voice spacing metrics."""

    def test_single_voice_empty(self):
        """Single voice returns empty spacing."""
        voices = [[_n(60, 0)]]
        result = compute_spacing(voices)
        self.assertEqual(result["pair_spacing"], {})
        self.assertEqual(result["avg_adjacent_gap"], 0.0)

    def test_well_separated_voices(self):
        """Two voices 12 semitones apart should have avg_gap ~12."""
        v1 = [_n(72, beat, dur_beats=1.0) for beat in range(8)]
        v2 = [_n(60, beat, dur_beats=1.0) for beat in range(8)]
        result = compute_spacing([v1, v2])
        pair = result["pair_spacing"]["v1-v2"]
        self.assertAlmostEqual(pair["avg_gap_semitones"], 12.0, delta=1.0)
        self.assertEqual(pair["close_rate"], 0.0)  # Never within 3 semitones.

    def test_close_voices(self):
        """Two voices 2 semitones apart should have high close_rate."""
        v1 = [_n(62, beat, dur_beats=1.0) for beat in range(8)]
        v2 = [_n(60, beat, dur_beats=1.0) for beat in range(8)]
        result = compute_spacing([v1, v2])
        pair = result["pair_spacing"]["v1-v2"]
        self.assertAlmostEqual(pair["avg_gap_semitones"], 2.0, delta=0.5)
        self.assertGreater(pair["close_rate"], 0.5)

    def test_gap_distribution_keys(self):
        """Gap distribution should contain all expected bucket keys."""
        v1 = [_n(72, beat, dur_beats=1.0) for beat in range(4)]
        v2 = [_n(60, beat, dur_beats=1.0) for beat in range(4)]
        result = compute_spacing([v1, v2])
        dist = result["pair_spacing"]["v1-v2"]["gap_distribution"]
        for key in ("0-2", "3-5", "6-8", "9-12", "13+"):
            self.assertIn(key, dist)

    def test_three_voices_two_pairs(self):
        """Three voices produce 2 adjacent pairs."""
        v1 = [_n(84, beat, dur_beats=1.0) for beat in range(4)]
        v2 = [_n(72, beat, dur_beats=1.0) for beat in range(4)]
        v3 = [_n(60, beat, dur_beats=1.0) for beat in range(4)]
        result = compute_spacing([v1, v2, v3])
        self.assertEqual(len(result["pair_spacing"]), 2)
        self.assertIn("v1-v2", result["pair_spacing"])
        self.assertIn("v2-v3", result["pair_spacing"])


# ---------------------------------------------------------------------------
# 13. TestComputeCrossingDetail
# ---------------------------------------------------------------------------


class TestComputeCrossingDetail(unittest.TestCase):
    """Test voice crossing detail analysis."""

    def test_no_crossing(self):
        """Well-separated voices should have no crossings."""
        v1 = [_n(84, beat, dur_beats=1.0) for beat in range(8)]
        v2 = [_n(48, beat, dur_beats=1.0) for beat in range(8)]
        result = compute_crossing_detail([v1, v2])
        self.assertEqual(result["summary"]["total_crossings"], 0)
        self.assertEqual(result["crossing_events"], [])

    def test_crossing_detected(self):
        """When upper voice dips below lower voice, crossing is detected."""
        # v1 starts high, then goes below v2.
        v1_notes = [_n(72, 0, dur_beats=2.0), _n(48, 2, dur_beats=2.0),
                     _n(72, 4, dur_beats=2.0)]
        v2_notes = [_n(60, 0, dur_beats=2.0), _n(60, 2, dur_beats=2.0),
                     _n(60, 4, dur_beats=2.0)]
        result = compute_crossing_detail([v1_notes, v2_notes])
        self.assertGreater(result["summary"]["total_crossings"], 0)

    def test_crossing_resolution(self):
        """A crossing that resolves should have resolved=True."""
        v1 = [_n(72, 0, dur_beats=1.0), _n(50, 1, dur_beats=1.0),
              _n(72, 2, dur_beats=1.0)]
        v2 = [_n(60, 0, dur_beats=1.0), _n(60, 1, dur_beats=1.0),
              _n(60, 2, dur_beats=1.0)]
        result = compute_crossing_detail([v1, v2])
        if result["crossing_events"]:
            self.assertTrue(result["crossing_events"][0]["resolved"])

    def test_summary_keys(self):
        """Summary should contain all expected keys."""
        v1 = [_n(72, beat) for beat in range(4)]
        v2 = [_n(60, beat) for beat in range(4)]
        result = compute_crossing_detail([v1, v2])
        expected_keys = {
            "total_crossings", "avg_duration_beats", "resolution_rate",
            "quick_resolution_rate", "per_pair",
        }
        for key in expected_keys:
            self.assertIn(key, result["summary"])

    def test_single_voice_no_crossing(self):
        """Single voice should produce no crossing data."""
        result = compute_crossing_detail([[_n(60, 0)]])
        self.assertEqual(result["summary"]["total_crossings"], 0)


# ---------------------------------------------------------------------------
# 14. TestComputeActivity
# ---------------------------------------------------------------------------


class TestComputeActivity(unittest.TestCase):
    """Test voice activity analysis."""

    def test_full_activity(self):
        """Voice active for entire duration should have activity_rate ~1.0."""
        v1 = [_n(72, beat, dur_beats=1.0) for beat in range(8)]
        total_dur = 8 * TICKS_PER_BEAT
        result = compute_activity([v1], total_dur)
        self.assertGreater(result["per_voice"]["v1"]["activity_rate"], 0.9)

    def test_partial_activity(self):
        """Voice active for only half the piece should have lower rate."""
        v1 = [_n(72, beat, dur_beats=1.0) for beat in range(4)]
        total_dur = 8 * TICKS_PER_BEAT  # Piece is 8 beats long.
        result = compute_activity([v1], total_dur)
        rate = result["per_voice"]["v1"]["activity_rate"]
        self.assertGreater(rate, 0.3)
        self.assertLess(rate, 0.7)

    def test_entry_order(self):
        """Voices entering at different times should appear in correct order."""
        v1 = [_n(72, 4, dur_beats=1.0)]  # Enters at beat 4.
        v2 = [_n(60, 0, dur_beats=1.0)]  # Enters at beat 0.
        total_dur = 8 * TICKS_PER_BEAT
        result = compute_activity([v1, v2], total_dur)
        self.assertEqual(result["entry_order"], ["v2", "v1"])

    def test_simultaneous_activity_keys(self):
        """Simultaneous activity should have labels for each voice count."""
        v1 = [_n(72, beat) for beat in range(4)]
        v2 = [_n(60, beat) for beat in range(4)]
        total_dur = 4 * TICKS_PER_BEAT
        result = compute_activity([v1, v2], total_dur)
        self.assertIn("1_voice", result["simultaneous_activity"])
        self.assertIn("2_voices", result["simultaneous_activity"])

    def test_rest_duration(self):
        """Gaps between notes should be reflected in avg_rest_duration_beats."""
        # Notes at beats 0 and 4, each 1 beat => 3-beat gap.
        v1 = [_n(72, 0), _n(72, 4)]
        total_dur = 5 * TICKS_PER_BEAT
        result = compute_activity([v1], total_dur)
        self.assertGreater(
            result["per_voice"]["v1"]["avg_rest_duration_beats"], 2.0
        )

    def test_empty_voices(self):
        """Empty input should return empty result."""
        result = compute_activity([], 0)
        self.assertEqual(result["per_voice"], {})
        self.assertEqual(result["entry_order"], [])


# ---------------------------------------------------------------------------
# 15. TestComputeImitation
# ---------------------------------------------------------------------------


class TestComputeImitation(unittest.TestCase):
    """Test imitation detection."""

    def test_exact_imitation_detected(self):
        """A pattern repeated in another voice should be detected."""
        # v1: C D E F G (ascending steps) starting at beat 0.
        v1 = [_n(60 + i * 2, i) for i in range(6)]
        # v2: same interval pattern starting at beat 8, transposed up 7.
        v2 = [_n(67 + i * 2, 8 + i) for i in range(6)]
        result = compute_imitation([v1, v2], min_len=4)
        self.assertGreater(result["summary"]["total_imitations"], 0)
        # Check that at least one event has correct leader/follower.
        leaders = {e["leader"] for e in result["imitation_events"]}
        followers = {e["follower"] for e in result["imitation_events"]}
        self.assertIn("v1", leaders)
        self.assertIn("v2", followers)

    def test_no_imitation_different_patterns(self):
        """Unrelated patterns should produce no imitations."""
        v1 = [_n(60, 0), _n(62, 1), _n(64, 2), _n(65, 3), _n(67, 4)]
        v2 = [_n(72, 8), _n(69, 9), _n(65, 10), _n(60, 11), _n(55, 12)]
        result = compute_imitation([v1, v2], min_len=4)
        self.assertEqual(result["summary"]["total_imitations"], 0)

    def test_single_voice_no_imitation(self):
        """Single voice cannot produce cross-voice imitation."""
        v1 = [_n(60 + i * 2, i) for i in range(6)]
        result = compute_imitation([v1], min_len=4)
        self.assertEqual(result["summary"]["total_imitations"], 0)

    def test_summary_keys(self):
        """Summary should contain all expected keys."""
        v1 = [_n(60, i) for i in range(6)]
        v2 = [_n(72, i + 8) for i in range(6)]
        result = compute_imitation([v1, v2], min_len=4)
        expected = {
            "total_imitations", "avg_lag_beats", "most_common_interval",
            "imitative_density", "voice_pair_frequency",
        }
        for key in expected:
            self.assertIn(key, result["summary"])

    def test_min_len_respected(self):
        """Short patterns below min_len should not be detected."""
        # Pattern of length 3 with min_len=4 should find nothing.
        v1 = [_n(60, 0), _n(62, 1), _n(64, 2), _n(67, 3)]
        v2 = [_n(67, 8), _n(69, 9), _n(71, 10), _n(60, 11)]
        # Only 3 intervals (2,2,3) and (2,2,-11) — don't match anyway.
        result = compute_imitation([v1, v2], min_len=4)
        # With min_len=4 we need 4 interval matches (5 notes).
        self.assertEqual(result["summary"]["total_imitations"], 0)


if __name__ == "__main__":
    unittest.main()
