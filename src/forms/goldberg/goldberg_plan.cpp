// Goldberg Variations plan implementation.

#include "forms/goldberg/goldberg_plan.h"

namespace bach {

const char* goldbergVariationTypeToString(GoldbergVariationType type) {
  switch (type) {
    case GoldbergVariationType::Aria:           return "Aria";
    case GoldbergVariationType::Ornamental:     return "Ornamental";
    case GoldbergVariationType::Invention:      return "Invention";
    case GoldbergVariationType::Fughetta:       return "Fughetta";
    case GoldbergVariationType::Canon:          return "Canon";
    case GoldbergVariationType::Dance:          return "Dance";
    case GoldbergVariationType::FrenchOverture: return "FrenchOverture";
    case GoldbergVariationType::HandCrossing:   return "HandCrossing";
    case GoldbergVariationType::Toccata:        return "Toccata";
    case GoldbergVariationType::TrillEtude:     return "TrillEtude";
    case GoldbergVariationType::ScalePassage:   return "ScalePassage";
    case GoldbergVariationType::BravuraChordal: return "BravuraChordal";
    case GoldbergVariationType::BlackPearl:     return "BlackPearl";
    case GoldbergVariationType::AllaBreveFugal: return "AllaBreveFugal";
    case GoldbergVariationType::Quodlibet:      return "Quodlibet";
  }
  return "Unknown";
}

namespace {

// Key signatures used in the Goldberg Variations.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr KeySignature kGMinor = {Key::G, true};

// Default figura profile for Inventio mode variations (not using figurae).
constexpr FiguraProfile kDefaultInventioFigura = {
    FiguraType::Circulatio,  // primary
    FiguraType::Circulatio,  // secondary
    4,                       // notes_per_beat
    DirectionBias::Symmetric,
    0.7f,                    // chord_tone_ratio
    0.3f                     // sequence_probability
};

// Default canon descriptor for non-canon variations.
constexpr CanonDescriptor kDefaultCanon = {0, false, 1};

/// @brief Helper to build an Elaboratio variation descriptor.
GoldbergVariationDescriptor makeElaboratio(
    int var_num, GoldbergVariationType type, KeySignature key,
    TimeSignature time_sig, MeterProfile meter, uint8_t voices,
    std::pair<int, int> ratio, TempoReferenceUnit tempo_ref,
    ArticulationProfile artic, float grid,
    FiguraType primary, FiguraType secondary, uint8_t notes_per_beat,
    DirectionBias dir, float chord_ratio, float seq_prob,
    GoldbergTempoCharacter character = GoldbergTempoCharacter::Dance) {
  GoldbergVariationDescriptor desc{};
  desc.variation_number = var_num;
  desc.type = type;
  desc.melody_mode = MelodyMode::Elaboratio;
  desc.key = key;
  desc.time_sig = time_sig;
  desc.meter_profile = meter;
  desc.num_voices = voices;
  desc.canon = kDefaultCanon;
  desc.bpm_override = 0;
  desc.tempo_ratio = ratio;
  desc.tempo_reference = tempo_ref;
  desc.minor_profile = key.is_minor ? MinorModeProfile::MixedBaroqueMinor
                                    : MinorModeProfile::NaturalMinor;
  desc.articulation = artic;
  desc.grid_strictness = grid;
  desc.figura = {primary, secondary, notes_per_beat, dir, chord_ratio, seq_prob};
  desc.soggetto_character = SubjectCharacter::Severe;  // default for Elaboratio
  desc.ornament_variation_on_repeat = false;
  desc.tempo_character = character;
  return desc;
}

/// @brief Helper to build an Inventio variation descriptor (non-canon).
GoldbergVariationDescriptor makeInventio(
    int var_num, GoldbergVariationType type, KeySignature key,
    TimeSignature time_sig, MeterProfile meter, uint8_t voices,
    std::pair<int, int> ratio, TempoReferenceUnit tempo_ref,
    ArticulationProfile artic, float grid,
    SubjectCharacter character,
    GoldbergTempoCharacter tempo_char = GoldbergTempoCharacter::Stable) {
  GoldbergVariationDescriptor desc{};
  desc.variation_number = var_num;
  desc.type = type;
  desc.melody_mode = MelodyMode::Inventio;
  desc.key = key;
  desc.time_sig = time_sig;
  desc.meter_profile = meter;
  desc.num_voices = voices;
  desc.canon = kDefaultCanon;
  desc.bpm_override = 0;
  desc.tempo_ratio = ratio;
  desc.tempo_reference = tempo_ref;
  desc.minor_profile = key.is_minor ? MinorModeProfile::MixedBaroqueMinor
                                    : MinorModeProfile::NaturalMinor;
  desc.articulation = artic;
  desc.grid_strictness = grid;
  desc.figura = kDefaultInventioFigura;
  desc.soggetto_character = character;
  desc.ornament_variation_on_repeat = false;
  desc.tempo_character = tempo_char;
  return desc;
}

/// @brief Helper to build a Canon variation descriptor.
GoldbergVariationDescriptor makeCanon(
    int var_num, KeySignature key, TimeSignature time_sig,
    MeterProfile meter, uint8_t voices,
    std::pair<int, int> ratio, TempoReferenceUnit tempo_ref,
    ArticulationProfile artic, float grid,
    int canon_interval, bool canon_inverted, uint8_t canon_delay,
    SubjectCharacter character) {
  GoldbergVariationDescriptor desc{};
  desc.variation_number = var_num;
  desc.type = GoldbergVariationType::Canon;
  desc.melody_mode = MelodyMode::Inventio;
  desc.key = key;
  desc.time_sig = time_sig;
  desc.meter_profile = meter;
  desc.num_voices = voices;
  desc.canon = {canon_interval, canon_inverted, canon_delay};
  desc.bpm_override = 0;
  desc.tempo_ratio = ratio;
  desc.tempo_reference = tempo_ref;
  desc.minor_profile = key.is_minor ? MinorModeProfile::MixedBaroqueMinor
                                    : MinorModeProfile::NaturalMinor;
  desc.articulation = artic;
  desc.grid_strictness = grid;
  desc.figura = kDefaultInventioFigura;
  desc.soggetto_character = character;
  desc.ornament_variation_on_repeat = false;
  desc.tempo_character = GoldbergTempoCharacter::Stable;  // Canons need phase coherence.
  return desc;
}

}  // namespace

std::vector<GoldbergVariationDescriptor> createGoldbergPlan() {
  std::vector<GoldbergVariationDescriptor> plan;
  plan.reserve(32);

  // Common time signatures.
  constexpr TimeSignature kThreeFour = {3, 4};
  constexpr TimeSignature kThreeEight = {3, 8};
  constexpr TimeSignature kSixEight = {6, 8};
  constexpr TimeSignature kTwoTwo = {2, 2};

  // Common tempo ratios (±30% of base BPM, range 50-94 @base72).
  constexpr auto kRatio1_1 = std::pair<int, int>{1, 1};    // 72 BPM (Aria/Sarabande).
  constexpr auto kRatio5_4 = std::pair<int, int>{5, 4};    // 90 BPM (moderate fast).
  constexpr auto kRatio4_3 = std::pair<int, int>{4, 3};    // 96 BPM (virtuosic).
  constexpr auto kRatio6_5 = std::pair<int, int>{6, 5};    // 86 BPM (moderate).
  constexpr auto kRatio3_4 = std::pair<int, int>{3, 4};    // 54 BPM (overture grave).
  constexpr auto kRatio7_10 = std::pair<int, int>{7, 10};  // 50 BPM (lament adagio).

  // --- Var 0: Aria ---
  plan.push_back(makeElaboratio(
      0, GoldbergVariationType::Aria, kGMajor,
      kThreeFour, MeterProfile::SarabandeTriple, 2,
      kRatio1_1, TempoReferenceUnit::Quarter,
      ArticulationProfile::Legato, 1.0f,
      FiguraType::Sarabande, FiguraType::Sarabande, 1,
      DirectionBias::Symmetric, 0.7f, 0.3f,
      GoldbergTempoCharacter::Expressive));

  // --- Var 1: Ornamental ---
  plan.push_back(makeElaboratio(
      1, GoldbergVariationType::Ornamental, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 0.85f,
      FiguraType::Circulatio, FiguraType::Circulatio, 4,
      DirectionBias::Symmetric, 0.7f, 0.3f));

  // --- Var 2: Invention ---
  plan.push_back(makeInventio(
      2, GoldbergVariationType::Invention, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio6_5, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 0.85f,
      SubjectCharacter::Playful,
      GoldbergTempoCharacter::Dance));

  // --- Var 3: Canon at the unison ---
  plan.push_back(makeCanon(
      3, kGMajor, kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 1.0f,
      0, false, 1, SubjectCharacter::Severe));

  // --- Var 4: Dance (Passepied) ---
  plan.push_back(makeElaboratio(
      4, GoldbergVariationType::Dance, kGMajor,
      kThreeEight, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Eighth,
      ArticulationProfile::Detache, 0.85f,
      FiguraType::Passepied, FiguraType::Passepied, 2,
      DirectionBias::Ascending, 0.7f, 0.3f));

  // --- Var 5: Ornamental ---
  plan.push_back(makeElaboratio(
      5, GoldbergVariationType::Ornamental, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 0.85f,
      FiguraType::Circulatio, FiguraType::Circulatio, 4,
      DirectionBias::Symmetric, 0.7f, 0.3f));

  // --- Var 6: Canon at the 2nd ---
  plan.push_back(makeCanon(
      6, kGMajor, kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 1.0f,
      2, false, 1, SubjectCharacter::Playful));

  // --- Var 7: Dance (Gigue) ---
  plan.push_back(makeElaboratio(
      7, GoldbergVariationType::Dance, kGMajor,
      kSixEight, MeterProfile::StandardTriple, 2,
      kRatio1_1, TempoReferenceUnit::DottedQuarter,
      ArticulationProfile::Detache, 0.85f,
      FiguraType::Gigue, FiguraType::Gigue, 2,
      DirectionBias::Ascending, 0.7f, 0.3f));

  // --- Var 8: Hand Crossing ---
  plan.push_back(makeElaboratio(
      8, GoldbergVariationType::HandCrossing, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio4_3, TempoReferenceUnit::Quarter,
      ArticulationProfile::Brillante, 0.85f,
      FiguraType::Batterie, FiguraType::Batterie, 4,
      DirectionBias::Alternating, 0.7f, 0.3f,
      GoldbergTempoCharacter::Virtuosic));

  // --- Var 9: Canon at the 3rd ---
  plan.push_back(makeCanon(
      9, kGMajor, kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 1.0f,
      4, false, 1, SubjectCharacter::Playful));

  // --- Var 10: Fughetta ---
  plan.push_back(makeInventio(
      10, GoldbergVariationType::Fughetta, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 4,
      kRatio6_5, TempoReferenceUnit::Quarter,
      ArticulationProfile::Legato, 1.0f,
      SubjectCharacter::Severe));

  // --- Var 11: Toccata ---
  plan.push_back(makeElaboratio(
      11, GoldbergVariationType::Toccata, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio4_3, TempoReferenceUnit::Quarter,
      ArticulationProfile::Brillante, 0.85f,
      FiguraType::Arpeggio, FiguraType::Arpeggio, 4,
      DirectionBias::Alternating, 0.7f, 0.3f,
      GoldbergTempoCharacter::Virtuosic));

  // --- Var 12: Canon at the 4th ---
  plan.push_back(makeCanon(
      12, kGMajor, kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 1.0f,
      5, false, 1, SubjectCharacter::Severe));

  // --- Var 13: Ornamental (Sarabande) ---
  plan.push_back(makeElaboratio(
      13, GoldbergVariationType::Ornamental, kGMajor,
      kThreeFour, MeterProfile::SarabandeTriple, 2,
      kRatio1_1, TempoReferenceUnit::Quarter,
      ArticulationProfile::Legato, 0.85f,
      FiguraType::Sarabande, FiguraType::Sarabande, 2,
      DirectionBias::Symmetric, 0.7f, 0.3f));

  // --- Var 14: Trill Etude ---
  plan.push_back(makeElaboratio(
      14, GoldbergVariationType::TrillEtude, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 0.85f,
      FiguraType::Trillo, FiguraType::Trillo, 4,
      DirectionBias::Symmetric, 0.7f, 0.3f));

  // --- Var 15: Canon at the 5th (inverted), G minor ---
  {
    auto desc = makeCanon(
        15, kGMinor, kThreeFour, MeterProfile::StandardTriple, 3,
        kRatio5_4, TempoReferenceUnit::Quarter,
        ArticulationProfile::Moderato, 1.0f,
        7, true, 1, SubjectCharacter::Noble);
    desc.minor_profile = MinorModeProfile::MixedBaroqueMinor;
    plan.push_back(desc);
  }

  // --- Var 16: French Overture ---
  {
    auto desc = makeElaboratio(
        16, GoldbergVariationType::FrenchOverture, kGMajor,
        kTwoTwo, MeterProfile::StandardTriple, 3,
        kRatio3_4, TempoReferenceUnit::Half,
        ArticulationProfile::FrenchDotted, 0.85f,
        FiguraType::DottedGrave, FiguraType::DottedGrave, 1,
        DirectionBias::Symmetric, 0.7f, 0.3f,
        GoldbergTempoCharacter::Expressive);
    desc.soggetto_character = SubjectCharacter::Noble;
    plan.push_back(desc);
  }

  // --- Var 17: Hand Crossing ---
  plan.push_back(makeElaboratio(
      17, GoldbergVariationType::HandCrossing, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio4_3, TempoReferenceUnit::Quarter,
      ArticulationProfile::Brillante, 0.85f,
      FiguraType::Batterie, FiguraType::Batterie, 4,
      DirectionBias::Alternating, 0.7f, 0.3f,
      GoldbergTempoCharacter::Virtuosic));

  // --- Var 18: Canon at the 6th ---
  plan.push_back(makeCanon(
      18, kGMajor, kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 1.0f,
      9, false, 1, SubjectCharacter::Playful));

  // --- Var 19: Dance (Passepied) ---
  plan.push_back(makeElaboratio(
      19, GoldbergVariationType::Dance, kGMajor,
      kThreeEight, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Eighth,
      ArticulationProfile::Detache, 0.85f,
      FiguraType::Passepied, FiguraType::Passepied, 2,
      DirectionBias::Ascending, 0.7f, 0.3f));

  // --- Var 20: Hand Crossing ---
  plan.push_back(makeElaboratio(
      20, GoldbergVariationType::HandCrossing, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio4_3, TempoReferenceUnit::Quarter,
      ArticulationProfile::Brillante, 0.85f,
      FiguraType::Batterie, FiguraType::Batterie, 4,
      DirectionBias::Alternating, 0.7f, 0.3f,
      GoldbergTempoCharacter::Virtuosic));

  // --- Var 21: Canon at the 7th, G minor ---
  {
    auto desc = makeCanon(
        21, kGMinor, kThreeFour, MeterProfile::StandardTriple, 3,
        kRatio5_4, TempoReferenceUnit::Quarter,
        ArticulationProfile::Moderato, 1.0f,
        11, false, 1, SubjectCharacter::Restless);
    desc.minor_profile = MinorModeProfile::HarmonicMinor;
    plan.push_back(desc);
  }

  // --- Var 22: Alla Breve Fugal ---
  plan.push_back(makeInventio(
      22, GoldbergVariationType::AllaBreveFugal, kGMajor,
      kTwoTwo, MeterProfile::StandardTriple, 4,
      kRatio1_1, TempoReferenceUnit::Half,
      ArticulationProfile::Legato, 1.0f,
      SubjectCharacter::Severe));

  // --- Var 23: Scale Passage ---
  plan.push_back(makeElaboratio(
      23, GoldbergVariationType::ScalePassage, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio4_3, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 0.85f,
      FiguraType::Tirata, FiguraType::Tirata, 4,
      DirectionBias::Ascending, 0.7f, 0.3f,
      GoldbergTempoCharacter::Virtuosic));

  // --- Var 24: Canon at the 8th (octave) ---
  plan.push_back(makeCanon(
      24, kGMajor, kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 1.0f,
      12, false, 1, SubjectCharacter::Playful));

  // --- Var 25: Black Pearl (Adagio), G minor ---
  {
    GoldbergVariationDescriptor desc{};
    desc.variation_number = 25;
    desc.type = GoldbergVariationType::BlackPearl;
    desc.melody_mode = MelodyMode::Special;
    desc.key = kGMinor;
    desc.time_sig = kThreeFour;
    desc.meter_profile = MeterProfile::SarabandeTriple;
    desc.num_voices = 2;
    desc.canon = kDefaultCanon;
    desc.bpm_override = 0;
    desc.tempo_ratio = kRatio7_10;
    desc.tempo_reference = TempoReferenceUnit::Quarter;
    desc.minor_profile = MinorModeProfile::MixedBaroqueMinor;
    desc.articulation = ArticulationProfile::Legato;
    desc.grid_strictness = 1.0f;
    desc.figura = kDefaultInventioFigura;
    desc.soggetto_character = SubjectCharacter::Noble;
    desc.ornament_variation_on_repeat = false;
    desc.tempo_character = GoldbergTempoCharacter::Lament;
    plan.push_back(desc);
  }

  // --- Var 26: Dance (Sarabande) ---
  plan.push_back(makeElaboratio(
      26, GoldbergVariationType::Dance, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio6_5, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 0.85f,
      FiguraType::Sarabande, FiguraType::Sarabande, 2,
      DirectionBias::Symmetric, 0.7f, 0.3f));

  // --- Var 27: Canon at the 9th ---
  plan.push_back(makeCanon(
      27, kGMajor, kThreeFour, MeterProfile::StandardTriple, 3,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 1.0f,
      14, false, 1, SubjectCharacter::Playful));

  // --- Var 28: Trill Etude ---
  plan.push_back(makeElaboratio(
      28, GoldbergVariationType::TrillEtude, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio5_4, TempoReferenceUnit::Quarter,
      ArticulationProfile::Moderato, 0.85f,
      FiguraType::Trillo, FiguraType::Trillo, 4,
      DirectionBias::Symmetric, 0.7f, 0.3f));

  // --- Var 29: Bravura Chordal ---
  plan.push_back(makeElaboratio(
      29, GoldbergVariationType::BravuraChordal, kGMajor,
      kThreeFour, MeterProfile::StandardTriple, 2,
      kRatio4_3, TempoReferenceUnit::Quarter,
      ArticulationProfile::Brillante, 0.7f,
      FiguraType::Bariolage, FiguraType::Bariolage, 4,
      DirectionBias::Alternating, 0.7f, 0.3f,
      GoldbergTempoCharacter::Virtuosic));

  // --- Var 30: Quodlibet ---
  {
    GoldbergVariationDescriptor desc{};
    desc.variation_number = 30;
    desc.type = GoldbergVariationType::Quodlibet;
    desc.melody_mode = MelodyMode::Special;
    desc.key = kGMajor;
    desc.time_sig = kThreeFour;
    desc.meter_profile = MeterProfile::StandardTriple;
    desc.num_voices = 4;
    desc.canon = kDefaultCanon;
    desc.bpm_override = 0;
    desc.tempo_ratio = kRatio1_1;
    desc.tempo_reference = TempoReferenceUnit::Quarter;
    desc.minor_profile = MinorModeProfile::NaturalMinor;
    desc.articulation = ArticulationProfile::Moderato;
    desc.grid_strictness = 0.75f;
    desc.figura = kDefaultInventioFigura;
    desc.soggetto_character = SubjectCharacter::Playful;
    desc.ornament_variation_on_repeat = false;
    plan.push_back(desc);
  }

  // --- Var 31: Da Capo Aria ---
  plan.push_back(makeElaboratio(
      31, GoldbergVariationType::Aria, kGMajor,
      kThreeFour, MeterProfile::SarabandeTriple, 2,
      kRatio1_1, TempoReferenceUnit::Quarter,
      ArticulationProfile::Legato, 1.0f,
      FiguraType::Sarabande, FiguraType::Sarabande, 1,
      DirectionBias::Symmetric, 0.7f, 0.3f,
      GoldbergTempoCharacter::Expressive));

  return plan;
}

}  // namespace bach
