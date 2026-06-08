#!/usr/bin/env python3
"""Byte-stable C++ mirror constants for the Composer closure harness.

Every PHASE* constant and helper in this module mirrors a fixture in
``src/composer/harness_fixture.cpp`` and is a load-bearing drift guard: the
mirror-test suite (``scripts/tests/test_*_mirror.py``) asserts these stay
byte-identical with the C++ source. Do NOT reformat or edit the values; the
structural predictor reports false mismatches if they drift.

This file is GENERATED from the C++ harness fixtures by
``scripts/bachlib/gen_mirror.py``. Regenerate (rather than hand-edit) after any
fixture change::

    python3 scripts/bach_tools.py gen-mirror
"""

from __future__ import annotations


# CelloPrelude arpeggio fixture mirror. Keep byte-identical with kBarPlan / kFigures
# in buildCelloPreludeFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches.
#   kBarPlan[bar] = (root_pc unused here, bass, mid, top); I IV V I IV V I I.
CELLO_PRELUDE_BARPLAN: tuple[tuple[int, int, int], ...] = (
    (48, 55, 64),  # C: C3  G3  E4
    (53, 60, 69),  # F: F3  C4  A4
    (55, 62, 71),  # G: G3  D4  B4
    (48, 64, 67),  # C: C3  E4  G4
    (53, 60, 69),  # F: F3  C4  A4
    (55, 62, 71),  # G: G3  D4  B4
    (48, 64, 67),  # C: C3  E4  G4
    (48, 55, 64),  # C: C3  G3  E4
)
# kFigures[seed % 4]; each slot indexes {0=bass, 1=mid, 2=top}.
CELLO_PRELUDE_FIGURES: tuple[tuple[int, int, int, int], ...] = (
    (1, 0, 1, 2),  # mid-bass-mid-top
    (1, 2, 1, 0),  # mid-top-mid-bass
    (0, 1, 2, 1),  # bass-mid-top-mid
    (2, 1, 0, 1),  # top-mid-bass-mid
)

# Chaconne chaconne fixture mirror. Keep byte-identical with kGroundPitch /
# kVarT0 / the C-minor scale walk in buildChaconneFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (P16-D) asserts these match the C++ source.
#   kGroundPitch[bar in cycle]: descending tetrachord C3 Bb2 Ab2 G2.
CHACONNE_GROUND: tuple[int, ...] = (48, 46, 44, 43)
# kVarT0[bar in cycle]: the lowest variation tone of each chord (i VII VI V
# descent), in a singable ~C4-C5 register; each bar's scalar wave starts here.
CHACONNE_VAR_T0: tuple[int, ...] = (60, 58, 56, 55)
# C natural-minor scale pitch classes (chaconneInScale in the C++ fixture).
CHACONNE_CMIN_SCALE: tuple[int, ...] = (0, 2, 3, 5, 7, 8, 10)
# Per-block notes-per-beat (kBlocks in buildChaconneFixture): Ground=quarter,
# Respond=eighth, Propel/Assert=sixteenth. Used by the variation predictor.
CHACONNE_BLOCK_NOTES_PER_BEAT: tuple[int, ...] = (1, 2, 4, 4)


def chaconne_scale_up(midi: int, steps: int) -> int:
    """Walk `steps` C-natural-minor scale degrees up from `midi`.

    Mirrors chaconneScaleUp in src/composer/harness_fixture.cpp byte-for-byte.
    """
    cur = midi
    for _ in range(steps):
        for add in range(1, 13):
            if (cur + add) % 12 in CHACONNE_CMIN_SCALE:
                cur += add
                break
    return cur

# OrganPrelude organ-prelude fixture mirror. Keep byte-identical with kBarRoot /
# kBarMinor and the C-major scale walk in buildOrganPreludeFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (test_organ_prelude_mirror.py) asserts these match
# the C++ source.
#   kBarRoot[bar]: per-bar chord root pitch class (the diatonic prelude
#   progression I V vi iii IV I ii V repeated to fill 16 bars).
ORGAN_PRELUDE_BAR_ROOT: tuple[int, ...] = (
    0, 7, 9, 4, 5, 0, 2, 7, 0, 7, 9, 4, 5, 2, 7, 0,
)
# kBarMinor[bar]: True where the bar's diatonic degree is minor-quality
# (ii / iii / vi); selects a minor third over the root for that bar's triad.
ORGAN_PRELUDE_BAR_MINOR: tuple[bool, ...] = (
    False, False, True, True, False, False, True, False,
    False, False, True, True, False, True, False, False,
)
# C major scale pitch classes (organPreludeInScale in the C++ fixture).
ORGAN_PRELUDE_CMAJ_SCALE: tuple[int, ...] = (0, 2, 4, 5, 7, 9, 11)


def organPrelude_scale_up(midi: int, steps: int) -> int:
    """Walk `steps` C-major scale degrees up from `midi`.

    Mirrors organPreludeScaleUp in src/composer/harness_fixture.cpp byte-for-byte.
    """
    cur = midi
    for _ in range(steps):
        for add in range(1, 13):
            if (cur + add) % 12 in ORGAN_PRELUDE_CMAJ_SCALE:
                cur += add
                break
    return cur

# OrganToccata organ-toccata fixture mirror. Keep byte-identical with kBarRoot in
# buildOrganToccataFixture (src/composer/harness_fixture.cpp): the per-bar chord
# root pitch classes of the diatonic C-major I IV V vi progression, cycled
# over 16 bars (bar b -> root = ORGAN_TOCCATA_BAR_ROOT[b % 4]; the vi degree,
# index 3, is minor). The OrganToccata fixture reuses the OrganPrelude C-major scale
# walk (ORGAN_PRELUDE_CMAJ_SCALE / organPrelude_scale_up) for its figuration, so no
# separate scale constant is defined here. test_organ_toccata_mirror.py guards
# ORGAN_TOCCATA_BAR_ROOT against the C++ source.
ORGAN_TOCCATA_BAR_ROOT: tuple[int, ...] = (0, 5, 7, 9)  # I IV V vi.

# ChoralePrelude organ-chorale-prelude fixture mirror. Keep byte-identical with
# kCfSkeleton / kBarRoot and the embellishment / scale walk in
# buildChoralePreludeFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches. A drift-guard test (test_chorale_prelude_mirror.py)
# asserts these match the C++ source.
#   kCfSkeleton[bar]: the fixed chorale tune, one structural tone per bar
#   (stepwise, C3-G3). Each tone is a chord tone of that bar's chord.
CHORALE_PRELUDE_CF_SKELETON: tuple[int, ...] = (
    48, 50, 52, 53, 52, 50, 48, 47, 48, 50, 52, 53, 55, 53, 50, 48,
)
#   kBarRoot[bar]: per-bar chord root pitch class (the diatonic progression
#   I V I IV I V I V I V I IV V V V I, every triad major / kBarMinor all false).
CHORALE_PRELUDE_BAR_ROOT: tuple[int, ...] = (
    0, 7, 0, 5, 0, 7, 0, 7, 0, 7, 0, 5, 7, 7, 7, 0,
)


def choralePrelude_cf_embellished() -> list[tuple[int, int]]:
    """Predict the embellished cantus firmus note stream (48 notes).

    Mirrors the embellishment loop in buildChoralePreludeFixture
    (src/composer/harness_fixture.cpp) byte-for-byte: each bar is the skeleton
    tone as a half note on the downbeat, followed by two stepwise quarter-note
    passing tones walking toward the NEXT bar's skeleton tone (the last bar
    walks back to its own tone) via the C++ ``stepToward`` helper (one diatonic
    step if the adjacent semitone is in the C-major scale, else two semitones).

    @return Sorted [(start_tick, pitch)] for the embellished CF (16 bars x 3).
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    half = ticks_per_beat * 2
    quarter = ticks_per_beat

    def step_toward(frm: int, to: int) -> int:
        if to > frm:
            return frm + (1 if (frm + 1) % 12 in ORGAN_PRELUDE_CMAJ_SCALE else 2)
        if to < frm:
            return frm - (1 if (frm - 1) % 12 in ORGAN_PRELUDE_CMAJ_SCALE else 2)
        return frm

    seq: list[tuple[int, int]] = []
    bars = len(CHORALE_PRELUDE_CF_SKELETON)
    for bar in range(bars):
        tone = CHORALE_PRELUDE_CF_SKELETON[bar]
        nxt = CHORALE_PRELUDE_CF_SKELETON[bar + 1] if bar + 1 < bars else CHORALE_PRELUDE_CF_SKELETON[bar]
        base = bar * ticks_per_bar
        seq.append((base, tone))  # downbeat skeleton tone (half note).
        p1 = step_toward(tone, nxt)
        p2 = step_toward(p1, nxt)
        seq.append((base + half, p1))
        seq.append((base + half + quarter, p2))
    seq.sort()
    return seq


# Passacaglia organ-passacaglia fixture mirror. Keep byte-identical with kGroundPitch
# / kVarT0 / kRootPc / kIsMinor and the C-minor scale walk in buildPassacagliaFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (test_passacaglia_mirror.py) asserts these match the
# C++ source. The passacaglia reuses the Chaconne C-minor scale walk
# (CHACONNE_CMIN_SCALE / chaconne_scale_up) for its variation figuration, so no
# separate scale constant is defined here.
#   kGroundPitch[bar in cycle]: the immutable 8-bar ground bass, one whole-note
#   per bar, descending then cadencing (C3 Bb2 Ab2 G2 F2 Eb2 D2 G2).
PASSACAGLIA_GROUND: tuple[int, ...] = (48, 46, 44, 43, 41, 39, 38, 43)
# kVarT0[bar in cycle]: the lowest variation tone of each chord (the 8-bar cycle
# i VII VI V iv III ii0 V), in a singable ~C4-C5 register; each bar's scalar wave
# starts here.
PASSACAGLIA_VAR_T0: tuple[int, ...] = (60, 58, 56, 55, 53, 51, 50, 55)
# kRootPc[bar in cycle] / kIsMinor[bar in cycle]: the 8-bar harmonic cycle's
# per-bar chord root pitch class and minor-quality flag (i VII VI V iv III ii0 V).
PASSACAGLIA_BAR_ROOT: tuple[int, ...] = (0, 10, 8, 7, 5, 3, 2, 7)
PASSACAGLIA_BAR_MINOR: tuple[bool, ...] = (True, False, False, False, True, False, True, False)
# Per-cycle notes-per-beat (kBlocks in buildPassacagliaFixture): cycle0=eighth,
# cycle1/2=sixteenth. Used by the variation predictor.
PASSACAGLIA_BLOCK_NPB: tuple[int, ...] = (2, 4, 4)

# TrioSonata organ-trio-sonata fixture mirror. Keep byte-identical with kBarRoot /
# kBarMinor and the per-voice scalar-wave / pedal construction in
# buildTrioSonataFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches. A drift-guard test (test_trio_sonata_mirror.py)
# asserts these match the C++ source. The trio sonata reuses the OrganPrelude C-major
# scale walk (ORGAN_PRELUDE_CMAJ_SCALE / organPrelude_scale_up) for its V0/V1 figuration, so
# no separate scale constant is defined here.
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 16 bars (the vi degree, index 3, is minor).
TRIO_SONATA_BAR_ROOT: tuple[int, ...] = (0, 5, 7, 9)  # I IV V vi.
TRIO_SONATA_BAR_MINOR: tuple[bool, ...] = (False, False, False, True)
# Per-voice register base octave offset and notes-per-beat density:
#   V0 (RH / Great):  base 72 + root_pc, 4 notes/beat (sixteenths) -> 16/bar.
#   V1 (LH / Swell):  base 60 + root_pc, 2 notes/beat (eighths)    ->  8/bar.
#   V2 (Pedal):       base 40 + root_pc, 1 note /beat (quarters)   ->  4/bar.
TRIO_SONATA_V0_BASE = 72
TRIO_SONATA_V1_BASE = 60
TRIO_SONATA_PEDAL_BASE = 40
TRIO_SONATA_V0_NPB = 4
TRIO_SONATA_V1_NPB = 2

# Fantasia organ-fantasia fixture mirror. Keep byte-identical with kBarRoot /
# kBarMinor and the four-section construction (kSpecs) in buildFantasiaFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (test_fantasia_mirror.py) asserts these match
# the C++ source. The fantasia reuses the OrganPrelude C-major scale walk
# (ORGAN_PRELUDE_CMAJ_SCALE / organPrelude_scale_up) for its scalar-wave figuration, so no
# separate scale constant is defined here.
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 16 bars (the vi degree, index 3, is minor).
FANTASIA_BAR_ROOT: tuple[int, ...] = (0, 5, 7, 9)  # I IV V vi.
FANTASIA_BAR_MINOR: tuple[bool, ...] = (False, False, False, True)
# kSpecs[4]: the four contrasting sections, each 4 bars, tiling bars 0..15.
# Each tuple is (first_bar, last_bar_inclusive, density_level, base_midi, kind)
# where kind selects the note shape: 0=quarters (4/bar), 1=eighths (8/bar),
# 2=sixteenths (16/bar), 3=half-notes (2/bar).
#   A bars 0-3:   Free,    density 4,  base C3 (48), quarters.
#   B bars 4-7:   Fugal,   density 8,  base C4 (60), eighths.
#   C bars 8-11:  Toccata, density 16, base C5 (72), sixteenths.
#   D bars 12-15: Chordal, density 2,  base C4 (60), half-notes.
FANTASIA_SECTIONS: tuple[tuple[int, int, int, int, int], ...] = (
    (0, 3, 4, 48, 0),
    (4, 7, 8, 60, 1),
    (8, 11, 16, 72, 2),
    (12, 15, 2, 60, 3),
)
# Validator section_contrast_required margins (kMinDensityMargin /
# kMinRegisterMargin in validator.cpp): adjacent sections must differ by >= 2
# notes/bar OR >= 5 semitones of mean register, else one soft MusicalFail.
FANTASIA_MIN_DENSITY_MARGIN = 2
FANTASIA_MIN_REGISTER_MARGIN = 5

# KeyboardSuite keyboard-suite fixture mirror. Keep byte-identical with the
# kBarRoot / kBarMinor / kGroundPitch arrays and the kMovements[5] table in
# buildKeyboardSuiteFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches. A drift-guard test (test_suite_mirror.py)
# asserts these match the C++ source. The KeyboardSuite fixture is a reuse-only
# assembly: it reuses the OrganPrelude C-major scale walk (ORGAN_PRELUDE_CMAJ_SCALE /
# organPrelude_scale_up) for its scalar-wave figuration and the GroundCarrier
# machinery for its bass, so no new scale / carrier constant is defined here.
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 20 bars (the vi degree, index 3, is minor).
KEYBOARD_SUITE_BAR_ROOT: tuple[int, ...] = (0, 5, 7, 9)  # I IV V vi.
KEYBOARD_SUITE_BAR_MINOR: tuple[bool, ...] = (False, False, False, True)
#   kGroundPitch[bar in cycle]: the immutable 4-bar C-major ground bass, one
#   whole-note per bar, chord root an octave low (C3 F2 G2 A2), tiled 5x.
KEYBOARD_SUITE_GROUND: tuple[int, ...] = (48, 41, 43, 45)
# ground_bass_period = 4 bars * kTicksPerBar (1920) = 7680.
KEYBOARD_SUITE_GROUND_PERIOD = 4 * 1920
# kMovements[5]: the five contiguous 4-bar movements tiling bars 0..19.
# Each tuple is (first_bar, last_bar_inclusive, carrier, kind, base_midi) where
# carrier 0 = FigurationCarrier, 1 = FantasiaCarrier, and kind selects the note
# shape: 0 = eighths (8/bar), 1 = sixteenths (16/bar), 2 = half-notes (2/bar).
#   1 Prelude   bars 0-3:   FigurationCarrier, sixteenths, base C5 (72) -> 64.
#   2 Allemande bars 4-7:   FantasiaCarrier,   eighths,    base C5 (72) -> 32.
#   3 Sarabande bars 8-11:  FantasiaCarrier,   half-notes, base C5 (72) ->  8.
#   4 Courante  bars 12-15: FigurationCarrier, eighths,    base E5 (76) -> 32.
#   5 Gigue     bars 16-19: FantasiaCarrier,   sixteenths, base E5 (76) -> 64.
KEYBOARD_SUITE_MOVEMENTS: tuple[tuple[int, int, int, int, int], ...] = (
    (0, 3, 0, 1, 72),
    (4, 7, 1, 0, 72),
    (8, 11, 1, 2, 72),
    (12, 15, 0, 0, 76),
    (16, 19, 1, 1, 76),
)

# PreludeAndFugue WTC-pair fixture mirror. Keep byte-identical with the
# kBarRoot[4] / kBarMinor[4] arrays, the prelude section table (two V0
# FigurationCarrier sections + one V1 bass support, with is_pedal_prep on the
# SECOND V0 section) and the fugue add_subject window / transposition scheme in
# buildPreludeAndFugueFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches. A drift-guard test (test_wtc_pair_mirror.py)
# asserts these match the C++ source. The PreludeAndFugue fixture is a reuse-only
# assembly: the prelude reuses the OrganPrelude C-major scale walk (ORGAN_PRELUDE_CMAJ_SCALE
# / organPrelude_scale_up) and FigurationCarrier; the fugue reuses the kFugueCompleteSubjects
# catalog (FUGUE_COMPLETE_SUBJECTS) plus SubjectCarrier / AnswerCarrier, so no new scale
# / carrier / subject constant is defined here.
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 24 bars (the vi degree, index 3, is minor).
PRELUDE_AND_FUGUE_BAR_ROOT: tuple[int, ...] = (0, 5, 7, 9)  # I IV V vi.
PRELUDE_AND_FUGUE_BAR_MINOR: tuple[bool, ...] = (False, False, False, True)
# Prelude section table. Each tuple is (voice, first_bar, last_bar_inclusive,
# base_midi, notes_per_beat, is_pedal_prep):
#   V0 sec0 bars 0-3: base C4 (60), 16 sixteenths/bar -> 64 notes (bit 52).
#   V0 sec1 bars 4-7: base C4 (60), 16 sixteenths/bar, is_pedal_prep -> 64 notes
#     (bits 52 + 54). The FINAL V0 prelude section is the pedal-prep link.
#   V1 bass bars 0-7: base G3 (55), 8 eighths/bar -> 64 notes (bit 52).
# The two V0 sections share voice 0 / intent FigurationCarrier, so they merge
# into ONE (0, "FigurationCarrier") group of 128 notes; the V1 bass is its own
# (1, "FigurationCarrier") group of 64 notes.
PRELUDE_AND_FUGUE_PRELUDE_SECTIONS: tuple[tuple[int, int, int, int, int, bool], ...] = (
    (0, 0, 3, 60, 4, False),
    (0, 4, 7, 60, 4, True),
    (1, 0, 7, 55, 2, False),
)
# Fugue subject window / transposition scheme. Each tuple is (voice, first_bar,
# intent, semis). The subject pattern is FUGUE_COMPLETE_SUBJECTS[subj_a] where subj_a =
# (seed // 4) % 5. The real answer (-5) is built note-by-note like the subject
# (NO tonal mapping). Note: V0 SubjectCarrier appears in TWO windows (bars 8-11
# and the bar-20 stretto leader); both merge into one (0, "SubjectCarrier") group.
PRELUDE_AND_FUGUE_FUGUE_ENTRIES: tuple[tuple[int, int, str, int], ...] = (
    (0, 8, "SubjectCarrier", 0),    # V0 subject verbatim.
    (1, 12, "AnswerCarrier", -5),   # V1 real answer (-P4).
    (2, 16, "SubjectCarrier", -12), # V2 re-entry (-P8).
    (0, 20, "SubjectCarrier", 0),   # V0 stretto leader (subject verbatim).
)

# GoldbergVariations Goldberg-style fixture mirror. Keep byte-identical with the
# kGroundPitch[kCycleBars] / kBarRoot[kCycleBars] / kBarMinor[kCycleBars] arrays
# and the kBlockSpec[kBlocks] table in buildGoldbergVariationsFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (test_goldberg_mirror.py) asserts these match the
# C++ source. The GoldbergVariations fixture is a reduced-scope, reuse-only assembly: it
# reuses the Passacaglia Passacaglia carriers (PassacagliaGround / PassacagliaVariation)
# + bits and the OrganPrelude C-major scale walk (ORGAN_PRELUDE_CMAJ_SCALE /
# organPrelude_scale_up) for its variation figuration, so no new scale / carrier
# constant is defined here.
#   kGroundPitch[bar in cycle]: the immutable 4-bar Goldberg-style bass, one
#   whole-note per bar, chord root an octave low (C2 F2 G2 A2 = roots of I IV V
#   vi), tiled 5x over the 20 bars.
GOLDBERG_VARIATIONS_GROUND: tuple[int, ...] = (36, 41, 43, 45)
# passacaglia_ground_period = 4 bars * kTicksPerBar (1920) = 7680.
GOLDBERG_VARIATIONS_GROUND_PERIOD = 4 * 1920
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 20 bars (the vi degree, index 3, is minor).
GOLDBERG_VARIATIONS_BAR_ROOT: tuple[int, ...] = (0, 5, 7, 9)  # I IV V vi.
GOLDBERG_VARIATIONS_BAR_MINOR: tuple[bool, ...] = (False, False, False, True)
# kBlockSpec[kBlocks]: the five contiguous 4-bar variation blocks tiling bars
# 0..19. Each tuple is (density_level, notes_per_bar m, base_midi, is_climax):
#   Block 0 Aria   bars 0-3:   density 0, m=2  (two half-notes/bar), base C5 (72),
#                              NOT climax. SPECIAL layout: not a scalar wave.
#   Block 1 Var1   bars 4-7:   density 1, m=4  (quarters),  base 72, not climax.
#   Block 2 Var2   bars 8-11:  density 2, m=8  (eighths),   base 72, not climax.
#   Block 3 Var3   bars 12-15: density 2, m=8  (eighths),   base 72, not climax.
#   Block 4 Var4   bars 16-19: density 3, m=16 (sixteenths), base 72, IS climax.
GOLDBERG_VARIATIONS_BLOCK_SPEC: tuple[tuple[int, int, int, bool], ...] = (
    (0, 2, 72, False),
    (1, 4, 72, False),
    (2, 8, 72, False),
    (2, 8, 72, False),
    (3, 16, 72, True),
)



# 5 subject patterns × 16 quarter-note pitches. Mirrors kSubjectPatterns in
# src/composer/harness_fixture.cpp; keep byte-identical with the C++ catalog
# or harness assertions diverge from CLI output.
SUBJECT_PATTERNS: tuple[tuple[int, ...], ...] = (
    (72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72),
    (84, 83, 84, 79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 76, 71, 72),
    (79, 76, 79, 84, 76, 79, 76, 72, 74, 77, 74, 71, 72, 76, 71, 72),
    (71, 72, 76, 77, 74, 76, 77, 79, 76, 77, 79, 81, 77, 79, 71, 72),
    (76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 77, 79, 77, 71, 72),
)

# FugueComplete uses its own subject catalog (kFugueCompleteSubjects in
# src/composer/harness_fixture.cpp): slots 0/1/4 match the shared catalog,
# slots 2/3 replace the two statistically weak subjects with higher-prob
# diatonic ones. The structural predictor must mirror this exactly or it
# reports false length/pitch mismatches for seeds that select slot 2/3.
FUGUE_COMPLETE_SUBJECTS: tuple[tuple[int, ...], ...] = (
    (72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72),
    (76, 74, 72, 74, 76, 77, 79, 77, 76, 79, 77, 76, 74, 72, 71, 72),
    (79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 74, 71, 72),
    (72, 74, 76, 77, 79, 77, 79, 81, 79, 77, 76, 74, 76, 74, 71, 72),
    (76, 77, 79, 81, 79, 77, 76, 77, 79, 77, 76, 74, 72, 74, 71, 72),
)


# The FugueComplete fixture is the all-technique case: every provenance RuleBit
# (0..46) must be exercised, so the closure invocation must supply all 47
# --required-rule-bit masks. provenance.h RuleBit currently tops out at
# AffektCurveApplied = 46, so max_bit + 1 == 47.
FUGUE_COMPLETE_REQUIRED_BIT_COUNT = 47

# CelloPrelude (Solo String Flow) stamps exactly two RuleBits on every note:
# ArpeggioFlowActive = 47 and ImplicitVoiceTracked = 48 (provenance.h). Both
# must be asserted or the bit checks are trivially bypassed.
CELLO_PRELUDE_REQUIRED_BITS: tuple[int, ...] = (47, 48)

# Chaconne (Solo String Arch) stamps three RuleBits across its notes (provenance.h):
# GroundBassReplayed = 49 (every ground note), VariationRoleApplied = 50 (every
# variation note), and TextureDensityShift = 51 (the first note of each
# density-changing variation). All three must be asserted or the bit checks
# are trivially bypassed.
CHACONNE_REQUIRED_BITS: tuple[int, ...] = (49, 50, 51)

# OrganPrelude (Organ Prelude) stamps three RuleBits across its notes (provenance.h):
# FigurationCommitted = 52 (every figuration note), CadenzaApplied = 53 (every
# note of the is_cadenza section), and PedalPreparation = 54 (the sustained
# dominant pedal). All three must be asserted or the bit checks
# are trivially bypassed.
ORGAN_PRELUDE_REQUIRED_BITS: tuple[int, ...] = (52, 53, 54)

# OrganToccata (Organ Toccata) stamps two RuleBits across its notes (provenance.h):
# ToccataArchetypeApplied = 55 (every toccata-section note) and SectionTransition
# = 56 (the first note of each section flagged is_section_head). Every archetype
# has at least one section head, so bit 56 fires >= 1 time per seed (once for
# Perpetuus, twice for Dramaticus/Sectionalis, four times for Concertato); the
# per-bit "fired anywhere in the seed" check is therefore satisfied. Both must
# be asserted or the bit checks are trivially bypassed.
ORGAN_TOCCATA_REQUIRED_BITS: tuple[int, ...] = (55, 56)

# ChoralePrelude (Organ Chorale Prelude) stamps two RuleBits across its notes
# (provenance.h): CantusFirmusReplayed = 57 (every cantus-firmus carrier note)
# and CFEmbellishmentApplied = 58 (every cantus-firmus note when the embellished
# line is replayed; this fixture always embellishes, so bit 58 fires on all 48
# CF notes). Both must be asserted or the bit checks are trivially
# bypassed.
CHORALE_PRELUDE_REQUIRED_BITS: tuple[int, ...] = (57, 58)

# Passacaglia (Organ Passacaglia) stamps three RuleBits across its notes
# (provenance.h): PassacagliaGroundReplayed = 59 (every ground note),
# VariationApplied = 60 (every variation note), and ClimaxPlaced = 61 (every note
# of the is_climax variation block). All three must be asserted or the
# bit checks are trivially bypassed.
PASSACAGLIA_REQUIRED_BITS: tuple[int, ...] = (59, 60, 61)

# TrioSonata (Organ Trio Sonata) stamps one dedicated RuleBit across all of its
# notes (provenance.h): TrioVoiceIndependent = 62, set on every TrioVoiceCarrier
# note of all three voices. It must be asserted or the bit checks
# are trivially bypassed. (The baseline ChordTone / FugueHarmonized / FugueModulating bits
# emitMaterialNote adds are not phase-specific and so are not part of the
# dedicated closure set, matching every other carrier phase.)
TRIO_SONATA_REQUIRED_BITS: tuple[int, ...] = (62,)

# Fantasia (Organ Fantasia) stamps one dedicated RuleBit across all of its notes
# (provenance.h): FantasiaSectionContrast = 63 (the LAST free RuleBit), set on
# every FantasiaCarrier note of all four contrasting sections. It must be
# asserted or the bit checks are trivially bypassed. (The baseline
# ChordTone / FugueHarmonized / FugueModulating bits emitMaterialNote adds are not phase-specific
# and so are not part of the dedicated closure set, matching every other carrier
# phase.)
FANTASIA_REQUIRED_BITS: tuple[int, ...] = (63,)

# The KeyboardSuite keyboard suite is a reuse-only assembly: it stamps three
# already-existing RuleBits across its notes (provenance.h): FigurationCommitted
# = 52 (every FigurationCarrier dance note, movements 1 & 4),
# FantasiaSectionContrast = 63 (every FantasiaCarrier dance note, movements 2/3/5),
# and GroundBassReplayed = 49 (every GroundCarrier bass note). No new RuleBit is
# introduced. All three must be asserted or the bit checks are trivially
# bypassed.
KEYBOARD_SUITE_REQUIRED_BITS: tuple[int, ...] = (52, 63, 49)

# The PreludeAndFugue WTC Prelude+Fugue pair is a reuse-only assembly: it stamps
# two already-existing prelude RuleBits across its prelude notes (provenance.h):
# FigurationCommitted = 52 (every FigurationCarrier prelude note, all three
# sections) and PedalPreparation = 54 (the SECOND V0 prelude section only, the
# is_pedal_prep prelude->fugue link). No new RuleBit is introduced. The fugue
# carriers (SubjectCarrier / AnswerCarrier) have NO identity bit (they carry only
# ChordTone), so the fugue half is asserted STRUCTURALLY (span intent + bar
# windows), not by a bit. Both prelude bits must be asserted or the bit checks
# are trivially bypassed.
PRELUDE_AND_FUGUE_REQUIRED_BITS: tuple[int, ...] = (52, 54)

# The GoldbergVariations Goldberg-style immutable-bass variation skeleton is a
# reduced-scope, reuse-only assembly: it stamps the three already-existing
# Passacaglia Passacaglia RuleBits across its notes (provenance.h):
# PassacagliaGroundReplayed = 59 (every PassacagliaGround note), VariationApplied
# = 60 (every PassacagliaVariation note), and ClimaxPlaced = 61 (every note of
# the is_climax variation block, i.e. block 4 / bars 16-19). No new RuleBit is
# introduced. All three must be asserted or the bit checks are trivially
# bypassed.
GOLDBERG_VARIATIONS_REQUIRED_BITS: tuple[int, ...] = (59, 60, 61)
