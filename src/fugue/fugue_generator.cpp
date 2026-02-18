// Fugue generator: delegates to the constraint-driven pipeline.

#include "fugue/fugue_generator.h"

#include "fugue/fugue_pipeline.h"

namespace bach {

FugueResult generateFugue(const FugueConfig& config) {
  return generateFuguePipeline(config);
}

}  // namespace bach
