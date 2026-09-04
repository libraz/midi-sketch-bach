#include "composer/counterpoint_budget.h"

#include <cstddef>
#include <string>

#include "composer/validator.h"

namespace bach::composer {
namespace {

// Vertical rules each form is still allowed to break, measured over the whole
// request surface: every character, every duration scale and the maximum bar
// count, in both modes, over a seed range wide enough that the maxima stopped
// moving. A rule is listed for a form only because that form was observed to
// break it somewhere on that surface -- a rule that never matched is absent and
// therefore closed.
//
// Every form except the passacaglia satisfies `doubling_no_seventh` everywhere,
// and the monophonic cello prelude has no voice pair at all, so it satisfies
// every rule that compares two lines. Closures narrower than that are noted at
// the form they belong to.
//
// Sorted by form ordinal, then by rule_id, so the regression can reject a
// duplicate or a misplaced row.
constexpr CounterpointBudgetEntry kOpenRules[] = {
    // Fugue
    {FormType::Fugue, "anti_parallel_perfect"},
    {FormType::Fugue, "battuta"},
    {FormType::Fugue, "cross_relation"},
    {FormType::Fugue, "doubling_no_leading_tone"},
    {FormType::Fugue, "hidden_parallel_fifth"},
    {FormType::Fugue, "hidden_parallel_octave"},
    {FormType::Fugue, "invertible_at_octave"},
    {FormType::Fugue, "parallel_fifth"},
    {FormType::Fugue, "parallel_octave"},
    {FormType::Fugue, "strong_beat_dissonance"},
    {FormType::Fugue, "unprepared_dissonance"},
    {FormType::Fugue, "vertical_dissonance"},
    // PreludeAndFugue
    {FormType::PreludeAndFugue, "anti_parallel_perfect"},
    {FormType::PreludeAndFugue, "battuta"},
    {FormType::PreludeAndFugue, "cross_relation"},
    {FormType::PreludeAndFugue, "doubling_no_leading_tone"},
    {FormType::PreludeAndFugue, "hidden_parallel_fifth"},
    {FormType::PreludeAndFugue, "hidden_parallel_octave"},
    {FormType::PreludeAndFugue, "invertible_at_octave"},
    {FormType::PreludeAndFugue, "parallel_fifth"},
    {FormType::PreludeAndFugue, "parallel_octave"},
    {FormType::PreludeAndFugue, "strong_beat_dissonance"},
    {FormType::PreludeAndFugue, "unprepared_dissonance"},
    {FormType::PreludeAndFugue, "vertical_dissonance"},
    // TrioSonata -- `invertible_at_octave` judges the upper pair alone, and in
    // this texture that is exactly the pair the second manual voice is guarded
    // against as it is written. The guard admits a fifth, which inverts to a
    // fourth and is tolerated here, so `parallel_fifth` stays open.
    {FormType::TrioSonata, "anti_parallel_perfect"},
    {FormType::TrioSonata, "battuta"},
    {FormType::TrioSonata, "cross_relation"},
    {FormType::TrioSonata, "doubling_no_leading_tone"},
    {FormType::TrioSonata, "hidden_parallel_fifth"},
    {FormType::TrioSonata, "hidden_parallel_octave"},
    {FormType::TrioSonata, "parallel_fifth"},
    {FormType::TrioSonata, "parallel_octave"},
    {FormType::TrioSonata, "strong_beat_dissonance"},
    {FormType::TrioSonata, "unprepared_dissonance"},
    {FormType::TrioSonata, "vertical_dissonance"},
    // ChoralePrelude -- the cantus firmus never doubles a leading tone. Its
    // three lines all arrive on the bar head over a bass pinned to one octave,
    // and the tone before the head is re-aimed until the fifth is gone; the
    // octave is not always reachable that way, so `parallel_octave` stays open.
    {FormType::ChoralePrelude, "anti_parallel_perfect"},
    {FormType::ChoralePrelude, "battuta"},
    {FormType::ChoralePrelude, "cross_relation"},
    {FormType::ChoralePrelude, "hidden_parallel_fifth"},
    {FormType::ChoralePrelude, "hidden_parallel_octave"},
    {FormType::ChoralePrelude, "invertible_at_octave"},
    {FormType::ChoralePrelude, "parallel_octave"},
    {FormType::ChoralePrelude, "strong_beat_dissonance"},
    {FormType::ChoralePrelude, "unprepared_dissonance"},
    {FormType::ChoralePrelude, "vertical_dissonance"},
    // ToccataAndFugue
    {FormType::ToccataAndFugue, "anti_parallel_perfect"},
    {FormType::ToccataAndFugue, "battuta"},
    {FormType::ToccataAndFugue, "cross_relation"},
    {FormType::ToccataAndFugue, "doubling_no_leading_tone"},
    {FormType::ToccataAndFugue, "hidden_parallel_fifth"},
    {FormType::ToccataAndFugue, "hidden_parallel_octave"},
    {FormType::ToccataAndFugue, "invertible_at_octave"},
    {FormType::ToccataAndFugue, "parallel_fifth"},
    {FormType::ToccataAndFugue, "parallel_octave"},
    {FormType::ToccataAndFugue, "strong_beat_dissonance"},
    {FormType::ToccataAndFugue, "unprepared_dissonance"},
    {FormType::ToccataAndFugue, "vertical_dissonance"},
    // Passacaglia -- the only form that still doubles a seventh.
    {FormType::Passacaglia, "anti_parallel_perfect"},
    {FormType::Passacaglia, "battuta"},
    {FormType::Passacaglia, "cross_relation"},
    {FormType::Passacaglia, "doubling_no_leading_tone"},
    {FormType::Passacaglia, "doubling_no_seventh"},
    {FormType::Passacaglia, "hidden_parallel_fifth"},
    {FormType::Passacaglia, "hidden_parallel_octave"},
    {FormType::Passacaglia, "invertible_at_octave"},
    {FormType::Passacaglia, "parallel_fifth"},
    {FormType::Passacaglia, "parallel_octave"},
    {FormType::Passacaglia, "strong_beat_dissonance"},
    {FormType::Passacaglia, "unprepared_dissonance"},
    {FormType::Passacaglia, "vertical_dissonance"},
    // FantasiaAndFugue -- its countersubjects already invert at the octave.
    {FormType::FantasiaAndFugue, "anti_parallel_perfect"},
    {FormType::FantasiaAndFugue, "battuta"},
    {FormType::FantasiaAndFugue, "cross_relation"},
    {FormType::FantasiaAndFugue, "doubling_no_leading_tone"},
    {FormType::FantasiaAndFugue, "hidden_parallel_fifth"},
    {FormType::FantasiaAndFugue, "hidden_parallel_octave"},
    {FormType::FantasiaAndFugue, "parallel_fifth"},
    {FormType::FantasiaAndFugue, "parallel_octave"},
    {FormType::FantasiaAndFugue, "strong_beat_dissonance"},
    {FormType::FantasiaAndFugue, "unprepared_dissonance"},
    {FormType::FantasiaAndFugue, "vertical_dissonance"},
    // CelloPrelude -- monophonic, so every rule that compares two lines is
    // closed permanently. What remains is judged against the harmonic plan.
    {FormType::CelloPrelude, "strong_beat_dissonance"},
    {FormType::CelloPrelude, "unprepared_dissonance"},
    // Chaconne -- two voices, the lower of them the immutable ground. Every
    // onset of the upper line is ranked against that ground while the material
    // is built, and the cadential coda ranks the two tones it writes over the
    // bass at both ends, so no true parallel of either class survives. What the
    // ranking does accept is a lesser fault in place of a worse one, which is
    // why the contrary-motion and hidden rules stay open.
    {FormType::Chaconne, "anti_parallel_perfect"},
    {FormType::Chaconne, "cross_relation"},
    {FormType::Chaconne, "doubling_no_leading_tone"},
    {FormType::Chaconne, "hidden_parallel_fifth"},
    {FormType::Chaconne, "hidden_parallel_octave"},
    {FormType::Chaconne, "strong_beat_dissonance"},
    {FormType::Chaconne, "unprepared_dissonance"},
    {FormType::Chaconne, "vertical_dissonance"},
    // GoldbergVariations
    {FormType::GoldbergVariations, "anti_parallel_perfect"},
    {FormType::GoldbergVariations, "battuta"},
    {FormType::GoldbergVariations, "cross_relation"},
    {FormType::GoldbergVariations, "doubling_no_leading_tone"},
    {FormType::GoldbergVariations, "hidden_parallel_fifth"},
    {FormType::GoldbergVariations, "hidden_parallel_octave"},
    {FormType::GoldbergVariations, "parallel_fifth"},
    {FormType::GoldbergVariations, "parallel_octave"},
    {FormType::GoldbergVariations, "strong_beat_dissonance"},
    {FormType::GoldbergVariations, "unprepared_dissonance"},
    {FormType::GoldbergVariations, "vertical_dissonance"},
};

constexpr std::size_t kOpenRuleCount = sizeof(kOpenRules) / sizeof(kOpenRules[0]);

}  // namespace

bool counterpointRuleIsClosed(FormType form, const std::string& rule_id) {
  if (counterpointRuleGeometry(rule_id) != RuleGeometry::Vertical) {
    return false;
  }
  for (const CounterpointBudgetEntry& entry : kOpenRules) {
    if (entry.form == form && rule_id == entry.rule_id) {
      return false;
    }
  }
  return true;
}

void applyCounterpointBudget(FormType form, ValidationReport* report) {
  if (report == nullptr) {
    return;
  }
  for (const RuleObservation& observation : report->observations) {
    if (observation.total <= 0 || !counterpointRuleIsClosed(form, observation.rule_id)) {
      continue;
    }
    ValidationFailure failure;
    failure.span_id = kInvalidSpanId;
    failure.rule_id = observation.rule_id;
    failure.kind = FailKind::MusicalFail;
    report->failures.push_back(failure);
  }
}

const CounterpointBudgetEntry* counterpointBudgetTable(std::size_t* count) {
  if (count != nullptr) {
    *count = kOpenRuleCount;
  }
  return kOpenRules;
}

}  // namespace bach::composer
