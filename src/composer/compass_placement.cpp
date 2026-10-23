#include "composer/compass_placement.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <utility>
#include <vector>

#include "core/instrument_program.h"
#include "core/pitch_utils.h"

namespace bach::composer {

namespace {

// One candidate placement: an octave offset per note for one output offset.
struct Plan {
  int octave_shift = 0;
  std::vector<int> delta;
  std::size_t moved = 0;
  std::size_t crossing = 0;
  std::size_t pinned = 0;
};

bool soundsAt(const NoteEvent& note, Tick tick) {
  return note.start_tick <= tick && tick < note.start_tick + note.duration;
}

class Planner {
 public:
  Planner(const ComposeResult& result, const Material& material, const HarmonicPlan& harmony,
          InstrumentPitchRange range, Tick ticks_per_bar)
      : notes_(result.notes), material_(material), range_(range) {
    const Tick bar_ticks = ticks_per_bar > 0 ? ticks_per_bar : kTicksPerBar;
    bar_.resize(notes_.size());
    for (std::size_t idx = 0; idx < notes_.size(); ++idx) {
      // An ornament tone belongs to the bar of the note it decorates, so a
      // figure is never split across a moved and an unmoved bar.
      const bool authored =
          idx < result.provenance.size() && result.provenance[idx].has_authored_note;
      const Tick anchor =
          authored ? result.provenance[idx].authored_start_tick : notes_[idx].start_tick;
      bar_[idx] = static_cast<std::size_t>(anchor / bar_ticks);
    }
    bar_ticks_ = bar_ticks;
    markPinned(harmony);
  }

  // Moves bars until every note sounds inside the compass at `offset`.
  Plan plan(int offset) const {
    Plan out;
    out.delta.assign(notes_.size(), 0);
    // Each round moves the earliest outside note inward, so the bound is never
    // reached by a score that can be placed at all.
    for (std::size_t round = 0; round <= notes_.size(); ++round) {
      std::size_t first = notes_.size();
      for (std::size_t idx = 0; idx < notes_.size(); ++idx) {
        if (side(idx, out.delta, offset) != 0 &&
            (first == notes_.size() || bar_[idx] < bar_[first] ||
             (bar_[idx] == bar_[first] && notes_[idx].start_tick < notes_[first].start_tick)))
          first = idx;
      }
      if (first == notes_.size())
        break;
      moveBar(bar_[first], -side(first, out.delta, offset) * interval::kOctave, offset, &out);
    }
    return out;
  }

 private:
  // Notes a declaration pins to an absolute pitch, which the validator reads
  // back against that declaration rather than against the authored note.
  void markPinned(const HarmonicPlan& harmony) {
    pinned_.assign(notes_.size(), false);
    const auto pinWindow = [&](VoiceId voice, Tick begin, Tick end) {
      for (std::size_t idx = 0; idx < notes_.size(); ++idx) {
        if (notes_[idx].voice == voice && notes_[idx].start_tick >= begin &&
            notes_[idx].start_tick < end)
          pinned_[idx] = true;
      }
    };
    const auto pinSounding = [&](Tick tick) {
      for (std::size_t idx = 0; idx < notes_.size(); ++idx) {
        if (soundsAt(notes_[idx], tick))
          pinned_[idx] = true;
      }
    };
    for (const SequenceTemplate& tmpl : material_.sequence_templates) {
      Tick step = tmpl.step_length_ticks;
      if (step == 0) {
        for (Tick duration : tmpl.seed_durations)
          step += duration;
      }
      pinWindow(tmpl.voice, tmpl.target_start_tick,
                tmpl.target_start_tick + step * static_cast<Tick>(tmpl.num_steps));
    }
    for (const SuspensionPattern& pattern : material_.suspension_patterns)
      pinWindow(pattern.voice, pattern.preparation_tick, pattern.resolution_tick + 1);
    // A cadence formula is read from the approach beat, the instant before the
    // arrival and the arrival itself.
    for (const CadenceEvent& cadence : harmony.cadences) {
      if (cadence.tick >= kTicksPerBeat)
        pinSounding(cadence.tick - kTicksPerBeat);
      if (cadence.tick >= 1)
        pinSounding(cadence.tick - 1);
      pinSounding(cadence.tick);
    }
    for (const CadenceCell& cell : material_.cadence_cells) {
      pinSounding(cell.approach_tick);
      pinSounding(cell.cadence_tick);
    }
  }

  int pitch(std::size_t idx, const std::vector<int>& delta) const {
    return static_cast<int>(notes_[idx].pitch) + delta[idx];
  }

  // +1 above the compass, -1 below it, 0 inside.
  int side(std::size_t idx, const std::vector<int>& delta, int offset) const {
    const int sounding = pitch(idx, delta) + offset;
    if (sounding > static_cast<int>(range_.high))
      return 1;
    if (sounding < static_cast<int>(range_.low))
      return -1;
    return 0;
  }

  // Voices the group must take along: every voice doubling one of its members
  // in a window that reaches this bar.
  void closeOverDoublings(std::size_t bar_index, std::vector<VoiceId>* group) const {
    const Tick bar_start = static_cast<Tick>(bar_index) * bar_ticks_;
    const Tick bar_end = bar_start + bar_ticks_;
    for (bool grew = true; grew;) {
      grew = false;
      for (const DoublingWindow& window : material_.declared_doublings) {
        if (window.end_tick <= bar_start || window.start_tick >= bar_end)
          continue;
        const bool has_lead =
            std::find(group->begin(), group->end(), window.lead_voice) != group->end();
        const bool has_doubled =
            std::find(group->begin(), group->end(), window.doubled_voice) != group->end();
        if (has_lead != has_doubled) {
          group->push_back(has_lead ? window.doubled_voice : window.lead_voice);
          grew = true;
        }
      }
    }
  }

  // Whether moving `members` by `shift` keeps them inside the compass and, when
  // `keep_order`, on the same side of every overlapping note left in place.
  bool fits(const std::vector<std::size_t>& members, int shift, int offset,
            const std::vector<int>& delta, bool keep_order) const {
    std::vector<bool> member(notes_.size(), false);
    for (std::size_t idx : members)
      member[idx] = true;
    for (std::size_t idx : members) {
      const int moved = pitch(idx, delta) + shift;
      if (moved + offset > static_cast<int>(range_.high) ||
          moved + offset < static_cast<int>(range_.low) || moved < 0 || moved > 127)
        return false;
      if (!keep_order)
        continue;
      const Tick begin = notes_[idx].start_tick;
      const Tick end = begin + notes_[idx].duration;
      for (std::size_t other = 0; other < notes_.size(); ++other) {
        if (member[other] || notes_[other].voice == notes_[idx].voice)
          continue;
        const Tick other_begin = notes_[other].start_tick;
        if (other_begin >= end || begin >= other_begin + notes_[other].duration)
          continue;
        const int before = pitch(idx, delta) - pitch(other, delta);
        const int after = moved - pitch(other, delta);
        if (before == 0 || after == 0 || (before > 0) != (after > 0))
          return false;
      }
    }
    return true;
  }

  void moveBar(std::size_t bar_index, int shift, int offset, Plan* out) const {
    const int outward = -shift / interval::kOctave;
    // Voices with a note in this bar, ordered from the edge the move starts at
    // by their mean pitch here.
    std::vector<std::pair<double, VoiceId>> ranked;
    std::vector<VoiceId> outside;
    for (std::size_t idx = 0; idx < notes_.size(); ++idx) {
      if (bar_[idx] != bar_index)
        continue;
      const VoiceId voice = notes_[idx].voice;
      if (side(idx, out->delta, offset) == outward &&
          std::find(outside.begin(), outside.end(), voice) == outside.end())
        outside.push_back(voice);
      if (std::none_of(ranked.begin(), ranked.end(),
                       [voice](const auto& entry) { return entry.second == voice; })) {
        double sum = 0.0;
        int count = 0;
        for (std::size_t other = 0; other < notes_.size(); ++other) {
          if (bar_[other] == bar_index && notes_[other].voice == voice) {
            sum += pitch(other, out->delta);
            ++count;
          }
        }
        ranked.emplace_back(sum / count, voice);
      }
    }
    std::sort(ranked.begin(), ranked.end(), [outward](const auto& lhs, const auto& rhs) {
      if (lhs.first != rhs.first)
        return outward > 0 ? lhs.first > rhs.first : lhs.first < rhs.first;
      return lhs.second < rhs.second;
    });

    const auto membersOf = [&](const std::vector<VoiceId>& voices) {
      std::vector<std::size_t> members;
      for (std::size_t idx = 0; idx < notes_.size(); ++idx) {
        if (bar_[idx] == bar_index &&
            std::find(voices.begin(), voices.end(), notes_[idx].voice) != voices.end())
          members.push_back(idx);
      }
      return members;
    };
    const auto anyPinned = [&](const std::vector<std::size_t>& members) {
      return std::any_of(members.begin(), members.end(),
                         [&](std::size_t idx) { return pinned_[idx]; });
    };
    const auto apply = [&](const std::vector<std::size_t>& members, bool crossing) {
      for (std::size_t idx : members) {
        out->delta[idx] += shift;
        if (pinned_[idx])
          ++out->pinned;
      }
      out->moved += members.size();
      if (crossing)
        out->crossing += members.size();
    };

    // The smallest run of adjacent voices, from the moving edge, that takes
    // every voice outside the compass, crosses nothing left in place and holds
    // no pinned note.
    std::vector<VoiceId> group;
    for (const auto& entry : ranked) {
      group.push_back(entry.second);
      const bool covers = std::all_of(outside.begin(), outside.end(), [&group](VoiceId voice) {
        return std::find(group.begin(), group.end(), voice) != group.end();
      });
      if (!covers)
        continue;
      std::vector<VoiceId> closed = group;
      closeOverDoublings(bar_index, &closed);
      const std::vector<std::size_t> members = membersOf(closed);
      if (!anyPinned(members) && fits(members, shift, offset, out->delta, /*keep_order=*/true)) {
        apply(members, /*crossing=*/false);
        return;
      }
    }
    // No such group: the outside voices move alone, crossing what they must.
    std::vector<VoiceId> alone = outside;
    closeOverDoublings(bar_index, &alone);
    const std::vector<std::size_t> members = membersOf(alone);
    if (fits(members, shift, offset, out->delta, /*keep_order=*/false)) {
      apply(members, /*crossing=*/true);
      return;
    }
    // A bar wider than the compass in one voice: only its outside notes move.
    std::vector<std::size_t> stray;
    for (std::size_t idx : members) {
      if (side(idx, out->delta, offset) == outward)
        stray.push_back(idx);
    }
    apply(stray, /*crossing=*/true);
  }

  const std::vector<NoteEvent>& notes_;
  const Material& material_;
  InstrumentPitchRange range_;
  Tick bar_ticks_ = kTicksPerBar;
  std::vector<std::size_t> bar_;
  std::vector<bool> pinned_;
};

}  // namespace

std::optional<CompassPlacement> placeInOutputCompass(
    ComposeResult* result, const Material& material, const HarmonicPlan& harmony, Key key,
    InstrumentType instrument, Tick ticks_per_bar,
    const std::function<bool(const ComposeResult&)>& accept) {
  const std::optional<int> nearest = selectOutputOctaveShift(result->notes, key, instrument);
  if (!nearest)
    return std::nullopt;
  CompassPlacement placement;
  placement.octave_shift = *nearest;
  const InstrumentPitchRange range = pitchRangeFor(instrument);
  if (!range.hard_limit || result->notes.empty())
    return placement;

  const int key_shift = keyTranspositionSemitones(key);
  int low = 127;
  int high = 0;
  for (const NoteEvent& note : result->notes) {
    low = std::min(low, static_cast<int>(note.pitch) + key_shift);
    high = std::max(high, static_cast<int>(note.pitch) + key_shift);
  }

  // Every displacement leaving less than an octave outside on either side is a
  // candidate, ranked by pinned notes moved, then notes moved across voices,
  // then notes moved; a whole-score fit moves none and wins outright.
  const Planner planner(*result, material, harmony, range, ticks_per_bar);
  std::vector<Plan> plans;
  for (int octaves = -10; octaves <= 10; ++octaves) {
    const int shift = octaves * interval::kOctave;
    if (static_cast<int>(range.low) - (low + shift) >= interval::kOctave ||
        (high + shift) - static_cast<int>(range.high) >= interval::kOctave)
      continue;
    Plan candidate = planner.plan(key_shift + shift);
    candidate.octave_shift = shift;
    plans.push_back(std::move(candidate));
  }
  const auto rank = [](const Plan& plan) {
    return std::make_tuple(plan.pinned, plan.crossing, plan.moved, std::abs(plan.octave_shift),
                           plan.octave_shift);
  };
  std::sort(plans.begin(), plans.end(),
            [&rank](const Plan& lhs, const Plan& rhs) { return rank(lhs) < rank(rhs); });

  for (const Plan& plan : plans) {
    if (plan.moved == 0) {
      placement.octave_shift = plan.octave_shift;
      return placement;
    }
    ComposeResult moved = *result;
    for (std::size_t idx = 0; idx < moved.notes.size(); ++idx) {
      const int delta = plan.delta[idx];
      if (delta == 0)
        continue;
      moved.notes[idx].pitch = static_cast<std::uint8_t>(moved.notes[idx].pitch + delta);
      if (idx < moved.provenance.size() && moved.provenance[idx].has_authored_note) {
        moved.provenance[idx].authored_pitch =
            static_cast<std::uint8_t>(moved.provenance[idx].authored_pitch + delta);
      }
    }
    if (accept(moved)) {
      *result = std::move(moved);
      placement.octave_shift = plan.octave_shift;
      placement.displaced_notes = plan.moved;
      placement.crossing_notes = plan.crossing;
      placement.pinned_notes = plan.pinned;
      return placement;
    }
  }
  return placement;
}

}  // namespace bach::composer
