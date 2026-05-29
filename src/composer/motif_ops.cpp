#include "composer/motif_ops.h"

#include <algorithm>
#include <cstdint>

namespace bach::composer::motif_ops {

namespace {

inline std::uint8_t clampPitch(int p) {
  if (p < 0)
    return 0;
  if (p > 127)
    return 127;
  return static_cast<std::uint8_t>(p);
}

}  // namespace

std::vector<MaterialNote> invertMelody(const std::vector<MaterialNote>& notes, std::uint8_t pivot) {
  std::vector<MaterialNote> out;
  out.reserve(notes.size());
  for (const auto& n : notes) {
    MaterialNote m = n;
    m.pitch = clampPitch(2 * static_cast<int>(pivot) - static_cast<int>(n.pitch));
    out.push_back(m);
  }
  return out;
}

std::vector<MaterialNote> retrogradeMelody(const std::vector<MaterialNote>& notes,
                                           Tick start_tick) {
  if (notes.empty())
    return {};
  const std::size_t count = notes.size();
  const Tick original_start = notes.front().start_tick;
  // (offset_from_start, duration) per note, reversed below.
  std::vector<Tick> offsets(count);
  std::vector<Tick> durations(count);
  for (std::size_t i = 0; i < count; ++i) {
    offsets[i] = notes[i].start_tick - original_start;
    durations[i] = notes[i].duration;
  }
  std::vector<Tick> gaps;
  gaps.reserve(count > 0 ? count - 1 : 0);
  for (std::size_t i = 0; i + 1 < count; ++i) {
    const Tick end_cur = offsets[i] + durations[i];
    const Tick start_next = offsets[i + 1];
    gaps.push_back(start_next >= end_cur ? start_next - end_cur : 0);
  }
  std::vector<MaterialNote> out(count);
  Tick cursor = start_tick;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t src = count - 1 - i;
    out[i] = notes[src];
    out[i].start_tick = cursor;
    cursor += notes[src].duration;
    if (i + 1 < count) {
      const std::size_t gap_idx = count - 2 - i;
      cursor += gaps[gap_idx];
    }
  }
  return out;
}

std::vector<MaterialNote> augmentDuration(const std::vector<MaterialNote>& notes, Tick start_tick,
                                          int factor) {
  if (notes.empty())
    return {};
  if (factor <= 0)
    factor = 1;
  const Tick original_start = notes.front().start_tick;
  std::vector<MaterialNote> out;
  out.reserve(notes.size());
  for (const auto& n : notes) {
    MaterialNote m = n;
    const Tick offset = n.start_tick - original_start;
    m.start_tick = start_tick + offset * static_cast<Tick>(factor);
    m.duration = n.duration * static_cast<Tick>(factor);
    out.push_back(m);
  }
  return out;
}

std::vector<MaterialNote> diminishDuration(const std::vector<MaterialNote>& notes, Tick start_tick,
                                           int factor) {
  if (notes.empty())
    return {};
  if (factor <= 0)
    factor = 1;
  const Tick original_start = notes.front().start_tick;
  const Tick ufactor = static_cast<Tick>(factor);
  std::vector<MaterialNote> out;
  out.reserve(notes.size());
  for (const auto& n : notes) {
    MaterialNote m = n;
    const Tick offset = n.start_tick - original_start;
    m.start_tick = start_tick + offset / ufactor;
    m.duration = std::max(n.duration / ufactor, static_cast<Tick>(1));
    out.push_back(m);
  }
  return out;
}

std::vector<MaterialNote> reanchorMelody(const std::vector<MaterialNote>& notes, Tick start_tick) {
  if (notes.empty())
    return {};
  const Tick original_start = notes.front().start_tick;
  std::vector<MaterialNote> out;
  out.reserve(notes.size());
  for (const auto& n : notes) {
    MaterialNote m = n;
    m.start_tick = start_tick + (n.start_tick - original_start);
    out.push_back(m);
  }
  return out;
}

std::vector<MaterialNote> applyTransform(const std::vector<MaterialNote>& source,
                                         EpisodeMotifTransform transform, Tick target_start_tick,
                                         std::uint8_t invert_pivot, int factor) {
  switch (transform) {
    case EpisodeMotifTransform::Original:
      return reanchorMelody(source, target_start_tick);
    case EpisodeMotifTransform::Invert: {
      // Invert first (preserves source timing), then re-anchor.
      auto inverted = invertMelody(source, invert_pivot);
      return reanchorMelody(inverted, target_start_tick);
    }
    case EpisodeMotifTransform::Retrograde:
      return retrogradeMelody(source, target_start_tick);
    case EpisodeMotifTransform::Augment:
      return augmentDuration(source, target_start_tick, factor);
    case EpisodeMotifTransform::Diminish:
      return diminishDuration(source, target_start_tick, factor);
  }
  return {};
}

EpisodeMotifTransform characterToTransform(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return EpisodeMotifTransform::Original;
    case SubjectCharacter::Playful:
      return EpisodeMotifTransform::Invert;
    case SubjectCharacter::Noble:
      return EpisodeMotifTransform::Augment;
    case SubjectCharacter::Restless:
      return EpisodeMotifTransform::Diminish;
  }
  return EpisodeMotifTransform::Original;
}

}  // namespace bach::composer::motif_ops
