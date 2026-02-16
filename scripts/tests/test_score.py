"""Tests for Bach reference scoring module."""

import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.model import (
    Note, Provenance, NoteSource, Score, Track, TICKS_PER_BEAT, TICKS_PER_BAR,
)
from scripts.bach_analyzer.reference import available_categories, load_category
from scripts.bach_analyzer.rules.base import Category, RuleResult, Severity, Violation
from scripts.bach_analyzer.score import (
    BachScore,
    DimensionScore,
    SubScore,
    compute_score,
    compute_zscore,
    extract_interval_profile,
    extract_motion_profile,
    extract_rhythm_profile,
    extract_texture_profile,
    extract_vertical_profile,
    extract_voice_entries,
    jsd,
    jsd_to_points,
    zscore_to_points,
    _compute_penalties,
    _detect_cantus_firmus,
    _detect_ground_bass_regularity,
    _normalize,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _n(pitch, tick, dur=240, voice="soprano", source=NoteSource.UNKNOWN, entry=0):
    """Create a Note for testing."""
    prov = Provenance(source=source, entry_number=entry)
    return Note(
        pitch=pitch, velocity=80, start_tick=tick, duration=dur,
        voice=voice, provenance=prov,
    )


def _track(name, notes):
    return Track(name=name, notes=notes)


def _make_fugue_score():
    """Create a small 3-voice fugue-like score for integration testing."""
    # Soprano: subject entry at tick 0 (C5=72)
    soprano_notes = [
        _n(72, 0, 480, "soprano", NoteSource.FUGUE_SUBJECT, entry=0),
        _n(74, 480, 480, "soprano", NoteSource.FUGUE_SUBJECT),
        _n(76, 960, 480, "soprano", NoteSource.FUGUE_SUBJECT),
        _n(77, 1440, 480, "soprano", NoteSource.FUGUE_SUBJECT),
        _n(79, 1920, 240, "soprano", NoteSource.FREE_COUNTERPOINT),
        _n(77, 2160, 240, "soprano", NoteSource.FREE_COUNTERPOINT),
        _n(76, 2400, 240, "soprano", NoteSource.FREE_COUNTERPOINT),
        _n(74, 2640, 240, "soprano", NoteSource.FREE_COUNTERPOINT),
        _n(72, 2880, 480, "soprano", NoteSource.FREE_COUNTERPOINT),
        _n(74, 3360, 480, "soprano", NoteSource.FREE_COUNTERPOINT),
    ]

    # Alto: answer entry at tick 1920 (G4=67, a 5th below)
    alto_notes = [
        _n(67, 1920, 480, "alto", NoteSource.FUGUE_ANSWER, entry=1),
        _n(69, 2400, 480, "alto", NoteSource.FUGUE_ANSWER),
        _n(71, 2880, 480, "alto", NoteSource.FUGUE_ANSWER),
        _n(72, 3360, 480, "alto", NoteSource.FUGUE_ANSWER),
    ]

    # Tenor: subject entry at tick 3840 (C4=60)
    tenor_notes = [
        _n(60, 3840, 480, "tenor", NoteSource.FUGUE_SUBJECT, entry=2),
        _n(62, 4320, 480, "tenor", NoteSource.FUGUE_SUBJECT),
        _n(64, 4800, 480, "tenor", NoteSource.FUGUE_SUBJECT),
        _n(65, 5280, 480, "tenor", NoteSource.FUGUE_SUBJECT),
    ]

    return Score(
        tracks=[
            _track("soprano", soprano_notes),
            _track("alto", alto_notes),
            _track("tenor", tenor_notes),
        ],
        seed=42,
        form="fugue",
        key="C_major",
        voices=3,
    )


# ---------------------------------------------------------------------------
# Math foundation tests
# ---------------------------------------------------------------------------

class TestNormalize(unittest.TestCase):
    def test_normalize_simple(self):
        d = {"a": 2.0, "b": 3.0, "c": 5.0}
        result = _normalize(d)
        self.assertAlmostEqual(result["a"], 0.2)
        self.assertAlmostEqual(result["b"], 0.3)
        self.assertAlmostEqual(result["c"], 0.5)

    def test_normalize_empty(self):
        self.assertEqual(_normalize({}), {})

    def test_normalize_already_normalized(self):
        d = {"x": 0.5, "y": 0.5}
        result = _normalize(d)
        self.assertAlmostEqual(sum(result.values()), 1.0)


class TestJSD(unittest.TestCase):
    def test_identical_distributions(self):
        d = {"a": 0.5, "b": 0.3, "c": 0.2}
        self.assertAlmostEqual(jsd(d, d), 0.0, places=5)

    def test_completely_different(self):
        p = {"a": 1.0}
        q = {"b": 1.0}
        val = jsd(p, q)
        self.assertGreater(val, 0.5)
        self.assertLessEqual(val, 1.0)

    def test_similar_distributions(self):
        p = {"a": 0.5, "b": 0.3, "c": 0.2}
        q = {"a": 0.45, "b": 0.35, "c": 0.2}
        val = jsd(p, q)
        self.assertGreater(val, 0.0)
        self.assertLess(val, 0.1)

    def test_symmetry(self):
        p = {"a": 0.7, "b": 0.3}
        q = {"a": 0.4, "b": 0.6}
        self.assertAlmostEqual(jsd(p, q), jsd(q, p), places=10)

    def test_empty(self):
        self.assertEqual(jsd({}, {}), 0.0)


class TestJSDToPoints(unittest.TestCase):
    def test_zero_jsd(self):
        self.assertAlmostEqual(jsd_to_points(0.0), 100.0)

    def test_small_jsd(self):
        pts = jsd_to_points(0.05)
        self.assertGreater(pts, 50.0)

    def test_large_jsd(self):
        pts = jsd_to_points(0.5)
        self.assertLess(pts, 10.0)

    def test_clamped(self):
        self.assertGreaterEqual(jsd_to_points(10.0), 0.0)
        self.assertLessEqual(jsd_to_points(-1.0), 100.0)


class TestComputeZscore(unittest.TestCase):
    def test_at_mean(self):
        self.assertAlmostEqual(compute_zscore(5.0, 5.0, 1.0), 0.0)

    def test_one_std_above(self):
        self.assertAlmostEqual(compute_zscore(6.0, 5.0, 1.0), 1.0)

    def test_two_std_below(self):
        self.assertAlmostEqual(compute_zscore(3.0, 5.0, 1.0), -2.0)

    def test_zero_std(self):
        self.assertAlmostEqual(compute_zscore(5.0, 5.0, 0.0), 0.0)


class TestZscoreToPoints(unittest.TestCase):
    def test_zero_zscore(self):
        self.assertAlmostEqual(zscore_to_points(0.0), 100.0)

    def test_one_std(self):
        pts = zscore_to_points(1.0)
        self.assertGreater(pts, 50.0)
        self.assertLess(pts, 100.0)
        # Should be ~60.65
        self.assertAlmostEqual(pts, 100 * math.exp(-0.5), places=1)

    def test_two_std(self):
        pts = zscore_to_points(2.0)
        self.assertGreater(pts, 10.0)
        self.assertLess(pts, 60.0)

    def test_negative_same_as_positive(self):
        self.assertAlmostEqual(zscore_to_points(1.5), zscore_to_points(-1.5))


# ---------------------------------------------------------------------------
# Reference data tests
# ---------------------------------------------------------------------------

class TestReferenceData(unittest.TestCase):
    def test_available_categories(self):
        cats = available_categories()
        self.assertIn("organ_fugue", cats)
        self.assertIn("wtc1", cats)
        self.assertIn("solo_cello_suite", cats)
        self.assertIn("trio_sonata", cats)
        self.assertEqual(len(cats), 7)

    def test_load_category(self):
        ref = load_category("organ_fugue")
        self.assertEqual(ref["category"], "organ_fugue")
        self.assertEqual(ref["work_count"], 8)
        self.assertIn("scalars", ref)
        self.assertIn("distributions", ref)

    def test_distribution_sums(self):
        for cat in available_categories():
            ref = load_category(cat)
            for dist_name, dist in ref["distributions"].items():
                total = sum(dist.values())
                self.assertAlmostEqual(total, 1.0, places=1,
                    msg=f"{cat}/{dist_name} sum={total}")

    def test_missing_category(self):
        with self.assertRaises(FileNotFoundError):
            load_category("nonexistent_category")


# ---------------------------------------------------------------------------
# Profile extraction tests
# ---------------------------------------------------------------------------

class TestExtractIntervalProfile(unittest.TestCase):
    def test_simple_scale(self):
        # C major ascending scale: C4, D4, E4, F4, G4
        notes = [_n(60, 0), _n(62, 240), _n(64, 480), _n(65, 720), _n(67, 960)]
        score = Score(tracks=[_track("v1", notes)])
        ip = extract_interval_profile(score)

        self.assertEqual(ip["total"], 4)
        self.assertGreater(ip["stepwise_ratio"], 0.5)  # Most are steps
        self.assertGreater(ip["avg_interval"], 1.0)

    def test_empty_score(self):
        score = Score(tracks=[_track("v1", [])])
        ip = extract_interval_profile(score)
        self.assertEqual(ip["total"], 0)
        self.assertEqual(ip["stepwise_ratio"], 0.0)

    def test_distribution_sums_to_one(self):
        notes = [_n(60, 0), _n(67, 240), _n(72, 480), _n(60, 720)]
        score = Score(tracks=[_track("v1", notes)])
        ip = extract_interval_profile(score)
        total = sum(ip["distribution"].values())
        self.assertAlmostEqual(total, 1.0, places=5)


class TestExtractRhythmProfile(unittest.TestCase):
    def test_uniform_sixteenths(self):
        # All 16th notes (240 ticks = 0.5 beats)
        notes = [_n(60, i * 240, 240) for i in range(8)]
        score = Score(tracks=[_track("v1", notes)])
        rp = extract_rhythm_profile(score)
        # 240 ticks = 0.5 beats -> 8th note bin
        self.assertGreater(rp["distribution"].get("8th", 0), 0)

    def test_mixed_durations(self):
        notes = [
            _n(60, 0, 60),     # 32nd
            _n(62, 60, 120),   # 16th
            _n(64, 180, 240),  # 8th
            _n(65, 420, 480),  # quarter
            _n(67, 900, 960),  # half
        ]
        score = Score(tracks=[_track("v1", notes)])
        rp = extract_rhythm_profile(score)
        dist = rp["distribution"]
        total = sum(dist.values())
        self.assertAlmostEqual(total, 1.0, places=5)


class TestExtractVerticalProfile(unittest.TestCase):
    def test_single_voice_not_applicable(self):
        notes = [_n(60, 0), _n(62, 480)]
        score = Score(tracks=[_track("v1", notes)])
        vp = extract_vertical_profile(score)
        self.assertFalse(vp["applicable"])

    def test_two_voices_consonant(self):
        # Perfect 5th (C4=60, G4=67) throughout
        upper = [_n(67, 0, 1920, "soprano")]
        lower = [_n(60, 0, 1920, "bass")]
        score = Score(tracks=[_track("soprano", upper), _track("bass", lower)])
        vp = extract_vertical_profile(score)
        self.assertTrue(vp["applicable"])
        self.assertGreater(vp["consonance_ratio"], 0.5)


class TestExtractMotionProfile(unittest.TestCase):
    def test_single_voice_not_applicable(self):
        notes = [_n(60, 0), _n(62, 480)]
        score = Score(tracks=[_track("v1", notes)])
        mp = extract_motion_profile(score)
        self.assertFalse(mp["applicable"])

    def test_contrary_motion(self):
        # Upper goes up, lower goes down
        upper = [_n(72, 0, 480, "soprano"), _n(76, 480, 480, "soprano")]
        lower = [_n(60, 0, 480, "bass"), _n(55, 480, 480, "bass")]
        score = Score(tracks=[_track("soprano", upper), _track("bass", lower)])
        mp = extract_motion_profile(score)
        # Should detect contrary motion
        self.assertTrue(mp["applicable"])


class TestExtractTextureProfile(unittest.TestCase):
    def test_single_voice(self):
        notes = [_n(60, 0, 960)]
        score = Score(tracks=[_track("v1", notes)])
        tp = extract_texture_profile(score)
        self.assertIn("1", tp["distribution"])
        self.assertAlmostEqual(tp["avg_active_voices"], 1.0, places=0)

    def test_two_voices(self):
        upper = [_n(72, 0, 1920, "soprano")]
        lower = [_n(60, 0, 1920, "bass")]
        score = Score(tracks=[_track("soprano", upper), _track("bass", lower)])
        tp = extract_texture_profile(score)
        self.assertGreater(tp["avg_active_voices"], 1.5)


class TestExtractVoiceEntries(unittest.TestCase):
    def test_fugue_entries(self):
        score = _make_fugue_score()
        ve = extract_voice_entries(score)
        self.assertTrue(ve["has_entries"])
        self.assertEqual(len(ve["entries"]), 3)
        # First entry should be soprano at tick 0
        self.assertEqual(ve["entries"][0]["voice"], "soprano")
        self.assertEqual(ve["entries"][0]["tick"], 0)
        # Second entry should be alto at tick 1920
        self.assertEqual(ve["entries"][1]["voice"], "alto")
        # Entry intervals should include 5th/4th
        self.assertGreater(ve["fifth_fourth_ratio"], 0)

    def test_no_provenance(self):
        notes = [_n(60, 0), _n(62, 480)]
        score = Score(tracks=[_track("v1", notes)])
        ve = extract_voice_entries(score)
        self.assertFalse(ve["has_entries"])


# ---------------------------------------------------------------------------
# Penalty computation tests
# ---------------------------------------------------------------------------

class TestComputePenalties(unittest.TestCase):
    def test_no_violations(self):
        results = [
            RuleResult(rule_name="parallel_perfect", category=Category.COUNTERPOINT, passed=True),
        ]
        penalties = _compute_penalties(results)
        self.assertEqual(penalties.get("counterpoint", 0), 0.0)

    def test_critical_violation(self):
        results = [
            RuleResult(
                rule_name="parallel_perfect",
                category=Category.COUNTERPOINT,
                passed=False,
                violations=[
                    Violation(rule_name="parallel_perfect", category=Category.COUNTERPOINT,
                              severity=Severity.CRITICAL),
                ],
            ),
        ]
        penalties = _compute_penalties(results)
        self.assertEqual(penalties["counterpoint"], 5.0)

    def test_penalty_capped_at_20(self):
        violations = [
            Violation(rule_name="parallel_perfect", category=Category.COUNTERPOINT,
                      severity=Severity.CRITICAL)
            for _ in range(10)  # 10 * 5 = 50, should cap at 20
        ]
        results = [
            RuleResult(
                rule_name="parallel_perfect",
                category=Category.COUNTERPOINT,
                passed=False,
                violations=violations,
            ),
        ]
        penalties = _compute_penalties(results)
        self.assertEqual(penalties["counterpoint"], 20.0)


# ---------------------------------------------------------------------------
# Dimension scorer tests
# ---------------------------------------------------------------------------

class TestComputeScore(unittest.TestCase):
    def test_basic_fugue_score(self):
        """A small fugue should produce a valid BachScore."""
        score = _make_fugue_score()
        bach_score = compute_score(score, "organ_fugue")
        self.assertIsInstance(bach_score, BachScore)
        self.assertGreaterEqual(bach_score.total, 0)
        self.assertLessEqual(bach_score.total, 100)
        self.assertIn(bach_score.grade, ("A", "B", "C", "D", "F"))
        self.assertEqual(bach_score.category, "organ_fugue")

    def test_all_dimensions_present(self):
        score = _make_fugue_score()
        bach_score = compute_score(score, "organ_fugue")
        expected_dims = {"structure", "melody", "harmony", "counterpoint", "rhythm", "texture"}
        self.assertEqual(set(bach_score.dimensions.keys()), expected_dims)

    def test_melody_always_applicable(self):
        score = _make_fugue_score()
        bach_score = compute_score(score, "organ_fugue")
        self.assertTrue(bach_score.dimensions["melody"].applicable)

    def test_counterpoint_disabled(self):
        score = _make_fugue_score()
        bach_score = compute_score(score, "organ_fugue", counterpoint_enabled=False)
        self.assertFalse(bach_score.dimensions["counterpoint"].applicable)

    def test_with_validation_results(self):
        score = _make_fugue_score()
        violations = [
            Violation(rule_name="parallel_perfect", category=Category.COUNTERPOINT,
                      severity=Severity.ERROR),
            Violation(rule_name="parallel_perfect", category=Category.COUNTERPOINT,
                      severity=Severity.ERROR),
        ]
        results = [
            RuleResult(
                rule_name="parallel_perfect",
                category=Category.COUNTERPOINT,
                passed=False,
                violations=violations,
            ),
        ]
        bach_score = compute_score(score, "organ_fugue", results=results)
        # Counterpoint dimension should have a penalty
        self.assertGreater(bach_score.dimensions["counterpoint"].penalty, 0)

    def test_grade_mapping(self):
        score = _make_fugue_score()
        bach_score = compute_score(score, "organ_fugue")
        grade = bach_score.grade
        total = bach_score.total
        if total >= 90:
            self.assertEqual(grade, "A")
        elif total >= 75:
            self.assertEqual(grade, "B")
        elif total >= 60:
            self.assertEqual(grade, "C")
        elif total >= 40:
            self.assertEqual(grade, "D")
        else:
            self.assertEqual(grade, "F")


class TestSingleVoiceScore(unittest.TestCase):
    """Test scoring with single-voice (solo instrument) pieces."""

    def test_solo_cello_score(self):
        # Single voice, solo instrument
        notes = [_n(48 + (i % 12), i * 120, 120, "cello") for i in range(40)]
        score = Score(
            tracks=[_track("cello", notes)],
            form="cello_prelude",
        )
        bach_score = compute_score(score, "solo_cello_suite", counterpoint_enabled=False)
        self.assertIsInstance(bach_score, BachScore)
        # Harmony and counterpoint should be non-applicable
        self.assertFalse(bach_score.dimensions["harmony"].applicable)
        self.assertFalse(bach_score.dimensions["counterpoint"].applicable)
        # Melody should still be applicable
        self.assertTrue(bach_score.dimensions["melody"].applicable)


# ---------------------------------------------------------------------------
# Integration test with fixture
# ---------------------------------------------------------------------------

class TestFixtureIntegration(unittest.TestCase):
    """Test with the sample_output.json fixture."""

    @classmethod
    def setUpClass(cls):
        fixture_path = Path(__file__).parent / "fixtures" / "sample_output.json"
        if not fixture_path.exists():
            raise unittest.SkipTest("sample_output.json fixture not found")
        from scripts.bach_analyzer.runner import load_score
        cls.score = load_score(str(fixture_path))

    def test_score_from_fixture(self):
        bach_score = compute_score(self.score, "organ_fugue")
        self.assertIsInstance(bach_score, BachScore)
        self.assertGreaterEqual(bach_score.total, 0)
        self.assertLessEqual(bach_score.total, 100)
        self.assertIn(bach_score.grade, ("A", "B", "C", "D", "F"))

    def test_all_profiles_extractable(self):
        """All profile extraction functions should work on real data."""
        ip = extract_interval_profile(self.score)
        self.assertGreater(ip["total"], 0)

        rp = extract_rhythm_profile(self.score)
        self.assertGreater(sum(rp["distribution"].values()), 0)

        vp = extract_vertical_profile(self.score)
        self.assertTrue(vp["applicable"])

        mp = extract_motion_profile(self.score)
        self.assertTrue(mp["applicable"])

        tp = extract_texture_profile(self.score)
        self.assertGreater(tp["avg_active_voices"], 0)


# ---------------------------------------------------------------------------
# Ground bass regularity tests
# ---------------------------------------------------------------------------

class TestDetectGroundBassRegularity(unittest.TestCase):
    """Tests for _detect_ground_bass_regularity."""

    def _make_passacaglia_score(self, period_bars=4, num_periods=4):
        """Create a score with a repeating bass pattern.

        Bass plays C on beat 1 of each period-start bar, with a fixed
        pitch-class pattern within each period.
        """
        pattern_pcs = [0, 7, 5, 0]  # C, G, F, C as pitch classes
        # Extend/truncate to match period length
        bar_pattern = (pattern_pcs * ((period_bars // len(pattern_pcs)) + 1))[:period_bars]

        bass_notes = []
        for period_idx in range(num_periods):
            for bar_idx in range(period_bars):
                global_bar = period_idx * period_bars + bar_idx
                tick = global_bar * TICKS_PER_BAR
                # Bass note on beat 1 with pitch based on pattern
                pitch = 36 + bar_pattern[bar_idx]  # C2 range
                bass_notes.append(
                    _n(pitch, tick, TICKS_PER_BAR, "bass", NoteSource.GROUND_BASS)
                )

        # Add an upper voice so we have multi-voice texture
        upper_notes = []
        total_bars = period_bars * num_periods
        for bar_idx in range(total_bars):
            tick = bar_idx * TICKS_PER_BAR
            pitch = 72 + (bar_idx % 7)  # Some melody in C5 range
            upper_notes.append(
                _n(pitch, tick, TICKS_PER_BEAT, "soprano", NoteSource.FREE_COUNTERPOINT)
            )

        return Score(
            tracks=[_track("soprano", upper_notes), _track("bass", bass_notes)],
            form="passacaglia",
        )

    def test_regular_ground_bass(self):
        """A perfectly regular ground bass should score high."""
        score = self._make_passacaglia_score(period_bars=4, num_periods=6)
        result = _detect_ground_bass_regularity(score)
        self.assertGreater(result, 40.0)

    def test_no_bass_pattern(self):
        """Random pitches with no periodicity should score low."""
        import random
        rng = random.Random(42)
        bass_notes = []
        for bar_idx in range(24):
            tick = bar_idx * TICKS_PER_BAR
            pitch = 36 + rng.randint(0, 11)
            bass_notes.append(_n(pitch, tick, TICKS_PER_BAR, "bass"))
        upper_notes = [_n(72, bar_idx * TICKS_PER_BAR, TICKS_PER_BEAT, "soprano")
                       for bar_idx in range(24)]
        score = Score(
            tracks=[_track("soprano", upper_notes), _track("bass", bass_notes)],
            form="passacaglia",
        )
        result = _detect_ground_bass_regularity(score)
        # Random data should have lower regularity than structured pattern
        regular_score = self._make_passacaglia_score(period_bars=4, num_periods=6)
        regular_result = _detect_ground_bass_regularity(regular_score)
        self.assertLess(result, regular_result)

    def test_too_short(self):
        """Very short scores should return 0."""
        bass_notes = [_n(36, 0, TICKS_PER_BAR, "bass")]
        score = Score(tracks=[_track("bass", bass_notes)], form="passacaglia")
        result = _detect_ground_bass_regularity(score)
        self.assertEqual(result, 0.0)

    def test_transposed_repetition(self):
        """Bass pattern transposed by a fixed interval should still be detected."""
        # Period of 4 bars: C-E-G-C, then transposed up by P5: G-B-D-G
        bass_notes = []
        base_pattern = [0, 4, 7, 0]  # C, E, G, C
        for period_idx in range(4):
            transpose = (period_idx * 7) % 12  # Transpose by P5 each time
            for bar_idx in range(4):
                global_bar = period_idx * 4 + bar_idx
                tick = global_bar * TICKS_PER_BAR
                pitch = 36 + (base_pattern[bar_idx] + transpose) % 12
                bass_notes.append(_n(pitch, tick, TICKS_PER_BAR, "bass"))
        upper_notes = [_n(72, b * TICKS_PER_BAR, TICKS_PER_BEAT, "soprano")
                       for b in range(16)]
        score = Score(
            tracks=[_track("soprano", upper_notes), _track("bass", bass_notes)],
            form="passacaglia",
        )
        result = _detect_ground_bass_regularity(score)
        self.assertGreater(result, 30.0)


# ---------------------------------------------------------------------------
# Cantus firmus detection tests
# ---------------------------------------------------------------------------

class TestDetectCantusFirmus(unittest.TestCase):
    """Tests for _detect_cantus_firmus."""

    def test_clear_cantus_firmus(self):
        """Voice with much longer notes on strong beats should score high."""
        # CF voice: whole notes on beat 1
        cf_notes = []
        for bar_idx in range(8):
            tick = bar_idx * TICKS_PER_BAR
            pitch = 60 + (bar_idx % 5)
            cf_notes.append(_n(pitch, tick, TICKS_PER_BAR, "soprano",
                              NoteSource.CANTUS_FIXED))

        # Counter voice: eighth notes
        counter_notes = []
        for bar_idx in range(8):
            for beat in range(8):  # 8 eighth notes per bar
                tick = bar_idx * TICKS_PER_BAR + beat * (TICKS_PER_BEAT // 2)
                pitch = 48 + (beat % 7)
                counter_notes.append(_n(pitch, tick, TICKS_PER_BEAT // 2, "alto",
                                        NoteSource.FREE_COUNTERPOINT))

        score = Score(
            tracks=[_track("soprano", cf_notes), _track("alto", counter_notes)],
            form="chorale_prelude",
        )
        result = _detect_cantus_firmus(score)
        self.assertGreater(result, 60.0)

    def test_no_cantus_firmus(self):
        """Equal-duration voices should score low."""
        voice1_notes = [_n(60, i * TICKS_PER_BEAT, TICKS_PER_BEAT, "soprano")
                        for i in range(16)]
        voice2_notes = [_n(48, i * TICKS_PER_BEAT, TICKS_PER_BEAT, "alto")
                        for i in range(16)]
        score = Score(
            tracks=[_track("soprano", voice1_notes), _track("alto", voice2_notes)],
            form="chorale_prelude",
        )
        result = _detect_cantus_firmus(score)
        # Equal durations -> ratio ~1.0 -> 0 points from duration
        self.assertLess(result, 50.0)

    def test_single_voice(self):
        """Single voice should return 0 (no CF possible)."""
        notes = [_n(60, i * TICKS_PER_BEAT, TICKS_PER_BEAT, "soprano") for i in range(8)]
        score = Score(tracks=[_track("soprano", notes)], form="chorale_prelude")
        result = _detect_cantus_firmus(score)
        self.assertEqual(result, 0.0)

    def test_cf_on_weak_beats(self):
        """CF with long notes but on weak beats should score lower."""
        # CF on beat 2 (weak beat)
        cf_notes = []
        for bar_idx in range(8):
            tick = bar_idx * TICKS_PER_BAR + TICKS_PER_BEAT  # Beat 2
            cf_notes.append(_n(60 + bar_idx % 5, tick, TICKS_PER_BAR - TICKS_PER_BEAT,
                              "soprano"))

        counter_notes = []
        for bar_idx in range(8):
            for beat in range(4):
                tick = bar_idx * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                counter_notes.append(_n(48 + beat, tick, TICKS_PER_BEAT // 2, "alto"))

        score = Score(
            tracks=[_track("soprano", cf_notes), _track("alto", counter_notes)],
            form="chorale_prelude",
        )

        # Compare with CF on strong beats
        cf_strong = []
        for bar_idx in range(8):
            tick = bar_idx * TICKS_PER_BAR  # Beat 1
            cf_strong.append(_n(60 + bar_idx % 5, tick, TICKS_PER_BAR, "soprano"))
        score_strong = Score(
            tracks=[_track("soprano", cf_strong), _track("alto", counter_notes)],
            form="chorale_prelude",
        )

        result_weak = _detect_cantus_firmus(score)
        result_strong = _detect_cantus_firmus(score_strong)
        self.assertLess(result_weak, result_strong)


# ---------------------------------------------------------------------------
# Form-aware structure scoring tests
# ---------------------------------------------------------------------------

class TestFormAwareStructureScoring(unittest.TestCase):
    """Test that _score_structure uses form-specific metrics."""

    def test_passacaglia_includes_ground_bass(self):
        """Passacaglia form should include ground_bass_regularity sub-score."""
        # Build a passacaglia-like score
        bass_notes = []
        pattern = [0, 7, 5, 0]
        for period_idx in range(4):
            for bar_idx in range(4):
                global_bar = period_idx * 4 + bar_idx
                tick = global_bar * TICKS_PER_BAR
                pitch = 36 + pattern[bar_idx]
                bass_notes.append(
                    _n(pitch, tick, TICKS_PER_BAR, "bass", NoteSource.GROUND_BASS)
                )
        upper_notes = [_n(72 + (b % 5), b * TICKS_PER_BAR, TICKS_PER_BEAT, "soprano")
                       for b in range(16)]
        score = Score(
            tracks=[_track("soprano", upper_notes), _track("bass", bass_notes)],
            form="passacaglia",
        )
        bach_score = compute_score(
            score, "organ_fugue", form_name="passacaglia"
        )
        structure = bach_score.dimensions["structure"]
        self.assertTrue(structure.applicable)
        sub_names = [s.name for s in structure.sub_scores]
        self.assertIn("ground_bass_regularity", sub_names)

    def test_chorale_prelude_includes_cantus(self):
        """Chorale prelude form should include cantus_firmus sub-score."""
        cf_notes = [_n(60 + (b % 5), b * TICKS_PER_BAR, TICKS_PER_BAR, "soprano",
                       NoteSource.CANTUS_FIXED) for b in range(8)]
        counter_notes = []
        for b in range(8):
            for beat in range(8):
                tick = b * TICKS_PER_BAR + beat * (TICKS_PER_BEAT // 2)
                counter_notes.append(_n(48 + beat % 7, tick, TICKS_PER_BEAT // 2, "alto"))
        score = Score(
            tracks=[_track("soprano", cf_notes), _track("alto", counter_notes)],
            form="chorale_prelude",
        )
        bach_score = compute_score(
            score, "orgelbuchlein", form_name="chorale_prelude"
        )
        structure = bach_score.dimensions["structure"]
        self.assertTrue(structure.applicable)
        sub_names = [s.name for s in structure.sub_scores]
        self.assertIn("cantus_firmus", sub_names)

    def test_fugue_still_uses_voice_entries(self):
        """Fugue should still use voice_entry_intervals (backward compat)."""
        score = _make_fugue_score()
        bach_score = compute_score(score, "organ_fugue", form_name="fugue")
        structure = bach_score.dimensions["structure"]
        self.assertTrue(structure.applicable)
        sub_names = [s.name for s in structure.sub_scores]
        self.assertIn("voice_entry_intervals", sub_names)
        # Should NOT have ground bass or cantus firmus for fugue
        self.assertNotIn("ground_bass_regularity", sub_names)
        self.assertNotIn("cantus_firmus", sub_names)

    def test_chaconne_includes_ground_bass(self):
        """Chaconne form should include ground_bass_regularity sub-score."""
        bass_notes = []
        pattern = [0, 5, 7, 0]
        for period_idx in range(6):
            for bar_idx in range(4):
                global_bar = period_idx * 4 + bar_idx
                tick = global_bar * TICKS_PER_BAR
                pitch = 55 + pattern[bar_idx]
                bass_notes.append(
                    _n(pitch, tick, TICKS_PER_BAR, "violin", NoteSource.GROUND_BASS)
                )
        score = Score(
            tracks=[_track("violin", bass_notes)],
            form="chaconne",
        )
        bach_score = compute_score(
            score, "solo_violin", counterpoint_enabled=False, form_name="chaconne"
        )
        structure = bach_score.dimensions["structure"]
        self.assertTrue(structure.applicable)
        sub_names = [s.name for s in structure.sub_scores]
        self.assertIn("ground_bass_regularity", sub_names)

    def test_no_form_name_still_works(self):
        """When form_name is empty, should fall back to existing behavior."""
        score = _make_fugue_score()
        bach_score = compute_score(score, "organ_fugue")
        structure = bach_score.dimensions["structure"]
        # Should still work (voice entries from provenance)
        self.assertTrue(structure.applicable)

    def test_unknown_form_texture_only(self):
        """Unknown form with no fugue provenance should use texture only."""
        notes = [_n(60 + (i % 7), i * TICKS_PER_BEAT, TICKS_PER_BEAT, "soprano")
                 for i in range(32)]
        notes2 = [_n(48 + (i % 5), i * TICKS_PER_BEAT, TICKS_PER_BEAT, "bass")
                  for i in range(32)]
        score = Score(
            tracks=[_track("soprano", notes), _track("bass", notes2)],
            form="trio_sonata",
        )
        bach_score = compute_score(
            score, "trio_sonata", form_name="trio_sonata"
        )
        structure = bach_score.dimensions["structure"]
        sub_names = [s.name for s in structure.sub_scores]
        # No voice entries (no fugue provenance), no ground bass, no CF
        self.assertNotIn("voice_entry_intervals", sub_names)
        self.assertNotIn("ground_bass_regularity", sub_names)
        self.assertNotIn("cantus_firmus", sub_names)
        # Should have texture density match
        self.assertIn("texture_density_match", sub_names)


if __name__ == "__main__":
    unittest.main()
