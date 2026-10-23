#ifndef BACH_COMPOSER_VOICE_ONSET_INDEX_H
#define BACH_COMPOSER_VOICE_ONSET_INDEX_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/basic_types.h"

namespace bach::composer {

/**
 * @brief Voice-grouped onset index over a finished score.
 *
 * Groups note indices by voice and keeps each group ordered by onset, so
 * "which note of this voice sounds at this tick" costs a binary search rather
 * than a scan of the whole note list. The rules that read a finished score ask
 * this question once per voice per onset, which is why it is worth an index.
 *
 * The index aliases the note list it was built from and does not copy it; the
 * caller must keep that list alive and unmodified for the index's lifetime.
 */
class VoiceOnsetIndex {
 public:
  explicit VoiceOnsetIndex(const std::vector<NoteEvent>& notes)
      : notes_(notes), by_voice_(256), reach_(256) {
    std::array<std::size_t, 256> counts{};
    for (const NoteEvent& note : notes)
      ++counts[note.voice];
    for (std::size_t voice = 0; voice < by_voice_.size(); ++voice)
      by_voice_[voice].reserve(counts[voice]);
    for (std::size_t index = 0; index < notes.size(); ++index)
      by_voice_[notes[index].voice].push_back(index);
    for (std::size_t voice = 0; voice < by_voice_.size(); ++voice) {
      auto& indices = by_voice_[voice];
      std::stable_sort(indices.begin(), indices.end(), [&](std::size_t left, std::size_t right) {
        return notes_[left].start_tick < notes_[right].start_tick;
      });
      // reach_[voice][k]: latest end tick among the first k + 1 notes, so a
      // backward search can stop once nothing earlier can still be sounding.
      auto& reach = reach_[voice];
      reach.reserve(indices.size());
      Tick latest_end = 0;
      for (const std::size_t index : indices) {
        latest_end = std::max<Tick>(latest_end, notes_[index].start_tick + notes_[index].duration);
        reach.push_back(latest_end);
      }
    }
  }

  /// @brief Note indices of one voice, ordered by onset (note-list order on ties).
  const std::vector<std::size_t>& voiceNotes(VoiceId voice) const { return by_voice_[voice]; }

  std::size_t startingAt(VoiceId voice, Tick tick) const {
    const auto& indices = by_voice_[voice];
    const auto it = std::lower_bound(
        indices.begin(), indices.end(), tick,
        [&](std::size_t index, Tick value) { return notes_[index].start_tick < value; });
    return it != indices.end() && notes_[*it].start_tick == tick ? *it : notes_.size();
  }

  /// @brief Every note of `voice` that starts exactly at `tick`, in note-list order.
  std::pair<std::vector<std::size_t>::const_iterator, std::vector<std::size_t>::const_iterator>
  startingRange(VoiceId voice, Tick tick) const {
    const auto& indices = by_voice_[voice];
    const auto first = std::lower_bound(
        indices.begin(), indices.end(), tick,
        [&](std::size_t index, Tick value) { return notes_[index].start_tick < value; });
    const auto last = std::upper_bound(
        first, indices.end(), tick,
        [&](Tick value, std::size_t index) { return value < notes_[index].start_tick; });
    return {first, last};
  }

  /// @brief The latest-starting note of `voice` sounding at `tick`, or notes.size().
  std::size_t soundingAt(VoiceId voice, Tick tick) const {
    std::size_t found = notes_.size();
    forEachSoundingAt(voice, tick, [&found](std::size_t index) {
      found = index;
      return false;
    });
    return found;
  }

  /// @brief Visit the notes of `voice` sounding at `tick`, latest onset first.
  ///
  /// `visit` returns true to continue and false to stop. Notes sharing an onset
  /// are visited in reverse note-list order.
  template <typename Visit>
  void forEachSoundingAt(VoiceId voice, Tick tick, Visit visit) const {
    const auto& indices = by_voice_[voice];
    const auto& reach = reach_[voice];
    auto it = std::upper_bound(
        indices.begin(), indices.end(), tick,
        [&](Tick value, std::size_t index) { return value < notes_[index].start_tick; });
    while (it != indices.begin()) {
      --it;
      if (reach[static_cast<std::size_t>(it - indices.begin())] <= tick)
        return;
      const NoteEvent& note = notes_[*it];
      if (note.start_tick <= tick && tick < note.start_tick + note.duration && !visit(*it))
        return;
    }
  }

  std::uint8_t pitchAt(VoiceId voice, Tick tick) const {
    const std::size_t index = soundingAt(voice, tick);
    return index < notes_.size() ? notes_[index].pitch : 0;
  }

  /// @brief Tick this voice's sound at `tick` runs out at, or `tick` when silent.
  ///
  /// Walking the union of onsets cannot see a rest that opens and closes
  /// strictly between two of them: nothing starts inside a silence, so the walk
  /// steps over it and reads the tones on either side as consecutive motion. A
  /// succession rule asks this whether the sound it heard at the previous onset
  /// lasted all the way to the current one.
  Tick soundEndAt(VoiceId voice, Tick tick) const {
    const std::size_t index = soundingAt(voice, tick);
    return index < notes_.size() ? notes_[index].start_tick + notes_[index].duration : tick;
  }

  /// @brief True when this voice strikes a note strictly between two ticks.
  ///
  /// Succession between two voices is not a distance in ticks. Two tones follow
  /// one another only when neither of the two voices speaks in between; a rule
  /// that asks "within a beat" instead admits pairs with a whole figure
  /// standing between them, which is a different relation and one the ear does
  /// not hear as succession at all.
  ///
  /// @param voice The voice to search.
  /// @param after Exclusive lower bound.
  /// @param before Exclusive upper bound.
  /// @return True when an onset of `voice` lies in the open interval.
  bool hasOnsetBetween(VoiceId voice, Tick after, Tick before) const {
    const auto& indices = by_voice_[voice];
    const auto it = std::upper_bound(
        indices.begin(), indices.end(), after,
        [&](Tick value, std::size_t index) { return value < notes_[index].start_tick; });
    return it != indices.end() && notes_[*it].start_tick < before;
  }

 private:
  const std::vector<NoteEvent>& notes_;
  std::vector<std::vector<std::size_t>> by_voice_;
  std::vector<std::vector<Tick>> reach_;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VOICE_ONSET_INDEX_H
