// Per-decision seed draws: one independent, platform-stable value per musical choice.

#ifndef BACH_CORE_SEED_DECISION_H
#define BACH_CORE_SEED_DECISION_H

#include <array>
#include <cassert>
#include <cstdint>
#include <numeric>

namespace bach {
namespace rng {

/// @brief Identifies one seeded musical choice.
///
/// Values are explicit and append-only: a draw hashes the numeric tag, so renumbering a tag
/// changes shipped output. Two sites that draw the same musical choice share one tag.
enum class SeedDecision : std::uint32_t {
  kSubject = 1,
  kCadenceBlocks = 2,
  kPicardy = 3,
  kEntryInterval = 4,
  kSecondStretto = 5,
  kFigurationPhase = 6,
  kFreeHarmony = 7,
  kToccataArchetype = 8,
  kPedalWalk = 9,
  kFantasiaOrder = 10,
  kCarryVoiceStart = 11,
  kEpisodeLimb = 12,
  kEpisodeModel = 13,
  kEpisodeRestVoice = 14,
  kTrioFigureShape = 15,
  kTrioExchange = 16,
  kLinearPedalTurn = 17,
  kLinearFigure = 18,
  kCantusOffset = 19,
  kCantusPhrase = 20,
  kCantusFigure = 21,
  kGoldbergOffset = 22,
  kGoldbergPalette = 23,
  kGroundDirection = 24,
  kGroundPalette = 25,
  kGroundRotation = 26,
  kGroundAnchor = 27,
  kLinearAnchor = 28,
  kLinearReach = 29,
  kLinearCellPhase = 30,
  kLinearArchOrder = 31,
  kLinearThirdsRow = 32,
  kGroundPassingDirection = 33,
  kGroundPassingSlot = 34,
  kGoldbergBassContour = 35,
  kGoldbergFigure = 36,
  kChoraleQuarterContour = 37,
  kPassacagliaDelayedResolution = 38,
  kEpisodeChainHost = 39,
  kEpisodeChainStart = 40,
  kChoraleNeighbourSide = 41,
  kLinearOutlineTurn = 42,
};

/// @brief Independent 32-bit draw for one decision of one piece.
///
/// A splitmix64 finalizer over (tag, seed, index). Integer-only with fixed-width wraparound, so
/// native and WASM builds produce identical values. Different tags give uncorrelated draws for
/// the same seed, which removes the lockstep that raw `seed % N` sites share.
/// @param seed The resolved request seed (never a derived or combined seed).
/// @param tag The decision being drawn.
/// @param index Distinguishes repeated draws of one decision (cycle, bar, voice); 0 when drawn
/// once.
/// @return A uniformly distributed 32-bit value.
constexpr std::uint32_t seedDraw(std::uint32_t seed, SeedDecision tag, std::uint32_t index = 0) {
  std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tag)) << 32) | seed;
  key ^= static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
  key += 0x9E3779B97F4A7C15ULL;
  key = (key ^ (key >> 30)) * 0xBF58476D1CE4E5B9ULL;
  key = (key ^ (key >> 27)) * 0x94D049BB133111EBULL;
  key ^= key >> 31;
  return static_cast<std::uint32_t>(key >> 32);
}

/// @brief Choice in [0, modulus) for one decision.
///
/// Multiply-shift reduction of seedDraw: the bias is at most modulus / 2^32 per value.
/// A rotation keeps its structure and draws only its offset:
/// `(seedChoice(seed, tag, n) + cycle) % n`.
/// @param seed The resolved request seed.
/// @param tag The decision being drawn.
/// @param modulus Number of alternatives; must be > 0.
/// @param index Distinguishes repeated draws of one decision; 0 when drawn once.
/// @return A value in [0, modulus).
constexpr std::uint32_t seedChoice(std::uint32_t seed, SeedDecision tag, std::uint32_t modulus,
                                   std::uint32_t index = 0) {
  return static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(seedDraw(seed, tag, index)) * modulus) >> 32);
}

/// Largest family seedBlockChoice draws from.
inline constexpr std::uint32_t kMaxBlockChoice = 32;

namespace detail {

/// @brief Value at `position` of block `block`'s salted shuffle of 0..modulus-1 (Fisher-Yates).
/// The high index bit keeps these draws apart from seedChoice draws of the same tag.
constexpr std::uint32_t blockShuffleAt(std::uint32_t block, SeedDecision tag, std::uint32_t modulus,
                                       std::uint32_t index, std::uint32_t position) {
  std::uint32_t order[kMaxBlockChoice] = {};
  for (std::uint32_t pos = 0; pos < modulus; ++pos)
    order[pos] = pos;
  for (std::uint32_t pos = modulus - 1; pos > 0; --pos) {
    const std::uint32_t draw = seedDraw(block, tag, 0x80000000u | (index << 5) | pos);
    const auto pick =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(draw) * (pos + 1)) >> 32);
    const std::uint32_t held = order[pos];
    order[pos] = order[pick];
    order[pick] = held;
  }
  return order[position];
}

}  // namespace detail

/// @brief Choice in [0, modulus) drawn without replacement over each block of seeds.
///
/// Seeds fall into aligned blocks of `modulus`; each block takes its own salted shuffle and
/// `seed % modulus` reads a position, so the `modulus` seeds of a block use every value once.
/// A block whose first value repeats the previous block's last swaps its first two, so
/// neighbouring seeds never share a value. The swap never reaches a block's last position, so a
/// lookup reads this block's shuffle and at most the previous one. With two alternatives the
/// rule would force strict alternation, a period of 2; draw those with seedChoice.
/// @param seed The resolved request seed.
/// @param tag The decision being drawn.
/// @param modulus Number of alternatives, in [3, kMaxBlockChoice].
/// @param index Distinguishes repeated draws of one decision; below 2^26.
/// @return A value in [0, modulus).
constexpr std::uint32_t seedBlockChoice(std::uint32_t seed, SeedDecision tag, std::uint32_t modulus,
                                        std::uint32_t index = 0) {
  assert(modulus >= 3 && modulus <= kMaxBlockChoice);
  const std::uint32_t block = seed / modulus;
  const std::uint32_t position = seed % modulus;
  if (block == 0 || position > 1)
    return detail::blockShuffleAt(block, tag, modulus, index, position);
  const std::uint32_t first = detail::blockShuffleAt(block, tag, modulus, index, 0);
  const bool repeats = first == detail::blockShuffleAt(block - 1, tag, modulus, index, modulus - 1);
  if (position == 0)
    return repeats ? detail::blockShuffleAt(block, tag, modulus, index, 1) : first;
  return repeats ? first : detail::blockShuffleAt(block, tag, modulus, index, 1);
}

/// Most width-`width` coordinates seedTupleChoice draws with its last coordinate.
inline constexpr std::uint32_t kMaxTupleWide = 4;

/// @brief A tuple drawn by seedTupleChoice: `wide_count` values in [0, width), then `last`.
struct TupleChoice {
  std::array<std::uint32_t, kMaxTupleWide> wide = {};
  std::uint32_t last = 0;
};

namespace detail {

/// @brief Smallest multiplier keeping the last coordinate of seedTupleChoice a bijection whose
/// value changes at every step of a block.
constexpr std::uint32_t tupleMultiplier(std::uint32_t width, std::uint32_t wide_count,
                                        std::uint32_t size) {
  std::uint32_t wide_span = 1;
  for (std::uint32_t coord = 0; coord < wide_count; ++coord)
    wide_span *= width;
  for (std::uint32_t mult = 1; mult < size; ++mult) {
    if (mult == size - 1 || std::gcd(mult, size) != 1)
      continue;
    if (std::gcd((1 + mult * (wide_span % size)) % size, size) == 1)
      return mult;
  }
  assert(false && "width and size admit no multiplier");
  return 1;
}

/// @brief Label of raw value `raw` of coordinate `coord` in block `block`: the block's salted
/// shuffle, with the block's first label traded away when it repeats the previous block's last.
constexpr std::uint32_t tupleLabel(std::uint32_t block, SeedDecision tag, std::uint32_t modulus,
                                   std::uint32_t index, std::uint32_t first_raw,
                                   std::uint32_t last_raw, std::uint32_t raw) {
  const std::uint32_t first = blockShuffleAt(block, tag, modulus, index, first_raw);
  if (block == 0 || first != blockShuffleAt(block - 1, tag, modulus, index, last_raw))
    return blockShuffleAt(block, tag, modulus, index, raw);
  std::uint32_t other = 0;
  while (other == first_raw || other == last_raw)
    ++other;
  if (raw == first_raw)
    return blockShuffleAt(block, tag, modulus, index, other);
  if (raw == other)
    return first;
  return blockShuffleAt(block, tag, modulus, index, raw);
}

}  // namespace detail

/// @brief `wide_count` choices in [0, width) and one in [0, size), drawn jointly without
/// replacement over each block of seeds.
///
/// Position p in an aligned block of M = width^wide_count * size seeds has base-width digits
/// d_0.. and a last digit d_s = (p / width^wide_count) % size; wide coordinate 0 is d_0, wide
/// coordinate i is (d_0 + d_i) % width, and the last is (d_s + m * p) % size with
/// m = detail::tupleMultiplier. Every coordinate is relabelled by its block's salted shuffle,
/// and a first label repeating the previous block's last is traded for one no edge position
/// uses. Properties:
/// - every aligned block of M seeds takes every tuple once;
/// - consecutive seeds differ in every coordinate, across block edges as well;
/// - every wide coordinate takes each value once over every aligned block of `width` seeds;
/// - a lookup reads its own block's shuffles and the previous block's only.
/// @param seed The resolved request seed.
/// @param tag The decision being drawn.
/// @param width Alternatives of each wide coordinate, in [3, kMaxBlockChoice]; with `size` it
/// must admit a multiplier, which every width-4 draw does.
/// @param wide_count Number of wide coordinates, in [1, kMaxTupleWide].
/// @param size Alternatives of the last coordinate, in [3, kMaxBlockChoice].
/// @param index Distinguishes draws of one decision; below 2^22.
/// @return The drawn tuple; `wide` entries past `wide_count` stay 0.
constexpr TupleChoice seedTupleChoice(std::uint32_t seed, SeedDecision tag, std::uint32_t width,
                                      std::uint32_t wide_count, std::uint32_t size,
                                      std::uint32_t index = 0) {
  assert(width >= 3 && width <= kMaxBlockChoice);
  assert(wide_count >= 1 && wide_count <= kMaxTupleWide);
  assert(size >= 3 && size <= kMaxBlockChoice);
  std::uint32_t wide_span = 1;
  for (std::uint32_t coord = 0; coord < wide_count; ++coord)
    wide_span *= width;
  const std::uint32_t block_size = wide_span * size;
  const std::uint32_t block = seed / block_size;
  const std::uint32_t position = seed % block_size;
  const std::uint32_t mult = detail::tupleMultiplier(width, wide_count, size);

  std::uint32_t digits[kMaxTupleWide] = {};
  std::uint32_t rest = position;
  for (std::uint32_t coord = 0; coord < wide_count; ++coord) {
    digits[coord] = rest % width;
    rest /= width;
  }
  TupleChoice out;
  for (std::uint32_t coord = 0; coord < wide_count; ++coord) {
    const std::uint32_t raw = coord == 0 ? digits[0] : (digits[0] + digits[coord]) % width;
    // The block's last position has every digit at width - 1.
    const std::uint32_t last_raw = coord == 0 ? width - 1 : width - 2;
    out.wide[coord] = detail::tupleLabel(block, tag, width, (index << 4) | coord, 0, last_raw, raw);
  }
  const auto last_raw_value = static_cast<std::uint32_t>(
      (rest % size + static_cast<std::uint64_t>(mult) * position) % size);
  const std::uint32_t last_edge = (size - 1 + size - mult % size) % size;
  out.last = detail::tupleLabel(block, tag, size, (index << 4) | 15u, 0, last_edge, last_raw_value);
  return out;
}

}  // namespace rng
}  // namespace bach

#endif  // BACH_CORE_SEED_DECISION_H
