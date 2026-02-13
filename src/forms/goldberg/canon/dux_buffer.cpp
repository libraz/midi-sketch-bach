// Bidirectional constraint buffer for canon generation.

#include "forms/goldberg/canon/dux_buffer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/pitch_utils.h"
#include "core/scale.h"

namespace bach {

DuxBuffer::DuxBuffer(const CanonSpec& spec, const TimeSignature& time_sig)
    : spec_(spec),
      delay_beats_(spec.delay_bars * static_cast<int>(time_sig.beatsPerBar())) {
  // Reserve space for a full 32-bar variation worth of beats.
  // 32 bars * beatsPerBar is the maximum for Goldberg variations.
  int max_beats = 32 * static_cast<int>(time_sig.beatsPerBar());
  dux_notes_.resize(static_cast<size_t>(max_beats), std::nullopt);
}

void DuxBuffer::recordDux(int beat_index, const NoteEvent& note) {
  if (beat_index < 0) return;

  // Grow buffer if needed.
  auto idx = static_cast<size_t>(beat_index);
  if (idx >= dux_notes_.size()) {
    dux_notes_.resize(idx + 1, std::nullopt);
  }

  dux_notes_[idx] = note;

  // Update melodic climax tracking.
  if (note.pitch > current_max_pitch_) {
    current_max_pitch_ = note.pitch;
    climax_beat_ = beat_index;
  }
}

std::optional<NoteEvent> DuxBuffer::deriveComes(int beat_index) const {
  // Comes has not entered yet if beat_index < delay_beats_.
  if (beat_index < delay_beats_) {
    return std::nullopt;
  }

  int source_beat = beat_index - delay_beats_;
  if (source_beat < 0) {
    return std::nullopt;
  }

  auto source_idx = static_cast<size_t>(source_beat);
  if (source_idx >= dux_notes_.size() || !dux_notes_[source_idx].has_value()) {
    return std::nullopt;
  }

  const NoteEvent& dux_note = dux_notes_[source_idx].value();

  // Build comes note: transformed pitch, offset tick, same duration.
  NoteEvent comes = dux_note;
  comes.pitch = transformPitch(dux_note.pitch);
  comes.start_tick = dux_note.start_tick +
                     static_cast<Tick>(delay_beats_) * kTicksPerBeat;
  comes.source = BachNoteSource::CanonComes;

  return comes;
}

uint8_t DuxBuffer::previewFutureComes(uint8_t candidate_dux_pitch) const {
  return transformPitch(candidate_dux_pitch);
}

float DuxBuffer::scoreClimaxAlignment(uint8_t candidate_pitch, int current_beat,
                                      const GoldbergStructuralGrid& grid) const {
  // Only score if the candidate would establish a new climax.
  if (candidate_pitch <= current_max_pitch_) {
    return 0.0f;
  }

  // Determine which bar the current beat falls in.
  // Assuming beatsPerBar derived from the delay configuration:
  // For 3/4 time, delay_beats_ / spec_.delay_bars gives beatsPerBar.
  int beats_per_bar = (spec_.delay_bars > 0)
                          ? delay_beats_ / spec_.delay_bars
                          : 3;  // Fallback to 3/4.
  int current_bar = (beats_per_bar > 0) ? current_beat / beats_per_bar : 0;

  // Clamp bar to grid range [0, 31].
  int clamped_bar = std::max(0, std::min(current_bar, 31));

  PhrasePosition pos = grid.getPhrasePosition(clamped_bar);

  if (pos == PhrasePosition::Intensification) {
    // Climax at Intensification: bonus (negative = better).
    return CanonMetricalRules::kClimaxPositionBonus;
  }

  // Penalty proportional to distance from nearest Intensification bar.
  // Intensification bars in 32-bar grid: bars 2, 6, 10, 14, 18, 22, 26, 30
  // (0-indexed, bar_in_phrase == 3 for Intensification).
  int min_distance = 32;  // Worst case.
  for (int bar = 0; bar < 32; ++bar) {
    if (grid.getPhrasePosition(bar) == PhrasePosition::Intensification) {
      int dist = std::abs(clamped_bar - bar);
      if (dist < min_distance) {
        min_distance = dist;
      }
    }
  }

  // Distance penalty: 0.5 per bar away from nearest Intensification.
  return static_cast<float>(min_distance) * 0.5f;
}

uint8_t DuxBuffer::transformPitch(uint8_t dux_pitch) const {
  Key key = spec_.key.tonic;
  ScaleType scale = getScaleType();

  if (spec_.transform == CanonTransform::Regular) {
    // Diatonic transposition by canon_interval scale degrees.
    int dux_degree = scale_util::pitchToAbsoluteDegree(dux_pitch, key, scale);
    int comes_degree = dux_degree + spec_.canon_interval;
    return scale_util::absoluteDegreeToPitch(comes_degree, key, scale);
  }

  // CanonTransform::Inverted:
  // Step 1: Invert diatonically around the tonic.
  // Step 2: Transpose by canon_interval scale degrees.
  // Note: tonicPitch uses musical octave (C4=octave 4, MIDI 60 = (4+1)*12),
  // while pitchToAbsoluteDegree uses MIDI octave (pitch/12). Convert by
  // subtracting 1 from the MIDI octave to get the musical octave.
  int musical_octave = static_cast<int>(dux_pitch) / 12 - 1;
  uint8_t tonic_pitch = tonicPitch(key, musical_octave);
  int tonic_degree = scale_util::pitchToAbsoluteDegree(tonic_pitch, key, scale);
  int dux_degree = scale_util::pitchToAbsoluteDegree(dux_pitch, key, scale);

  // Inversion: reflect around tonic degree.
  int inverted_degree = 2 * tonic_degree - dux_degree;

  // Then transpose by canon_interval.
  int comes_degree = inverted_degree + spec_.canon_interval;
  return scale_util::absoluteDegreeToPitch(comes_degree, key, scale);
}

ScaleType DuxBuffer::getScaleType() const {
  if (!spec_.key.is_minor) {
    return ScaleType::Major;
  }

  switch (spec_.minor_profile) {
    case MinorModeProfile::NaturalMinor:
      return ScaleType::NaturalMinor;
    case MinorModeProfile::HarmonicMinor:
      return ScaleType::HarmonicMinor;
    case MinorModeProfile::MixedBaroqueMinor:
      // For diatonic transposition in mixed baroque minor, use natural minor
      // as the base; melodic alterations are context-dependent and handled
      // at the note-selection level, not in the transform.
      return ScaleType::NaturalMinor;
  }

  return ScaleType::NaturalMinor;  // Fallback.
}

}  // namespace bach
