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
    // Fugue -- both true-parallel classes close, through three places where the
    // form chooses rather than stumbles. The stretto lays two verbatim theme
    // statements against each other, so every canon configuration is read before
    // one is committed and a configuration that sounds a true parallel is
    // refused even when it is the only quiet one on offer. The coda's cadence
    // voicing is registered like any other figuration, so the seam that hands
    // the wave over to it is read as a hand-over rather than as a rest -- an
    // unregistered section is silence to every guard downstream of it, and the
    // wave walked into the arrival in octaves for exactly that reason. And the
    // bar-head escape ranks a sustain-window clash below the parallel instead of
    // vetoing on it: against a theme walking in seconds the escape vocabulary is
    // regularly clash-free nowhere, and a veto there hands the onset back to the
    // parallel it was called to remove.
    {FormType::Fugue, "anti_parallel_perfect"},
    {FormType::Fugue, "battuta"},
    {FormType::Fugue, "cross_relation"},
    {FormType::Fugue, "doubling_no_leading_tone"},
    {FormType::Fugue, "hidden_parallel_fifth"},
    {FormType::Fugue, "hidden_parallel_octave"},
    {FormType::Fugue, "invertible_at_octave"},
    {FormType::Fugue, "strong_beat_dissonance"},
    {FormType::Fugue, "unprepared_dissonance"},
    {FormType::Fugue, "vertical_dissonance"},
    // PreludeAndFugue -- the fugue half is assembled by the same section builder,
    // so all three closures above hold here unchanged; the prelude half writes
    // its two voices through the same parallel-aware wave and adds no true
    // parallel of either class.
    {FormType::PreludeAndFugue, "anti_parallel_perfect"},
    {FormType::PreludeAndFugue, "battuta"},
    {FormType::PreludeAndFugue, "cross_relation"},
    {FormType::PreludeAndFugue, "doubling_no_leading_tone"},
    {FormType::PreludeAndFugue, "hidden_parallel_fifth"},
    {FormType::PreludeAndFugue, "hidden_parallel_octave"},
    {FormType::PreludeAndFugue, "invertible_at_octave"},
    {FormType::PreludeAndFugue, "strong_beat_dissonance"},
    {FormType::PreludeAndFugue, "unprepared_dissonance"},
    {FormType::PreludeAndFugue, "vertical_dissonance"},
    // TrioSonata -- `invertible_at_octave` judges the upper pair alone, and in
    // this texture that is exactly the pair the second manual voice is guarded
    // against as it is written. Both true-parallel classes close with it. The
    // pedal is laid down last against two finished manuals, and it separates a
    // true parallel from a hidden one rather than pooling them: its band spans a
    // thirteenth and a triad puts three tones in it, so a guard that treated the
    // two alike had nothing better than the tone it started from. Where all
    // three tones do run out the middle manual takes the arrival instead, and
    // the cadential figure is chosen against the surface it produces on both
    // sides of the resolution it pins the bass under.
    {FormType::TrioSonata, "anti_parallel_perfect"},
    {FormType::TrioSonata, "battuta"},
    {FormType::TrioSonata, "cross_relation"},
    {FormType::TrioSonata, "doubling_no_leading_tone"},
    {FormType::TrioSonata, "hidden_parallel_fifth"},
    {FormType::TrioSonata, "hidden_parallel_octave"},
    {FormType::TrioSonata, "strong_beat_dissonance"},
    {FormType::TrioSonata, "unprepared_dissonance"},
    {FormType::TrioSonata, "vertical_dissonance"},
    // ChoralePrelude -- the cantus firmus never doubles a leading tone, and no
    // true parallel of either class survives its two free ends. The tone before
    // an arrival is re-aimed over a bass pinned to a single octave, and where
    // the consonant window for that re-aim comes back empty it is widened to
    // admit a passing dissonance rather than let the parallel ship. The
    // cadential figure that pins the bass under its own resolution is then
    // chosen against the three-line surface it produces, not installed over one
    // already settled without it. The upper pair carries no octave either, so
    // `invertible_at_octave` closes with them; what the re-aim does accept is a
    // weaker approach in place of a worse one, which is why the contrary-motion
    // and hidden rules stay open.
    //
    // `anti_parallel_perfect` is settled rather than outstanding. What the form
    // writes against it is a single contrary-motion repeat of an octave between
    // the outer voices at a bass approach beat, and it writes it once over the
    // whole request surface. The re-aim that already ranks the true parallel at
    // that beat can rank the contrary repeat beside it, and that does remove the
    // occurrence -- but the tone it displaces is the preparation the dissonances
    // downstream of it lean on, so buying the octave back leaves those
    // unprepared, breaks leading-tone resolutions and opens consecutive leaps,
    // well past the vertical dissonances the move saves. Holding the
    // displacement to chord tones moves none of that, so the cost belongs to
    // moving the tone at all rather than to which tone replaces it. The row
    // stays until a repair appears that does not spend the approach to buy the
    // repeat.
    {FormType::ChoralePrelude, "anti_parallel_perfect", OpenReason::Accepted},
    {FormType::ChoralePrelude, "battuta"},
    {FormType::ChoralePrelude, "cross_relation"},
    {FormType::ChoralePrelude, "hidden_parallel_fifth"},
    {FormType::ChoralePrelude, "hidden_parallel_octave"},
    {FormType::ChoralePrelude, "strong_beat_dissonance"},
    {FormType::ChoralePrelude, "unprepared_dissonance"},
    {FormType::ChoralePrelude, "vertical_dissonance"},
    // ToccataAndFugue -- it shares its section builder with the fantasia, so
    // every closure listed for that form reaches this one, and both true-parallel
    // classes close with them. The one place this form wrote an octave at length
    // is the free section's opening rhetoric, which states its gesture low and
    // restates it exactly twelve semitones below in the neighbouring voice: a
    // registration effect written as two note streams rather than a second part.
    // That bar is now declared as a doubling and the declaration is checked
    // against the notes before it is read, so the pair is folded back into the
    // single line it is instead of being counted as two parts moving in octaves.
    // Nothing else in the form writes one.
    {FormType::ToccataAndFugue, "anti_parallel_perfect"},
    {FormType::ToccataAndFugue, "battuta"},
    {FormType::ToccataAndFugue, "cross_relation"},
    {FormType::ToccataAndFugue, "doubling_no_leading_tone"},
    {FormType::ToccataAndFugue, "hidden_parallel_fifth"},
    {FormType::ToccataAndFugue, "hidden_parallel_octave"},
    {FormType::ToccataAndFugue, "invertible_at_octave"},
    {FormType::ToccataAndFugue, "strong_beat_dissonance"},
    {FormType::ToccataAndFugue, "unprepared_dissonance"},
    {FormType::ToccataAndFugue, "vertical_dissonance"},
    // Passacaglia -- the only form that still doubles a seventh. Both
    // true-parallel classes close against an immutable ground, which leaves the
    // variation as the only side of the pair that can move. Every onset it owns
    // is scrubbed against the ground at beat grain, and the cadential suspension
    // that lands afterwards and rewrites one of those tones now re-reads the same
    // beat-grain reference: a cycle that states the ground in quarters moves
    // three times inside a bar, so a guard that vets against the bar head alone
    // is reading a succession nobody hears and discards the scrub's work on the
    // tone it replaces. `invertible_at_octave` closes with those two classes and
    // for the same reason: it is the parallel octave restricted to the adjacent
    // upper pair, so a texture that writes none at all writes none there either.
    {FormType::Passacaglia, "anti_parallel_perfect"},
    {FormType::Passacaglia, "battuta"},
    {FormType::Passacaglia, "cross_relation"},
    {FormType::Passacaglia, "doubling_no_leading_tone"},
    {FormType::Passacaglia, "doubling_no_seventh"},
    {FormType::Passacaglia, "hidden_parallel_fifth"},
    {FormType::Passacaglia, "hidden_parallel_octave"},
    {FormType::Passacaglia, "strong_beat_dissonance"},
    {FormType::Passacaglia, "unprepared_dissonance"},
    {FormType::Passacaglia, "vertical_dissonance"},
    // FantasiaAndFugue -- its countersubjects already invert at the octave, and
    // both true-parallel classes close. Its stretto reads four canon
    // configurations and refuses one that sounds a true parallel, where it used
    // to state the follower an octave below the leader at a one-bar delay
    // unconditionally. The fill that runs up to the stretto is written before
    // the block rather than after it, so the block's own lines have a preceding
    // bar to be read against. The free section's half-cadence bass and the
    // coda's inner voice both rank the register of a tone whose pitch class is
    // the design value, since walking each voice up from its own band floor puts
    // them a fixed perfect interval apart by construction. And the sustained
    // support leaves the chord for a free diatonic tone once no triad tone in
    // the band would do.
    {FormType::FantasiaAndFugue, "anti_parallel_perfect"},
    {FormType::FantasiaAndFugue, "battuta"},
    {FormType::FantasiaAndFugue, "cross_relation"},
    {FormType::FantasiaAndFugue, "doubling_no_leading_tone"},
    {FormType::FantasiaAndFugue, "hidden_parallel_fifth"},
    {FormType::FantasiaAndFugue, "hidden_parallel_octave"},
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
    // GoldbergVariations -- the imitative blocks are chosen rather than
    // repaired: each spans exactly the period of the immutable bass beneath it,
    // so the whole three-voice surface is read while the one free choice is
    // still open, and the free figuration between them is relieved arrival by
    // arrival. Both true-parallel classes close with that. The choice ranks the
    // two upper voices meeting on one pitch below the parallel it would
    // otherwise be paid for: a meeting costs the pair its audible independence
    // for an onset, a crossing breaks the register order the validator reads off
    // the notes, and only the crossing is a rule.
    //
    // `doubling_no_leading_tone` is settled rather than outstanding. The block
    // chooser ranks no term for it, and giving it one does remove every
    // occurrence -- but the only doubling-free assignment the choice can reach
    // is chromatic, and it pays cross relations several times over what it
    // saves, with vertical and strong-beat dissonances on top, against a smaller
    // return in leading-tone resolutions and augmented melodic intervals. Where
    // the term sits does not change that: ranked among the rules it gives the
    // same assignment as ranked below singability, and ranked as a tiebreak
    // below the continuous shape and travel terms it is never consulted at all.
    // Admitting the second round of configurations reaches the same assignment
    // again, so the candidate set holds one alternative rather than a better one
    // the ranking is failing to find. The chooser has no cross-relation term
    // either, which is what the repair steers into; the row stays until it does.
    {FormType::GoldbergVariations, "anti_parallel_perfect"},
    {FormType::GoldbergVariations, "battuta"},
    {FormType::GoldbergVariations, "cross_relation"},
    {FormType::GoldbergVariations, "doubling_no_leading_tone", OpenReason::Accepted},
    {FormType::GoldbergVariations, "hidden_parallel_fifth"},
    {FormType::GoldbergVariations, "hidden_parallel_octave"},
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
