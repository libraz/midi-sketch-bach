#include "composer/renderer.h"

#include <algorithm>
#include <map>

namespace bach::composer {

std::vector<Track> Renderer::render(const std::vector<NoteEvent>& notes) const {
  // Group by voice.
  std::map<VoiceId, std::vector<NoteEvent>> by_voice;
  for (const auto& note : notes) {
    // A zero-length MIDI note has no note-off interval and can leave a device
    // sounding indefinitely. Renderer is an output projection, so discard
    // invalid durations here without mutating the score/provenance vectors.
    if (note.duration == 0)
      continue;
    by_voice[note.voice].push_back(note);
  }

  std::vector<Track> tracks;
  for (auto& kv : by_voice) {
    auto& voice_notes = kv.second;
    std::stable_sort(
        voice_notes.begin(), voice_notes.end(),
        [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });
    // Clamp duration so consecutive same-voice notes do not overlap.
    for (std::size_t i = 0; i + 1 < voice_notes.size(); ++i) {
      const Tick next_start = voice_notes[i + 1].start_tick;
      const Tick this_end = voice_notes[i].start_tick + voice_notes[i].duration;
      if (this_end > next_start) {
        voice_notes[i].duration = next_start - voice_notes[i].start_tick;
      }
    }
    voice_notes.erase(std::remove_if(voice_notes.begin(), voice_notes.end(),
                                     [](const NoteEvent& note) { return note.duration == 0; }),
                      voice_notes.end());
    Track track;
    track.channel = kv.first;
    track.notes = std::move(voice_notes);
    tracks.push_back(std::move(track));
  }
  return tracks;
}

}  // namespace bach::composer
