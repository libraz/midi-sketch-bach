#include "composer/bar_material.h"

#include <cstdint>

namespace bach::composer {

Tick barTick(int bar) {
  return static_cast<Tick>(bar) * kTicksPerBar;
}

MaterialNote materialNote(Tick start, Tick dur, int pitch) {
  MaterialNote note;
  note.start_tick = start;
  note.duration = dur;
  note.pitch = static_cast<std::uint8_t>(pitch);
  return note;
}

int notesPerBeatFor(const ArcPoint& point, std::int8_t density_bias) {
  int tier = static_cast<int>(point.density_tier) + density_bias;
  if (tier < 0)
    tier = 0;
  if (tier > 3)
    tier = 3;
  if (tier == 0)
    return 1;
  return tier >= 2 ? 4 : 2;
}

int snapUpToChordTone(int start, int root_pc, bool minor) {
  const int third = minor ? 3 : 4;
  const int triad_pc[3] = {root_pc % 12, (root_pc + third) % 12, (root_pc + 7) % 12};
  int cur = start;
  while (cur % 12 != triad_pc[0] && cur % 12 != triad_pc[1] && cur % 12 != triad_pc[2])
    ++cur;
  return cur;
}

bool isChordTone(int pitch, int root_pc, bool minor) {
  const int third = minor ? 3 : 4;
  const int pc = ((pitch % 12) + 12) % 12;
  return pc == ((root_pc % 12) + 12) % 12 || pc == (root_pc + third) % 12 ||
         pc == (root_pc + 7) % 12;
}

int chordToneAbove(int from, int root_pc, bool minor) {
  int cur = from + 1;
  while (!isChordTone(cur, root_pc, minor))
    ++cur;
  return cur;
}

}  // namespace bach::composer
