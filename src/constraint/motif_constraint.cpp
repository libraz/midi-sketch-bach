// Episode constraint types: MotifOp dispatch, character params, transforms.

#include "constraint/motif_constraint.h"

#include "fugue/episode.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {

// ---------------------------------------------------------------------------
// motifOpToString
// ---------------------------------------------------------------------------

const char* motifOpToString(MotifOp op) {
  switch (op) {
    case MotifOp::Original:   return "Original";
    case MotifOp::Invert:     return "Invert";
    case MotifOp::Retrograde: return "Retrograde";
    case MotifOp::Diminish:   return "Diminish";
    case MotifOp::Augment:    return "Augment";
    case MotifOp::Fragment:   return "Fragment";
    case MotifOp::Sequence:   return "Sequence";
  }
  return "Unknown";  // NOLINT(clang-diagnostic-covered-switch-default): defensive fallback
}

// ---------------------------------------------------------------------------
// getCharacterParams -- design value table (Reduce Generation principle)
// ---------------------------------------------------------------------------

CharacterEpisodeParams getCharacterParams(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      // Original motif + diatonic inversion, wide imitation (1.5-2.5 beats),
      // descending stepwise sequence.
      return {MotifOp::Original, MotifOp::Invert, MotifOp::Original,
              1.5f, 2.5f, -1};

    case SubjectCharacter::Playful:
      // Retrograde + inversion, tight imitation (0.5-1.5 beats),
      // descending by two degrees for faster harmonic motion.
      return {MotifOp::Retrograde, MotifOp::Invert, MotifOp::Original,
              0.5f, 1.5f, -2};

    case SubjectCharacter::Noble:
      // Original + augmentation with retrograde secondary,
      // wide imitation, descending stepwise.
      return {MotifOp::Original, MotifOp::Augment, MotifOp::Retrograde,
              1.5f, 2.5f, -1};

    case SubjectCharacter::Restless:
      // Fragment + diminution, tight imitation,
      // descending by two degrees for urgency.
      return {MotifOp::Fragment, MotifOp::Diminish, MotifOp::Original,
              0.5f, 1.5f, -2};
  }
  // Fallback to Severe (should never reach here).
  return {MotifOp::Original, MotifOp::Invert, MotifOp::Original,
          1.5f, 2.5f, -1};
}

// ---------------------------------------------------------------------------
// applyMotifOp -- dispatcher to existing transform functions
// ---------------------------------------------------------------------------

std::vector<NoteEvent> applyMotifOp(const std::vector<NoteEvent>& notes, MotifOp op,
                                    Key key, ScaleType scale, int sequence_step) {
  if (notes.empty()) {
    return {};
  }

  switch (op) {
    case MotifOp::Original:
      return notes;

    case MotifOp::Invert:
      return invertMelodyDiatonic(notes, notes[0].pitch, key, scale);

    case MotifOp::Retrograde:
      return retrogradeMelody(notes, 0);

    case MotifOp::Diminish:
      return diminishMelody(notes, 0);

    case MotifOp::Augment:
      return augmentMelody(notes, 0);

    case MotifOp::Fragment: {
      auto fragments = fragmentMotif(notes, 2);
      if (fragments.empty() || fragments[0].empty()) {
        return notes;
      }
      return fragments[0];
    }

    case MotifOp::Sequence:
      return generateDiatonicSequence(notes, 1, sequence_step,
                                      motifDuration(notes), key, scale);
  }

  return notes;  // NOLINT(clang-diagnostic-covered-switch-default): defensive fallback
}

}  // namespace bach
