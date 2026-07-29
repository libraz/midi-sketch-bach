#include "composer/form_director.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "composer/arc.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/harness_fixture.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

// Global hard cap on resolved length (matches FormSpec::max_bars upper bound).
constexpr std::uint16_t kMaxBarsCap = 128;

// Scale multipliers applied to a form's natural length when no explicit
// target_bars override is given. Index by DurationScale ordinal.
constexpr std::array<std::uint16_t, 4> kScaleMultiplier = {{1, 2, 3, 4}};

// Static per-form layout table. Order matches the FormType enumerators so the
// ordinal indexes directly. Bar counts feed resolveBars(); meter feeds the
// HarnessFixture time-signature field.
constexpr std::array<FormSpec, 10> kFormSpecs = {{
    // num_voices, natural, min, max, snap, ts_num, ts_den, meter profile
    {3, 42, 20, 128, 4, 4, 4, MeterProfile::StandardTriple},  // Fugue
    {3, 24, 24, 128, 4, 4, 4, MeterProfile::StandardTriple},  // PreludeAndFugue
    {3, 16, 8, 128, 4, 4, 4, MeterProfile::StandardTriple},   // TrioSonata
    {3, 16, 8, 128, 4, 4, 4, MeterProfile::StandardTriple},   // ChoralePrelude
    {3, 32, 16, 128, 4, 4, 4, MeterProfile::StandardTriple},  // ToccataAndFugue
    {3, 24, 16, 128, 8, 3, 4, MeterProfile::StandardTriple},  // Passacaglia
    {3, 32, 16, 128, 4, 4, 4, MeterProfile::StandardTriple},  // FantasiaAndFugue
    {1, 8, 8, 128, 4, 4, 4, MeterProfile::StandardTriple},    // CelloPrelude
    {2, 16, 8, 128, 4, 3, 4, MeterProfile::SarabandeTriple},  // Chaconne
    {3, 20, 12, 128, 4, 4, 4, MeterProfile::StandardTriple},  // GoldbergVariations
}};

// Snap `value` to the nearest multiple of `snap` (round-to-nearest, ties up).
std::uint16_t snapToMultiple(std::uint32_t value, std::uint16_t snap) {
  if (snap <= 1) {
    return static_cast<std::uint16_t>(value);
  }
  const std::uint32_t snapped = ((value + snap / 2) / snap) * snap;
  return static_cast<std::uint16_t>(snapped);
}

// Free counterpoint may replace only a genuinely accompanimental secondary
// voice.  In the current form catalogue that is the Passacaglia V1 counterline:
// its ground (V2) and variation voice (V0) remain structural material.  Other
// TrioVoiceCarrier uses are complete trio lines, a chorale bass, or otherwise
// structural carriers, so treating them as an eligible target would either
// erase form-defining material or make the public flag a silent no-op.
bool isFreeCounterpointTarget(FormType form, const Span& span) {
  return form == FormType::Passacaglia && span.intent == VoiceIntent::TrioVoiceCarrier &&
         span.voice == 1;
}

const ChordEvent* chordAtOrBefore(const HarmonicPlan& plan, Tick tick) {
  const ChordEvent* active = nullptr;
  for (const ChordEvent& chord : plan.chords) {
    if (chord.start_tick > tick)
      break;
    active = &chord;
  }
  return active;
}

CadenceType cadenceTypeAt(const HarmonicPlan& plan, Tick tick) {
  const ChordEvent* current = chordAtOrBefore(plan, tick);
  const ChordEvent* previous = nullptr;
  for (const ChordEvent& chord : plan.chords) {
    if (chord.start_tick >= tick)
      break;
    previous = &chord;
  }
  if (current == nullptr)
    return CadenceType::ImperfectAuthentic;
  const int tonic = plan.tonic_pc % 12;
  const int dominant = (tonic + 7) % 12;
  const int subdominant = (tonic + 5) % 12;
  const int submediant = (tonic + (plan.is_minor ? 8 : 9)) % 12;
  if (current->root_pc % 12 == dominant)
    return CadenceType::Half;
  if (previous != nullptr && previous->root_pc % 12 == dominant) {
    if (current->root_pc % 12 == tonic)
      return CadenceType::Perfect;
    if (current->root_pc % 12 == submediant)
      return CadenceType::Deceptive;
  }
  if (previous != nullptr && previous->root_pc % 12 == subdominant &&
      current->root_pc % 12 == tonic)
    return CadenceType::Plagal;
  return CadenceType::ImperfectAuthentic;
}

void declareCadenceVocabulary(FormType form, std::uint16_t bars, HarnessFixture* fixture) {
  if (fixture == nullptr || bars == 0 || fixture->harmony.chords.empty())
    return;
  HarmonicPlan& plan = fixture->harmony;
  const Tick ticks_per_bar = plan.ticksPerBar();
  std::vector<Tick> boundaries;
  const Tick final_tick = static_cast<Tick>(bars - 1) * ticks_per_bar;
  boundaries.push_back(final_tick);
  // The sectional builders author the free-section close as an actual half
  // cadence.  Other section_cadence_ticks (notably the fugue exposition
  // marker) are ornament sites rather than two-voice cadence formulas and are
  // deliberately not promoted to CadenceEvent metadata.
  if (form == FormType::ToccataAndFugue || form == FormType::FantasiaAndFugue) {
    boundaries.insert(boundaries.end(), fixture->section_cadence_ticks.begin(),
                      fixture->section_cadence_ticks.end());
  }
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

  for (Tick tick : boundaries) {
    const auto existing =
        std::find_if(plan.cadences.begin(), plan.cadences.end(),
                     [&](const CadenceEvent& cadence) { return cadence.tick == tick; });
    if (existing == plan.cadences.end()) {
      plan.cadences.push_back({tick, cadenceTypeAt(plan, tick)});
    }
  }
  std::stable_sort(
      plan.cadences.begin(), plan.cadences.end(),
      [](const CadenceEvent& left, const CadenceEvent& right) { return left.tick < right.tick; });

  const int dominant_pc = (plan.tonic_pc + 7) % 12;
  for (const CadenceEvent& cadence : plan.cadences) {
    ChordEvent* dominant = nullptr;
    for (ChordEvent& chord : plan.chords) {
      if (chord.start_tick > cadence.tick)
        break;
      if (cadence.type == CadenceType::Half && chord.start_tick == cadence.tick) {
        dominant = &chord;
      } else if (chord.start_tick < cadence.tick) {
        dominant = &chord;
      }
    }
    if (dominant == nullptr || dominant->root_pc % 12 != dominant_pc)
      continue;
    dominant->quality = ChordQuality::Dominant7;
    dominant->degree = RomanNumeral::V;
    dominant->function = HarmonicFunction::D;
    dominant->has_degree = true;
  }
}

}  // namespace

const FormSpec& formSpec(FormType form) {
  const std::size_t idx = static_cast<std::size_t>(form);
  // FormType is a fixed 10-value enum; any in-range ordinal maps directly.
  return kFormSpecs[idx < kFormSpecs.size() ? idx : 0];
}

bool isFormCharacterCompatible(FormType form, SubjectCharacter character) {
  // Chorale preludes are sacred, contemplative works; Playful and Restless
  // characters conflict with the genre's devotional nature.
  if (form == FormType::ChoralePrelude) {
    if (character == SubjectCharacter::Playful || character == SubjectCharacter::Restless) {
      return false;
    }
  }

  // Toccata and fugue demands virtuosic energy; Noble character's stately
  // pacing is incompatible with the genre's dramatic flair.
  if (form == FormType::ToccataAndFugue) {
    if (character == SubjectCharacter::Noble) {
      return false;
    }
  }

  return true;
}

std::uint16_t resolveBars(FormType form, DurationScale scale, std::uint16_t target_bars) {
  const FormSpec& spec = formSpec(form);

  // The public Goldberg `full` contract is the complete compressed layout:
  // aria + 30 four-bar variation slots + aria da capo = 128 bars.
  if (target_bars == 0 && form == FormType::GoldbergVariations && scale == DurationScale::Full) {
    return 128;
  }

  // target_bars > 0 overrides the scale entirely; otherwise derive a raw length
  // from the form's natural length and the requested scale multiplier.
  std::uint32_t raw = target_bars;
  if (target_bars == 0) {
    const std::size_t scale_idx = static_cast<std::size_t>(scale);
    const std::uint16_t mult =
        scale_idx < kScaleMultiplier.size() ? kScaleMultiplier[scale_idx] : 1;
    raw = static_cast<std::uint32_t>(spec.natural_bars) * mult;
  }

  // Snap to the form's granularity, then clamp into [min_bars, max_bars]
  // (max never exceeds the global cap).
  std::uint16_t snapped = snapToMultiple(raw, spec.snap_bars);
  const std::uint16_t hi = spec.max_bars < kMaxBarsCap ? spec.max_bars : kMaxBarsCap;
  if (snapped < spec.min_bars) {
    snapped = spec.min_bars;
  }
  if (snapped > hi) {
    snapped = hi;
  }
  return snapped;
}

ArcPoint arcPoint(std::size_t cycle_index, std::size_t cycle_count) {
  ArcPoint point{};
  if (cycle_count == 0) {
    cycle_count = 1;
  }
  if (cycle_index >= cycle_count) {
    cycle_index = cycle_count - 1;
  }

  // Place the single climax at ~80% of the available indices. A multi-cycle
  // arc must reserve its final cycle for resolution.
  const std::size_t last = cycle_count - 1;
  std::size_t climax_idx = (last * 4) / 5;  // ~80% position
  if (cycle_count >= 2 && climax_idx >= last) {
    climax_idx = last - 1;
  }
  // Establish occupies roughly the first quarter of the span.
  const std::size_t establish_end = cycle_count / 4;  // exclusive boundary

  point.is_climax = (cycle_index == climax_idx);

  if (cycle_index == climax_idx) {
    point.stage = ArcStage::Climax;
  } else if (cycle_index < establish_end) {
    point.stage = ArcStage::Establish;
  } else if (cycle_index < climax_idx) {
    point.stage = ArcStage::Develop;
  } else {
    point.stage = ArcStage::Resolve;
  }

  // Density and velocity rise monotonically from Establish up to the climax,
  // then fall back through Resolve. Register lifts into the climax and returns
  // to 0 by the final cycle. All curves are pure functions of the indices.
  if (cycle_index <= climax_idx) {
    // Rising limb: interpolate tiers 0..3 across [0, climax_idx].
    const std::size_t span = climax_idx == 0 ? 1 : climax_idx;
    const std::size_t tier = (cycle_index * 3) / span;  // 0..3
    point.density_tier = static_cast<std::uint8_t>(tier > 3 ? 3 : tier);
    point.velocity_tier = point.density_tier;
    point.register_shift = static_cast<std::int8_t>((cycle_index * 12) / span);
  } else {
    // Falling limb: from just below peak back down to a calm close.
    const std::size_t after = cycle_index - climax_idx;
    const std::size_t span = last == climax_idx ? 1 : (last - climax_idx);
    // Drop two tiers across the resolve limb (peak 3 -> ~1 at the end).
    const std::size_t drop = (after * 2) / span;
    const std::size_t tier = drop >= 2 ? 1 : 2 - drop;
    point.density_tier = static_cast<std::uint8_t>(tier);
    point.velocity_tier = point.density_tier;
    // Register returns linearly to 0 by the final cycle.
    const std::size_t shift = 12 - (after * 12) / span;
    point.register_shift = static_cast<std::int8_t>(after >= span ? 0 : shift);
  }

  // The climax cycle is the peak tier by design: climax values are output
  // directly as design values, not searched.
  if (point.is_climax) {
    point.density_tier = 3;
    point.velocity_tier = 3;
  }

  return point;
}

FormDirectorStatus buildFormFixture(const ComposeRequest& req, HarnessFixture* out) {
  if (out == nullptr) {
    return FormDirectorStatus::UnknownForm;
  }
  const std::size_t form_idx = static_cast<std::size_t>(req.form);
  if (form_idx >= kFormSpecs.size()) {
    return FormDirectorStatus::UnknownForm;
  }
  if (!isFormCharacterCompatible(req.form, req.character)) {
    return FormDirectorStatus::IncompatibleCharacter;
  }

  const FormSpec& spec = formSpec(req.form);

  // The director's own DurationScale axis is target_bars; an explicit
  // target_bars overrides, otherwise the natural length is used (Short).
  const std::uint16_t bars = resolveBars(req.form, DurationScale::Short, req.target_bars);

  // One arc cycle per snap window keeps the curve aligned to the form's
  // structural granularity (ground period, phrase grid, etc.).
  const std::uint16_t snap = spec.snap_bars == 0 ? 1 : spec.snap_bars;
  std::size_t cycle_count = bars / snap;
  if (cycle_count == 0) {
    cycle_count = 1;
  }

  ResolvedRequest resolved{
      bars,          req.seed, req.is_minor ? detail::Mode::Minor : detail::Mode::Major,
      req.character, spec,     cycle_count,
  };

  HarnessFixture fixture;
  switch (req.form) {
    case FormType::Fugue:
      fixture = buildFugueForm(resolved);
      break;
    case FormType::PreludeAndFugue:
      fixture = buildPreludeAndFugueForm(resolved);
      break;
    case FormType::TrioSonata:
      fixture = buildTrioSonataForm(resolved);
      break;
    case FormType::ChoralePrelude:
      fixture = buildChoralePreludeForm(resolved);
      break;
    case FormType::ToccataAndFugue:
      fixture = buildToccataAndFugueForm(resolved);
      break;
    case FormType::Passacaglia:
      fixture = buildPassacagliaForm(resolved);
      break;
    case FormType::FantasiaAndFugue:
      fixture = buildFantasiaAndFugueForm(resolved);
      break;
    case FormType::CelloPrelude:
      fixture = buildCelloPreludeForm(resolved);
      break;
    case FormType::Chaconne:
      fixture = buildChaconneForm(resolved);
      break;
    case FormType::GoldbergVariations:
      fixture = buildGoldbergVariationsForm(resolved);
      break;
  }

  // Stamp the form's meter onto the assembled fixture (the placeholder builders
  // replay 4/4 phase fixtures, so this is the authoritative meter source). The
  // same meter is copied onto the fixture's HarmonicPlan, which is the carrier
  // every meter-sensitive validator / candidate-search site reads to derive the
  // bar length (HarmonicPlan::ticksPerBar()). Keeping the two in lockstep means
  // a 3/4 form (chaconne / passacaglia) validates against 1440-tick bars while
  // the default-4/4 phase fixtures stay byte-identical.
  fixture.ts_numerator = spec.ts_numerator;
  fixture.ts_denominator = spec.ts_denominator;
  fixture.harmony.ts_numerator = spec.ts_numerator;
  fixture.harmony.ts_denominator = spec.ts_denominator;
  fixture.harmony.meter_profile = spec.meter_profile;
  annotateDiatonicChordMetadata(&fixture.harmony);
  declareCadenceVocabulary(req.form, resolved.bars, &fixture);

  // Opt-in free-counterpoint activation. With the flag off this loop never
  // runs and the fixture is the unchanged carrier-assembly result (byte-stable
  // across every form). With it on, only a genuinely accompanimental secondary
  // voice may switch from verbatim replay to the scored candidate search.
  // Rejecting a form with no such span makes the public option observable and
  // avoids changing a structural carrier merely because it shares a generic
  // TrioVoiceCarrier intent.
  if (req.enable_free_counterpoint) {
    bool rerouted = false;
    for (Span& span : fixture.voice_plan.spans) {
      if (isFreeCounterpointTarget(req.form, span)) {
        span.intent = VoiceIntent::SequentialCounterline;
        rerouted = true;
      }
    }
    if (!rerouted) {
      return FormDirectorStatus::FreeCounterpointUnavailable;
    }
  }

  *out = fixture;
  return FormDirectorStatus::Ok;
}

}  // namespace bach::composer
