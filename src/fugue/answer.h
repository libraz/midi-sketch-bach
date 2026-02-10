// Fugue answer: real and tonal answer derivation from a subject.

#ifndef BACH_FUGUE_ANSWER_H
#define BACH_FUGUE_ANSWER_H

#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/subject.h"

namespace bach {

/// The fugue answer (comes): derived from the subject, transposed to the
/// dominant key.
struct Answer {
  std::vector<NoteEvent> notes;
  AnswerType type = AnswerType::Real;
  Key key = Key::G;  // Default: dominant of C
};

/// @brief Generate an answer from the given subject.
///
/// Real answers transpose all notes up a perfect 5th. Tonal answers
/// adjust tonic-dominant relationships so that the answer fits the
/// harmonic context (tonic -> dominant becomes dominant -> tonic).
///
/// @param subject The fugue subject (dux).
/// @param type Answer type. If Auto, the type is auto-detected.
/// @return The derived Answer.
Answer generateAnswer(const Subject& subject,
                      AnswerType type = AnswerType::Auto);

/// @brief Automatically detect whether a real or tonal answer is needed.
///
/// If the subject begins with a tonic-to-dominant leap (or vice versa),
/// a tonal answer is preferred. Otherwise, a real answer is used.
///
/// @param subject The fugue subject to analyze.
/// @return Real or Tonal (never returns Auto).
AnswerType autoDetectAnswerType(const Subject& subject);

}  // namespace bach

#endif  // BACH_FUGUE_ANSWER_H
