// Per-decision seed draws: one independent, platform-stable value per musical choice.

#ifndef BACH_CORE_SEED_DECISION_H
#define BACH_CORE_SEED_DECISION_H

#include <cstdint>

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

}  // namespace rng
}  // namespace bach

#endif  // BACH_CORE_SEED_DECISION_H
