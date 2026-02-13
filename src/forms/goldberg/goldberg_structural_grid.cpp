// Goldberg Variations 32-bar structural grid implementation.

#include "forms/goldberg/goldberg_structural_grid.h"

#include <algorithm>

namespace bach {

namespace {

// G major bass pitches in octave 3 (MIDI).
constexpr uint8_t kBassG = 55;   // G3
constexpr uint8_t kBassFs = 54;  // F#3
constexpr uint8_t kBassE = 52;   // E3
constexpr uint8_t kBassD = 50;   // D3
constexpr uint8_t kBassCs = 49;  // C#3
constexpr uint8_t kBassC = 48;   // C3
constexpr uint8_t kBassB = 47;   // B2
constexpr uint8_t kBassA = 45;   // A2

/// @brief Uniform tension profile where all dimensions share the same value.
constexpr TensionProfile uniformTension(float val) {
  return {val, val, val, val};
}

/// @brief Clamp bar index to [0, 31].
constexpr int clampBar(int bar) {
  return bar < 0 ? 0 : (bar > 31 ? 31 : bar);
}

/// @brief Build a StructuralBarInfo entry.
StructuralBarInfo makeBarInfo(
    uint8_t primary_pitch,
    std::optional<uint8_t> resolution_pitch,
    bool is_descending,
    uint8_t scale_idx,
    HarmonicFunction function,
    ChordDegree degree,
    BarInPhrase bip,
    std::optional<CadenceType> cadence,
    StructuralLevel level,
    float tension_val,
    uint8_t phrase_group) {
  StructuralBarInfo info{};
  info.bass_motion.primary_pitch = primary_pitch;
  info.bass_motion.resolution_pitch = resolution_pitch;
  info.bass_motion.is_descending_scale_member = is_descending;
  info.bass_motion.scale_degree_index = scale_idx;
  info.function = function;
  info.chord_degree = degree;
  info.bar_in_phrase = bip;
  info.phrase_pos = toPhrasePosition(bip);
  info.cadence = cadence;
  info.is_structural_bar = (bip == 4);
  info.phrase_group = phrase_group;
  info.highest_level = level;
  info.tension = uniformTension(tension_val);
  return info;
}

}  // namespace

// ---------------------------------------------------------------------------
// Factory methods
// ---------------------------------------------------------------------------

GoldbergStructuralGrid GoldbergStructuralGrid::createMajor() {
  GoldbergStructuralGrid grid;

  using HF = HarmonicFunction;
  using CD = ChordDegree;
  using CT = CadenceType;
  using SL = StructuralLevel;

  // Shorthand for no cadence and no resolution.
  constexpr auto kNoCad = std::optional<CadenceType>{};
  constexpr auto kNoRes = std::optional<uint8_t>{};

  // Bar 1-8: First phrase group (descending bass scale G-F#-E-D-C-B-A-G).
  // Bar 1: I, G(55), BIP=1, Opening, no cadence, BarLevel, T=0.1
  grid.bars_[0] = makeBarInfo(
      kBassG, kNoRes, true, 0, HF::Tonic, CD::I,
      1, kNoCad, SL::BarLevel, 0.1f, 0);

  // Bar 2: V6, F#(54), BIP=2, Expansion, no cadence, BarLevel, T=0.2
  grid.bars_[1] = makeBarInfo(
      kBassFs, kNoRes, true, 1, HF::Dominant, CD::V,
      2, kNoCad, SL::BarLevel, 0.2f, 0);

  // Bar 3: vi, E(52), BIP=3, Intensification, no cadence, BarLevel, T=0.3
  grid.bars_[2] = makeBarInfo(
      kBassE, kNoRes, true, 2, HF::Tonic, CD::vi,
      3, kNoCad, SL::BarLevel, 0.3f, 0);

  // Bar 4: iii->V, D(50), BIP=4, Cadence, Half, Phrase4, T=0.4
  grid.bars_[3] = makeBarInfo(
      kBassD, kNoRes, true, 3, HF::Mediant, CD::iii,
      4, CT::Half, SL::Phrase4, 0.4f, 0);

  // Bar 5: IV, C(48), BIP=1, Opening, no cadence, BarLevel, T=0.3
  grid.bars_[4] = makeBarInfo(
      kBassC, kNoRes, true, 4, HF::Subdominant, CD::IV,
      1, kNoCad, SL::BarLevel, 0.3f, 1);

  // Bar 6: I6, B(47), BIP=2, Expansion, no cadence, BarLevel, T=0.4
  grid.bars_[5] = makeBarInfo(
      kBassB, kNoRes, true, 5, HF::Tonic, CD::I,
      2, kNoCad, SL::BarLevel, 0.4f, 1);

  // Bar 7: ii->V, A(45), BIP=3, Intensification, no cadence, BarLevel, T=0.5
  grid.bars_[6] = makeBarInfo(
      kBassA, kNoRes, true, 6, HF::Subdominant, CD::ii,
      3, kNoCad, SL::BarLevel, 0.5f, 1);

  // Bar 8: I, B(47)->G(55), BIP=4, Cadence, Perfect, Phrase8, T=0.2
  grid.bars_[7] = makeBarInfo(
      kBassB, kBassG, true, 7, HF::Tonic, CD::I,
      4, CT::Perfect, SL::Phrase8, 0.2f, 1);

  // Bar 9-16: Second phrase group (no descending scale).
  // Bar 9: ii, A(45), BIP=1, Opening, no cadence, BarLevel, T=0.3
  grid.bars_[8] = makeBarInfo(
      kBassA, kNoRes, false, 0, HF::Subdominant, CD::ii,
      1, kNoCad, SL::BarLevel, 0.3f, 2);

  // Bar 10: V, D(50), BIP=2, Expansion, no cadence, BarLevel, T=0.5
  grid.bars_[9] = makeBarInfo(
      kBassD, kNoRes, false, 0, HF::Dominant, CD::V,
      2, kNoCad, SL::BarLevel, 0.5f, 2);

  // Bar 11: vi, E(52), BIP=3, Intensification, no cadence, BarLevel, T=0.6
  grid.bars_[10] = makeBarInfo(
      kBassE, kNoRes, false, 0, HF::Tonic, CD::vi,
      3, kNoCad, SL::BarLevel, 0.6f, 2);

  // Bar 12: V/vi, B(47), BIP=4, Cadence, Half, Phrase4, T=0.5
  grid.bars_[11] = makeBarInfo(
      kBassB, kNoRes, false, 0, HF::Applied, CD::V_of_vi,
      4, CT::Half, SL::Phrase4, 0.5f, 2);

  // Bar 13: IV, C(48), BIP=1, Opening, no cadence, BarLevel, T=0.4
  grid.bars_[12] = makeBarInfo(
      kBassC, kNoRes, false, 0, HF::Subdominant, CD::IV,
      1, kNoCad, SL::BarLevel, 0.4f, 3);

  // Bar 14: V, D(50), BIP=2, Expansion, no cadence, BarLevel, T=0.6
  grid.bars_[13] = makeBarInfo(
      kBassD, kNoRes, false, 0, HF::Dominant, CD::V,
      2, kNoCad, SL::BarLevel, 0.6f, 3);

  // Bar 15: I, G(55), BIP=3, Intensification, no cadence, BarLevel, T=0.7
  grid.bars_[14] = makeBarInfo(
      kBassG, kNoRes, false, 0, HF::Tonic, CD::I,
      3, kNoCad, SL::BarLevel, 0.7f, 3);

  // Bar 16: V->I, D(50)->G(55), BIP=4, Cadence, Half, Section16, T=0.5
  grid.bars_[15] = makeBarInfo(
      kBassD, kBassG, false, 0, HF::Dominant, CD::V,
      4, CT::Half, SL::Section16, 0.5f, 3);

  // Bar 17-24: Third phrase group (second half begins).
  // Bar 17: I, G(55), BIP=1, Opening, no cadence, BarLevel, T=0.2
  grid.bars_[16] = makeBarInfo(
      kBassG, kNoRes, false, 0, HF::Tonic, CD::I,
      1, kNoCad, SL::BarLevel, 0.2f, 4);

  // Bar 18: viio6, F#(54), BIP=2, Expansion, no cadence, BarLevel, T=0.3
  grid.bars_[17] = makeBarInfo(
      kBassFs, kNoRes, false, 0, HF::Dominant, CD::viiDim,
      2, kNoCad, SL::BarLevel, 0.3f, 4);

  // Bar 19: ii, A(45), BIP=3, Intensification, no cadence, BarLevel, T=0.4
  grid.bars_[18] = makeBarInfo(
      kBassA, kNoRes, false, 0, HF::Subdominant, CD::ii,
      3, kNoCad, SL::BarLevel, 0.4f, 4);

  // Bar 20: V/V, A(45)->D(50), BIP=4, Cadence, Half, Phrase4, T=0.5
  grid.bars_[19] = makeBarInfo(
      kBassA, kBassD, false, 0, HF::Applied, CD::V_of_V,
      4, CT::Half, SL::Phrase4, 0.5f, 4);

  // Bar 21: V, D(50), BIP=1, Opening, no cadence, BarLevel, T=0.3
  grid.bars_[20] = makeBarInfo(
      kBassD, kNoRes, false, 0, HF::Dominant, CD::V,
      1, kNoCad, SL::BarLevel, 0.3f, 5);

  // Bar 22: I6, B(47), BIP=2, Expansion, no cadence, BarLevel, T=0.4
  grid.bars_[21] = makeBarInfo(
      kBassB, kNoRes, false, 0, HF::Tonic, CD::I,
      2, kNoCad, SL::BarLevel, 0.4f, 5);

  // Bar 23: ii7, A(45), BIP=3, Intensification, no cadence, BarLevel, T=0.5
  grid.bars_[22] = makeBarInfo(
      kBassA, kNoRes, false, 0, HF::Subdominant, CD::ii,
      3, kNoCad, SL::BarLevel, 0.5f, 5);

  // Bar 24: V->I, B(47)->G(55), BIP=4, Cadence, Perfect, Phrase8, T=0.3
  grid.bars_[23] = makeBarInfo(
      kBassB, kBassG, false, 0, HF::Dominant, CD::V,
      4, CT::Perfect, SL::Phrase8, 0.3f, 5);

  // Bar 25-32: Fourth phrase group (final buildup and resolution).
  // Bar 25: vi, E(52), BIP=1, Opening, no cadence, BarLevel, T=0.4
  grid.bars_[24] = makeBarInfo(
      kBassE, kNoRes, false, 0, HF::Tonic, CD::vi,
      1, kNoCad, SL::BarLevel, 0.4f, 6);

  // Bar 26: IV, C(48), BIP=2, Expansion, no cadence, BarLevel, T=0.5
  grid.bars_[25] = makeBarInfo(
      kBassC, kNoRes, false, 0, HF::Subdominant, CD::IV,
      2, kNoCad, SL::BarLevel, 0.5f, 6);

  // Bar 27: viio/V, C#(49)->D(50), BIP=3, Intensification, no cadence, BarLevel, T=0.6
  grid.bars_[26] = makeBarInfo(
      kBassCs, kBassD, false, 0, HF::Dominant, CD::viiDim,
      3, kNoCad, SL::BarLevel, 0.6f, 6);

  // Bar 28: V, D(50), BIP=4, Cadence, Half, Phrase4, T=0.7
  grid.bars_[27] = makeBarInfo(
      kBassD, kNoRes, false, 0, HF::Dominant, CD::V,
      4, CT::Half, SL::Phrase4, 0.7f, 6);

  // Bar 29: I6/4, G(55), BIP=1, Opening, no cadence, BarLevel, T=0.8
  grid.bars_[28] = makeBarInfo(
      kBassG, kNoRes, false, 0, HF::Tonic, CD::I,
      1, kNoCad, SL::BarLevel, 0.8f, 7);

  // Bar 30: V7, D(50), BIP=2, Expansion, no cadence, BarLevel, T=0.85
  grid.bars_[29] = makeBarInfo(
      kBassD, kNoRes, false, 0, HF::Dominant, CD::V,
      2, kNoCad, SL::BarLevel, 0.85f, 7);

  // Bar 31: V->I, D(50)->G(55), BIP=3, Intensification, no cadence, BarLevel, T=0.9
  grid.bars_[30] = makeBarInfo(
      kBassD, kBassG, false, 0, HF::Dominant, CD::V,
      3, kNoCad, SL::BarLevel, 0.9f, 7);

  // Bar 32: I, G(55), BIP=4, Cadence, Perfect, Global32, T=0.0
  grid.bars_[31] = makeBarInfo(
      kBassG, kNoRes, false, 0, HF::Tonic, CD::I,
      4, CT::Perfect, SL::Global32, 0.0f, 7);

  return grid;
}

GoldbergStructuralGrid GoldbergStructuralGrid::createMinor(MinorModeProfile /*profile*/) {
  // Placeholder: return major grid. Minor-mode adaptation is a future task.
  return createMajor();
}

// ---------------------------------------------------------------------------
// Bar access
// ---------------------------------------------------------------------------

const StructuralBarInfo& GoldbergStructuralGrid::getBar(int bar) const {
  return bars_[static_cast<size_t>(clampBar(bar))];
}

// ---------------------------------------------------------------------------
// Bass queries
// ---------------------------------------------------------------------------

const StructuralBassMotion& GoldbergStructuralGrid::getBassMotion(int bar) const {
  return getBar(bar).bass_motion;
}

uint8_t GoldbergStructuralGrid::getStructuralBassPitch(int bar) const {
  return getBar(bar).bass_motion.primary_pitch;
}

bool GoldbergStructuralGrid::hasBassResolution(int bar) const {
  return getBar(bar).bass_motion.resolution_pitch.has_value();
}

bool GoldbergStructuralGrid::isCadenceBar(int bar) const {
  return getBar(bar).cadence.has_value();
}

// ---------------------------------------------------------------------------
// Phrase queries
// ---------------------------------------------------------------------------

BarInPhrase GoldbergStructuralGrid::getBarInPhrase(int bar) const {
  return getBar(bar).bar_in_phrase;
}

PhrasePosition GoldbergStructuralGrid::getPhrasePosition(int bar) const {
  return getBar(bar).phrase_pos;
}

std::optional<CadenceType> GoldbergStructuralGrid::getCadenceType(int bar) const {
  return getBar(bar).cadence;
}

// ---------------------------------------------------------------------------
// Structural level queries
// ---------------------------------------------------------------------------

StructuralLevel GoldbergStructuralGrid::getStructuralLevel(int bar) const {
  return getBar(bar).highest_level;
}

const TensionProfile& GoldbergStructuralGrid::getTension(int bar) const {
  return getBar(bar).tension;
}

float GoldbergStructuralGrid::getAggregateTension(int bar) const {
  return getBar(bar).tension.aggregate();
}

bool GoldbergStructuralGrid::isSectionBoundary(int bar) const {
  auto level = getStructuralLevel(bar);
  return level >= StructuralLevel::Phrase8;
}

// ---------------------------------------------------------------------------
// Hierarchical views
// ---------------------------------------------------------------------------

GoldbergStructuralGrid::Phrase4View GoldbergStructuralGrid::getPhrase4(
    int phrase_index) const {
  int clamped = std::clamp(phrase_index, 0, 7);
  int start = clamped * 4;

  Phrase4View view{};
  view.start_bar = start;
  view.cadence = bars_[static_cast<size_t>(start + 3)].cadence;

  // Find peak tension across the 4 bars.
  float peak_val = 0.0f;
  int peak_idx = start;
  for (int idx = start; idx < start + 4; ++idx) {
    float agg = bars_[static_cast<size_t>(idx)].tension.aggregate();
    if (agg > peak_val) {
      peak_val = agg;
      peak_idx = idx;
    }
  }
  view.peak_tension = bars_[static_cast<size_t>(peak_idx)].tension;

  return view;
}

GoldbergStructuralGrid::Phrase8View GoldbergStructuralGrid::getPhrase8(
    int section_index) const {
  int clamped = std::clamp(section_index, 0, 3);
  int start = clamped * 8;

  Phrase8View view{};
  view.start_bar = start;
  view.final_cadence = bars_[static_cast<size_t>(start + 7)].cadence;
  view.phrases[0] = getPhrase4(clamped * 2);
  view.phrases[1] = getPhrase4(clamped * 2 + 1);

  return view;
}

GoldbergStructuralGrid::Section16View GoldbergStructuralGrid::getSection16(int half) const {
  int clamped = std::clamp(half, 0, 1);
  int start = clamped * 16;

  Section16View view{};
  view.start_bar = start;
  view.section_cadence = bars_[static_cast<size_t>(start + 15)].cadence;
  view.phrases[0] = getPhrase8(clamped * 2);
  view.phrases[1] = getPhrase8(clamped * 2 + 1);

  return view;
}

// ---------------------------------------------------------------------------
// Timeline conversion
// ---------------------------------------------------------------------------

HarmonicTimeline GoldbergStructuralGrid::toTimeline(const KeySignature& key,
                                                     const TimeSignature& time_sig) const {
  HarmonicTimeline timeline;
  Tick ticks_per_bar = time_sig.ticksPerBar();

  for (int idx = 0; idx < 32; ++idx) {
    const auto& bar_info = bars_[static_cast<size_t>(idx)];

    HarmonicEvent event{};
    event.tick = static_cast<Tick>(idx) * ticks_per_bar;
    event.end_tick = static_cast<Tick>(idx + 1) * ticks_per_bar;
    event.key = key.tonic;
    event.is_minor = key.is_minor;

    // Build chord from the grid's chord degree.
    event.chord.degree = bar_info.chord_degree;
    event.chord.quality = key.is_minor ? minorKeyQuality(bar_info.chord_degree)
                                       : majorKeyQuality(bar_info.chord_degree);
    // Root pitch: use bass pitch as proxy (the grid is bass-driven).
    event.chord.root_pitch = bar_info.bass_motion.primary_pitch;
    event.chord.inversion = 0;

    event.bass_pitch = bar_info.bass_motion.primary_pitch;

    // Weight from bar-in-phrase: structural bars get higher weight.
    event.weight = bar_info.is_structural_bar ? 1.0f : 0.75f;
    event.is_immutable = true;  // Grid events are structural design values.

    timeline.addEvent(event);
  }

  return timeline;
}

}  // namespace bach
