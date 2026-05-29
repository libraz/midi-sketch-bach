#ifndef BACH_COMPOSER_MATERIAL_H
#define BACH_COMPOSER_MATERIAL_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach::composer {

// One note inside a pre-determined material fragment (subject, answer,
// fixed counterline, etc.). Pitch and duration are inputs to the
// composer pipeline; CandidateSearch does not enumerate alternatives.
struct MaterialNote {
  Tick start_tick = 0;
  Tick duration = 0;
  std::uint8_t pitch = 0;
};

// Bundle of pre-determined material fragments available to the planner.
//
// `subject` feeds SubjectCarrier spans; `answer` feeds AnswerCarrier
// spans introduced in Phase 4. Later phases add `countersubject` and
// per-character episode fragments. Each fragment is a flat note list;
// structure (phrase boundaries, repeat units) is the planner's
// responsibility.
struct Material {
  std::vector<MaterialNote> subject;
  std::vector<MaterialNote> answer;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_MATERIAL_H
