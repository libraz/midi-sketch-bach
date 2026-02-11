// Random number generation utilities for deterministic music generation.

#ifndef BACH_CORE_RNG_UTIL_H
#define BACH_CORE_RNG_UTIL_H

#include <cstddef>
#include <numeric>
#include <random>
#include <vector>

namespace bach {
namespace rng {

/// @brief Roll a probability check against a threshold.
/// @param rng Mersenne Twister RNG instance.
/// @param threshold Probability threshold in [0.0, 1.0].
/// @return True if the random roll is below threshold.
inline bool rollProbability(std::mt19937& rng, float threshold) {
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng) < threshold;
}

/// @brief Generate a random integer in [min, max] inclusive.
/// @param rng Mersenne Twister RNG instance.
/// @param min Minimum value (inclusive).
/// @param max Maximum value (inclusive).
/// @return Random integer in the specified range.
inline int rollRange(std::mt19937& rng, int min, int max) {
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng);
}

/// @brief Generate a random float in [min, max].
/// @param rng Mersenne Twister RNG instance.
/// @param min Minimum value.
/// @param max Maximum value.
/// @return Random float in the specified range.
inline float rollFloat(std::mt19937& rng, float min, float max) {
  std::uniform_real_distribution<float> dist(min, max);
  return dist(rng);
}

/// @brief Select a random element from a mutable container.
/// @tparam Container Container type with operator[] and size().
/// @param rng Mersenne Twister RNG instance.
/// @param container Non-empty container to select from.
/// @return Reference to the selected element.
template <typename Container>
inline auto& selectRandom(std::mt19937& rng, Container& container) {
  std::uniform_int_distribution<size_t> dist(0, container.size() - 1);
  return container[dist(rng)];
}

/// @brief Select a random element from a const container.
/// @tparam Container Container type with operator[] and size().
/// @param rng Mersenne Twister RNG instance.
/// @param container Non-empty container to select from.
/// @return Const reference to the selected element.
template <typename Container>
inline const auto& selectRandom(std::mt19937& rng, const Container& container) {
  std::uniform_int_distribution<size_t> dist(0, container.size() - 1);
  return container[dist(rng)];
}

/// @brief Select a random index from a container.
/// @tparam Container Container type with size().
/// @param rng Mersenne Twister RNG instance.
/// @param container Non-empty container.
/// @return Random index in [0, container.size() - 1].
template <typename Container>
inline size_t selectRandomIndex(std::mt19937& rng, const Container& container) {
  std::uniform_int_distribution<size_t> dist(0, container.size() - 1);
  return dist(rng);
}

/// @brief Select an element from options using weighted probabilities.
/// @tparam T Element type.
/// @param rng Mersenne Twister RNG instance.
/// @param options Non-empty vector of choices.
/// @param weights Corresponding weights (must be same size as options, all >= 0).
/// @return The selected element.
template <typename T>
inline T selectWeighted(std::mt19937& rng, const std::vector<T>& options,
                        const std::vector<float>& weights) {
  float total = std::accumulate(weights.begin(), weights.end(), 0.0f);
  std::uniform_real_distribution<float> dist(0.0f, total);
  float roll = dist(rng);
  float cumulative = 0.0f;
  for (size_t i = 0; i < options.size(); ++i) {
    cumulative += weights[i];
    if (roll < cumulative) return options[i];
  }
  return options.back();
}

}  // namespace rng
}  // namespace bach

#endif  // BACH_CORE_RNG_UTIL_H
