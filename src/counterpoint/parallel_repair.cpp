// Shared parallel perfect consonance repair.
// Consolidates duplicate logic from postValidateNotes() and FugueGenerator.

#include "counterpoint/parallel_repair.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "core/interval.h"
#include "core/note_source.h"
#include "core/scale.h"

namespace bach {

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
    for (size_t i = 0; i < notes.size(); ++i) {
      auto& n = notes[i];
      if (n.voice < params.num_voices) {
        vns[n.voice].push_back({i, n.start_tick, n.start_tick + n.duration});
      }
    }
    for (auto& v : vns) {
      std::sort(v.begin(), v.end(),
                [](const VN& a, const VN& b) { return a.start < b.start; });
    }

    // Sounding note index at tick for a voice (-1 if silent).
    auto soundIdx = [&](uint8_t v, Tick t) -> int {
      for (auto it = vns[v].rbegin(); it != vns[v].rend(); ++it) {
        if (it->start <= t && t < it->end)
          return static_cast<int>(it->idx);
        if (it->end <= t) return -1;
      }
      return -1;
    };
    auto soundPitch = [&](uint8_t v, Tick t) -> int {
      int i = soundIdx(v, t);
      return (i >= 0) ? static_cast<int>(notes[i].pitch) : -1;
    };

    // Max tick for scan range.
    Tick max_tick = 0;
    for (auto& n : notes) {
      Tick e = n.start_tick + n.duration;
      if (e > max_tick) max_tick = e;
    }

    bool any_fixed = false;

    for (uint8_t va = 0; va < params.num_voices; ++va) {
      for (uint8_t vb = va + 1; vb < params.num_voices; ++vb) {
        // Two scan modes: beat grid (C++ analyzer) and common onsets (Python).
        for (int scan_mode = 0; scan_mode < 2; ++scan_mode) {
          std::vector<Tick> scan_ticks;
          if (scan_mode == 0) {
            for (Tick t = 0; t < max_tick; t += kTicksPerBeat) {
              scan_ticks.push_back(t);
            }
          } else {
            std::set<Tick> oa, ob;
            for (auto& vn : vns[va]) oa.insert(vn.start);
            for (auto& vn : vns[vb]) ob.insert(vn.start);
            for (Tick t : oa) {
              if (ob.count(t)) scan_ticks.push_back(t);
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

            auto addC = [&](int ni, uint8_t v, std::initializer_list<int> sh) {
              FC& c = cands[nc++]; c.ni = ni; c.v = v; c.ns = 0;
              for (int s : sh) { if (c.ns < 12) c.shifts[c.ns++] = s; }
            };

            // Determine shift lists based on protection level.
            auto shiftsFor = [](ProtectionLevel pl) -> std::initializer_list<int> {
              if (pl == ProtectionLevel::Flexible)
                return {1, -1, 2, -2, 3, -3, 4, -4, 12, -12};
              if (pl == ProtectionLevel::Structural)
                return {12, -12};
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
            for (int c2 = 0; c2 < nc && !fixed; ++c2) {
              auto& fc = cands[c2];
              Key nk = params.key_at_tick(notes[fc.ni].start_tick);

              for (int si = 0; si < fc.ns; ++si) {
                int delta = fc.shifts[si];
                int cp = static_cast<int>(notes[fc.ni].pitch) + delta;
                if (cp < 0 || cp > 127) continue;
                uint8_t ucp = static_cast<uint8_t>(cp);

                // Diatonic check for step shifts (not octave shifts).
                if (std::abs(delta) <= 4 &&
                    !scale_util::isScaleTone(ucp, nk, params.scale))
                  continue;

                // Voice range check.
                auto [lo, hi] = params.voice_range(fc.v);
                if (ucp < lo || ucp > hi) continue;

                // Melodic leap: no more than octave from previous note in voice.
                {
                  int prev_p = -1;
                  for (auto it = vns[fc.v].rbegin(); it != vns[fc.v].rend(); ++it) {
                    if (static_cast<int>(it->idx) == fc.ni) continue;
                    if (it->start < notes[fc.ni].start_tick) {
                      prev_p = static_cast<int>(notes[it->idx].pitch);
                      break;
                    }
                  }
                  if (prev_p >= 0 && std::abs(cp - prev_p) > 12) continue;
                }

                // Check no new parallel with any other voice.
                bool new_par = false;
                for (uint8_t ov = 0; ov < params.num_voices && !new_par; ++ov) {
                  if (ov == fc.v) continue;
                  int ovc = soundPitch(ov, t);
                  int ovpv = soundPitch(ov, pt);
                  int fpv = soundPitch(fc.v, pt);
                  if (ovc < 0 || ovpv < 0 || fpv < 0) continue;
                  int p_i2 = std::abs(fpv - ovpv);
                  int c_i2 = std::abs(cp - ovc);
                  if (interval_util::isPerfectConsonance(p_i2) &&
                      interval_util::isPerfectConsonance(c_i2)) {
                    int p2s = interval_util::compoundToSimple(p_i2);
                    int c2s = interval_util::compoundToSimple(c_i2);
                    if (p2s == c2s) {
                      int mf = cp - fpv, mo = ovc - ovpv;
                      if (mf != 0 && mo != 0 && (mf > 0) == (mo > 0))
                        new_par = true;
                    }
                  }
                }
                if (new_par) continue;

                notes[fc.ni].pitch = ucp;
                ca = soundPitch(va, t);
                cb = soundPitch(vb, t);
                fixed = true;
                any_fixed = true;
                ++total_fixed;
                break;
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
