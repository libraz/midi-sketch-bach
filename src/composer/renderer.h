#ifndef BACH_COMPOSER_RENDERER_H
#define BACH_COMPOSER_RENDERER_H

#include <vector>

#include "composer/provenance.h"
#include "core/basic_types.h"

namespace bach::composer {

// Renderer assembles tracks for MIDI output.
//
// Strict contract (rebuild plan §禁止事項):
//   * Renderer does NOT change any note's pitch.
//   * Renderer does NOT change any note's start_tick.
//   * Renderer MAY shorten note duration to prevent same-voice overlap.
//   * Renderer MAY group notes into Track structures by voice id.
//
// Anything beyond the above (snap, smooth, repair, recompose) is
// explicitly forbidden and indicates the failure mode the rebuild is
// escaping.
class Renderer {
 public:
  // Returns one Track per distinct voice in `notes`, sorted by voice id.
  // Same-voice overlapping notes have their durations clamped so the
  // earlier note ends at the later note's start_tick. No pitch
  // modifications are performed.
  std::vector<Track> render(const std::vector<NoteEvent>& notes) const;

  // Provenance pass-through. Renderer never edits provenance.
  std::vector<NoteProvenance> passthroughProvenance(const std::vector<NoteProvenance>& in) const {
    return in;
  }
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_RENDERER_H
