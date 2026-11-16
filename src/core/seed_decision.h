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
  kGoldbergFigure = 36,
  kChoraleQuarterContour = 37,
  kPassacagliaDelayedResolution = 38,
  kEpisodeChainHost = 39,
  kEpisodeChainStart = 40,
  kChoraleNeighbourSide = 41,
  kLinearOutlineTurn = 42,
  kTrioBlockOrder = 43,
  kGoldbergBassLayout = 44,
  kTrioShapePhase = 46,
  kTrioBassRoute = 47,
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

/// @brief Block `block`'s salted shuffle of 0..modulus-1 (Fisher-Yates) into `order`.
/// The high index bit keeps these draws apart from seedChoice draws of the same tag.
constexpr void fillBlockShuffle(std::uint32_t block, SeedDecision tag, std::uint32_t modulus,
                                std::uint32_t index, std::uint32_t (&order)[kMaxBlockChoice]) {
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
}

/// @brief Value at `position` of block `block`'s salted shuffle of 0..modulus-1.
constexpr std::uint32_t blockShuffleAt(std::uint32_t block, SeedDecision tag, std::uint32_t modulus,
                                       std::uint32_t index, std::uint32_t position) {
  std::uint32_t order[kMaxBlockChoice] = {};
  fillBlockShuffle(block, tag, modulus, index, order);
  return order[position];
}

}  // namespace detail

/// @brief Choice in [0, modulus) drawn without replacement over each block of seeds.
///
/// Seeds fall into aligned blocks of `modulus`; each block takes its own salted shuffle and
/// `seed % modulus` reads a position, so the `modulus` seeds of a block use every value once.
/// Each of a block's first `clear` positions that holds a value the previous block's shuffle
/// placed within `clear` seeds of it trades with the lowest position in
/// [clear, modulus - clear) clear of those values, so no two seeds within `clear` of each other
/// share a value. No trade reaches a block's last `clear` positions, so a lookup reads this
/// block's shuffle and at most the previous one. With two alternatives the rule would force
/// strict alternation, a period of 2; draw those with seedChoice.
/// @param seed The resolved request seed.
/// @param tag The decision being drawn.
/// @param modulus Number of alternatives, in [3, kMaxBlockChoice].
/// @param index Distinguishes repeated draws of one decision; below 2^26.
/// @param clear Seed distance within which no two seeds share a value; 2 * clear < modulus.
/// @return A value in [0, modulus).
constexpr std::uint32_t seedBlockChoice(std::uint32_t seed, SeedDecision tag, std::uint32_t modulus,
                                        std::uint32_t index = 0, std::uint32_t clear = 1) {
  assert(modulus >= 3 && modulus <= kMaxBlockChoice);
  assert(clear >= 1 && 2 * clear < modulus);
  const std::uint32_t block = seed / modulus;
  const std::uint32_t position = seed % modulus;
  std::uint32_t order[kMaxBlockChoice] = {};
  detail::fillBlockShuffle(block, tag, modulus, index, order);
  if (block == 0 || position >= modulus - clear)
    return order[position];
  std::uint32_t tail[kMaxBlockChoice] = {};
  detail::fillBlockShuffle(block - 1, tag, modulus, index, tail);
  for (std::uint32_t pos = 0; pos < clear; ++pos) {
    // The previous block's values lying within `clear` seeds of this position.
    const auto within_clear = [&](std::uint32_t value) {
      for (std::uint32_t back = modulus - clear + pos; back < modulus; ++back) {
        if (tail[back] == value)
          return true;
      }
      return false;
    };
    if (!within_clear(order[pos]))
      continue;
    for (std::uint32_t other = clear; other < modulus - clear; ++other) {
      if (!within_clear(order[other])) {
        const std::uint32_t held = order[pos];
        order[pos] = order[other];
        order[other] = held;
        break;
      }
    }
  }
  return order[position];
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

/// @brief Raw value of seedTupleChoice coordinate `coord` (wide_count = the last) at `position`
/// of a block, before relabelling. Position p has base-width digits d_0.., a w-block index
/// p / width, and a last digit d_s = (p / width^wide_count) % size.
constexpr std::uint32_t tupleRaw(std::uint32_t position, std::uint32_t width,
                                 std::uint32_t wide_count, std::uint32_t size, std::uint32_t mult,
                                 std::uint32_t coord) {
  std::uint32_t digits[kMaxTupleWide] = {};
  std::uint32_t rest = position;
  for (std::uint32_t idx = 0; idx < wide_count; ++idx) {
    digits[idx] = rest % width;
    rest /= width;
  }
  const std::uint32_t w_block = position / width;
  // Coordinate 0 takes a permutation of d_0 that changes from one w-block to the next.
  std::uint32_t first = (digits[0] + 2 * digits[wide_count > 1 ? 1 : 0]) % width;
  if (wide_count == 1)
    first = (w_block % 2 == 1 && digits[0] >= width - 2) ? 2 * width - 3 - digits[0] : digits[0];
  if (coord == 0)
    return first;
  if (coord < wide_count)
    return (digits[0] + digits[coord]) % width;
  if (wide_count == 1)
    return (w_block + first) % size;
  return static_cast<std::uint32_t>((rest % size + static_cast<std::uint64_t>(mult) * position) %
                                    size);
}

}  // namespace detail

/// @brief `wide_count` choices in [0, width) and one in [0, size), drawn jointly without
/// replacement over each block of seeds.
///
/// Seeds fall into aligned blocks of M = width^wide_count * size; each coordinate's raw value
/// (detail::tupleRaw) is relabelled by its block's salted shuffle, and a first label repeating
/// the previous block's last is traded for one no edge position uses. Properties:
/// - every aligned block of M seeds takes every tuple once;
/// - consecutive seeds differ in every coordinate, across block edges as well;
/// - every wide coordinate takes each value once over every aligned block of `width` seeds;
/// - no coordinate is a function of seed % width over a block;
/// - a lookup reads its own block's shuffles and the previous block's only.
/// @param seed The resolved request seed.
/// @param tag The decision being drawn.
/// @param width Alternatives of each wide coordinate; the properties are proved for 4 only.
/// @param wide_count Number of wide coordinates, in [1, kMaxTupleWide].
/// @param size Alternatives of the last coordinate, in [3, kMaxBlockChoice].
/// @param index Distinguishes draws of one decision; below 2^22.
/// @return The drawn tuple; `wide` entries past `wide_count` stay 0.
constexpr TupleChoice seedTupleChoice(std::uint32_t seed, SeedDecision tag, std::uint32_t width,
                                      std::uint32_t wide_count, std::uint32_t size,
                                      std::uint32_t index = 0) {
  assert(width == 4);
  assert(wide_count >= 1 && wide_count <= kMaxTupleWide);
  assert(size >= 3 && size <= kMaxBlockChoice);
  std::uint32_t block_size = size;
  for (std::uint32_t coord = 0; coord < wide_count; ++coord)
    block_size *= width;
  const std::uint32_t block = seed / block_size;
  const std::uint32_t position = seed % block_size;
  // The one-coordinate form reads no multiplier.
  const std::uint32_t mult = wide_count == 1 ? 0 : detail::tupleMultiplier(width, wide_count, size);
  // Raw value of `coord` at a block position; coordinate wide_count is the last.
  const auto raw_at = [&](std::uint32_t pos, std::uint32_t coord) {
    return detail::tupleRaw(pos, width, wide_count, size, mult, coord);
  };
  TupleChoice out;
  for (std::uint32_t coord = 0; coord <= wide_count; ++coord) {
    const std::uint32_t modulus = coord < wide_count ? width : size;
    const std::uint32_t salt = (index << 4) | (coord < wide_count ? coord : 15u);
    const std::uint32_t label =
        detail::tupleLabel(block, tag, modulus, salt, raw_at(0, coord),
                           raw_at(block_size - 1, coord), raw_at(position, coord));
    if (coord < wide_count)
      out.wide[coord] = label;
    else
      out.last = label;
  }
  return out;
}

}  // namespace rng
}  // namespace bach

#endif  // BACH_CORE_SEED_DECISION_H
