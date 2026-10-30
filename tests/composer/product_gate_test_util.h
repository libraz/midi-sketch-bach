#ifndef BACH_TESTS_COMPOSER_PRODUCT_GATE_TEST_UTIL_H
#define BACH_TESTS_COMPOSER_PRODUCT_GATE_TEST_UTIL_H

// The shipped request surface and the gate the product judges it by.
//
// shippedRequestCells is the one enumerator of that surface: every sweep that
// claims to cover what ships takes its cells from it, so a sweep cannot narrow
// an axis without saying so in the slice it asks for. The only request it never
// yields is a form x character pair the director refuses as IncompatibleCharacter;
// every other cell has to compose, and a sweep that meets a refusal fails.
//
// The output key is a placement axis only: the composer works in C, and the key
// reaches the notes solely through compass placement, which moves single notes
// by an octave where the transposed score would leave the instrument. Each cell
// therefore carries the key its seed rotates to rather than multiplying the grid
// by twelve.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// For kDefaultBpm and CompositionRequest alone, both header-only: trill pacing
// is tempo-dependent, so the decorated array depends on the tempo the product
// renders at.
#include "application/composition_service.h"
#include "composer/compass_placement.h"
#include "composer/composer.h"
#include "composer/counterpoint_budget.h"
#include "composer/form_director.h"
#include "composer/harness_fixture.h"
#include "composer/ornament_pass.h"
#include "composer/validator.h"

namespace bach::composer::test {

/// Every form the product ships, in FormType order.
inline constexpr std::array<FormType, 10> kShippedForms = {{
    FormType::Fugue,
    FormType::PreludeAndFugue,
    FormType::TrioSonata,
    FormType::ChoralePrelude,
    FormType::ToccataAndFugue,
    FormType::Passacaglia,
    FormType::FantasiaAndFugue,
    FormType::CelloPrelude,
    FormType::Chaconne,
    FormType::GoldbergVariations,
}};

/// Every subject character a request may name.
inline constexpr std::array<SubjectCharacter, 4> kAllCharacters = {{
    SubjectCharacter::Severe,
    SubjectCharacter::Playful,
    SubjectCharacter::Noble,
    SubjectCharacter::Restless,
}};

/// Form x character pairs the director accepts. Pinned so the enumerator's one
/// skip cannot widen without a test noticing.
inline constexpr std::size_t kCompatiblePairCount = 37;

/// Which lengths of a form a sweep reads.
enum class LengthAxis : std::uint8_t {
  Natural,  ///< The Short scale only.
  Longest,  ///< The longest request the form accepts (`--bars 128`).
  Shipped,  ///< Every distinct length the four scales and the longest request resolve to.
};

/// A slice of the shipped request surface. Defaults are the whole surface over seeds 1-16.
struct ShippedSurface {
  std::vector<FormType> forms{kShippedForms.begin(), kShippedForms.end()};
  std::vector<SubjectCharacter> characters{kAllCharacters.begin(), kAllCharacters.end()};
  std::vector<bool> modes{false, true};
  LengthAxis lengths = LengthAxis::Shipped;
  std::uint32_t first_seed = 1;
  std::uint32_t seed_count = 16;
};

/// One shipped request: what the composer receives, plus the key its output is placed in.
struct ShippedCell {
  ComposeRequest request;  ///< target_bars is always the resolved length, as the product passes.
  Key tonic = Key::C;
};

/// @brief Output key a seed rotates to: the circle of fifths from C, so any twelve
///        consecutive seeds reach every tonic in each mode.
inline Key shippedTonic(std::uint32_t seed) {
  return static_cast<Key>((7u * seed) % 12u);
}

/// @brief Distinct resolved lengths of `form` on `axis`, ascending.
inline std::vector<std::uint16_t> shippedLengths(FormType form, LengthAxis axis) {
  constexpr DurationScale kScales[] = {DurationScale::Short, DurationScale::Medium,
                                       DurationScale::Long, DurationScale::Full};
  std::vector<std::uint16_t> lengths;
  if (axis != LengthAxis::Longest) {
    for (DurationScale scale : kScales) {
      lengths.push_back(resolveBars(form, scale, /*target_bars=*/0));
      if (axis == LengthAxis::Natural)
        break;
    }
  }
  if (axis != LengthAxis::Natural)
    lengths.push_back(resolveBars(form, DurationScale::Short, formSpec(form).max_bars));
  std::sort(lengths.begin(), lengths.end());
  lengths.erase(std::unique(lengths.begin(), lengths.end()), lengths.end());
  return lengths;
}

/// @brief Every request in `surface` the product accepts, grouped by form, then
///        character, mode, length and seed.
inline std::vector<ShippedCell> shippedRequestCells(const ShippedSurface& surface) {
  std::vector<ShippedCell> cells;
  for (FormType form : surface.forms) {
    const std::vector<std::uint16_t> lengths = shippedLengths(form, surface.lengths);
    for (SubjectCharacter character : surface.characters) {
      // The one refusal a shipped request may meet, and the only cell left out.
      if (!isFormCharacterCompatible(form, character))
        continue;
      for (bool minor : surface.modes) {
        for (std::uint16_t bars : lengths) {
          for (std::uint32_t offset = 0; offset < surface.seed_count; ++offset) {
            ShippedCell cell;
            cell.request.form = form;
            cell.request.character = character;
            cell.request.is_minor = minor;
            cell.request.target_bars = bars;
            cell.request.seed = surface.first_seed + offset;
            cell.tonic = shippedTonic(cell.request.seed);
            cells.push_back(cell);
          }
        }
      }
    }
  }
  return cells;
}

/// @brief One-line name of a cell for failure traces.
inline std::string cellName(const ShippedCell& cell) {
  return std::string(formTypeToString(cell.request.form)) + " " +
         subjectCharacterToString(cell.request.character) + " " + keyToString(cell.tonic) +
         (cell.request.is_minor ? " minor" : " major") + " bars " +
         std::to_string(cell.request.target_bars) + " seed " + std::to_string(cell.request.seed);
}

/// @brief The product request for a cell, every other field at its product default.
inline application::CompositionRequest compositionRequestFor(const ShippedCell& cell) {
  application::CompositionRequest request;
  request.form = cell.request.form;
  request.key = {cell.tonic, cell.request.is_minor};
  request.character = cell.request.character;
  request.target_bars = cell.request.target_bars;
  request.seed = cell.request.seed;
  return request;
}

/// @brief One request judged the way the product judges it before shipping.
struct ProductGateResult {
  bool built = false;      ///< The form director accepted the request.
  bool generated = false;  ///< Generation-scope validation passed.
  bool placed = false;     ///< Compass placement found an octave inside MIDI.
  ComposeResult composed;  ///< Composer output before decoration.
  ComposeResult shipped;   ///< Decorated and placed output.
  HarnessFixture fixture;  ///< The fixture both arrays were built from.
  /// FinalScore validation plus the counterpoint budget over `shipped`.
  std::vector<ValidationFailure> failures;

  /// True exactly when application::compose would return Ok for the request.
  bool ships() const { return built && generated && placed && failures.empty(); }
};

/// @brief Compose, decorate and place a request exactly as the product does in
///        `tonic`, then validate at FinalScore scope behind the counterpoint budget.
/// @note Every stage runs even after an earlier one fails, so a caller can read
///       the arrays of a refused request; ships() is the product's verdict.
inline ProductGateResult runProductGate(const ComposeRequest& request, Key tonic) {
  ProductGateResult out;
  if (buildFormFixture(request, &out.fixture) != FormDirectorStatus::Ok)
    return out;
  out.built = true;
  out.composed = Composer{}.run(out.fixture.material, out.fixture.harmony, out.fixture.voice_plan);
  out.generated = out.composed.validation.status == ValidationStatus::Ok;
  out.shipped = out.composed;
  std::uint32_t total_ticks = 0;
  for (const NoteEvent& note : out.shipped.notes)
    total_ticks = std::max(total_ticks, note.start_tick + note.duration);
  OrnamentParams ornament;
  ornament.character = request.character;
  ornament.instrument = defaultInstrumentForForm(request.form);
  ornament.mode = request.is_minor ? detail::Mode::Minor : detail::Mode::Major;
  ornament.seed = request.seed;
  ornament.bpm = application::kDefaultBpm;
  resolveFixtureOrnamentContext(out.fixture, request.form, total_ticks, &ornament);
  applyOrnamentPass(out.shipped, ornament);
  const auto validate = [&](const ComposeResult& result) {
    ValidationReport report =
        Validator{}.validate(result.notes, result.provenance, out.fixture.harmony,
                             out.fixture.material, ValidationScope::FinalScore);
    applyCounterpointBudget(request.form, &report);
    return report;
  };
  const auto placement = placeInOutputCompass(
      &out.shipped, out.fixture.material, out.fixture.harmony, tonic, ornament.instrument,
      out.fixture.harmony.ticksPerBar(),
      [&](const ComposeResult& placed) { return validate(placed).failures.empty(); });
  out.placed = placement.has_value();
  out.failures = validate(out.shipped).failures;
  return out;
}

/// @brief runProductGate for a cell of the shipped surface.
inline ProductGateResult runProductGate(const ShippedCell& cell) {
  return runProductGate(cell.request, cell.tonic);
}

/// @brief runProductGate in the key the shipped surface assigns to the request's seed.
inline ProductGateResult runProductGate(const ComposeRequest& request) {
  return runProductGate(request, shippedTonic(request.seed));
}

/// @brief Judge every cell of `slice` by the product gate and hand each one that
///        ships to `visit(cell, gate)`. A cell the product refuses fails the
///        calling test instead of being visited.
/// @return The number of cells visited.
template <typename Visit>
std::size_t sweepShippedCells(const ShippedSurface& slice, Visit&& visit) {
  const std::vector<ShippedCell> cells = shippedRequestCells(slice);
  EXPECT_FALSE(cells.empty()) << "the slice holds no shipped request";
  std::size_t visited = 0;
  for (const ShippedCell& cell : cells) {
    const ProductGateResult gate = runProductGate(cell);
    if (!gate.ships()) {
      ADD_FAILURE() << cellName(cell) << " is a request the product refuses (built " << gate.built
                    << ", generated " << gate.generated << ", placed " << gate.placed
                    << ", first final failure "
                    << (gate.failures.empty() ? std::string("none") : gate.failures.front().rule_id)
                    << ")";
      continue;
    }
    visit(cell, gate);
    ++visited;
  }
  return visited;
}

}  // namespace bach::composer::test

#endif  // BACH_TESTS_COMPOSER_PRODUCT_GATE_TEST_UTIL_H
