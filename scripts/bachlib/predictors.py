#!/usr/bin/env python3
"""Structural carrier predictors for the Composer closure harness.

Each ``expected_*_sequence`` function predicts the byte-stable (voice, intent)
note streams a seed fixture should produce, mirroring the corresponding
``buildPhaseN`` fixture in ``src/composer/harness_fixture.cpp``. The
:func:`structural_check` driver compares those predictions against the observed
carrier streams extracted from generated / provenance JSON. Mirror constants are
imported from :mod:`bachlib.mirror`; the per-phase layout from
:mod:`bachlib.phases`.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from bachlib.mirror import (
    FUGUE_COMPLETE_SUBJECTS,
    CELLO_PRELUDE_BARPLAN,
    CELLO_PRELUDE_FIGURES,
    CHACONNE_BLOCK_NOTES_PER_BEAT,
    CHACONNE_GROUND,
    CHACONNE_VAR_T0,
    ORGAN_PRELUDE_BAR_MINOR,
    ORGAN_PRELUDE_BAR_ROOT,
    ORGAN_TOCCATA_BAR_ROOT,
    CHORALE_PRELUDE_BAR_ROOT,
    CHORALE_PRELUDE_CF_SKELETON,
    PASSACAGLIA_BLOCK_NPB,
    PASSACAGLIA_GROUND,
    PASSACAGLIA_VAR_T0,
    TRIO_SONATA_BAR_MINOR,
    TRIO_SONATA_BAR_ROOT,
    TRIO_SONATA_PEDAL_BASE,
    TRIO_SONATA_V0_BASE,
    TRIO_SONATA_V0_NPB,
    TRIO_SONATA_V1_BASE,
    TRIO_SONATA_V1_NPB,
    FANTASIA_BAR_MINOR,
    FANTASIA_BAR_ROOT,
    FANTASIA_SECTIONS,
    KEYBOARD_SUITE_BAR_MINOR,
    KEYBOARD_SUITE_BAR_ROOT,
    KEYBOARD_SUITE_GROUND,
    KEYBOARD_SUITE_MOVEMENTS,
    PRELUDE_AND_FUGUE_BAR_MINOR,
    PRELUDE_AND_FUGUE_BAR_ROOT,
    PRELUDE_AND_FUGUE_FUGUE_ENTRIES,
    PRELUDE_AND_FUGUE_PRELUDE_SECTIONS,
    GOLDBERG_VARIATIONS_BAR_ROOT,
    GOLDBERG_VARIATIONS_BLOCK_SPEC,
    GOLDBERG_VARIATIONS_GROUND,
    SUBJECT_PATTERNS,
    chaconne_scale_up,
    organPrelude_scale_up,
    choralePrelude_cf_embellished,
)
from bachlib.phases import PHASE_LAYOUT


def expected_arpeggio_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the CelloPrelude ArpeggioFlow note stream for a seed fixture.

    Mirrors buildCelloPreludeFixture in src/composer/harness_fixture.cpp: 8 bars,
    one chord per bar (CELLO_PRELUDE_BARPLAN), each beat arpeggiated as 4 sixteenths
    re-ordered by the seed-selected figure (CELLO_PRELUDE_FIGURES[seed % 4]). The
    figure index is seed % 4, which equals fixture["harm_idx"].

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(0, "ArpeggioFlow"): sorted [(start_tick, pitch)]} (128 notes).
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    sixteenth = ticks_per_beat // 4
    figure = CELLO_PRELUDE_FIGURES[fixture["harm_idx"] % 4]
    seq: list[tuple[int, int]] = []
    for bar in range(8):
        tones = CELLO_PRELUDE_BARPLAN[bar]  # (bass, mid, top)
        for beat in range(4):
            for s in range(4):
                tick = bar * ticks_per_bar + beat * ticks_per_beat + s * sixteenth
                seq.append((tick, tones[figure[s]]))
    seq.sort()
    return {(0, "ArpeggioFlow"): seq}


def expected_ground_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the Chaconne GroundCarrier + VariationCarrier note streams.

    Mirrors buildChaconneFixture in src/composer/harness_fixture.cpp: 16 bars =
    4 cycles of a 4-bar immutable ground bass (CHACONNE_GROUND, one whole-note
    per bar, V1) plus four variation blocks (V0) of rising density. Each
    variation bar is a stepwise scalar wave through C natural minor, starting
    `offset = seed % 4` scale degrees above the bar's lowest chord tone
    (CHACONNE_VAR_T0): an ascending run of (m/2 + 1) degrees mirrored back down
    (dropping the duplicated peak) and tiled to m = 4 * notes_per_beat notes.
    The offset is seed % 4, which equals fixture["harm_idx"].

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(1, "GroundCarrier"): [(start_tick, pitch)] (16 notes),
             (0, "VariationCarrier"): [(start_tick, pitch)]} sorted by tick.
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    cycle_bars = 4

    # GroundCarrier: the immutable ground bass period-tiled four times.
    ground: list[tuple[int, int]] = []
    for cycle in range(4):
        for bar in range(cycle_bars):
            tick = (cycle * cycle_bars + bar) * ticks_per_bar
            ground.append((tick, CHACONNE_GROUND[bar]))
    ground.sort()

    # VariationCarrier: four blocks, each 4 bars, density-tiered. Each bar is a
    # scalar wave (ascending then descending) through C natural minor.
    offset = fixture["harm_idx"] % 4
    variation: list[tuple[int, int]] = []
    for block in range(4):
        npb = CHACONNE_BLOCK_NOTES_PER_BEAT[block]
        step = ticks_per_beat // npb
        block_base = block * cycle_bars * ticks_per_bar
        for bar in range(cycle_bars):
            m = 4 * npb
            start = chaconne_scale_up(CHACONNE_VAR_T0[bar], offset)
            wave = [chaconne_scale_up(start, i) for i in range(m // 2 + 1)]
            wave += wave[-2::-1]
            for beat in range(4):
                for sub in range(npb):
                    tick = (block_base + bar * ticks_per_bar + beat * ticks_per_beat +
                            sub * step)
                    idx = beat * npb + sub
                    variation.append((tick, wave[idx % len(wave)]))
    variation.sort()
    return {(1, "GroundCarrier"): ground, (0, "VariationCarrier"): variation}


def expected_figuration_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the OrganPrelude FigurationCarrier note streams for a seed fixture.

    Mirrors buildOrganPreludeFixture in src/composer/harness_fixture.cpp: 16 bars,
    one triad per bar (ORGAN_PRELUDE_BAR_ROOT / ORGAN_PRELUDE_BAR_MINOR). V0 carries three
    FigurationCarrier sections (bars 0-7, 8-11, 12-15-cadenza) of sixteenth-note
    scalar-wave figuration; all three are voice 0 so they merge into one
    (0, "FigurationCarrier") group. V1 carries a one-note-per-bar bass support
    line (bars 0-13) plus a single sustained dominant pedal (bars 14-15), both
    voice 1, merging into one (1, "FigurationCarrier") group.

    Each figuration bar opens on a chord tone (the validator anchors only the
    bar downbeat): start = scale_up(60 + root, offset) snapped up to the nearest
    triad pitch class, then an ascending run of (16/2 + 1) = 9 scale degrees
    mirrored back down (dropping the peak duplicate) and tiled to 16 sixteenths.
    The offset is seed % 4, which equals fixture["harm_idx"].

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(0, "FigurationCarrier"): [(start_tick, pitch)] (256 notes),
             (1, "FigurationCarrier"): [(start_tick, pitch)] (15 notes)} sorted.
    """
    ticks_per_bar = 1920
    sixteenth = 120  # kTicksPerBeat (480) / 4.
    offset = fixture["harm_idx"] % 4

    # V0 figuration: 16 sixteenths per bar, all 16 bars (sec0 0-7, sec1 8-11,
    # sec2 12-15); every bar uses the identical scalar-wave construction.
    v0: list[tuple[int, int]] = []
    for bar in range(16):
        root = ORGAN_PRELUDE_BAR_ROOT[bar]
        third = 3 if ORGAN_PRELUDE_BAR_MINOR[bar] else 4
        triad = {root % 12, (root + third) % 12, (root + 7) % 12}
        start = organPrelude_scale_up(60 + root, offset)
        while start % 12 not in triad:
            start += 1
        wave = [organPrelude_scale_up(start, i) for i in range(16 // 2 + 1)]
        wave += wave[-2::-1]
        for n in range(16):
            tick = bar * ticks_per_bar + n * sixteenth
            v0.append((tick, wave[n % len(wave)]))
    v0.sort()

    # V1 bass support: one chord-root note per bar for bars 0-13 (~C2-B2), then a
    # single sustained dominant pedal (G2 = pc 7, MIDI 43) covering bars 14-15.
    v1: list[tuple[int, int]] = []
    for bar in range(14):
        tick = bar * ticks_per_bar
        v1.append((tick, 36 + (ORGAN_PRELUDE_BAR_ROOT[bar] % 12)))
    v1.append((14 * ticks_per_bar, 43))
    v1.sort()

    return {(0, "FigurationCarrier"): v0, (1, "FigurationCarrier"): v1}


def expected_toccata_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the OrganToccata ToccataCarrier note stream for a seed fixture.

    Mirrors buildOrganToccataFixture in src/composer/harness_fixture.cpp: 16 bars,
    one triad per bar from the diatonic C-major I IV V vi progression cycled
    over 16 bars (bar b -> root = ORGAN_TOCCATA_BAR_ROOT[b % 4]; minor = (b % 4 == 3)).
    The single voice (V0) carries continuous sixteenth-note scalar-wave
    figuration. The archetype (seed % 4) only changes how the bars are grouped
    into ToccataCarrier sections; it does NOT change the pitches. Because all
    sections are voice 0 / intent ToccataCarrier, they merge into ONE
    (0, "ToccataCarrier") group of 256 contiguous notes, so the predictor needs
    only the scalar-wave offset = (seed // 4) % 4 (NOT the archetype).

    Each bar opens on a chord tone: start = organPrelude_scale_up(60 + root, offset)
    snapped up to the nearest triad pitch class, then an ascending run of
    (16/2 + 1) = 9 C-major scale degrees mirrored back down (dropping the peak
    duplicate) and tiled to 16 sixteenths. Reuses the OrganPrelude C-major scale walk
    verbatim (the toccata figuration is identical to the prelude figuration).

    @param fixture fixture_for_seed() output (with injected "seed") for the seed.
    @return {(0, "ToccataCarrier"): [(start_tick, pitch)]} (256 notes), sorted.
    """
    ticks_per_bar = 1920
    sixteenth = 120  # kTicksPerBeat (480) / 4.
    offset = (fixture["seed"] // 4) % 4

    seq: list[tuple[int, int]] = []
    for bar in range(16):
        cyc = bar % 4
        root = ORGAN_TOCCATA_BAR_ROOT[cyc]
        minor = cyc == 3
        third = 3 if minor else 4
        triad = {root % 12, (root + third) % 12, (root + 7) % 12}
        start = organPrelude_scale_up(60 + root, offset)
        while start % 12 not in triad:
            start += 1
        wave = [organPrelude_scale_up(start, i) for i in range(16 // 2 + 1)]
        wave += wave[-2::-1]
        for n in range(16):
            tick = bar * ticks_per_bar + n * sixteenth
            seq.append((tick, wave[n % len(wave)]))
    seq.sort()
    return {(0, "ToccataCarrier"): seq}


def expected_chorale_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the ChoralePrelude FigurationCarrier + CantusFirmusCarrier note streams.

    Mirrors buildChoralePreludeFixture in src/composer/harness_fixture.cpp: 16 bars,
    one major triad per bar (CHORALE_PRELUDE_BAR_ROOT, all major). V1 carries the
    embellished cantus firmus (CHORALE_PRELUDE_CF_SKELETON via choralePrelude_cf_embellished:
    each bar = skeleton tone half note on the downbeat + two stepwise quarter
    passing tones toward the next bar's tone). V0 carries the exact OrganPrelude
    scalar-wave figuration (16 sixteenths/bar, opening on a chord tone snapped
    from organPrelude_scale_up(60 + root, offset), ascending 9 degrees mirrored and
    tiled to 16). The offset is seed % 4, which equals fixture["harm_idx"].

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(0, "FigurationCarrier"): [(start_tick, pitch)] (256 notes),
             (1, "CantusFirmusCarrier"): [(start_tick, pitch)] (48 notes)} sorted.
    """
    ticks_per_bar = 1920
    sixteenth = 120  # kTicksPerBeat (480) / 4.
    offset = fixture["harm_idx"] % 4

    # V0 figuration: 16 sixteenths per bar, all 16 bars; identical scalar-wave
    # construction to OrganPrelude (every ChoralePrelude bar is major, so third = 4).
    v0: list[tuple[int, int]] = []
    for bar in range(16):
        root = CHORALE_PRELUDE_BAR_ROOT[bar]
        triad = {root % 12, (root + 4) % 12, (root + 7) % 12}
        start = organPrelude_scale_up(60 + root, offset)
        while start % 12 not in triad:
            start += 1
        wave = [organPrelude_scale_up(start, i) for i in range(16 // 2 + 1)]
        wave += wave[-2::-1]
        for n in range(16):
            tick = bar * ticks_per_bar + n * sixteenth
            v0.append((tick, wave[n % len(wave)]))
    v0.sort()

    # V1 cantus firmus: the embellished chorale tune (downbeats == skeleton).
    v1 = choralePrelude_cf_embellished()

    return {(0, "FigurationCarrier"): v0, (1, "CantusFirmusCarrier"): v1}


def expected_passacaglia_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the Passacaglia PassacagliaGround + PassacagliaVariation note streams.

    Mirrors buildPassacagliaFixture in src/composer/harness_fixture.cpp: 24 bars =
    3 cycles of an immutable 8-bar ground bass (PASSACAGLIA_GROUND, one whole-note
    per bar, V1) plus three variation blocks (V0), one per cycle, of rising
    density (PASSACAGLIA_BLOCK_NPB = 2 / 4 / 4 notes-per-beat). Each variation bar
    is a stepwise scalar wave through C natural minor, starting ``offset =
    seed % 4`` scale degrees above the bar's lowest chord tone (PASSACAGLIA_VAR_T0):
    an ascending run of (m/2 + 1) degrees mirrored back down (dropping the
    duplicated peak) and tiled to m = 4 * notes_per_beat notes. The offset is
    seed % 4, which equals fixture["harm_idx"]. The is_climax flag on the last
    cycle changes only the ClimaxPlaced bit (not pitch / tick), so it is not
    modeled here.

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(1, "PassacagliaGround"): [(start_tick, pitch)] (24 notes),
             (0, "PassacagliaVariation"): [(start_tick, pitch)]} sorted by tick.
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    cycle_bars = 8
    cycles = 3

    # PassacagliaGround: the immutable ground bass period-tiled three times.
    ground: list[tuple[int, int]] = []
    for cycle in range(cycles):
        for bar in range(cycle_bars):
            tick = (cycle * cycle_bars + bar) * ticks_per_bar
            ground.append((tick, PASSACAGLIA_GROUND[bar]))
    ground.sort()

    # PassacagliaVariation: one block per cycle, each 8 bars, density-tiered.
    # Each bar is a scalar wave (ascending then descending) through C natural
    # minor.
    offset = fixture["harm_idx"] % 4
    variation: list[tuple[int, int]] = []
    for cycle in range(cycles):
        npb = PASSACAGLIA_BLOCK_NPB[cycle]
        step = ticks_per_beat // npb
        block_base = cycle * cycle_bars * ticks_per_bar
        for bar in range(cycle_bars):
            m = 4 * npb
            start = chaconne_scale_up(PASSACAGLIA_VAR_T0[bar], offset)
            wave = [chaconne_scale_up(start, i) for i in range(m // 2 + 1)]
            wave += wave[-2::-1]
            for beat in range(4):
                for sub in range(npb):
                    tick = (block_base + bar * ticks_per_bar + beat * ticks_per_beat +
                            sub * step)
                    idx = beat * npb + sub
                    variation.append((tick, wave[idx % len(wave)]))
    variation.sort()
    return {(1, "PassacagliaGround"): ground, (0, "PassacagliaVariation"): variation}


def expected_trio_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the TrioSonata TrioVoiceCarrier note streams for a seed fixture.

    Mirrors buildTrioSonataFixture in src/composer/harness_fixture.cpp: 16 bars,
    one triad per bar from the diatonic C-major I IV V vi progression cycled
    over 16 bars (bar b -> root = TRIO_SONATA_BAR_ROOT[b % 4]; minor = (b % 4 == 3)).
    Three independent TrioVoiceCarrier voices, each replayed verbatim:

      - V0 (RH / Great): 16 sixteenth-notes/bar (4 notes/beat -> m=16). The bar
        opens on a chord tone: start = organPrelude_scale_up(72 + root_pc, offset)
        snapped UP to the nearest triad pitch class, then an ascending run of
        (m/2 + 1) C-major scale degrees mirrored back down (dropping the peak
        duplicate) and tiled to m. 256 notes total.
      - V1 (LH / Swell): 8 eighth-notes/bar (2 notes/beat -> m=8), identical
        scalar-wave construction from base 60 + root_pc. 128 notes total.
      - V2 (Pedal): 4 quarter-notes/bar (1 note/beat). Beats 0 and 2 are the bar
        root (40 + root_pc); beats 1 and 3 are a perfect fifth above
        (40 + root_pc + 7). 64 notes total.

    Total = 448 notes. The offset is seed % 4, which equals fixture["harm_idx"].
    Reuses the OrganPrelude C-major scale walk verbatim for V0/V1 (the trio figuration
    is the same scalar-wave construction as the prelude / toccata figuration).

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(0, "TrioVoiceCarrier"): [(start_tick, pitch)] (256 notes),
             (1, "TrioVoiceCarrier"): [(start_tick, pitch)] (128 notes),
             (2, "TrioVoiceCarrier"): [(start_tick, pitch)] (64 notes)} sorted.
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    offset = fixture["harm_idx"] % 4

    def scalar_voice(base_octave: int, notes_per_beat: int) -> list[tuple[int, int]]:
        seq: list[tuple[int, int]] = []
        step = ticks_per_beat // notes_per_beat
        for bar in range(16):
            cyc = bar % 4
            root = TRIO_SONATA_BAR_ROOT[cyc]
            third = 3 if TRIO_SONATA_BAR_MINOR[cyc] else 4
            triad = {root % 12, (root + third) % 12, (root + 7) % 12}
            start = organPrelude_scale_up(base_octave + root, offset)
            while start % 12 not in triad:
                start += 1
            m = 4 * notes_per_beat
            wave = [organPrelude_scale_up(start, i) for i in range(m // 2 + 1)]
            wave += wave[-2::-1]
            for beat in range(4):
                for sub in range(notes_per_beat):
                    tick = (bar * ticks_per_bar + beat * ticks_per_beat +
                            sub * step)
                    idx = beat * notes_per_beat + sub
                    seq.append((tick, wave[idx % len(wave)]))
        seq.sort()
        return seq

    v0 = scalar_voice(TRIO_SONATA_V0_BASE, TRIO_SONATA_V0_NPB)
    v1 = scalar_voice(TRIO_SONATA_V1_BASE, TRIO_SONATA_V1_NPB)

    # V2 pedal: root on strong beats (0, 2), perfect fifth on weak beats (1, 3).
    v2: list[tuple[int, int]] = []
    for bar in range(16):
        root_pc = TRIO_SONATA_BAR_ROOT[bar % 4] % 12
        root_midi = TRIO_SONATA_PEDAL_BASE + root_pc
        fifth_midi = root_midi + 7
        for beat in range(4):
            tick = bar * ticks_per_bar + beat * ticks_per_beat
            v2.append((tick, root_midi if beat % 2 == 0 else fifth_midi))
    v2.sort()

    return {
        (0, "TrioVoiceCarrier"): v0,
        (1, "TrioVoiceCarrier"): v1,
        (2, "TrioVoiceCarrier"): v2,
    }


def expected_fantasia_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the Fantasia FantasiaCarrier note stream for a seed fixture.

    Mirrors buildFantasiaFixture in src/composer/harness_fixture.cpp: 16 bars,
    one triad per bar from the diatonic C-major I IV V vi progression cycled
    over 16 bars (bar b -> root = FANTASIA_BAR_ROOT[b % 4]; minor = (b % 4 == 3)).
    A single voice (V0) is organized into FOUR contiguous 4-bar FantasiaCarrier
    sections (FANTASIA_SECTIONS), each a contrasting texture density / register:

      - A bars 0-3   (Free,    base C3 48): 4 quarter notes/bar. The bar opens on
        a chord tone (start = organPrelude_scale_up(base, offset) snapped UP to the
        nearest triad pitch class), then walks the C-major scale up-up-down:
        [start, +1, +2, +1] degrees. 16 notes.
      - B bars 4-7   (Fugal,   base C4 60): 8 eighth notes/bar, scalar wave
        (ascending (m/2+1) degrees mirrored back down, dropping the peak, tiled
        to m = 4*2 = 8). 32 notes.
      - C bars 8-11  (Toccata, base C5 72): 16 sixteenth notes/bar, identical
        scalar-wave construction (m = 4*4 = 16). 64 notes.
      - D bars 12-15 (Chordal, base C4 60): 2 half notes/bar, [start, +1]
        chord-tone-anchored stepwise pair. 8 notes.

    All four sections are voice 0 / intent FantasiaCarrier, so they merge into
    ONE (0, "FantasiaCarrier") group of 120 contiguous notes. The offset is
    seed % 4, which equals fixture["harm_idx"]. Reuses the OrganPrelude C-major scale
    walk verbatim (the fantasia figuration is the same scalar-wave construction
    as the prelude / toccata / trio figuration).

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(0, "FantasiaCarrier"): [(start_tick, pitch)]} (120 notes), sorted.
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    eighth = ticks_per_beat // 2
    sixteenth = ticks_per_beat // 4
    half = ticks_per_beat * 2
    offset = fixture["harm_idx"] % 4

    def chord_tone_start(bar: int, base_midi: int) -> int:
        cyc = bar % 4
        root = FANTASIA_BAR_ROOT[cyc]
        third = 3 if FANTASIA_BAR_MINOR[cyc] else 4
        triad = {root % 12, (root + third) % 12, (root + 7) % 12}
        start = organPrelude_scale_up(base_midi, offset)
        while start % 12 not in triad:
            start += 1
        return start

    seq: list[tuple[int, int]] = []
    for first_bar, last_bar, _density, base_midi, kind in FANTASIA_SECTIONS:
        for bar in range(first_bar, last_bar + 1):
            start = chord_tone_start(bar, base_midi)
            bar_base = bar * ticks_per_bar
            if kind == 0:  # quarters: [start, +1, +2, +1].
                wave = [start, organPrelude_scale_up(start, 1),
                        organPrelude_scale_up(start, 2), organPrelude_scale_up(start, 1)]
                for beat in range(4):
                    seq.append((bar_base + beat * ticks_per_beat, wave[beat]))
            elif kind in (1, 2):  # eighths / sixteenths: scalar wave.
                npb = 2 if kind == 1 else 4
                step = eighth if kind == 1 else sixteenth
                m = 4 * npb
                wave = [organPrelude_scale_up(start, i) for i in range(m // 2 + 1)]
                wave += wave[-2::-1]
                for beat in range(4):
                    for sub in range(npb):
                        tick = bar_base + beat * ticks_per_beat + sub * step
                        idx = beat * npb + sub
                        seq.append((tick, wave[idx % len(wave)]))
            elif kind == 3:  # half-notes: [start, +1].
                wave = [start, organPrelude_scale_up(start, 1)]
                for h in range(2):
                    seq.append((bar_base + h * half, wave[h]))
    seq.sort()
    return {(0, "FantasiaCarrier"): seq}


def expected_suite_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the KeyboardSuite keyboard-suite carrier note streams for a seed fixture.

    Mirrors buildKeyboardSuiteFixture in src/composer/harness_fixture.cpp: 20 bars,
    one triad per bar from the diatonic C-major I IV V vi progression cycled over
    20 bars (bar b -> root = KEYBOARD_SUITE_BAR_ROOT[b % 4]; minor = (b % 4 == 3)). The
    suite is a reuse-only assembly of the proven Chaconne/OrganPrelude/Fantasia carriers:

      - V0 dance line: five contiguous 4-bar movements (KEYBOARD_SUITE_MOVEMENTS). Each
        bar opens on a chord tone (start = organPrelude_scale_up(base_midi, offset)
        snapped UP to the nearest triad pitch class) and runs the canonical
        scalar wave (ascending (m/2 + 1) C-major scale degrees mirrored back down,
        dropping the duplicated peak, tiled to m notes/bar). Movements 1 & 4 are
        FigurationCarrier (intent FigurationCarrier); movements 2, 3, 5 are
        FantasiaCarrier. Because all FigurationCarrier spans are voice 0 they merge
        into ONE (0, "FigurationCarrier") group, and all FantasiaCarrier spans
        merge into ONE (0, "FantasiaCarrier") group:
          Prelude   (sixteenths, m=16, base 72): 64 notes -> Figuration.
          Allemande (eighths,    m=8,  base 72): 32 notes -> Fantasia.
          Sarabande (half-notes,       base 72):  8 notes -> Fantasia.
          Courante  (eighths,    m=8,  base 76): 32 notes -> Figuration.
          Gigue     (sixteenths, m=16, base 76): 64 notes -> Fantasia.
        => (0, "FigurationCarrier") = 64 + 32 = 96 notes,
           (0, "FantasiaCarrier")  = 32 + 8 + 64 = 104 notes.
      - V1 ground bass: the immutable 4-bar ground (KEYBOARD_SUITE_GROUND, one whole-note
        per bar, cycle-relative ticks) period-tiled 5x over the 20 bars (5 clean
        cycles, 20 notes). One (1, "GroundCarrier") group.

    Total = 96 + 104 + 20 = 220 notes. The offset is seed % 4, which equals
    fixture["harm_idx"]. Reuses the OrganPrelude C-major scale walk verbatim.

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(0, "FigurationCarrier"): [(start_tick, pitch)] (96 notes),
             (0, "FantasiaCarrier"):  [(start_tick, pitch)] (104 notes),
             (1, "GroundCarrier"):    [(start_tick, pitch)] (20 notes)} sorted.
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    eighth = ticks_per_beat // 2
    sixteenth = ticks_per_beat // 4
    half = ticks_per_beat * 2
    cycle_bars = 4
    offset = fixture["harm_idx"] % 4

    def build_wave(bar: int, base_midi: int, m: int) -> list[int]:
        cyc = bar % cycle_bars
        root_pc = KEYBOARD_SUITE_BAR_ROOT[cyc]
        third = 3 if KEYBOARD_SUITE_BAR_MINOR[cyc] else 4
        triad = {root_pc % 12, (root_pc + third) % 12, (root_pc + 7) % 12}
        start = organPrelude_scale_up(base_midi, offset)
        while start % 12 not in triad:
            start += 1
        wave = [organPrelude_scale_up(start, i) for i in range(m // 2 + 1)]
        wave += wave[-2::-1]
        return wave

    # V0 movements -> two merged carrier groups by carrier kind.
    figuration: list[tuple[int, int]] = []
    fantasia: list[tuple[int, int]] = []
    for first_bar, last_bar, carrier, kind, base_midi in KEYBOARD_SUITE_MOVEMENTS:
        dst = figuration if carrier == 0 else fantasia
        for bar in range(first_bar, last_bar + 1):
            bar_base = bar * ticks_per_bar
            if kind in (0, 1):  # eighths (npb=2) or sixteenths (npb=4).
                npb = 2 if kind == 0 else 4
                step = sixteenth if npb == 4 else eighth
                m = 4 * npb
                wave = build_wave(bar, base_midi, m)
                for beat in range(4):
                    for sub in range(npb):
                        tick = bar_base + beat * ticks_per_beat + sub * step
                        idx = beat * npb + sub
                        dst.append((tick, wave[idx % len(wave)]))
            elif kind == 2:  # half-notes: [start, +1].
                wave = build_wave(bar, base_midi, 2)
                for h in range(2):
                    dst.append((bar_base + h * half, wave[h % len(wave)]))
    figuration.sort()
    fantasia.sort()

    # V1 ground bass: the immutable 4-bar ground period-tiled 5x.
    ground: list[tuple[int, int]] = []
    cycles = 20 // cycle_bars
    for cycle in range(cycles):
        for bar in range(cycle_bars):
            tick = (cycle * cycle_bars + bar) * ticks_per_bar
            ground.append((tick, KEYBOARD_SUITE_GROUND[bar]))
    ground.sort()

    return {
        (0, "FigurationCarrier"): figuration,
        (0, "FantasiaCarrier"): fantasia,
        (1, "GroundCarrier"): ground,
    }


def expected_wtc_pair_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the PreludeAndFugue WTC Prelude+Fugue pair carrier note streams.

    Mirrors buildPreludeAndFugueFixture in src/composer/harness_fixture.cpp: a 24-bar /
    3-voice C-major movement (Prelude bars 0-7 + Fugue bars 8-23), one triad per
    bar from the diatonic I IV V vi progression cycled over 24 bars (bar b -> root
    = PRELUDE_AND_FUGUE_BAR_ROOT[b % 4]; minor = (b % 4 == 3)). The PreludeAndFugue fixture is a
    reuse-only assembly:

      PRELUDE (FigurationCarrier, PRELUDE_AND_FUGUE_PRELUDE_SECTIONS). Unlike the
      single-voice OrganPrelude prelude (which anchors only the bar downbeat), the WTC pair
      sounds two figuration voices simultaneously, so EVERY beat is anchored to a
      chord tone: the per-bar anchor is start = organPrelude_scale_up(base_midi + root,
      offset) snapped UP to the nearest triad pitch class, and EVERY beat restarts
      from that same anchor, walking organPrelude_scale_up(anchor, sub) for the rest of
      the beat (a 4-fold repeated up-run sawtooth). offset = seed % 4 ==
      fixture["harm_idx"].
        - V0 sec0 bars 0-3: 16 sixteenths/bar, base C4 (60) -> 64 notes.
        - V0 sec1 bars 4-7: 16 sixteenths/bar, base C4 (60), pedal-prep -> 64.
        - V1 bass bars 0-7: 8 eighths/bar,    base G3 (55) -> 64 notes.
      The two V0 sections share voice 0 / intent FigurationCarrier, so they merge
      into ONE (0, "FigurationCarrier") group of 128 notes; the V1 bass is its own
      (1, "FigurationCarrier") group of 64 notes.

      FUGUE (PRELUDE_AND_FUGUE_FUGUE_ENTRIES). 16 quarter-notes per entry: note n -> bar =
      first_bar + n // 4, beat = n % 4, pitch = subj_pat[n] + semis where subj_pat
      = FUGUE_COMPLETE_SUBJECTS[(seed // 4) % 5]. Real answer = subject - 5 (-P4),
      re-entry = subject - 12 (-P8), stretto leader = subject verbatim.
        - V0 SubjectCarrier bars 8-11  (+0)  -> joins (0, "SubjectCarrier").
        - V1 AnswerCarrier  bars 12-15 (-5)  -> (1, "AnswerCarrier").
        - V2 SubjectCarrier bars 16-19 (-12) -> (2, "SubjectCarrier").
        - V0 SubjectCarrier bars 20-23 (+0)  -> joins (0, "SubjectCarrier").
      The two V0 SubjectCarrier windows (bars 8-11 + bars 20-23) MERGE into one
      (0, "SubjectCarrier") group of 32 notes.

    Total = 128 + 64 + 32 + 16 + 16 = 256 notes. subj_a needs the raw seed, so
    fixture must carry "seed".

    @param fixture fixture_for_seed() output (with injected "seed") for the seed.
    @return {(0, "FigurationCarrier"): [(start_tick, pitch)] (128 notes),
             (1, "FigurationCarrier"): [(start_tick, pitch)] (64 notes),
             (0, "SubjectCarrier"):    [(start_tick, pitch)] (32 notes),
             (1, "AnswerCarrier"):     [(start_tick, pitch)] (16 notes),
             (2, "SubjectCarrier"):    [(start_tick, pitch)] (16 notes)} sorted.
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    sixteenth = ticks_per_beat // 4  # 120 ticks.
    eighth = ticks_per_beat // 2     # 240 ticks.
    cycle_bars = 4
    offset = fixture["harm_idx"] % 4

    def append_figuration_bar(
        dst: list[tuple[int, int]], bar: int, base_midi: int, notes_per_beat: int
    ) -> None:
        cyc = bar % cycle_bars
        root_pc = PRELUDE_AND_FUGUE_BAR_ROOT[cyc]
        third = 3 if PRELUDE_AND_FUGUE_BAR_MINOR[cyc] else 4
        triad = {root_pc % 12, (root_pc + third) % 12, (root_pc + 7) % 12}
        # Per-bar chord-tone anchor: root in base_midi's octave, +offset degrees,
        # snapped UP to a chord tone. EVERY beat restarts from this anchor.
        anchor = organPrelude_scale_up(base_midi + root_pc, offset)
        while anchor % 12 not in triad:
            anchor += 1
        step = sixteenth if notes_per_beat == 4 else eighth
        for beat in range(4):
            for sub in range(notes_per_beat):
                tick = bar * ticks_per_bar + beat * ticks_per_beat + sub * step
                dst.append((tick, organPrelude_scale_up(anchor, sub)))

    figuration_v0: list[tuple[int, int]] = []
    figuration_v1: list[tuple[int, int]] = []
    for voice, first_bar, last_bar, base_midi, npb, _pedal in PRELUDE_AND_FUGUE_PRELUDE_SECTIONS:
        dst = figuration_v0 if voice == 0 else figuration_v1
        for bar in range(first_bar, last_bar + 1):
            append_figuration_bar(dst, bar, base_midi, npb)
    figuration_v0.sort()
    figuration_v1.sort()

    # Fugue entries: 16 quarter-notes each. The two V0 SubjectCarrier windows
    # merge into one group; group by (voice, intent).
    subj_pat = FUGUE_COMPLETE_SUBJECTS[(fixture["seed"] // 4) % 5]
    groups: dict[tuple[int, str], list[tuple[int, int]]] = {}
    for voice, first_bar, intent, semis in PRELUDE_AND_FUGUE_FUGUE_ENTRIES:
        seq = groups.setdefault((voice, intent), [])
        for n in range(16):
            bar = first_bar + n // 4
            beat = n % 4
            tick = bar * ticks_per_bar + beat * ticks_per_beat
            seq.append((tick, subj_pat[n] + semis))
    for key in groups:
        groups[key].sort()

    out: dict[tuple[int, str], list[tuple[int, int]]] = {
        (0, "FigurationCarrier"): figuration_v0,
        (1, "FigurationCarrier"): figuration_v1,
    }
    out.update(groups)
    return out


def expected_goldberg_sequence(
    fixture: dict[str, Any],
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict the dedicated Goldberg bass + variation carrier note streams.

    Mirrors buildGoldbergVariationsFixture in src/composer/harness_fixture.cpp: 20 bars =
    an Aria (bars 0-3) + four variations (bars 4-19), C major. V2
    carries the immutable 4-bar Goldberg-style bass (GOLDBERG_VARIATIONS_GROUND, one
    whole-note per bar) tiled 5x (GoldbergBassCarrier). V0 carries five 4-bar
    blocks (GoldbergVariationCarrier, GOLDBERG_VARIATIONS_BLOCK_SPEC): the Aria (block 0) plus
    four rising-density variations. The is_climax flag on block 4 changes only
    the ClimaxPlaced bit (not pitch / tick), so it is not modeled here.

    Each variation bar starts from a chord-tone anchor:
      chord_start = 72 + ((root_pc - (72 % 12) + 12) % 12)  (= 72/77/79/81 for the
        I/IV/V/vi roots; all already C-major scale tones, so the C++
        organPreludeScaleUp(chord_start, 0) is a no-op);
      start = organPrelude_scale_up(chord_start, offset).
    The two block kinds differ:
      - Aria (block 0, m=2): two half-notes/bar -> note0 = (bar_start, start),
        note1 = (bar_start + 960, organPrelude_scale_up(start, 1)). This is NOT a
        scalar wave (the special sarabande-like layout).
      - Blocks 1-4 (uniform): npb = m / 4 (1 / 2 / 2 / 4); step = 480 if npb==1
        else 240 if npb==2 else 120. Build the wave (ascending m/2+1 degrees
        mirrored back down, dropping the duplicated peak), then emit beat 0..3 x
        sub 0..npb-1 -> pitch = wave[(beat*npb + sub) % len(wave)].

    Per-voice note counts: V0 = 8 + 16 + 32 + 32 + 64 = 152; V1 = 20. Total 172.
    The offset is seed % 4, which equals fixture["harm_idx"]. Reuses the OrganPrelude
    C-major scale walk verbatim.

    @param fixture fixture_for_seed() output for the seed under test.
    @return {(2, "GoldbergBassCarrier"): [(start_tick, pitch)] (20 notes),
             (0, "GoldbergVariationCarrier"): [(start_tick, pitch)] (152 notes)}
            sorted by tick.
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    half = ticks_per_beat * 2  # 960.
    cycle_bars = 4
    blocks = 5
    offset = fixture["harm_idx"] % 4

    # GoldbergBassCarrier: the immutable 4-bar Goldberg bass period-tiled 5x.
    ground: list[tuple[int, int]] = []
    cycles = (cycle_bars * blocks) // cycle_bars  # 5.
    for cycle in range(cycles):
        for bar in range(cycle_bars):
            tick = (cycle * cycle_bars + bar) * ticks_per_bar
            ground.append((tick, GOLDBERG_VARIATIONS_GROUND[bar]))
    ground.sort()

    # GoldbergVariationCarrier: the Aria block + four rising-density variation blocks.
    variation: list[tuple[int, int]] = []
    for block in range(blocks):
        _density, m, base_midi, _is_climax = GOLDBERG_VARIATIONS_BLOCK_SPEC[block]
        block_base = block * cycle_bars * ticks_per_bar
        for bar in range(cycle_bars):
            root_pc = GOLDBERG_VARIATIONS_BAR_ROOT[bar]
            # Snap base_midi UP to the bar chord root in base_midi's octave, then
            # `offset` C-major degrees up. (organPrelude_scale_up(chord_start, 0) is a
            # no-op since chord_start is already a scale tone.)
            chord_start = base_midi + ((root_pc - (base_midi % 12) + 12) % 12)
            start = organPrelude_scale_up(chord_start, offset)
            bar_start = block_base + bar * ticks_per_bar
            if block == 0:
                # Aria: half note (start) then half note (start + 1 degree).
                variation.append((bar_start, start))
                variation.append((bar_start + half, organPrelude_scale_up(start, 1)))
                continue
            # Uniform-subdivision scalar wave.
            npb = m // 4
            step = 480 if npb == 1 else (240 if npb == 2 else 120)
            wave = [organPrelude_scale_up(start, i) for i in range(m // 2 + 1)]
            wave += wave[-2::-1]
            for beat in range(4):
                for sub in range(npb):
                    tick = bar_start + beat * ticks_per_beat + sub * step
                    idx = beat * npb + sub
                    variation.append((tick, wave[idx % len(wave)]))
    variation.sort()
    return {
        (2, "GoldbergBassCarrier"): ground,
        (0, "GoldbergVariationCarrier"): variation,
    }


def expected_carrier_sequences(
    phase: str, fixture: dict[str, Any]
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict (voice, intent) -> [(start_tick, pitch)] for a seed fixture.

    NOTE (structural_ok scope): this predictor validates ONLY the
    exposition entries (V0 subject, V1 answer, V2 re-entry) plus the V0
    stretto leader for FugueComplete. Counterline, development, NCT and rhythm
    carriers (~90% of the notes in a full fugue) are NOT modeled here, so
    structural_ok is a necessary, not sufficient, structural guarantee.

    Mirrors V0 SubjectCarrier / V1 AnswerCarrier / V2 SubjectCarrier
    re-entry assembly in src/composer/harness_fixture.cpp.

    This is fully layout-driven: there is no per-phase hard-coded carrier
    table, so any phase registered in PHASE_LAYOUT (including FugueComplete) reuses
    the generic exposition derivation below. FugueComplete carries the standard
    three exposition entries (subject_bars=4, with_answer, with_third_entry)
    and intentionally omits the "development" branch, so no 42-bar-specific
    sequence is hand-coded here (which would be brittle); the structural check
    therefore validates only the three exposition entries for FugueComplete.

    CelloPrelude has no exposition; it dispatches to the verbatim-arpeggio predictor.
    Chaconne has no exposition; it dispatches to the ground/variation predictor.
    OrganPrelude has no exposition; it dispatches to the figuration predictor.
    OrganToccata has no exposition; it dispatches to the toccata predictor.
    ChoralePrelude has no exposition; it dispatches to the chorale predictor.
    Passacaglia has no exposition; it dispatches to the passacaglia predictor.
    TrioSonata has no exposition; it dispatches to the trio-sonata predictor.
    Fantasia has no exposition; it dispatches to the fantasia predictor.
    KeyboardSuite has no exposition; it dispatches to the keyboard-suite predictor.
    """
    if phase == "PreludeAndFugue":
        return expected_wtc_pair_sequence(fixture)
    if phase == "GoldbergVariations":
        return expected_goldberg_sequence(fixture)
    if phase == "CelloPrelude":
        return expected_arpeggio_sequence(fixture)
    if phase == "Chaconne":
        return expected_ground_sequence(fixture)
    if phase == "OrganPrelude":
        return expected_figuration_sequence(fixture)
    if phase == "OrganToccata":
        return expected_toccata_sequence(fixture)
    if phase == "ChoralePrelude":
        return expected_chorale_sequence(fixture)
    if phase == "Passacaglia":
        return expected_passacaglia_sequence(fixture)
    if phase == "TrioSonata":
        return expected_trio_sequence(fixture)
    if phase == "Fantasia":
        return expected_fantasia_sequence(fixture)
    if phase == "KeyboardSuite":
        return expected_suite_sequence(fixture)
    layout = PHASE_LAYOUT[phase]
    subject_bars = layout["subject_bars"]
    subject_blocks = subject_bars // 4
    subj_a = fixture["subj_idx"]
    ticks_per_bar = 1920
    ticks_per_beat = 480
    patterns = FUGUE_COMPLETE_SUBJECTS if phase == "FugueComplete" else SUBJECT_PATTERNS
    out: dict[tuple[int, str], list[tuple[int, int]]] = {}

    v0_seq: list[tuple[int, int]] = []
    for blk in range(subject_blocks):
        pattern = patterns[(subj_a + blk) % 5]
        for n in range(16):
            tick = (blk * 4 + n // 4) * ticks_per_bar + (n % 4) * ticks_per_beat
            v0_seq.append((tick, pattern[n]))
    # A fugue may restate the subject verbatim in V0 as a stretto leader;
    # that span also carries intent SubjectCarrier, so it joins the
    # (0, SubjectCarrier) group in extract_carrier_sequences and the
    # expected sequence must include it. FugueDevelopment puts the leader at bar 20
    # (via the "development" flag); FugueComplete declares "stretto_leader_bar".
    leader_base_bar = None
    if layout.get("development", False):
        leader_base_bar = 20
    elif layout.get("stretto_leader_bar") is not None:
        leader_base_bar = layout["stretto_leader_bar"]
    if leader_base_bar is not None:
        pattern = patterns[subj_a]
        for n in range(16):
            tick = (leader_base_bar + n // 4) * ticks_per_bar + (n % 4) * ticks_per_beat
            v0_seq.append((tick, pattern[n]))
    out[(0, "SubjectCarrier")] = v0_seq

    if layout["with_answer"]:
        pattern = patterns[subj_a]
        v1_seq: list[tuple[int, int]] = []
        use_tonal = layout.get("tonal_answer", False)
        tonic_pc = 0  # harness fixes C major / tonic_pc = 0
        dom_pc = 7
        head_length = 4
        for n in range(16):
            tick = (subject_bars + n // 4) * ticks_per_bar + (n % 4) * ticks_per_beat
            base = pattern[n] - 5
            if use_tonal and n < head_length:
                src_pc = pattern[n] % 12
                if src_pc == tonic_pc:
                    target_pc = dom_pc
                elif src_pc == dom_pc:
                    target_pc = tonic_pc
                else:
                    v1_seq.append((tick, base))
                    continue
                # closestPcOctaveTo(target_pc, anchor=base)
                anchor_pc = base % 12
                delta = (target_pc - anchor_pc + 24) % 12
                if delta > 6:
                    delta -= 12
                pitch = max(0, min(127, base + delta))
                v1_seq.append((tick, pitch))
            else:
                v1_seq.append((tick, base))
        out[(1, "AnswerCarrier")] = v1_seq

    if layout["with_third_entry"]:
        pattern = patterns[subj_a]
        v2_seq: list[tuple[int, int]] = []
        for n in range(16):
            tick = (2 * subject_bars + n // 4) * ticks_per_bar + (n % 4) * ticks_per_beat
            v2_seq.append((tick, pattern[n] - 12))
        out[(2, "SubjectCarrier")] = v2_seq

    return out


def extract_carrier_sequences(
    generated_json: Path, provenance_json: Path
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Extract observed (voice, intent) -> sorted [(start_tick, pitch)]."""
    with generated_json.open(encoding="utf-8") as f:
        gen = json.load(f)
    with provenance_json.open(encoding="utf-8") as f:
        prov = json.load(f)
    gen_notes = gen.get("notes", [])
    prov_notes = prov.get("notes", [])
    by_key: dict[tuple[int, str], list[tuple[int, int]]] = {}
    for g, p in zip(gen_notes, prov_notes):
        intent = p.get("voice_intent")
        if intent not in (
            "SubjectCarrier",
            "AnswerCarrier",
            "ArpeggioFlow",
            "GroundCarrier",
            "VariationCarrier",
            "FigurationCarrier",
            "ToccataCarrier",
            "CantusFirmusCarrier",
            "PassacagliaGround",
            "PassacagliaVariation",
            "TrioVoiceCarrier",
            "FantasiaCarrier",
            "GoldbergBassCarrier",
            "GoldbergVariationCarrier",
            "GoldbergInnerVoiceCarrier",
        ):
            continue
        voice = int(g.get("voice", -1))
        by_key.setdefault((voice, intent), []).append(
            (int(g["start_tick"]), int(g["pitch"]))
        )
    for key in by_key:
        by_key[key].sort()
    return by_key


def first_difference(expected: list[tuple[int, int]],
                     actual: list[tuple[int, int]]) -> dict[str, Any]:
    if len(expected) != len(actual):
        return {"length_mismatch": {"expected": len(expected), "actual": len(actual)}}
    for i, (e, a) in enumerate(zip(expected, actual)):
        if e != a:
            return {"index": i, "expected": e, "actual": a}
    return {}


def structural_check(generated_json: Path, provenance_json: Path,
                     phase: str, fixture: dict[str, Any]) -> dict[str, Any]:
    """Verify carrier voices replay the fixture catalog byte-identically."""
    expected = expected_carrier_sequences(phase, fixture)
    try:
        actual = extract_carrier_sequences(generated_json, provenance_json)
    except (OSError, json.JSONDecodeError) as exc:
        return {"ok": False, "error": str(exc)}

    result: dict[str, Any] = {}
    if phase == "CelloPrelude":
        keys = [((0, "ArpeggioFlow"), "arpeggio_ok", "arpeggio_diff")]
    elif phase == "Chaconne":
        keys = [
            ((1, "GroundCarrier"), "ground_ok", "ground_diff"),
            ((0, "VariationCarrier"), "variation_ok", "variation_diff"),
        ]
    elif phase == "OrganPrelude":
        keys = [
            ((0, "FigurationCarrier"), "figuration_ok", "figuration_diff"),
            ((1, "FigurationCarrier"), "bass_ok", "bass_diff"),
        ]
    elif phase == "OrganToccata":
        keys = [
            ((0, "ToccataCarrier"), "toccata_ok", "toccata_diff"),
        ]
    elif phase == "ChoralePrelude":
        keys = [
            ((0, "FigurationCarrier"), "figuration_ok", "figuration_diff"),
            ((1, "CantusFirmusCarrier"), "cantus_firmus_ok", "cantus_firmus_diff"),
        ]
    elif phase == "Passacaglia":
        keys = [
            ((1, "PassacagliaGround"), "ground_ok", "ground_diff"),
            ((0, "PassacagliaVariation"), "variation_ok", "variation_diff"),
        ]
    elif phase == "TrioSonata":
        keys = [
            ((0, "TrioVoiceCarrier"), "rh_ok", "rh_diff"),
            ((1, "TrioVoiceCarrier"), "lh_ok", "lh_diff"),
            ((2, "TrioVoiceCarrier"), "pedal_ok", "pedal_diff"),
        ]
    elif phase == "Fantasia":
        keys = [
            ((0, "FantasiaCarrier"), "fantasia_ok", "fantasia_diff"),
        ]
    elif phase == "KeyboardSuite":
        keys = [
            ((0, "FigurationCarrier"), "figuration_ok", "figuration_diff"),
            ((0, "FantasiaCarrier"), "fantasia_ok", "fantasia_diff"),
            ((1, "GroundCarrier"), "ground_ok", "ground_diff"),
        ]
    elif phase == "GoldbergVariations":
        keys = [
            ((2, "GoldbergBassCarrier"), "ground_ok", "ground_diff"),
            ((0, "GoldbergVariationCarrier"), "variation_ok", "variation_diff"),
        ]
    elif phase == "PreludeAndFugue":
        keys = [
            ((0, "FigurationCarrier"), "figuration_ok", "figuration_diff"),
            ((1, "FigurationCarrier"), "bass_ok", "bass_diff"),
            ((0, "SubjectCarrier"), "subject_ok", "subject_diff"),
            ((1, "AnswerCarrier"), "answer_ok", "answer_diff"),
            ((2, "SubjectCarrier"), "v2_subject_ok", "v2_subject_diff"),
        ]
    else:
        keys = [
            ((0, "SubjectCarrier"), "subject_ok", "subject_diff"),
            ((1, "AnswerCarrier"), "answer_ok", "answer_diff"),
            ((2, "SubjectCarrier"), "v2_subject_ok", "v2_subject_diff"),
        ]
    all_ok = True
    for key, ok_field, diff_field in keys:
        if key not in expected:
            continue
        exp_seq = expected[key]
        act_seq = actual.get(key, [])
        passed = exp_seq == act_seq
        result[ok_field] = passed
        if not passed:
            result[diff_field] = first_difference(exp_seq, act_seq)
            all_ok = False
    result["ok"] = all_ok
    return result
