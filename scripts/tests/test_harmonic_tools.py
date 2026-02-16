"""Tests for harmonic analysis tools in bach_reference_server.py.

Covers chord estimation, degree-to-roman conversion, suspension detection,
bass track detection, chord support PC extraction, and scoring module
harmonic helpers. Uses MCP mock pattern from test_ngram_tools.py.
"""

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock

# Add project root to path.
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

# ---------------------------------------------------------------------------
# Mock the mcp package so the server module can be imported.
# ---------------------------------------------------------------------------


class _FakeFastMCP:
    def __init__(self, *a, **kw):
        pass

    def tool(self):
        """Decorator that just returns the function unmodified."""
        def _decorator(fn):
            return fn
        return _decorator

    def run(self):
        pass


_mcp_mock = MagicMock()
_mcp_mock.server.fastmcp.FastMCP = _FakeFastMCP
sys.modules.setdefault("mcp", _mcp_mock)
sys.modules.setdefault("mcp.server", _mcp_mock.server)
sys.modules.setdefault("mcp.server.fastmcp", _mcp_mock.server.fastmcp)

from scripts.bach_reference_server import (
    CHORD_TEMPLATES,
    ChordEstimate,
    MAJOR_SCALE_SEMITONES,
    MINOR_SCALE_SEMITONES,
    _DEGREE_TO_FUNCTION,
    _MAJOR_NUMERALS,
    _MINOR_NUMERALS,
    _beat_strength,
    _degree_to_roman,
    _detect_bass_track,
    _detect_suspension,
    _estimate_chord,
    _extract_chord_support_pcs,
    reference_to_score,
    sounding_note_at,
)
from scripts.bach_analyzer.model import (
    Note,
    Score,
    Track,
    TICKS_PER_BAR,
    TICKS_PER_BEAT,
)
from scripts.bach_analyzer.score import (
    _count_cadences_simplified,
    _count_parallel_perfects,
    _extract_degree_distribution,
    _extract_function_distribution,
    _extract_nct_distribution,
    _normalize,
    extract_bass_per_beat,
)


# ---------------------------------------------------------------------------
# Test helpers
# ---------------------------------------------------------------------------


def _n(pitch, tick, dur=TICKS_PER_BEAT, voice="soprano"):
    """Create a Note for testing."""
    return Note(pitch=pitch, velocity=80, start_tick=tick, duration=dur, voice=voice)


def _track(name, notes):
    """Create a Track for testing."""
    return Track(name=name, notes=notes)


def _score(tracks, form="fugue"):
    """Create a Score for testing."""
    return Score(tracks=tracks, form=form)


# ---------------------------------------------------------------------------
# TestChordEstimation
# ---------------------------------------------------------------------------


class TestChordEstimation(unittest.TestCase):
    """Tests for _estimate_chord template matching and confidence."""

    def test_major_triad_root_position(self):
        """C-E-G weighted PCs should estimate C major, root position, high confidence."""
        # C=0, E=4, G=7 with strong weights
        weighted_pcs = {0: 1.0, 4: 0.8, 7: 0.9}
        result = _estimate_chord(weighted_pcs, bass_pc=0, tonic=0, is_minor=False)
        self.assertEqual(result.root_pc, 0)
        self.assertEqual(result.quality, "M")
        self.assertEqual(result.degree, 0)
        self.assertEqual(result.inversion, "root")
        self.assertGreater(result.confidence, 0.5)

    def test_minor_triad(self):
        """A-C-E weighted PCs should estimate A minor."""
        # A=9, C=0, E=4
        weighted_pcs = {9: 1.0, 0: 0.8, 4: 0.9}
        result = _estimate_chord(weighted_pcs, bass_pc=9, tonic=0, is_minor=False)
        self.assertEqual(result.root_pc, 9)
        self.assertEqual(result.quality, "m")

    def test_dominant_seventh(self):
        """G-B-D-F should estimate G dom7."""
        # G=7, B=11, D=2, F=5
        weighted_pcs = {7: 1.0, 11: 0.8, 2: 0.7, 5: 0.6}
        result = _estimate_chord(weighted_pcs, bass_pc=7, tonic=0, is_minor=False)
        self.assertEqual(result.root_pc, 7)
        self.assertEqual(result.quality, "dom7")

    def test_diminished_triad(self):
        """B-D-F should estimate B dim."""
        # B=11, D=2, F=5
        weighted_pcs = {11: 1.0, 2: 0.8, 5: 0.7}
        result = _estimate_chord(weighted_pcs, bass_pc=11, tonic=0, is_minor=False)
        self.assertEqual(result.root_pc, 11)
        self.assertEqual(result.quality, "dim")

    def test_first_inversion(self):
        """E in bass with C-E-G PCs should detect 1st inversion of C major."""
        # Bass is E (pc=4), chord is C-E-G
        weighted_pcs = {0: 0.8, 4: 1.0, 7: 0.7}
        result = _estimate_chord(weighted_pcs, bass_pc=4, tonic=0, is_minor=False)
        self.assertEqual(result.root_pc, 0)
        self.assertEqual(result.quality, "M")
        self.assertEqual(result.inversion, "1st")

    def test_single_pc_low_confidence(self):
        """Only one PC should produce confidence near 0."""
        weighted_pcs = {0: 1.0}
        result = _estimate_chord(weighted_pcs, bass_pc=0, tonic=0, is_minor=False)
        # Single PC: the code returns confidence=0.15
        self.assertLessEqual(result.confidence, 0.2)

    def test_two_pcs_capped_confidence(self):
        """Only two PCs should have confidence <= 0.6."""
        weighted_pcs = {0: 1.0, 4: 0.9}
        result = _estimate_chord(weighted_pcs, bass_pc=0, tonic=0, is_minor=False)
        self.assertLessEqual(result.confidence, 0.6)

    def test_markov_prior(self):
        """V->I should be boosted when prev_chord is V (degree=4)."""
        weighted_pcs = {0: 0.8, 4: 0.7, 7: 0.6}
        prev_v = ChordEstimate(
            root_pc=7, quality="M", degree=4,
            inversion="root", confidence=0.8, bass_pc=7,
        )
        result_with_prior = _estimate_chord(
            weighted_pcs, bass_pc=0, tonic=0, is_minor=False,
            prev_chord=prev_v,
        )
        result_without = _estimate_chord(
            weighted_pcs, bass_pc=0, tonic=0, is_minor=False,
            prev_chord=None,
        )
        # Both should identify C major; with Markov prior, confidence may differ
        self.assertEqual(result_with_prior.root_pc, 0)
        self.assertEqual(result_without.root_pc, 0)
        # The V->I transition pair (4, 0) has prior=1.5, so score should be boosted
        self.assertGreaterEqual(result_with_prior.confidence, result_without.confidence)

    def test_empty_pcs(self):
        """Empty dict should produce confidence 0.0."""
        result = _estimate_chord({}, bass_pc=None, tonic=0, is_minor=False)
        self.assertEqual(result.confidence, 0.0)
        self.assertEqual(result.quality, "")


# ---------------------------------------------------------------------------
# TestDegreeToRoman
# ---------------------------------------------------------------------------


class TestDegreeToRoman(unittest.TestCase):
    """Tests for _degree_to_roman conversion."""

    def test_major_key_numerals(self):
        """All 7 degrees in major with M/m qualities should produce correct numerals."""
        # Major triads: I, IV, V use quality "M"
        self.assertEqual(_degree_to_roman(0, "M", is_minor=False), "I")
        self.assertEqual(_degree_to_roman(3, "M", is_minor=False), "IV")
        self.assertEqual(_degree_to_roman(4, "M", is_minor=False), "V")
        # Minor triads: ii, iii, vi use quality "m"
        self.assertEqual(_degree_to_roman(1, "m", is_minor=False), "ii")
        self.assertEqual(_degree_to_roman(2, "m", is_minor=False), "iii")
        self.assertEqual(_degree_to_roman(5, "m", is_minor=False), "vi")
        # Diminished: vii uses quality "dim"
        self.assertEqual(_degree_to_roman(6, "dim", is_minor=False), "vii\u00b0")

    def test_minor_key_numerals(self):
        """All 7 degrees in minor with appropriate qualities."""
        self.assertEqual(_degree_to_roman(0, "m", is_minor=True), "i")
        self.assertEqual(_degree_to_roman(2, "M", is_minor=True), "III")
        self.assertEqual(_degree_to_roman(4, "M", is_minor=True), "V")
        self.assertEqual(_degree_to_roman(1, "dim", is_minor=True), "ii\u00b0")

    def test_dom7_suffix(self):
        """degree=4 with quality='dom7' should produce 'V' (uppercase, no suffix)."""
        result = _degree_to_roman(4, "dom7", is_minor=False)
        self.assertEqual(result, "V")

    def test_dim7_suffix(self):
        """degree=6 with quality='dim7' should produce 'vii' + degree symbol."""
        result = _degree_to_roman(6, "dim7", is_minor=False)
        self.assertEqual(result, "vii\u00b0")

    def test_aug_suffix(self):
        """Augmented quality should produce uppercase + '+'."""
        result = _degree_to_roman(2, "aug", is_minor=False)
        self.assertEqual(result, "III+")

    def test_fallback_for_unknown_quality(self):
        """Unknown quality falls back to mode-based numeral table."""
        result = _degree_to_roman(0, "sus4", is_minor=False)
        # Falls through to base = _MAJOR_NUMERALS[0] = "I"
        self.assertEqual(result, "I")


# ---------------------------------------------------------------------------
# TestDetectSuspension
# ---------------------------------------------------------------------------


class TestDetectSuspension(unittest.TestCase):
    """Tests for _detect_suspension."""

    def _chord(self, root=0, quality="M", degree=0):
        return ChordEstimate(
            root_pc=root, quality=quality, degree=degree,
            inversion="root", confidence=0.8, bass_pc=root,
        )

    def test_basic_suspension(self):
        """Previous note same pitch, next note step down, strong beat -> True."""
        # Previous: C5 at tick 0
        prev = _n(72, 0, TICKS_PER_BEAT)
        # Current: C5 at tick 480 (beat 2, which has strength 0.5)
        note = _n(72, TICKS_PER_BEAT, TICKS_PER_BEAT)
        # Next: B4 at tick 960 (step down by 1 semitone)
        nxt = _n(71, 2 * TICKS_PER_BEAT, TICKS_PER_BEAT)
        chord = self._chord()
        result = _detect_suspension(note, prev, nxt, chord, None)
        self.assertTrue(result)

    def test_not_suspension_weak_beat(self):
        """Same setup but on a weak beat (beat strength < 0.5) -> False."""
        # Tick at an eighth note position: beat_strength = 0.25
        weak_tick = TICKS_PER_BEAT + TICKS_PER_BEAT // 2  # 720 = 1.5 beats
        prev = _n(72, TICKS_PER_BEAT, TICKS_PER_BEAT // 2)
        note = _n(72, weak_tick, TICKS_PER_BEAT // 2)
        nxt = _n(71, weak_tick + TICKS_PER_BEAT // 2, TICKS_PER_BEAT // 2)
        chord = self._chord()
        result = _detect_suspension(note, prev, nxt, chord, None)
        self.assertFalse(result)

    def test_not_suspension_different_pitch(self):
        """Different pitch from prev -> False."""
        prev = _n(74, 0, TICKS_PER_BEAT)  # D5
        note = _n(72, TICKS_PER_BEAT, TICKS_PER_BEAT)  # C5 (different from D5)
        nxt = _n(71, 2 * TICKS_PER_BEAT, TICKS_PER_BEAT)
        chord = self._chord()
        result = _detect_suspension(note, prev, nxt, chord, None)
        self.assertFalse(result)

    def test_not_suspension_no_prev(self):
        """No previous note -> False."""
        note = _n(72, TICKS_PER_BEAT, TICKS_PER_BEAT)
        nxt = _n(71, 2 * TICKS_PER_BEAT, TICKS_PER_BEAT)
        chord = self._chord()
        result = _detect_suspension(note, None, nxt, chord, None)
        self.assertFalse(result)

    def test_not_suspension_ascending_resolution(self):
        """Resolution upward -> False (suspensions resolve down)."""
        prev = _n(72, 0, TICKS_PER_BEAT)
        note = _n(72, TICKS_PER_BEAT, TICKS_PER_BEAT)
        nxt = _n(74, 2 * TICKS_PER_BEAT, TICKS_PER_BEAT)  # D5 (up)
        chord = self._chord()
        result = _detect_suspension(note, prev, nxt, chord, None)
        self.assertFalse(result)


# ---------------------------------------------------------------------------
# TestDetectBassTrack
# ---------------------------------------------------------------------------


class TestDetectBassTrack(unittest.TestCase):
    """Tests for _detect_bass_track auto-detection."""

    def test_pedal_track_detected(self):
        """Score with a track named 'pedal' should select it."""
        s = _score([
            _track("soprano", [_n(72, 0)]),
            _track("alto", [_n(65, 0)]),
            _track("pedal", [_n(36, 0)]),
        ])
        result = _detect_bass_track(s)
        self.assertIsNotNone(result)
        self.assertEqual(result.name, "pedal")

    def test_lower_track_detected(self):
        """Score with a track named 'lower' should select it."""
        s = _score([
            _track("upper", [_n(72, 0)]),
            _track("lower", [_n(48, 0)]),
        ])
        result = _detect_bass_track(s)
        self.assertIsNotNone(result)
        self.assertEqual(result.name, "lower")

    def test_lowest_pitch_fallback(self):
        """Score with unnamed tracks should select the one with lowest avg pitch."""
        s = _score([
            _track("voice_a", [_n(72, 0), _n(76, 480)]),   # avg ~74
            _track("voice_b", [_n(60, 0), _n(64, 480)]),   # avg ~62
            _track("voice_c", [_n(40, 0), _n(44, 480)]),   # avg ~42 (lowest)
        ])
        result = _detect_bass_track(s)
        self.assertIsNotNone(result)
        self.assertEqual(result.name, "voice_c")

    def test_empty_score(self):
        """Score with no tracks should return None."""
        s = _score([])
        result = _detect_bass_track(s)
        self.assertIsNone(result)

    def test_priority_pedal_over_lower(self):
        """'pedal' should be preferred over 'lower'."""
        s = _score([
            _track("lower", [_n(48, 0)]),
            _track("pedal", [_n(36, 0)]),
        ])
        result = _detect_bass_track(s)
        self.assertIsNotNone(result)
        self.assertEqual(result.name, "pedal")


# ---------------------------------------------------------------------------
# TestExtractChordSupportPCs
# ---------------------------------------------------------------------------


class TestExtractChordSupportPCs(unittest.TestCase):
    """Tests for _extract_chord_support_pcs weight computation."""

    def test_single_note(self):
        """One note sounding should return its PC with appropriate weight."""
        notes = [_n(60, 0, TICKS_PER_BEAT, "soprano")]  # C4, quarter note at beat 1
        tracks_notes = [("soprano", notes)]
        pcs, bass_pc = _extract_chord_support_pcs(tracks_notes, 0, TICKS_PER_BEAT)
        self.assertIn(0, pcs)
        self.assertGreater(pcs[0], 0.0)

    def test_bass_boost(self):
        """Last track (bass) note should get 1.2x voice weight."""
        upper = [_n(72, 0, TICKS_PER_BEAT, "soprano")]
        lower = [_n(48, 0, TICKS_PER_BEAT, "bass")]
        # Two-track layout: soprano is index 0, bass is index 1 (last = bass)
        tracks_notes = [("soprano", upper), ("bass", lower)]

        pcs, bass_pc_val = _extract_chord_support_pcs(tracks_notes, 0, TICKS_PER_BEAT)
        # bass_pc should be the bass note's PC
        self.assertEqual(bass_pc_val, 0)  # C = pc 0

        # Weight for bass (pc 0) should be greater than weight for soprano (pc 0
        # happens to be same pitch class... use different PCs)
        upper2 = [_n(76, 0, TICKS_PER_BEAT, "soprano")]  # E5, pc=4
        lower2 = [_n(48, 0, TICKS_PER_BEAT, "bass")]     # C3, pc=0
        tracks2 = [("soprano", upper2), ("bass", lower2)]
        pcs2, _ = _extract_chord_support_pcs(tracks2, 0, TICKS_PER_BEAT)
        # Both at same duration and onset, but bass gets 1.2x -> bass weight > soprano weight
        self.assertGreater(pcs2[0], pcs2[4])

    def test_short_note_attenuation(self):
        """A very short note (< 0.25 beats) gets attenuated weight."""
        # 16th note = 120 ticks, which is 0.25 beats exactly. Use 100 ticks (< 0.25 beats)
        short_dur = 100  # < 0.25 * 480 = 120
        notes = [_n(60, TICKS_PER_BEAT, short_dur, "soprano")]  # on beat 2 (weak)
        tracks_notes = [("soprano", notes)]
        pcs, _ = _extract_chord_support_pcs(tracks_notes, TICKS_PER_BEAT, TICKS_PER_BEAT)
        # Short note on weak beat -> weight = 0.1
        self.assertIn(0, pcs)
        self.assertLessEqual(pcs[0], 0.2)

    def test_no_notes_sounding(self):
        """No notes at the sampled tick should return empty dict."""
        notes = [_n(60, 0, TICKS_PER_BEAT // 2, "soprano")]  # ends at tick 240
        tracks_notes = [("soprano", notes)]
        pcs, bass_pc = _extract_chord_support_pcs(tracks_notes, TICKS_PER_BEAT, TICKS_PER_BEAT)
        self.assertEqual(len(pcs), 0)
        self.assertIsNone(bass_pc)


# ---------------------------------------------------------------------------
# TestScoringEnhancements
# ---------------------------------------------------------------------------


class TestScoringEnhancements(unittest.TestCase):
    """Tests for harmonic helpers in scripts/bach_analyzer/score.py."""

    def test_bass_per_beat(self):
        """Simple 3-beat score with bass notes on C, G, C should return [0, 7, 0]."""
        # Bass voice with notes covering 3 beats
        bass_notes = [
            _n(48, 0, TICKS_PER_BEAT, "bass"),                  # C3 at beat 0
            _n(55, TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),     # G3 at beat 1
            _n(48, 2 * TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"), # C3 at beat 2
        ]
        s = _score([_track("bass", bass_notes)])
        result = extract_bass_per_beat(s)
        self.assertEqual(result, [0, 7, 0])

    def test_function_distribution(self):
        """C bass and G bass should produce T and D functions."""
        bass_notes = [
            _n(48, 0, TICKS_PER_BEAT, "bass"),              # C3 -> T
            _n(55, TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),  # G3 -> D
        ]
        s = _score([_track("bass", bass_notes)])
        dist = _extract_function_distribution(s)
        self.assertGreater(dist.get("T", 0), 0)
        self.assertGreater(dist.get("D", 0), 0)
        # Only T and D should be present
        self.assertAlmostEqual(dist.get("S", 0), 0.0)
        self.assertAlmostEqual(dist.get("M", 0), 0.0)

    def test_cadence_vi_motion(self):
        """G->C on strong beat should count as a cadence."""
        # Need 4 beats to have a full bar, place V->I at beat 3->4 (beat 4 = beat 1 of bar 2)
        bass_notes = [
            _n(48, 0, TICKS_PER_BEAT, "bass"),                          # C3, beat 0 (bar 1 beat 1)
            _n(48, TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),             # C3, beat 1
            _n(48, 2 * TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),        # C3, beat 2
            _n(55, 3 * TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),        # G3, beat 3
            _n(48, 4 * TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),        # C3, beat 4 (= bar 2 beat 1, strong)
        ]
        s = _score([_track("bass", bass_notes)])
        cadences = _count_cadences_simplified(s)
        self.assertGreater(cadences, 0)

    def test_parallel_perfects_detection(self):
        """Two voices moving P5->P5 in parallel should be detected."""
        # Soprano: C5->D5 (72->74), Bass: F3->G3 (53->55) -- both P5 apart
        upper = [
            _n(72, 0, TICKS_PER_BEAT, "soprano"),
            _n(74, TICKS_PER_BEAT, TICKS_PER_BEAT, "soprano"),
        ]
        lower = [
            _n(65, 0, TICKS_PER_BEAT, "bass"),    # 72-65=7 semitones = P5
            _n(67, TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),  # 74-67=7 semitones = P5
        ]
        s = _score([_track("soprano", upper), _track("bass", lower)])
        count = _count_parallel_perfects(s)
        self.assertGreater(count, 0)

    def test_no_parallel_perfects_contrary(self):
        """Contrary motion should produce zero parallel perfects."""
        # Soprano goes up, bass goes down: P5 -> m3 (different intervals)
        upper = [
            _n(72, 0, TICKS_PER_BEAT, "soprano"),
            _n(76, TICKS_PER_BEAT, TICKS_PER_BEAT, "soprano"),   # up to E5
        ]
        lower = [
            _n(65, 0, TICKS_PER_BEAT, "bass"),                    # F4 (P5 from C5)
            _n(64, TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),      # E4 down (contrary)
        ]
        s = _score([_track("soprano", upper), _track("bass", lower)])
        count = _count_parallel_perfects(s)
        self.assertEqual(count, 0.0)

    def test_nct_distribution_two_voices(self):
        """A two-voice score should produce a non-empty NCT distribution."""
        # Bass on C, upper voice plays C-D-E (D is passing tone over C triad)
        bass = [_n(48, 0, 3 * TICKS_PER_BEAT, "bass")]  # C3, sustained 3 beats
        soprano = [
            _n(72, 0, TICKS_PER_BEAT, "soprano"),               # C5, chord tone
            _n(74, TICKS_PER_BEAT, TICKS_PER_BEAT, "soprano"),  # D5, NCT
            _n(76, 2 * TICKS_PER_BEAT, TICKS_PER_BEAT, "soprano"),  # E5, chord tone
        ]
        s = _score([_track("soprano", soprano), _track("bass", bass)])
        dist = _extract_nct_distribution(s)
        # Should have some chord tones and some NCTs
        self.assertGreater(dist.get("chord_tone", 0), 0)
        total = sum(dist.values())
        self.assertAlmostEqual(total, 1.0, places=3)

    def test_degree_distribution(self):
        """Bass notes on C and G should produce I and V degrees."""
        bass_notes = [
            _n(48, 0, TICKS_PER_BEAT, "bass"),              # C3 -> I
            _n(55, TICKS_PER_BEAT, TICKS_PER_BEAT, "bass"),  # G3 -> V
        ]
        s = _score([_track("bass", bass_notes)])
        dist = _extract_degree_distribution(s)
        self.assertGreater(dist.get("I", 0), 0)
        self.assertGreater(dist.get("V", 0), 0)
        self.assertAlmostEqual(sum(dist.values()), 1.0, places=3)


if __name__ == "__main__":
    unittest.main()
