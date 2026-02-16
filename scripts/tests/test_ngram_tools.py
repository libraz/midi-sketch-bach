"""Tests for n-gram extraction utilities (ScaleDegree, pitch conversion, helpers).

These functions now live in scripts.bach_analyzer.music_theory, imported directly.
"""

import json
import sys
import unittest
from pathlib import Path

# Add project root to path.
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from scripts.bach_analyzer.music_theory import (
    MAJOR_SCALE_SEMITONES,
    MINOR_SCALE_SEMITONES,
    PC_DEGREE_MAJOR as _PC_DEGREE_MAJOR,
    PC_DEGREE_MINOR as _PC_DEGREE_MINOR,
    ScaleDegree,
    TONIC_TO_PC,
    beat_strength as _beat_strength,
    build_pc_to_degree_map as _build_pc_to_degree_map,
    classify_beat_position as _classify_beat_position,
    degree_interval,
    is_chord_tone_simple as _is_chord_tone_simple,
    pitch_to_scale_degree,
    quantize_duration as _quantize_duration,
)

from scripts.bach_analyzer.model import TICKS_PER_BEAT, TICKS_PER_BAR


class TestTonicToPC(unittest.TestCase):
    def test_basic_notes(self):
        self.assertEqual(TONIC_TO_PC["C"], 0)
        self.assertEqual(TONIC_TO_PC["G"], 7)
        self.assertEqual(TONIC_TO_PC["D"], 2)

    def test_enharmonics(self):
        self.assertEqual(TONIC_TO_PC["Eb"], TONIC_TO_PC["D#"])
        self.assertEqual(TONIC_TO_PC["F#"], TONIC_TO_PC["Gb"])
        self.assertEqual(TONIC_TO_PC["Ab"], TONIC_TO_PC["G#"])


class TestPCToDegreeMaps(unittest.TestCase):
    """Verify the precomputed pitch-class-to-degree maps."""

    def test_major_scale_diatonic(self):
        """All diatonic tones in major have accidental=0."""
        scale = [0, 2, 4, 5, 7, 9, 11]
        for d, pc in enumerate(scale):
            deg, acc = _PC_DEGREE_MAJOR[pc]
            self.assertEqual(deg, d, f"pc={pc}: expected degree {d}, got {deg}")
            self.assertEqual(acc, 0, f"pc={pc}: expected acc 0, got {acc}")

    def test_minor_scale_diatonic(self):
        """All diatonic tones in natural minor have accidental=0."""
        scale = [0, 2, 3, 5, 7, 8, 10]
        for d, pc in enumerate(scale):
            deg, acc = _PC_DEGREE_MINOR[pc]
            self.assertEqual(deg, d, f"pc={pc}: expected degree {d}, got {deg}")
            self.assertEqual(acc, 0, f"pc={pc}: expected acc 0, got {acc}")

    def test_major_chromatic_sharps(self):
        """Chromatic tones in major are represented as sharps of lower degree."""
        # C# = degree 0, acc +1 (not degree 1, acc -1)
        deg, acc = _PC_DEGREE_MAJOR[1]
        self.assertEqual(deg, 0)
        self.assertEqual(acc, 1)
        # F# = degree 3, acc +1
        deg, acc = _PC_DEGREE_MAJOR[6]
        self.assertEqual(deg, 3)
        self.assertEqual(acc, 1)

    def test_minor_leading_tone(self):
        """F# in G minor = raised 7th degree (leading tone)."""
        # In natural minor [0,2,3,5,7,8,10], pc 11 = degree 6 + 1 sharp.
        deg, acc = _PC_DEGREE_MINOR[11]
        self.assertEqual(deg, 6, "Leading tone should be degree 6")
        self.assertEqual(acc, 1, "Leading tone should have +1 accidental")


class TestPitchToScaleDegree(unittest.TestCase):
    """Test pitch_to_scale_degree with the plan's documented examples."""

    def test_c_major_f_sharp(self):
        """key=C major, pitch=F#4 (66) -> ScaleDegree(3, +1, ...)."""
        sd = pitch_to_scale_degree(66, 0, is_minor=False)
        self.assertEqual(sd.degree, 3)
        self.assertEqual(sd.accidental, 1)

    def test_g_minor_f_sharp(self):
        """key=G minor, pitch=F#4 (66) -> ScaleDegree(6, +1, ...) -- leading tone."""
        sd = pitch_to_scale_degree(66, 7, is_minor=True)
        self.assertEqual(sd.degree, 6)
        self.assertEqual(sd.accidental, 1)

    def test_c_major_c4(self):
        """C4 (60) in C major -> degree 0, acc 0."""
        sd = pitch_to_scale_degree(60, 0, is_minor=False)
        self.assertEqual(sd.degree, 0)
        self.assertEqual(sd.accidental, 0)

    def test_c_major_b4(self):
        """B4 (71) in C major -> degree 6, acc 0."""
        sd = pitch_to_scale_degree(71, 0, is_minor=False)
        self.assertEqual(sd.degree, 6)
        self.assertEqual(sd.accidental, 0)

    def test_low_pitch_negative_octave(self):
        """Pitches below tonic octave should have correct negative octaves."""
        # C2 (36) in G major (tonic=7), rel = 36-7 = 29, oct = 29//12 = 2
        sd = pitch_to_scale_degree(36, 7, is_minor=False)
        self.assertEqual(sd.octave, 2)


class TestDegreeInterval(unittest.TestCase):
    """Test degree_interval with the plan's confirmed examples."""

    def _di(self, pitch_a, pitch_b, tonic=0, is_minor=False):
        sd_a = pitch_to_scale_degree(pitch_a, tonic, is_minor)
        sd_b = pitch_to_scale_degree(pitch_b, tonic, is_minor)
        return degree_interval(sd_a, sd_b)

    def test_b4_to_c5(self):
        """B4->C5: degree_diff = +1 (minor 2nd ascending)."""
        dd, _ = self._di(71, 72)
        self.assertEqual(dd, 1)

    def test_c5_to_b4(self):
        """C5->B4: degree_diff = -1."""
        dd, _ = self._di(72, 71)
        self.assertEqual(dd, -1)

    def test_c4_to_d5(self):
        """C4->D5: degree_diff = +8 (9th = octave + 2nd)."""
        dd, _ = self._di(60, 74)
        self.assertEqual(dd, 8)

    def test_c4_to_c5(self):
        """C4->C5: degree_diff = +7 (octave = 7 diatonic steps)."""
        dd, _ = self._di(60, 72)
        self.assertEqual(dd, 7)

    def test_b4_to_c4(self):
        """B4->C4: degree_diff = -6 (descending 7th)."""
        dd, _ = self._di(71, 60)
        self.assertEqual(dd, -6)

    def test_c_to_f_sharp(self):
        """C4->F#4 in C major: degree_diff=3, chroma_diff=+1 (augmented 4th)."""
        dd, cd = self._di(60, 66)
        self.assertEqual(dd, 3)
        self.assertEqual(cd, 1)

    def test_c_to_f_natural(self):
        """C4->F4 in C major: degree_diff=3, chroma_diff=0 (perfect 4th)."""
        dd, cd = self._di(60, 65)
        self.assertEqual(dd, 3)
        self.assertEqual(cd, 0)

    def test_unison(self):
        """Same pitch -> (0, 0)."""
        dd, cd = self._di(60, 60)
        self.assertEqual(dd, 0)
        self.assertEqual(cd, 0)


class TestBeatClassification(unittest.TestCase):
    """Test beat position and strength classification."""

    def test_strong_beat(self):
        self.assertEqual(_classify_beat_position(0), "strong")
        self.assertEqual(_classify_beat_position(TICKS_PER_BAR), "strong")

    def test_mid_beat(self):
        self.assertEqual(_classify_beat_position(2 * TICKS_PER_BEAT), "mid")

    def test_weak_beats(self):
        self.assertEqual(_classify_beat_position(TICKS_PER_BEAT), "weak")
        self.assertEqual(_classify_beat_position(3 * TICKS_PER_BEAT), "weak")

    def test_beat_strength_ordering(self):
        s = _beat_strength
        # Downbeat > beat 3 > beat 2,4 > 8th offbeat > 16th offbeat
        self.assertGreater(s(0), s(2 * TICKS_PER_BEAT))
        self.assertGreater(s(2 * TICKS_PER_BEAT), s(TICKS_PER_BEAT))
        self.assertGreater(s(TICKS_PER_BEAT), s(TICKS_PER_BEAT // 2))


class TestQuantizeDuration(unittest.TestCase):
    def test_sixteenth(self):
        self.assertEqual(_quantize_duration(0.25, "sixteenth"), 1)
        self.assertEqual(_quantize_duration(0.5, "sixteenth"), 2)
        self.assertEqual(_quantize_duration(1.0, "sixteenth"), 4)

    def test_minimum_one(self):
        self.assertEqual(_quantize_duration(0.01, "sixteenth"), 1)


class TestChordToneSimple(unittest.TestCase):
    def test_root(self):
        self.assertTrue(_is_chord_tone_simple(60, 60))  # unison

    def test_major_third(self):
        self.assertTrue(_is_chord_tone_simple(64, 60))  # M3

    def test_minor_third(self):
        self.assertTrue(_is_chord_tone_simple(63, 60))  # m3

    def test_fifth(self):
        self.assertTrue(_is_chord_tone_simple(67, 60))  # P5

    def test_second_not_chord_tone(self):
        self.assertFalse(_is_chord_tone_simple(62, 60))  # M2

    def test_tritone_not_chord_tone(self):
        self.assertFalse(_is_chord_tone_simple(66, 60))  # TT


class TestKeySignaturesFile(unittest.TestCase):
    """Verify the generated key_signatures.json is well-formed."""

    @classmethod
    def setUpClass(cls):
        ks_path = Path(__file__).parent.parent.parent / "data" / "reference" / "key_signatures.json"
        if ks_path.is_file():
            with open(ks_path) as f:
                cls.data = json.load(f)
        else:
            cls.data = None

    def test_file_exists(self):
        self.assertIsNotNone(self.data, "key_signatures.json not found")

    def test_entry_count(self):
        if not self.data:
            self.skipTest("No key signatures data")
        self.assertEqual(len(self.data), 292)

    def test_entry_schema(self):
        if not self.data:
            self.skipTest("No key signatures data")
        for wid, entry in self.data.items():
            self.assertIn("tonic", entry, f"{wid} missing tonic")
            self.assertIn("mode", entry, f"{wid} missing mode")
            self.assertIn("confidence", entry, f"{wid} missing confidence")
            self.assertIn(entry["tonic"], TONIC_TO_PC, f"{wid} invalid tonic: {entry['tonic']}")
            self.assertIn(entry["mode"], ("major", "minor"), f"{wid} invalid mode")
            self.assertIn(entry["confidence"], ("verified", "inferred"), f"{wid} invalid confidence")

    def test_known_keys(self):
        """Spot-check well-known work keys."""
        if not self.data:
            self.skipTest("No key signatures data")
        checks = {
            "BWV578_fugue": ("G", "minor"),
            "BWV846_prelude": ("C", "major"),
            "BWV847_fugue": ("C", "minor"),
            "BWV988_00": ("G", "major"),
            "BWV988_15": ("G", "minor"),
            "BWV1007_1": ("G", "major"),
            "BWV1004_5": ("D", "minor"),
            "BWV565": ("D", "minor"),
        }
        for wid, (exp_tonic, exp_mode) in checks.items():
            entry = self.data[wid]
            self.assertEqual(entry["tonic"], exp_tonic, f"{wid} tonic mismatch")
            self.assertEqual(entry["mode"], exp_mode, f"{wid} mode mismatch")


if __name__ == "__main__":
    unittest.main()
