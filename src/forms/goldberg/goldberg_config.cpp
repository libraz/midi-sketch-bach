// Goldberg Variations generation entry point -- delegates to GoldbergGenerator.

#include "forms/goldberg/goldberg_config.h"

#include "forms/goldberg/goldberg_generator.h"

namespace bach {

GoldbergResult generateGoldbergVariations(const GoldbergConfig& config) {
  GoldbergGenerator generator;
  return generator.generate(config);
}

}  // namespace bach
