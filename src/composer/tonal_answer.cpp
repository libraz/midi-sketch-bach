#include "composer/tonal_answer.h"

#include <algorithm>
#include <cstdint>

namespace bach::composer::tonal_answer {

namespace {

inline std::uint8_t clampPitch(int p) {
  if (p < 0)
    return 0;
  if (p > 127)
    return 127;
  return static_cast<std::uint8_t>(p);
}

// Find the octave-equivalent pitch closest to `anchor` whose pitch
// class equals `target_pc`. Used so the mutated head pitch stays in
// the answer's register instead of jumping arbitrary octaves.
std::uint8_t closestPcOctaveTo(std::uint8_t target_pc, std::uint8_t anchor) {
  const int anchor_pc = anchor % 12;
  int delta = (target_pc - anchor_pc + 24) % 12;
  if (delta > 6)
    delta -= 12;
  return clampPitch(static_cast<int>(anchor) + delta);
}

}  // namespace

std::vector<MaterialNote> deriveTonalAnswer(const std::vector<MaterialNote>& subject,
                                            std::uint8_t tonic_pc, Tick target_start_tick,
                                            std::size_t head_length) {
  if (subject.empty())
    return {};
  const Tick original_start = subject.front().start_tick;
  const std::uint8_t tonic = static_cast<std::uint8_t>(tonic_pc % 12);
  const std::uint8_t dom = static_cast<std::uint8_t>((tonic_pc + 7) % 12);
  const std::size_t mutate_n = std::max<std::size_t>(head_length, 1);
  std::vector<MaterialNote> out;
  out.reserve(subject.size());
  for (std::size_t i = 0; i < subject.size(); ++i) {
    const auto& s = subject[i];
    MaterialNote m = s;
    m.start_tick = target_start_tick + (s.start_tick - original_start);
    // Base = real answer = subject - P4 (5 semitones).
    const std::uint8_t base = clampPitch(static_cast<int>(s.pitch) - 5);
    if (i < mutate_n) {
      const std::uint8_t src_pc = static_cast<std::uint8_t>(s.pitch % 12);
      if (src_pc == tonic) {
        m.pitch = closestPcOctaveTo(dom, base);
      } else if (src_pc == dom) {
        m.pitch = closestPcOctaveTo(tonic, base);
      } else {
        m.pitch = base;
      }
    } else {
      m.pitch = base;
    }
    out.push_back(m);
  }
  return out;
}

}  // namespace bach::composer::tonal_answer
