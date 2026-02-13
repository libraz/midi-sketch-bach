// Leap resolution implementation -- resolves unresolved melodic leaps by
// inserting contrary-step motion, with multiple protection conditions to
// preserve structural and musically meaningful note patterns.

#include "counterpoint/leap_resolution.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/scale.h"

namespace bach {

namespace leap_detail {

bool isLeadingTone(uint8_t pitch, Key key, ScaleType scale) {
  // Natural minor has a subtonic (whole step below tonic), not a leading tone.
  if (scale == ScaleType::NaturalMinor) return false;

  int leading_tone_pc = (static_cast<int>(key) + 11) % 12;
  return getPitchClass(pitch) == leading_tone_pc;
}

bool isTendencyResolution(uint8_t prev_pitch, uint8_t curr_pitch,
                          Key key, ScaleType scale) {
  int prev_pc = getPitchClass(prev_pitch);
  int curr_pc = getPitchClass(curr_pitch);
  int tonic_pc = static_cast<int>(key);

  // Leading tone -> tonic: ascending semitone (ti -> do).
  if (isLeadingTone(prev_pitch, key, scale) && curr_pc == tonic_pc) {
    int motion = static_cast<int>(curr_pitch) - static_cast<int>(prev_pitch);
    if (motion == 1) return true;
  }

  // 4th degree -> 3rd degree: descending (fa -> mi).
  int fourth_degree_pc = (tonic_pc + 5) % 12;
  if (prev_pc == fourth_degree_pc) {
    bool is_minor = (scale == ScaleType::NaturalMinor ||
                     scale == ScaleType::HarmonicMinor ||
                     scale == ScaleType::MelodicMinor ||
                     scale == ScaleType::Dorian);
    int third_degree_pc = is_minor ? (tonic_pc + 3) % 12 : (tonic_pc + 4) % 12;
    if (curr_pc == third_degree_pc) {
      int motion = static_cast<int>(prev_pitch) - static_cast<int>(curr_pitch);
      if (motion > 0) return true;  // Descending.
    }
  }

  // 7th degree above root -> 6th degree: descending step (prepared 7th resolving).
  int seventh_pc = (tonic_pc + 11) % 12;  // Major 7th above root.
  int minor_seventh_pc = (tonic_pc + 10) % 12;  // Minor 7th above root.
  if (prev_pc == seventh_pc || prev_pc == minor_seventh_pc) {
    int descent = static_cast<int>(prev_pitch) - static_cast<int>(curr_pitch);
    if (descent >= 1 && descent <= 2) return true;  // Descending step.
  }

  return false;
}

bool isSequencePattern(const uint8_t* pitches, int count) {
  if (count < 5) return false;

  // Compute signed intervals between consecutive pitches.
  int intervals[4];
  for (int idx = 0; idx < 4; ++idx) {
    intervals[idx] = static_cast<int>(pitches[idx + 1]) -
                     static_cast<int>(pitches[idx]);
  }

  // Two-interval pattern repeating: (a, b, a, b).
  return intervals[0] == intervals[2] && intervals[1] == intervals[3];
}

}  // namespace leap_detail

int resolveLeaps(std::vector<NoteEvent>& notes,
                 const LeapResolutionParams& params) {
  if (notes.size() < 3 || params.num_voices == 0) return 0;

  // Sort by (voice, start_tick) for per-voice processing.
  std::sort(notes.begin(), notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice) return lhs.voice < rhs.voice;
    return lhs.start_tick < rhs.start_tick;
  });

  int modified = 0;

  // Build per-voice index lists.
  for (uint8_t vid = 0; vid < params.num_voices; ++vid) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < notes.size(); ++idx) {
      if (notes[idx].voice == vid) idxs.push_back(idx);
    }

    int len = static_cast<int>(idxs.size());
    for (int pos = 0; pos + 2 < len; ++pos) {
      size_t i0 = idxs[pos];
      size_t i1 = idxs[pos + 1];
      size_t i2 = idxs[pos + 2];

      // F-B: Skip notes already modified by LeapResolution (vibration prevention).
      if (notes[i2].modified_by & static_cast<uint8_t>(NoteModifiedBy::LeapResolution)) {
        continue;
      }

      int leap = static_cast<int>(notes[i1].pitch) - static_cast<int>(notes[i0].pitch);
      int abs_leap = std::abs(leap);
      if (abs_leap < params.leap_threshold) continue;

      // === Protection conditions ===

      // P1: Source protection (Immutable/Structural -> skip).
      if (getProtectionLevel(notes[i2].source) != ProtectionLevel::Flexible) continue;

      // PA: Bar-line crossing protection (SP-A: tonic cadence limited).
      if (notes[i1].start_tick / kTicksPerBar != notes[i2].start_tick / kTicksPerBar) {
        if (params.is_chord_tone) {
          // Next bar is tonic harmony -> cadence, protect.
          Key next_key = params.key_at_tick(notes[i2].start_tick);
          if (params.is_chord_tone(notes[i2].start_tick,
                                   static_cast<uint8_t>(next_key))) {
            continue;
          }
          // Non-tonic -> allow modification (fall through).
        } else {
          continue;  // No harmonic info -> safe side, protect.
        }
      }

      Key key = params.key_at_tick(notes[i2].start_tick);
      ScaleType scale = params.scale_at_tick(notes[i2].start_tick);

      // P2: Strong-beat protection (FB3 + SP-D).
      if (notes[i2].start_tick % kTicksPerBeat == 0) {
        if (leap_detail::isLeadingTone(notes[i2].pitch, key, scale)) {
          if (abs_leap < 7) continue;  // P4 leap + strong-beat leading tone -> protect.
          // P5+ leap + strong-beat leading tone -> allow modification.
        } else {
          continue;  // Strong beat + non-leading-tone -> protect.
        }
      }

      // P3: Resolution check (FB1 + SP-B + F-C).
      int motion = static_cast<int>(notes[i2].pitch) - static_cast<int>(notes[i1].pitch);
      bool contrary = (leap > 0 && motion < 0) || (leap < 0 && motion > 0);
      bool step = std::abs(motion) <= 2;
      if (contrary && step) {
        if (!params.is_chord_tone) {
          continue;  // No harmonic info -> contrary step = resolved.
        }
        if (params.is_chord_tone(notes[i2].start_tick, notes[i2].pitch)) {
          continue;  // Chord tone -> resolved.
        }
        if (leap_detail::isTendencyResolution(notes[i1].pitch, notes[i2].pitch,
                                               key, scale)) {
          continue;  // Tendency resolution (ti->do, fa->mi, 7th->6th) -> resolved.
        }
        // SP-B: Previous harmony's chord tone also counts as resolved.
        if (notes[i1].start_tick != notes[i2].start_tick) {
          if (params.is_chord_tone(notes[i1].start_tick, notes[i2].pitch)) {
            continue;
          }
        }
      }

      // P4: Same-direction step chain protection (SP-C: 2 consecutive only).
      if (pos + 4 < len) {
        size_t i3 = idxs[pos + 3];
        size_t i4 = idxs[pos + 4];
        int dir_a = (notes[i2].pitch > notes[i1].pitch)  ? 1
                    : (notes[i2].pitch < notes[i1].pitch) ? -1
                                                          : 0;
        int dir_b = (notes[i3].pitch > notes[i2].pitch)  ? 1
                    : (notes[i3].pitch < notes[i2].pitch) ? -1
                                                          : 0;
        int dir_c = (notes[i4].pitch > notes[i3].pitch)  ? 1
                    : (notes[i4].pitch < notes[i3].pitch) ? -1
                                                          : 0;
        if (dir_a == dir_b && dir_b == dir_c && dir_a != 0) {
          int step_a = std::abs(static_cast<int>(notes[i2].pitch) -
                                static_cast<int>(notes[i1].pitch));
          int step_b = std::abs(static_cast<int>(notes[i3].pitch) -
                                static_cast<int>(notes[i2].pitch));
          if (step_a <= 2 && step_b <= 2) {
            continue;  // 2 consecutive same-direction steps -> scalar run, protect.
          }
        }
      }

      // P5: Chord-tone landing protection.
      if (params.is_chord_tone &&
          params.is_chord_tone(notes[i2].start_tick, notes[i2].pitch)) {
        continue;
      }

      // PF: Sequence pattern protection (F-A).
      if (pos + 4 < len) {
        uint8_t seq_pitches[5] = {
            notes[i0].pitch, notes[i1].pitch, notes[i2].pitch,
            notes[idxs[pos + 3]].pitch, notes[idxs[pos + 4]].pitch};
        if (leap_detail::isSequencePattern(seq_pitches, 5)) {
          continue;  // Sequence motif -> protect.
        }
      }

      // === Candidate search ===
      int resolve_dir = (leap > 0) ? -1 : 1;
      int best_pitch = -1;
      bool best_is_ct = false;

      for (int offset = 1; offset <= 2; ++offset) {
        int cand = static_cast<int>(notes[i1].pitch) + resolve_dir * offset;
        if (cand < 0 || cand > 127) continue;
        auto cand_u8 = static_cast<uint8_t>(cand);

        if (!scale_util::isScaleTone(cand_u8, key, scale)) continue;

        if (params.voice_range) {
          auto [low, high] = params.voice_range(vid);
          if (cand_u8 < low || cand_u8 > high) continue;
        }

        if (params.vertical_safe &&
            !params.vertical_safe(notes[i2].start_tick, vid, cand_u8)) {
          continue;
        }

        bool cand_is_ct = params.is_chord_tone
                              ? params.is_chord_tone(notes[i2].start_tick, cand_u8)
                              : false;

        if (best_pitch < 0 || (cand_is_ct && !best_is_ct)) {
          best_pitch = cand;
          best_is_ct = cand_is_ct;
        }
      }

      if (best_pitch >= 0 && static_cast<uint8_t>(best_pitch) != notes[i2].pitch) {
        notes[i2].pitch = static_cast<uint8_t>(best_pitch);
        notes[i2].modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
        ++modified;
      }
    }
  }

  return modified;
}

}  // namespace bach
