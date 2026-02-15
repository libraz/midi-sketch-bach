#include "counterpoint/coordinate_voices.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <optional>
#include <utility>

#include "core/interval.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/cross_relation.h"
#include "counterpoint/vertical_safe.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"

namespace bach {
namespace {

bool isSourceIn(BachNoteSource source,
                const std::vector<BachNoteSource>& sources) {
  for (auto src : sources) {
    if (src == source) return true;
  }
  return false;
}

/// Default priority: immutable sources first (0), then by voice descending
/// (lower voices = higher priority).
int defaultPriority(const NoteEvent& note,
                    const std::vector<BachNoteSource>& immutable_sources,
                    uint8_t num_voices) {
  if (isSourceIn(note.source, immutable_sources)) return 0;
  // Lower voices get lower priority numbers (higher priority).
  // Voice num_voices-1 (pedal/lowest) = 1, voice 0 (top) = num_voices.
  return static_cast<int>(num_voices - note.voice);
}

/// Check if a pitch creates harsh dissonance (m2/TT/M7) with sounding notes.
/// Used for weak-beat lightweight validation.
bool hasHarshDissonance(uint8_t pitch, uint8_t voice, Tick tick,
                        const std::vector<NoteEvent>& placed,
                        const WeakBeatDissonancePredicate& allow_pred,
                        uint8_t melodic_prev) {
  for (const auto& note : placed) {
    if (note.voice == voice) continue;
    if (note.start_tick + note.duration <= tick || note.start_tick > tick) continue;
    int simple =
        interval_util::compoundToSimple(absoluteInterval(pitch, note.pitch));
    if (simple == 1 || simple == 6 || simple == 11) {
      if (allow_pred && allow_pred(tick, voice, pitch, note.pitch, simple,
                                   melodic_prev)) {
        continue;
      }
      return true;
    }
  }
  return false;
}

/// Find previous pitch in the same voice from coordinated notes.
uint8_t findPrevPitchInVoice(uint8_t voice, Tick before_tick,
                             const std::vector<NoteEvent>& coordinated) {
  uint8_t best = 0;
  Tick best_tick = 0;
  for (const auto& note : coordinated) {
    if (note.voice != voice) continue;
    if (note.start_tick < before_tick && note.start_tick >= best_tick) {
      best_tick = note.start_tick;
      best = note.pitch;
    }
  }
  return best;
}

}  // namespace

std::vector<NoteEvent> coordinateVoices(std::vector<NoteEvent> all_notes,
                                        const CoordinationConfig& config) {
  BachRuleEvaluator cp_rules(config.num_voices);
  cp_rules.setFreeCounterpoint(true);
  CollisionResolver cp_resolver;
  cp_resolver.setHarmonicTimeline(config.timeline);
  CounterpointState cp_state;
  cp_state.setKey(config.tonic);
  for (uint8_t vid = 0; vid < config.num_voices; ++vid) {
    if (config.voice_range) {
      auto [low, high] = config.voice_range(vid);
      cp_state.registerVoice(vid, low, high);
    }
  }

  // Sort by tick for chronological processing.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Optionally build per-voice next-pitch map for NHT lookahead.
  std::map<std::pair<VoiceId, Tick>, std::optional<uint8_t>> next_pitch_map;
  if (config.use_next_pitch_map) {
    std::array<std::vector<size_t>, 5> voice_indices;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      uint8_t vid = all_notes[idx].voice;
      if (vid < 5) voice_indices[vid].push_back(idx);
    }
    for (uint8_t vid = 0; vid < 5; ++vid) {
      const auto& indices = voice_indices[vid];
      if (indices.size() < 2) continue;
      std::optional<uint8_t> last_candidate;
      Tick last_tick = 0;
      for (size_t pos = indices.size(); pos-- > 0;) {
        Tick this_tick = all_notes[indices[pos]].start_tick;
        if (last_tick > this_tick) {
          next_pitch_map[{vid, this_tick}] = last_candidate;
        }
        last_candidate = all_notes[indices[pos]].pitch;
        last_tick = this_tick;
      }
    }
  }

  std::vector<NoteEvent> coordinated;
  coordinated.reserve(all_notes.size());
  int accepted_count = 0;
  int total_count = 0;

  size_t idx = 0;
  while (idx < all_notes.size()) {
    Tick current_tick = all_notes[idx].start_tick;
    size_t group_end = idx;
    while (group_end < all_notes.size() &&
           all_notes[group_end].start_tick == current_tick) {
      ++group_end;
    }

    // Sort group by priority.
    std::sort(
        all_notes.begin() + static_cast<ptrdiff_t>(idx),
        all_notes.begin() + static_cast<ptrdiff_t>(group_end),
        [&config](const NoteEvent& lhs, const NoteEvent& rhs) {
          if (config.priority) {
            return config.priority(lhs) < config.priority(rhs);
          }
          int pri_lhs = defaultPriority(lhs, config.immutable_sources,
                                        config.num_voices);
          int pri_rhs = defaultPriority(rhs, config.immutable_sources,
                                        config.num_voices);
          return pri_lhs < pri_rhs;
        });

    for (size_t grp = idx; grp < group_end; ++grp) {
      const auto& note = all_notes[grp];
      ++total_count;

      // Tier 1: Immutable -- accept directly.
      if (isSourceIn(note.source, config.immutable_sources)) {
        cp_state.addNote(note.voice, note);
        coordinated.push_back(note);
        ++accepted_count;
        continue;
      }

      // Tier 2: Lightweight -- range + strong-beat consonance + weak-beat
      // harsh dissonance rejection.
      if (isSourceIn(note.source, config.lightweight_sources)) {
        if (config.voice_range) {
          auto [low, high] = config.voice_range(note.voice);
          if (note.pitch < low || note.pitch > high) continue;
        }

        uint8_t beat = beatInBar(note.start_tick);
        if (beat == 0 || beat == 2) {
          // Strong beat: chord-tone + vertical consonance.
          if (config.timeline) {
            const HarmonicEvent& harm = config.timeline->getAt(note.start_tick);
            if (!isChordTone(note.pitch, harm)) {
              // Snap repair: try +/-1, +/-2.
              bool snapped = false;
              for (int delta : {1, -1, 2, -2}) {
                auto [low, high] = config.voice_range(note.voice);
                uint8_t cand =
                    clampPitch(static_cast<int>(note.pitch) + delta, low, high);
                if (isChordTone(cand, harm) &&
                    checkVerticalConsonance(cand, note.voice, note.start_tick,
                                            coordinated, *config.timeline,
                                            config.num_voices)) {
                  NoteEvent fixed = note;
                  fixed.pitch = cand;
                  cp_state.addNote(fixed.voice, fixed);
                  coordinated.push_back(fixed);
                  ++accepted_count;
                  snapped = true;
                  break;
                }
              }
              // Fallback: nearestChordTone for tritone escape in dom7.
              if (!snapped) {
                auto [low, high] = config.voice_range(note.voice);
                uint8_t nct = nearestChordTone(note.pitch, harm);
                if (nct != note.pitch && nct >= low && nct <= high &&
                    checkVerticalConsonance(nct, note.voice, note.start_tick,
                                            coordinated, *config.timeline,
                                            config.num_voices)) {
                  NoteEvent fixed = note;
                  fixed.pitch = nct;
                  cp_state.addNote(fixed.voice, fixed);
                  coordinated.push_back(fixed);
                  ++accepted_count;
                  snapped = true;
                }
              }
              if (!snapped) continue;
              continue;  // NOLINT(readability-redundant-continue) explicit skip after snap
            }
            if (!checkVerticalConsonance(note.pitch, note.voice,
                                         note.start_tick, coordinated,
                                         *config.timeline, config.num_voices)) {
              continue;
            }
          }
        } else {
          // Weak beat: reject m2/TT/M7.
          uint8_t prev = findPrevPitchInVoice(note.voice, note.start_tick,
                                              coordinated);
          if (hasHarshDissonance(note.pitch, note.voice, note.start_tick,
                                 coordinated, config.weak_beat_allow, prev)) {
            continue;
          }
        }

        cp_state.addNote(note.voice, note);
        coordinated.push_back(note);
        ++accepted_count;
        continue;
      }

      // Tier 3: Full createBachNote pipeline.
      BachNoteOptions opts;
      opts.voice = note.voice;
      opts.desired_pitch = note.pitch;
      opts.tick = note.start_tick;
      opts.duration = note.duration;
      opts.velocity = note.velocity;
      opts.source = note.source;

      // NHT lookahead.
      if (config.use_next_pitch_map) {
        auto np_iter = next_pitch_map.find({note.voice, note.start_tick});
        if (np_iter != next_pitch_map.end()) {
          opts.next_pitch = np_iter->second;
        }
      }

      auto result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
      if (result.accepted) {
        // Vertical harsh gate (defense-in-depth, same as Tier 2 check).
        uint8_t prev_harsh = findPrevPitchInVoice(note.voice, note.start_tick,
                                                   coordinated);
        if (hasHarshDissonance(result.note.pitch, note.voice, note.start_tick,
                               coordinated, config.weak_beat_allow,
                               prev_harsh)) {
          bool found_alt = false;
          for (int delta : {1, -1, 2, -2}) {
            if (!config.voice_range) break;
            auto [low, high] = config.voice_range(note.voice);
            auto alt_opts = opts;
            alt_opts.desired_pitch =
                clampPitch(static_cast<int>(note.pitch) + delta, low, high);
            auto alt =
                createBachNote(&cp_state, &cp_rules, &cp_resolver, alt_opts);
            if (alt.accepted &&
                !hasHarshDissonance(alt.note.pitch, note.voice,
                                    note.start_tick, coordinated,
                                    config.weak_beat_allow, prev_harsh)) {
              coordinated.push_back(alt.note);
              ++accepted_count;
              found_alt = true;
              break;
            }
          }
          if (found_alt) continue;
          // No alternative: keep original (soft constraint).
        }
        // Optional cross-relation check.
        if (config.check_cross_relations) {
          if (hasCrossRelation(coordinated, config.num_voices, note.voice,
                               result.note.pitch, note.start_tick)) {
            bool found_alt = false;
            for (int delta : {1, -1, 2, -2}) {
              if (!config.voice_range) break;
              auto [low, high] = config.voice_range(note.voice);
              auto alt_opts = opts;
              alt_opts.desired_pitch = clampPitch(
                  static_cast<int>(note.pitch) + delta, low, high);
              auto alt =
                  createBachNote(&cp_state, &cp_rules, &cp_resolver, alt_opts);
              if (alt.accepted &&
                  !hasCrossRelation(coordinated, config.num_voices, note.voice,
                                    alt.note.pitch, note.start_tick)) {
                coordinated.push_back(alt.note);
                ++accepted_count;
                found_alt = true;
                break;
              }
            }
            if (found_alt) continue;
            // No alternative: keep original (soft constraint).
          }
        }
        coordinated.push_back(result.note);
        ++accepted_count;
      }
    }
    idx = group_end;
  }

  fprintf(stderr, "[%s] coordinateVoices: accepted %d/%d (%.0f%%)\n",
          config.form_name, accepted_count, total_count,
          total_count > 0 ? 100.0 * accepted_count / total_count : 0.0);
  return coordinated;
}

}  // namespace bach
