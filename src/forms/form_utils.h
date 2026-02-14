// Shared utilities for organ form implementations.
// Internal header -- not part of the public API.

#ifndef BACH_FORMS_FORM_UTILS_H
#define BACH_FORMS_FORM_UTILS_H

#include <algorithm>
#include <vector>

#include "core/basic_types.h"
#include "core/gm_program.h"

namespace bach {
namespace form_utils {

// Sort notes in every track by (start_tick, pitch).
inline void sortTrackNotes(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }
}

// Create standard organ tracks with the 4/5-voice mapping:
//   voice 0 → Ch0 ChurchOrgan  "Manual I (Great)"
//   voice 1 → Ch1 ReedOrgan    "Manual II (Swell)"
//   voice 2 → Ch2 ChurchOrgan  "Manual III (Positiv)"
//   voice 3 → Ch3 ChurchOrgan  "Pedal"
//   voice 4 → Ch4 ChurchOrgan  "Manual IV"
// Note: toccata uses a different pedal mapping and is NOT covered here.
inline std::vector<Track> createOrganTracks(uint8_t num_voices) {
  struct TrackSpec {
    uint8_t channel;
    uint8_t program;
    const char* name;
  };

  static constexpr TrackSpec kSpecs[] = {
      {0, GmProgram::kChurchOrgan, "Manual I (Great)"},
      {1, GmProgram::kReedOrgan, "Manual II (Swell)"},
      {2, GmProgram::kChurchOrgan, "Manual III (Positiv)"},
      {3, GmProgram::kChurchOrgan, "Pedal"},
      {4, GmProgram::kChurchOrgan, "Manual IV"},
  };

  std::vector<Track> tracks;
  tracks.reserve(num_voices);
  for (uint8_t idx = 0; idx < num_voices && idx < 5; ++idx) {
    Track track;
    track.channel = kSpecs[idx].channel;
    track.program = kSpecs[idx].program;
    track.name = kSpecs[idx].name;
    tracks.push_back(track);
  }
  return tracks;
}

}  // namespace form_utils
}  // namespace bach

#endif  // BACH_FORMS_FORM_UTILS_H
