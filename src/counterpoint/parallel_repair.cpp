// Shared parallel perfect consonance repair.
// Consolidates duplicate logic from postValidateNotes() and FugueGenerator.

#include "counterpoint/parallel_repair.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <vector>

#include "core/interval.h"
#include "core/note_source.h"
#include "core/scale.h"

namespace bach {

namespace {

/// Context for a single shift-candidate validation pass.
/// Encapsulates the modal differences between direct-fix (shift at current tick)
/// and adjacent-fix (shift at previous tick) so that tryShiftCandidate can
/// implement both paths with a single validation pipeline.
struct ShiftContext {
  int note_idx;            ///< Index of note being shifted.
  uint8_t voice;           ///< Voice of note being shifted.
  Tick check_tick;         ///< Tick for consonance/crossing checks (direct: t, adjacent: pt).
  Tick prev_tick;          ///< Previous beat tick (pt).
  Tick curr_tick;          ///< Current beat tick (t).
  int pitch_at_next_tick;  ///< For adjacent: unchanged pitch at t. -1 for direct mode.
  // Original parallel context (for adjacent: verify break).
  int orig_prev_pitch_a;  ///< Previous pitch in voice A (pp).
  int orig_prev_pitch_b;  ///< Previous pitch in voice B (pb2).
  int orig_curr_simple;   ///< Simple interval at current tick (cs).
  uint8_t voice_a;        ///< Voice A of the detected parallel.
  uint8_t voice_b;        ///< Voice B of the detected parallel.
  int curr_pitch_a;       ///< Current pitch in voice A (ca).
  int curr_pitch_b;       ///< Current pitch in voice B (cb).
  bool check_crossing;    ///< true for direct fix, false for adjacent.
};

/// @brief Try each shift candidate in order. Return the first valid delta, or nullopt.
///
/// Validates each candidate pitch shift against MIDI range, diatonic scale,
/// voice range, melodic leap, new-parallel, strong-beat consonance, and
/// (optionally) voice-crossing constraints.  In adjacent mode (pitch_at_next_tick >= 0),
/// also checks leap to next tick and verifies the shift actually breaks the
/// original detected parallel.
///
/// @param notes Current note list (read-only for validation).
/// @param params Repair parameters (key, scale, voice range, num_voices).
/// @param ctx Shift context describing the note, ticks, and original parallel.
/// @param shifts Array of candidate deltas to try.
/// @param num_shifts Number of elements in shifts.
/// @param sound_pitch Sounding pitch lookup: (voice, tick) -> pitch or -1.
/// @param find_prev_pitch Previous-note pitch lookup: (voice, start_tick, exclude_idx) -> pitch.
/// @return First valid delta, or std::nullopt if no candidate passes.
std::optional<int> tryShiftCandidate(
    const std::vector<NoteEvent>& notes,
    const ParallelRepairParams& params,
    const ShiftContext& ctx,
    const int* shifts, int num_shifts,
    const std::function<int(uint8_t, Tick)>& sound_pitch,
    const std::function<int(uint8_t, Tick, int)>& find_prev_pitch) {
  const int base_pitch = static_cast<int>(notes[ctx.note_idx].pitch);
  const Key note_key = params.key_at_tick(notes[ctx.note_idx].start_tick);
  const bool is_adjacent = (ctx.pitch_at_next_tick >= 0);

  for (int idx = 0; idx < num_shifts; ++idx) {
    int delta = shifts[idx];
    int candidate = base_pitch + delta;

    // 1. MIDI range check.
    if (candidate < 0 || candidate > 127) continue;
    uint8_t candidate_u8 = static_cast<uint8_t>(candidate);

    // 2. Diatonic scale check for step shifts (not octave shifts).
    if (std::abs(delta) <= 4 &&
        !scale_util::isScaleTone(candidate_u8, note_key, params.scale))
      continue;

    // 3. Voice range check.
    auto [range_lo, range_hi] = params.voice_range
        ? params.voice_range(ctx.voice, notes[ctx.note_idx].start_tick)
        : params.voice_range_static(ctx.voice);
    if (candidate_u8 < range_lo || candidate_u8 > range_hi) continue;

    // 4. Melodic leap: no more than octave from previous note in voice.
    {
      int prev_p = find_prev_pitch(ctx.voice, notes[ctx.note_idx].start_tick,
                                   ctx.note_idx);
      if (prev_p >= 0 && std::abs(candidate - prev_p) > 12) continue;
    }

    // 5. Adjacent-mode specific checks.
    if (is_adjacent) {
      // 5a. Melodic leap from pt to t must not exceed an octave.
      if (std::abs(candidate - ctx.pitch_at_next_tick) > 12) continue;

      // 5b. Verify this shift actually breaks the original parallel.
      int new_prev_a = (ctx.voice == ctx.voice_a)
          ? candidate : ctx.orig_prev_pitch_a;
      int new_prev_b = (ctx.voice == ctx.voice_b)
          ? candidate : ctx.orig_prev_pitch_b;
      int new_pi = std::abs(new_prev_a - new_prev_b);
      int orig_ci = std::abs(ctx.curr_pitch_a - ctx.curr_pitch_b);
      bool still_par = false;
      if (interval_util::isPerfectConsonance(new_pi) &&
          interval_util::isPerfectConsonance(orig_ci)) {
        int new_ps = interval_util::compoundToSimple(new_pi);
        if (new_ps == ctx.orig_curr_simple) {
          int nm_a = ctx.curr_pitch_a - new_prev_a;
          int nm_b = ctx.curr_pitch_b - new_prev_b;
          if (nm_a != 0 && nm_b != 0 && (nm_a > 0) == (nm_b > 0))
            still_par = true;
        }
      }
      if (still_par) continue;
    }

    // 6. Check no new parallel with any other voice.
    //
    // In direct mode the candidate replaces the note at curr_tick, so:
    //   shifted voice: prev = soundPitch(voice, prev_tick), curr = candidate.
    // In adjacent mode the candidate replaces the note at prev_tick, so:
    //   shifted voice: prev = candidate, curr = pitch_at_next_tick.
    int shifted_prev = is_adjacent ? candidate
                                   : sound_pitch(ctx.voice, ctx.prev_tick);
    int shifted_curr = is_adjacent ? ctx.pitch_at_next_tick : candidate;
    {
      bool new_par = false;
      for (uint8_t ov_idx = 0; ov_idx < params.num_voices && !new_par;
           ++ov_idx) {
        if (ov_idx == ctx.voice) continue;
        int ov_curr = sound_pitch(ov_idx, ctx.curr_tick);
        int ov_prev = sound_pitch(ov_idx, ctx.prev_tick);
        if (ov_curr < 0 || ov_prev < 0) continue;
        // In direct mode, shifted_prev may be -1 if voice is silent at pt.
        if (shifted_prev < 0) continue;
        int prev_interval = std::abs(shifted_prev - ov_prev);
        int curr_interval = std::abs(shifted_curr - ov_curr);
        if (interval_util::isPerfectConsonance(prev_interval) &&
            interval_util::isPerfectConsonance(curr_interval)) {
          int prev_simple = interval_util::compoundToSimple(prev_interval);
          int curr_simple = interval_util::compoundToSimple(curr_interval);
          if (prev_simple == curr_simple) {
            int motion_shifted = shifted_curr - shifted_prev;
            int motion_other = ov_curr - ov_prev;
            if (motion_shifted != 0 && motion_other != 0 &&
                (motion_shifted > 0) == (motion_other > 0))
              new_par = true;
          }
        }
      }
      if (new_par) continue;
    }

    // 7. Strong-beat consonance check: on beats 1 and 3 (4/4), reject
    //    candidates that create dissonance with any active voice.
    //    Uses check_tick (direct: t, adjacent: pt).  The candidate pitch
    //    is always the pitch sounding at check_tick after the shift.
    {
      Tick beat = (ctx.check_tick % kTicksPerBar) / kTicksPerBeat;
      if (beat == 0 || beat == 2) {
        bool dissonant = false;
        for (uint8_t ov_idx = 0;
             ov_idx < params.num_voices && !dissonant; ++ov_idx) {
          if (ov_idx == ctx.voice) continue;
          int ov_pitch = sound_pitch(ov_idx, ctx.check_tick);
          if (ov_pitch < 0) continue;
          int ivl = interval_util::compoundToSimple(
              std::abs(candidate - ov_pitch));
          if (!interval_util::isConsonance(ivl)) {
            // P4 is acceptable in 3+ voice contexts.
            if (!(params.num_voices >= 3 && ivl == 5)) {
              dissonant = true;
            }
          }
        }
        if (dissonant) continue;
      }
    }

    // 8. Voice crossing check (direct fix only): reject if shift creates
    //    crossing with any adjacent voice.
    if (ctx.check_crossing) {
      bool crosses = false;
      for (uint8_t ov_idx = 0;
           ov_idx < params.num_voices && !crosses; ++ov_idx) {
        if (ov_idx == ctx.voice) continue;
        int ov_pitch = sound_pitch(ov_idx, ctx.check_tick);
        if (ov_pitch < 0) continue;
        if (ctx.voice < ov_idx && candidate < ov_pitch) crosses = true;
        if (ctx.voice > ov_idx && candidate > ov_pitch) crosses = true;
      }
      if (crosses) continue;
    }

    return delta;
  }

  return std::nullopt;
}

}  // namespace

int repairParallelPerfect(std::vector<NoteEvent>& notes,
                          const ParallelRepairParams& params) {
  if (notes.empty() || params.num_voices < 2) return 0;

  int total_fixed = 0;

  for (int repair_iter = 0; repair_iter < params.max_iterations; ++repair_iter) {
    // Build per-voice sorted note lists for sounding-pitch lookup.
    struct VN {
      size_t idx;
      Tick start;
      Tick end;
    };
    std::vector<std::vector<VN>> vns(params.num_voices);
    for (size_t idx = 0; idx < notes.size(); ++idx) {
      auto& note = notes[idx];
      if (note.voice < params.num_voices) {
        vns[note.voice].push_back({idx, note.start_tick,
                                   note.start_tick + note.duration});
      }
    }
    for (auto& vec : vns) {
      std::sort(vec.begin(), vec.end(),
                [](const VN& lhs, const VN& rhs) { return lhs.start < rhs.start; });
    }

    // Sounding note index at tick for a voice (-1 if silent).
    // Uses binary search: O(log N) per lookup instead of O(N) linear scan.
    auto soundIdx = [&](uint8_t vid, Tick tick) -> int {
      auto& vnv = vns[vid];
      if (vnv.empty()) return -1;
      // upper_bound finds first note with start > tick.
      auto iter = std::upper_bound(vnv.begin(), vnv.end(), tick,
          [](Tick tck, const VN& vn) { return tck < vn.start; });
      if (iter == vnv.begin()) return -1;
      --iter;  // Now iter->start <= tick.
      if (tick < iter->end) return static_cast<int>(iter->idx);
      return -1;
    };
    std::function<int(uint8_t, Tick)> soundPitch =
        [&](uint8_t vid, Tick tick) -> int {
      int idx = soundIdx(vid, tick);
      return (idx >= 0) ? static_cast<int>(notes[idx].pitch) : -1;
    };

    // Find pitch of the note immediately before target_start in a voice,
    // excluding one specific index. Uses binary search: O(log N).
    std::function<int(uint8_t, Tick, int)> findPrevPitch =
        [&](uint8_t vid, Tick target_start, int exclude_idx) -> int {
      auto& vnv = vns[vid];
      // lower_bound finds first note with start >= target_start.
      auto iter = std::lower_bound(vnv.begin(), vnv.end(), target_start,
          [](const VN& vn, Tick tck) { return vn.start < tck; });
      while (iter != vnv.begin()) {
        --iter;
        if (static_cast<int>(iter->idx) != exclude_idx) {
          return static_cast<int>(notes[iter->idx].pitch);
        }
      }
      return -1;
    };

    // Max tick (invariant across iterations -- only pitches change, not timing).
    Tick max_tick = 0;
    for (auto& note : notes) {
      Tick end = note.start_tick + note.duration;
      if (end > max_tick) max_tick = end;
    }

    // Pre-build beat grid ticks (shared across all voice pairs).
    std::vector<Tick> beat_grid_ticks;
    beat_grid_ticks.reserve(max_tick / kTicksPerBeat + 1);
    for (Tick tick = 0; tick < max_tick; tick += kTicksPerBeat) {
      beat_grid_ticks.push_back(tick);
    }

    // Budget check: count remaining parallels across all voice pairs.
    // If within budget, stop repairing early to allow natural texture.
    if (params.parallel_budget > 0) {
      int parallel_count = 0;
      for (uint8_t va = 0; va < params.num_voices; ++va) {
        for (uint8_t vb = va + 1; vb < params.num_voices; ++vb) {
          int pp_chk = -1, pb2_chk = -1;
          for (Tick tick : beat_grid_ticks) {
            int ca_chk = soundPitch(va, tick);
            int cb_chk = soundPitch(vb, tick);
            if (ca_chk < 0 || cb_chk < 0 || pp_chk < 0 || pb2_chk < 0) {
              pp_chk = ca_chk; pb2_chk = cb_chk; continue;
            }
            if (ca_chk == pp_chk && cb_chk == pb2_chk) continue;
            int pi_chk = std::abs(pp_chk - pb2_chk);
            int ci_chk = std::abs(ca_chk - cb_chk);
            if (interval_util::isPerfectConsonance(pi_chk) &&
                interval_util::isPerfectConsonance(ci_chk) &&
                interval_util::compoundToSimple(pi_chk) ==
                    interval_util::compoundToSimple(ci_chk)) {
              int ma_chk = ca_chk - pp_chk, mb_chk = cb_chk - pb2_chk;
              if (ma_chk != 0 && mb_chk != 0 &&
                  (ma_chk > 0) == (mb_chk > 0)) {
                ++parallel_count;
              }
            }
            pp_chk = ca_chk; pb2_chk = cb_chk;
          }
        }
      }
      if (parallel_count <= params.parallel_budget) break;
    }

    bool any_fixed = false;

    for (uint8_t va = 0; va < params.num_voices; ++va) {
      for (uint8_t vb = va + 1; vb < params.num_voices; ++vb) {
        // Two scan modes: beat grid (C++ analyzer) and common onsets
        // (Python analyzer).  Each mode maintains its own previous-state
        // tracking so that beat-level parallels (across staggered onsets)
        // are detected correctly.
        for (int scan_mode = 0; scan_mode < 2; ++scan_mode) {
          std::vector<Tick> scan_ticks;
          if (scan_mode == 0) {
            scan_ticks = beat_grid_ticks;
          } else {
            std::set<Tick> oa, ob;
            for (auto& vn : vns[va]) oa.insert(vn.start);
            for (auto& vn : vns[vb]) ob.insert(vn.start);
            for (Tick tick : oa) {
              if (ob.count(tick)) scan_ticks.push_back(tick);
            }
          }

          int pp = -1, pb2 = -1;
          Tick pt = 0;
          for (Tick t : scan_ticks) {
            int ca = soundPitch(va, t);
            int cb = soundPitch(vb, t);
            if (ca < 0 || cb < 0 || pp < 0 || pb2 < 0) {
              pp = ca; pb2 = cb; pt = t; continue;
            }
            if (ca == pp && cb == pb2) continue;

            int pi = std::abs(pp - pb2), ci = std::abs(ca - cb);
            int ps = interval_util::compoundToSimple(pi);
            int cs = interval_util::compoundToSimple(ci);
            if (!(interval_util::isPerfectConsonance(pi) &&
                  interval_util::isPerfectConsonance(ci) && ps == cs)) {
              pp = ca; pb2 = cb; pt = t; continue;
            }
            int ma = ca - pp, mb = cb - pb2;
            if (!(ma != 0 && mb != 0 && (ma > 0) == (mb > 0))) {
              pp = ca; pb2 = cb; pt = t; continue;
            }

            // Parallel detected at tick t.
            int ia = soundIdx(va, t), ib = soundIdx(vb, t);
            if (ia < 0 || ib < 0) { pp = ca; pb2 = cb; pt = t; continue; }

            ProtectionLevel pa = getProtectionLevel(notes[ia].source);
            ProtectionLevel pb_l = getProtectionLevel(notes[ib].source);

            // Build fix candidates ordered by flexibility.
            // Flexible: step shifts + octave fallback.
            // Structural: octave shifts only.
            // Immutable: skip.
            struct FC { int ni; uint8_t v; int shifts[12]; int ns; };
            FC cands[2]; int nc = 0;

            auto addC = [&](int ni, uint8_t vid, std::initializer_list<int> sh) {
              FC& cand = cands[nc++]; cand.ni = ni; cand.v = vid; cand.ns = 0;
              for (int s : sh) { if (cand.ns < 12) cand.shifts[cand.ns++] = s; }
            };

            // Determine shift lists based on protection level.
            auto shiftsFor = [](ProtectionLevel plv) -> std::initializer_list<int> {
              static const auto flexible = {1, -1, 2, -2, 3, -3, 4, -4, 12, -12};
              static const auto structural = {12, -12};
              if (plv == ProtectionLevel::Flexible) return flexible;
              if (plv == ProtectionLevel::Structural ||
                  plv == ProtectionLevel::SemiImmutable) return structural;
              return {};  // Immutable
            };

            // Prefer fixing the less-protected (more flexible) note first.
            if (pa != ProtectionLevel::Immutable &&
                pb_l != ProtectionLevel::Immutable) {
              if (pa >= pb_l) {
                addC(ia, va, shiftsFor(pa));
                addC(ib, vb, shiftsFor(pb_l));
              } else {
                addC(ib, vb, shiftsFor(pb_l));
                addC(ia, va, shiftsFor(pa));
              }
            } else if (pa != ProtectionLevel::Immutable) {
              addC(ia, va, shiftsFor(pa));
            } else if (pb_l != ProtectionLevel::Immutable) {
              addC(ib, vb, shiftsFor(pb_l));
            }

            bool fixed = false;

            // Direct fix: try shifting a note at the current tick (t).
            for (int c2 = 0; c2 < nc && !fixed; ++c2) {
              auto& fc = cands[c2];
              auto result = tryShiftCandidate(
                  notes, params,
                  ShiftContext{fc.ni, fc.v, t, pt, t, -1,
                               pp, pb2, cs, va, vb, ca, cb, true},
                  fc.shifts, fc.ns, soundPitch, findPrevPitch);
              if (result) {
                notes[fc.ni].pitch = static_cast<uint8_t>(
                    static_cast<int>(notes[fc.ni].pitch) + *result);
                notes[fc.ni].modified_by |=
                    static_cast<uint8_t>(NoteModifiedBy::ParallelRepair);
                ca = soundPitch(va, t);
                cb = soundPitch(vb, t);
                fixed = true;
                any_fixed = true;
                ++total_fixed;
                break;
              }
            }

            // Adjacent flexible note repair: when notes at the current tick
            // cannot be fixed, try shifting a flexible note at the previous
            // tick (pt) to break the parallel motion pattern.
            if (!fixed) {
              int adj_ia = soundIdx(va, pt);
              int adj_ib = soundIdx(vb, pt);
              // Skip if the same note spans both pt and t (pitch change would
              // alter both intervals simultaneously).
              if (adj_ia >= 0 && adj_ib >= 0 &&
                  adj_ia != ia && adj_ib != ib) {
                ProtectionLevel adj_pa = getProtectionLevel(notes[adj_ia].source);
                ProtectionLevel adj_pb = getProtectionLevel(notes[adj_ib].source);
                nc = 0;
                if (adj_pa != ProtectionLevel::Immutable &&
                    adj_pb != ProtectionLevel::Immutable) {
                  if (adj_pa >= adj_pb) {
                    addC(adj_ia, va, shiftsFor(adj_pa));
                    addC(adj_ib, vb, shiftsFor(adj_pb));
                  } else {
                    addC(adj_ib, vb, shiftsFor(adj_pb));
                    addC(adj_ia, va, shiftsFor(adj_pa));
                  }
                } else if (adj_pa != ProtectionLevel::Immutable) {
                  addC(adj_ia, va, shiftsFor(adj_pa));
                } else if (adj_pb != ProtectionLevel::Immutable) {
                  addC(adj_ib, vb, shiftsFor(adj_pb));
                }

                for (int c2 = 0; c2 < nc && !fixed; ++c2) {
                  auto& fc2 = cands[c2];
                  int pitch_at_t = (fc2.v == va) ? ca : cb;
                  auto result = tryShiftCandidate(
                      notes, params,
                      ShiftContext{fc2.ni, fc2.v, pt, pt, t, pitch_at_t,
                                   pp, pb2, cs, va, vb, ca, cb, false},
                      fc2.shifts, fc2.ns, soundPitch, findPrevPitch);
                  if (result) {
                    notes[fc2.ni].pitch = static_cast<uint8_t>(
                        static_cast<int>(notes[fc2.ni].pitch) + *result);
                    notes[fc2.ni].modified_by |=
                        static_cast<uint8_t>(NoteModifiedBy::ParallelRepair);
                    fixed = true;
                    any_fixed = true;
                    ++total_fixed;
                    break;
                  }
                }
              }
            }

            pp = ca; pb2 = cb; pt = t;
          }
        }  // scan_mode
      }
    }
    if (!any_fixed) break;
  }

  return total_fixed;
}

}  // namespace bach
