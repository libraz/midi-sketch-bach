// Implementation of the 4-phase dissonance analysis pipeline.

#include "analysis/dissonance_analyzer.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

// ===========================================================================
// String conversions
// ===========================================================================

const char* dissonanceSeverityToString(DissonanceSeverity severity) {
  switch (severity) {
    case DissonanceSeverity::Low:    return "Low";
    case DissonanceSeverity::Medium: return "Medium";
    case DissonanceSeverity::High:   return "High";
  }
  return "Unknown";
}

const char* dissonanceTypeToString(DissonanceType type) {
  switch (type) {
    case DissonanceType::SimultaneousClash:        return "SimultaneousClash";
    case DissonanceType::NonChordTone:             return "NonChordTone";
    case DissonanceType::SustainedOverChordChange: return "SustainedOverChordChange";
    case DissonanceType::NonDiatonicNote:          return "NonDiatonicNote";
  }
  return "Unknown";
}

// ===========================================================================
// Internal helpers
// ===========================================================================

namespace {

/// @brief Extract notes for a single voice, sorted by start_tick.
std::vector<NoteEvent> voiceNotes(const std::vector<NoteEvent>& notes, VoiceId voice) {
  std::vector<NoteEvent> result;
  for (const auto& note : notes) {
    if (note.voice == voice) result.push_back(note);
  }
  std::sort(result.begin(), result.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  return result;
}

/// @brief Pitch sounding at tick, or -1 if silent.
int soundingPitch(const std::vector<NoteEvent>& sorted_voice, Tick tick) {
  int result = -1;
  for (const auto& note : sorted_voice) {
    if (note.start_tick <= tick && tick < note.start_tick + note.duration)
      result = static_cast<int>(note.pitch);
    if (note.start_tick > tick) break;
  }
  return result;
}

/// @brief Check if any note sounding at tick in voice is structural (immutable).
///
/// Structural notes (subject, answer, pedal, countersubject) cannot be moved;
/// clashes involving them are unavoidable and should be downgraded.
bool hasStructuralNoteAt(const std::vector<NoteEvent>& sorted_voice, Tick tick) {
  for (const auto& note : sorted_voice) {
    if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
      ProtectionLevel level = getProtectionLevel(note.source);
      if (level == ProtectionLevel::Immutable ||
          level == ProtectionLevel::SemiImmutable ||
          level == ProtectionLevel::Structural) {
        return true;
      }
    }
    if (note.start_tick > tick) break;
  }
  return false;
}

/// @brief Get total end tick of all notes.
Tick totalEndTick(const std::vector<NoteEvent>& notes) {
  Tick max_end = 0;
  for (const auto& note : notes) {
    Tick end = note.start_tick + note.duration;
    if (end > max_end) max_end = end;
  }
  return max_end;
}

/// @brief Check if a beat is "strong" (beats 0 and 2 in 4/4 time).
bool isStrongBeat(Tick tick) {
  uint8_t beat = beatInBar(tick);
  return beat == 0 || beat == 2;
}

/// @brief Check if a note at tick is a stepwise passing tone.
///
/// A passing tone is approached and left by step (1-2 semitones) in the
/// same direction.
bool isPassingTone(const std::vector<NoteEvent>& sorted_voice, size_t note_idx) {
  if (note_idx == 0 || note_idx + 1 >= sorted_voice.size()) return false;

  int prev_pitch = static_cast<int>(sorted_voice[note_idx - 1].pitch);
  int curr_pitch = static_cast<int>(sorted_voice[note_idx].pitch);
  int next_pitch = static_cast<int>(sorted_voice[note_idx + 1].pitch);

  int approach = curr_pitch - prev_pitch;
  int departure = next_pitch - curr_pitch;

  // Both must be stepwise (1-2 semitones) and in the same direction.
  if (std::abs(approach) < 1 || std::abs(approach) > 2) return false;
  if (std::abs(departure) < 1 || std::abs(departure) > 2) return false;
  return (approach > 0 && departure > 0) || (approach < 0 && departure < 0);
}

/// @brief Check if a note is a neighbor tone (step away and return to same pitch).
///
/// A neighbor tone departs by step (1-2 semitones) and returns to the
/// original pitch. This is a standard Bach non-chord tone ornament.
bool isNeighborTone(const std::vector<NoteEvent>& sorted_voice, size_t note_idx) {
  if (note_idx == 0 || note_idx + 1 >= sorted_voice.size()) return false;

  int prev_pitch = static_cast<int>(sorted_voice[note_idx - 1].pitch);
  int curr_pitch = static_cast<int>(sorted_voice[note_idx].pitch);
  int next_pitch = static_cast<int>(sorted_voice[note_idx + 1].pitch);

  // Must return to the same pitch as the preceding note.
  if (prev_pitch != next_pitch) return false;

  // Departure must be stepwise (1-2 semitones).
  int departure = std::abs(curr_pitch - prev_pitch);
  return departure >= 1 && departure <= 2;
}

/// @brief Check if a note is an escape tone (stepwise approach, leap away).
///
/// An escape tone is approached by step (1-2 semitones) and left by leap
/// (3+ semitones). This is a standard Baroque non-harmonic tone idiom.
bool isEscapeTone(const std::vector<NoteEvent>& sorted_voice, size_t note_idx) {
  if (note_idx == 0 || note_idx + 1 >= sorted_voice.size()) return false;

  int prev_pitch = static_cast<int>(sorted_voice[note_idx - 1].pitch);
  int curr_pitch = static_cast<int>(sorted_voice[note_idx].pitch);
  int next_pitch = static_cast<int>(sorted_voice[note_idx + 1].pitch);

  int approach = std::abs(curr_pitch - prev_pitch);
  int departure = std::abs(next_pitch - curr_pitch);

  return (approach >= 1 && approach <= 2) && (departure >= 3);
}

/// @brief Check if a note is an appoggiatura (strong-beat dissonance resolving by step).
///
/// An appoggiatura is a non-chord-tone on a strong beat that resolves by
/// step (1-2 semitones) to a chord tone. This is one of the most characteristic
/// dissonance types in Bach's counterpoint.
bool isAppoggiatura(const std::vector<NoteEvent>& sorted_voice, size_t note_idx,
                    const HarmonicTimeline& timeline) {
  if (note_idx + 1 >= sorted_voice.size()) return false;
  if (!isStrongBeat(sorted_voice[note_idx].start_tick)) return false;

  int curr_pitch = static_cast<int>(sorted_voice[note_idx].pitch);
  int next_pitch = static_cast<int>(sorted_voice[note_idx + 1].pitch);
  int step = std::abs(next_pitch - curr_pitch);

  // Resolution must be by step (1-2 semitones).
  if (step < 1 || step > 2) return false;

  // Resolution note must be a chord tone of the current chord.
  const HarmonicEvent& event = timeline.getAt(sorted_voice[note_idx].start_tick);
  return isChordTone(static_cast<uint8_t>(next_pitch), event);
}

/// @brief Check if a note is an anticipation (chord tone of the next chord).
///
/// An anticipation sounds a note that belongs to the upcoming chord before
/// the chord change occurs. Common in Bach's counterpoint.
bool isAnticipation(uint8_t pitch, Tick tick, Tick duration,
                    const HarmonicTimeline& timeline) {
  Tick next_event_tick = tick + duration;
  const auto& next_event = timeline.getAt(next_event_tick);
  // The next event must be at a different tick (i.e., a chord change happened).
  const auto& curr_event = timeline.getAt(tick);
  if (next_event.tick == curr_event.tick) return false;
  return isChordTone(pitch, next_event);
}

/// @brief Find a note by matching pitch and tick in a sorted voice.
/// @return Index into sorted_voice, or SIZE_MAX if not found.
size_t findNoteIndex(const std::vector<NoteEvent>& sorted_voice,
                     uint8_t pitch, Tick tick) {
  for (size_t i = 0; i < sorted_voice.size(); ++i) {
    if (sorted_voice[i].pitch == pitch && sorted_voice[i].start_tick <= tick &&
        tick < sorted_voice[i].start_tick + sorted_voice[i].duration) {
      return i;
    }
  }
  return SIZE_MAX;
}

/// @brief Build DissonanceEvent with tick/bar/beat populated.
DissonanceEvent makeEvent(DissonanceType type, DissonanceSeverity severity,
                          Tick tick, uint8_t pitch, uint8_t other_pitch,
                          VoiceId voice_a, VoiceId voice_b, int ivl,
                          const std::string& desc) {
  DissonanceEvent ev;
  ev.type = type;
  ev.severity = severity;
  ev.tick = tick;
  ev.bar = static_cast<uint32_t>(tickToBar(tick)) + 1;
  ev.beat = beatInBar(tick) + 1;
  ev.pitch = pitch;
  ev.other_pitch = other_pitch;
  ev.voice_a = voice_a;
  ev.voice_b = voice_b;
  ev.interval = ivl;
  ev.description = desc;
  return ev;
}

/// @brief Build the summary from a list of events and total duration.
DissonanceAnalysisSummary buildSummary(const std::vector<DissonanceEvent>& events,
                                       Tick total_duration) {
  DissonanceAnalysisSummary s;
  s.total = static_cast<uint32_t>(events.size());
  for (const auto& ev : events) {
    switch (ev.severity) {
      case DissonanceSeverity::High:   ++s.high_count; break;
      case DissonanceSeverity::Medium: ++s.medium_count; break;
      case DissonanceSeverity::Low:    ++s.low_count; break;
    }
    switch (ev.type) {
      case DissonanceType::SimultaneousClash:
        ++s.simultaneous_clash_count; break;
      case DissonanceType::NonChordTone:
        ++s.non_chord_tone_count; break;
      case DissonanceType::SustainedOverChordChange:
        ++s.sustained_over_chord_change_count; break;
      case DissonanceType::NonDiatonicNote:
        ++s.non_diatonic_note_count; break;
    }
  }
  uint32_t total_beats = (total_duration > 0)
      ? static_cast<uint32_t>(total_duration / kTicksPerBeat) : 1;
  s.density_per_beat = static_cast<float>(s.total) / static_cast<float>(total_beats);
  float weighted = static_cast<float>(s.high_count) +
                   static_cast<float>(s.medium_count) * 0.5f;
  s.weighted_density_per_beat = weighted / static_cast<float>(total_beats);
  return s;
}

/// @brief Escape a string for JSON output.
std::string jsonEscape(const std::string& str) {
  std::string result;
  result.reserve(str.size());
  for (char ch : str) {
    switch (ch) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      default:   result += ch; break;
    }
  }
  return result;
}

}  // namespace

// ===========================================================================
// Phase 1: Simultaneous Clashes
// ===========================================================================

std::vector<DissonanceEvent> detectSimultaneousClashes(
    const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  std::vector<DissonanceEvent> results;
  if (num_voices < 2 || notes.empty()) return results;

  // Build per-voice sorted note lists.
  std::vector<std::vector<NoteEvent>> voices(num_voices);
  for (VoiceId v = 0; v < num_voices; ++v) {
    voices[v] = voiceNotes(notes, v);
  }

  Tick end_tick = totalEndTick(notes);

  // Scan each beat position.
  for (Tick tick = 0; tick < end_tick; tick += kTicksPerBeat) {
    // Get sounding pitches for each voice.
    std::vector<int> pitches(num_voices, -1);
    for (VoiceId v = 0; v < num_voices; ++v) {
      pitches[v] = soundingPitch(voices[v], tick);
    }

    // Identify the bass voice (lowest sounding pitch) at this beat.
    VoiceId bass_voice = 0;
    int bass_pitch = 999;
    for (VoiceId v = 0; v < num_voices; ++v) {
      if (pitches[v] >= 0 && pitches[v] < bass_pitch) {
        bass_pitch = pitches[v];
        bass_voice = v;
      }
    }

    // Check all voice pairs.
    for (VoiceId va = 0; va < num_voices; ++va) {
      if (pitches[va] < 0) continue;
      for (VoiceId vb = va + 1; vb < num_voices; ++vb) {
        if (pitches[vb] < 0) continue;

        int ivl = absoluteInterval(static_cast<uint8_t>(pitches[va]),
                                   static_cast<uint8_t>(pitches[vb]));
        IntervalQuality quality = classifyInterval(ivl);

        // Perfect 4th between upper voices (neither is bass) is consonant
        // in 3+ voice counterpoint -- it is the inversion of a perfect 5th.
        if (quality == IntervalQuality::Dissonance &&
            interval_util::compoundToSimple(ivl) == interval::kPerfect4th &&
            va != bass_voice && vb != bass_voice) {
          continue;
        }

        if (quality == IntervalQuality::Dissonance) {
          // Determine severity.
          DissonanceSeverity severity = DissonanceSeverity::Medium;
          if (isStrongBeat(tick)) {
            severity = DissonanceSeverity::High;
          }
          // Wide register (>= 36 semitones apart) downgrades.
          if (ivl >= 36) {
            severity = DissonanceSeverity::Low;
          }
          // Clash involving a structural note (subject, answer, pedal) is
          // unavoidable -- downgrade since the note cannot be moved.
          if (severity != DissonanceSeverity::Low) {
            if (hasStructuralNoteAt(voices[va], tick) ||
                hasStructuralNoteAt(voices[vb], tick)) {
              severity = DissonanceSeverity::Low;
            }
          }

          std::string desc = std::string(intervalToName(ivl)) + " clash - voice " +
              std::to_string(va) + " (" + pitchToNoteName(static_cast<uint8_t>(pitches[va])) +
              ") vs voice " + std::to_string(vb) + " (" +
              pitchToNoteName(static_cast<uint8_t>(pitches[vb])) + ")";

          results.push_back(makeEvent(
              DissonanceType::SimultaneousClash, severity, tick,
              static_cast<uint8_t>(pitches[va]),
              static_cast<uint8_t>(pitches[vb]),
              va, vb, ivl, desc));
        }
      }
    }
  }

  return results;
}

// ===========================================================================
// Phase 2: Non-Chord Tones
// ===========================================================================

std::vector<DissonanceEvent> detectNonChordTones(
    const std::vector<NoteEvent>& notes, const HarmonicTimeline& timeline,
    const HarmonicTimeline* generation_timeline) {
  std::vector<DissonanceEvent> results;
  if (notes.empty() || timeline.size() == 0) return results;

  // Build per-voice sorted note lists for passing tone detection.
  // Determine max voice ID.
  VoiceId max_voice = 0;
  for (const auto& note : notes) {
    if (note.voice > max_voice) max_voice = note.voice;
  }
  std::vector<std::vector<NoteEvent>> voices(max_voice + 1);
  for (VoiceId v = 0; v <= max_voice; ++v) {
    voices[v] = voiceNotes(notes, v);
  }

  for (const auto& note : notes) {
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    if (!isChordTone(note.pitch, event)) {
      DissonanceSeverity severity = DissonanceSeverity::Medium;

      if (isStrongBeat(note.start_tick)) {
        severity = DissonanceSeverity::High;
      } else {
        severity = DissonanceSeverity::Medium;
      }

      // Check for recognized non-harmonic tone idioms -- downgrade if recognized.
      if (note.voice < voices.size()) {
        size_t idx = findNoteIndex(voices[note.voice], note.pitch, note.start_tick);
        if (idx != SIZE_MAX) {
          if (isPassingTone(voices[note.voice], idx) ||
              isNeighborTone(voices[note.voice], idx) ||
              isEscapeTone(voices[note.voice], idx)) {
            severity = DissonanceSeverity::Low;
          }
          // Appoggiatura: strong-beat dissonance resolving by step to chord tone.
          if (severity == DissonanceSeverity::High) {
            if (isAppoggiatura(voices[note.voice], idx, timeline)) {
              severity = DissonanceSeverity::Low;
            }
          }
        }
      }

      // Anticipation: note is a chord tone of the next chord.
      if (severity != DissonanceSeverity::Low) {
        if (isAnticipation(note.pitch, note.start_tick, note.duration, timeline)) {
          severity = DissonanceSeverity::Low;
        }
      }

      // Structural notes (subject, answer, pedal, false entry, etc.) cannot be
      // moved; their NCT status is unavoidable and should not inflate the
      // density metric.
      if (severity != DissonanceSeverity::Low) {
        ProtectionLevel level = getProtectionLevel(note.source);
        if (level == ProtectionLevel::Immutable ||
            level == ProtectionLevel::SemiImmutable ||
            level == ProtectionLevel::Structural) {
          severity = DissonanceSeverity::Low;
        }
      }

      // Dual-timeline fallback: if the note is a chord tone of the generation
      // timeline (beat-resolution), downgrade to Low. The note was intentionally
      // placed as a chord tone during generation.
      if (severity != DissonanceSeverity::Low && generation_timeline != nullptr) {
        const HarmonicEvent& gen_ev = generation_timeline->getAt(note.start_tick);
        if (isChordTone(note.pitch, gen_ev)) {
          severity = DissonanceSeverity::Low;
        }
      }

      std::string desc = std::string(dissonanceTypeToString(DissonanceType::NonChordTone)) +
          " - voice " + std::to_string(note.voice) + " (" +
          pitchToNoteName(note.pitch) + ") not in " +
          chordDegreeToString(event.chord.degree) + " chord";

      results.push_back(makeEvent(
          DissonanceType::NonChordTone, severity, note.start_tick,
          note.pitch, 0, note.voice, 0, 0, desc));
    }
  }

  return results;
}

// ===========================================================================
// Phase 3: Sustained Over Chord Change
// ===========================================================================

std::vector<DissonanceEvent> detectSustainedOverChordChange(
    const std::vector<NoteEvent>& notes, uint8_t num_voices,
    const HarmonicTimeline& timeline) {
  std::vector<DissonanceEvent> results;
  if (notes.empty() || timeline.size() < 2 || num_voices == 0) return results;

  const auto& events = timeline.events();

  // Build per-voice sorted note lists for suspension resolution detection.
  std::vector<std::vector<NoteEvent>> voices(num_voices);
  for (VoiceId v = 0; v < num_voices; ++v) {
    voices[v] = voiceNotes(notes, v);
  }

  // For each chord boundary, check if any notes sustain across it.
  for (size_t ei = 1; ei < events.size(); ++ei) {
    Tick boundary_tick = events[ei].tick;
    const HarmonicEvent& new_event = events[ei];
    const HarmonicEvent& prev_event = events[ei - 1];

    for (const auto& note : notes) {
      Tick note_end = note.start_tick + note.duration;
      // Note must have started before the boundary and still be sounding.
      if (note.start_tick < boundary_tick && note_end > boundary_tick) {
        if (!isChordTone(note.pitch, new_event)) {
          DissonanceSeverity severity = DissonanceSeverity::Medium;
          if (isStrongBeat(boundary_tick)) {
            severity = DissonanceSeverity::High;
          }

          // Suspension detection: preparation-suspension-resolution pattern.
          // 1. The held note was a chord tone in the previous chord (preparation).
          // 2. It is now non-chord-tone in the new chord (suspension).
          // 3. The next note in the same voice resolves down by step to a chord tone.
          if (isChordTone(note.pitch, prev_event) &&
              note.voice < voices.size()) {
            const auto& voice_notes = voices[note.voice];
            // Find the next note after this one in the same voice.
            for (size_t ni = 0; ni < voice_notes.size(); ++ni) {
              if (voice_notes[ni].start_tick == note.start_tick &&
                  voice_notes[ni].pitch == note.pitch) {
                // Look for the resolution note.
                if (ni + 1 < voice_notes.size()) {
                  int step = static_cast<int>(note.pitch) -
                             static_cast<int>(voice_notes[ni + 1].pitch);
                  // Resolution by descending step (1-2 semitones) to chord tone.
                  if (step >= 1 && step <= 2 &&
                      isChordTone(voice_notes[ni + 1].pitch, new_event)) {
                    severity = DissonanceSeverity::Low;
                  }
                }
                break;
              }
            }
          }

          std::string desc = std::string(
              dissonanceTypeToString(DissonanceType::SustainedOverChordChange)) +
              " - voice " + std::to_string(note.voice) + " (" +
              pitchToNoteName(note.pitch) + ") sustained into " +
              chordDegreeToString(new_event.chord.degree) + " chord";

          results.push_back(makeEvent(
              DissonanceType::SustainedOverChordChange, severity,
              boundary_tick, note.pitch, 0, note.voice, 0, 0, desc));
        }
      }
    }
  }

  return results;
}

// ===========================================================================
// Phase 4: Non-Diatonic Notes
// ===========================================================================

std::vector<DissonanceEvent> detectNonDiatonicNotes(
    const std::vector<NoteEvent>& notes, const KeySignature& key_sig) {
  std::vector<DissonanceEvent> results;
  if (notes.empty()) return results;

  // Build per-voice sorted note lists for passing tone detection.
  VoiceId max_voice = 0;
  for (const auto& note : notes) {
    if (note.voice > max_voice) max_voice = note.voice;
  }
  std::vector<std::vector<NoteEvent>> voices(max_voice + 1);
  for (VoiceId v = 0; v <= max_voice; ++v) {
    voices[v] = voiceNotes(notes, v);
  }

  for (const auto& note : notes) {
    if (!isDiatonicInKey(note.pitch, key_sig.tonic, key_sig.is_minor)) {
      DissonanceSeverity severity = DissonanceSeverity::Medium;

      // Bach chromatic passing/neighbor tones: stepwise + weak beat = Low severity.
      bool is_weak = !isStrongBeat(note.start_tick);
      bool is_ornamental = false;
      if (note.voice < voices.size()) {
        size_t idx = findNoteIndex(voices[note.voice], note.pitch, note.start_tick);
        if (idx != SIZE_MAX) {
          is_ornamental = isPassingTone(voices[note.voice], idx) ||
                          isNeighborTone(voices[note.voice], idx);
        }
      }

      if (is_weak && is_ornamental) {
        severity = DissonanceSeverity::Low;
      } else if (isStrongBeat(note.start_tick)) {
        severity = DissonanceSeverity::High;
      }

      std::string desc = std::string(
          dissonanceTypeToString(DissonanceType::NonDiatonicNote)) +
          " - voice " + std::to_string(note.voice) + " (" +
          pitchToNoteName(note.pitch) + ") not diatonic in " +
          keySignatureToString(key_sig);

      results.push_back(makeEvent(
          DissonanceType::NonDiatonicNote, severity, note.start_tick,
          note.pitch, 0, note.voice, 0, 0, desc));
    }
  }

  return results;
}

std::vector<DissonanceEvent> detectNonDiatonicNotes(
    const std::vector<NoteEvent>& notes, const HarmonicTimeline& timeline) {
  std::vector<DissonanceEvent> results;
  if (notes.empty() || timeline.size() == 0) return results;

  // Build per-voice sorted note lists for passing tone detection.
  VoiceId max_voice = 0;
  for (const auto& note : notes) {
    if (note.voice > max_voice) max_voice = note.voice;
  }
  std::vector<std::vector<NoteEvent>> voices(max_voice + 1);
  for (VoiceId v = 0; v <= max_voice; ++v) {
    voices[v] = voiceNotes(notes, v);
  }

  for (const auto& note : notes) {
    // Get the active key at this note's tick from the timeline.
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    KeySignature local_key;
    local_key.tonic = event.key;
    local_key.is_minor = event.is_minor;

    if (!isDiatonicInKey(note.pitch, local_key.tonic, local_key.is_minor)) {
      DissonanceSeverity severity = DissonanceSeverity::Medium;

      // Bach chromatic passing/neighbor tones: stepwise + weak beat = Low severity.
      bool is_weak = !isStrongBeat(note.start_tick);
      bool is_ornamental = false;
      if (note.voice < voices.size()) {
        size_t idx = findNoteIndex(voices[note.voice], note.pitch, note.start_tick);
        if (idx != SIZE_MAX) {
          is_ornamental = isPassingTone(voices[note.voice], idx) ||
                          isNeighborTone(voices[note.voice], idx);
        }
      }

      if (is_weak && is_ornamental) {
        severity = DissonanceSeverity::Low;
      } else if (isStrongBeat(note.start_tick)) {
        severity = DissonanceSeverity::High;
      }

      std::string desc = std::string(
          dissonanceTypeToString(DissonanceType::NonDiatonicNote)) +
          " - voice " + std::to_string(note.voice) + " (" +
          pitchToNoteName(note.pitch) + ") not diatonic in " +
          keySignatureToString(local_key);

      results.push_back(makeEvent(
          DissonanceType::NonDiatonicNote, severity, note.start_tick,
          note.pitch, 0, note.voice, 0, 0, desc));
    }
  }

  return results;
}

// ===========================================================================
// Orchestrators
// ===========================================================================

DissonanceAnalysisResult analyzeOrganDissonance(
    const std::vector<NoteEvent>& notes, uint8_t num_voices,
    const HarmonicTimeline& timeline, const KeySignature& key_sig,
    const HarmonicTimeline* generation_timeline) {
  DissonanceAnalysisResult result;

  auto phase1 = detectSimultaneousClashes(notes, num_voices);
  auto phase2 = detectNonChordTones(notes, timeline, generation_timeline);
  auto phase3 = detectSustainedOverChordChange(notes, num_voices, timeline);
  // Use modulation-aware non-diatonic detection when a generation timeline
  // is available, so notes in modulated sections are checked against the
  // local key rather than the global key signature.
  auto phase4 = generation_timeline
      ? detectNonDiatonicNotes(notes, *generation_timeline)
      : detectNonDiatonicNotes(notes, key_sig);

  result.events.reserve(phase1.size() + phase2.size() + phase3.size() + phase4.size());
  result.events.insert(result.events.end(), phase1.begin(), phase1.end());
  result.events.insert(result.events.end(), phase2.begin(), phase2.end());
  result.events.insert(result.events.end(), phase3.begin(), phase3.end());
  result.events.insert(result.events.end(), phase4.begin(), phase4.end());

  // Sort by tick for presentation.
  std::sort(result.events.begin(), result.events.end(),
            [](const DissonanceEvent& a, const DissonanceEvent& b) {
              return a.tick < b.tick;
            });

  result.summary = buildSummary(result.events, totalEndTick(notes));
  return result;
}

DissonanceAnalysisResult analyzeSoloStringDissonance(
    const std::vector<NoteEvent>& notes,
    const HarmonicTimeline& timeline, const KeySignature& key_sig) {
  DissonanceAnalysisResult result;

  auto phase2 = detectNonChordTones(notes, timeline);
  auto phase4 = detectNonDiatonicNotes(notes, key_sig);

  result.events.reserve(phase2.size() + phase4.size());
  result.events.insert(result.events.end(), phase2.begin(), phase2.end());
  result.events.insert(result.events.end(), phase4.begin(), phase4.end());

  std::sort(result.events.begin(), result.events.end(),
            [](const DissonanceEvent& a, const DissonanceEvent& b) {
              return a.tick < b.tick;
            });

  result.summary = buildSummary(result.events, totalEndTick(notes));
  return result;
}

// ===========================================================================
// DissonanceAnalysisResult methods
// ===========================================================================

std::string DissonanceAnalysisResult::toTextSummary(
    const char* system_name, uint8_t num_voices) const {
  std::ostringstream oss;
  oss << "=== Dissonance Analysis ===\n";
  oss << "System: " << system_name;
  if (num_voices > 1) {
    oss << " (" << static_cast<int>(num_voices) << " voices)";
  }
  oss << "\n";
  oss << "Total: " << summary.total
      << "  (High: " << summary.high_count
      << ", Medium: " << summary.medium_count
      << ", Low: " << summary.low_count << ")\n";

  char density_buf[32];
  std::snprintf(density_buf, sizeof(density_buf), "%.2f", summary.density_per_beat);
  char weighted_buf[32];
  std::snprintf(weighted_buf, sizeof(weighted_buf), "%.2f", summary.weighted_density_per_beat);
  oss << "Density: " << density_buf << "/beat (weighted: " << weighted_buf << "/beat)\n\n";

  oss << "  SimultaneousClash:        " << summary.simultaneous_clash_count << "\n";
  oss << "  NonChordTone:             " << summary.non_chord_tone_count << "\n";
  oss << "  SustainedOverChordChange: " << summary.sustained_over_chord_change_count << "\n";
  oss << "  NonDiatonicNote:          " << summary.non_diatonic_note_count << "\n";

  // List high-severity issues.
  bool has_high = false;
  for (const auto& ev : events) {
    if (ev.severity == DissonanceSeverity::High) {
      if (!has_high) {
        oss << "\nHigh-severity issues:\n";
        has_high = true;
      }
      oss << "  Bar " << ev.bar << ", Beat " << static_cast<int>(ev.beat)
          << ": " << ev.description << "\n";
    }
  }

  return oss.str();
}

std::string DissonanceAnalysisResult::toJson() const {
  std::ostringstream oss;
  oss << "{\n";
  oss << "  \"summary\": {\n";
  oss << "    \"total\": " << summary.total << ",\n";
  oss << "    \"high\": " << summary.high_count << ",\n";
  oss << "    \"medium\": " << summary.medium_count << ",\n";
  oss << "    \"low\": " << summary.low_count << ",\n";

  char density_buf[32];
  std::snprintf(density_buf, sizeof(density_buf), "%.4f", summary.density_per_beat);
  char weighted_buf[32];
  std::snprintf(weighted_buf, sizeof(weighted_buf), "%.4f", summary.weighted_density_per_beat);
  oss << "    \"density_per_beat\": " << density_buf << ",\n";
  oss << "    \"weighted_density_per_beat\": " << weighted_buf << ",\n";

  oss << "    \"by_type\": {\n";
  oss << "      \"SimultaneousClash\": " << summary.simultaneous_clash_count << ",\n";
  oss << "      \"NonChordTone\": " << summary.non_chord_tone_count << ",\n";
  oss << "      \"SustainedOverChordChange\": "
      << summary.sustained_over_chord_change_count << ",\n";
  oss << "      \"NonDiatonicNote\": " << summary.non_diatonic_note_count << "\n";
  oss << "    }\n";
  oss << "  },\n";
  oss << "  \"events\": [\n";

  for (size_t i = 0; i < events.size(); ++i) {
    const auto& ev = events[i];
    oss << "    {\n";
    oss << "      \"type\": \"" << dissonanceTypeToString(ev.type) << "\",\n";
    oss << "      \"severity\": \"" << dissonanceSeverityToString(ev.severity) << "\",\n";
    oss << "      \"tick\": " << ev.tick << ",\n";
    oss << "      \"bar\": " << ev.bar << ",\n";
    oss << "      \"beat\": " << static_cast<int>(ev.beat) << ",\n";
    oss << "      \"pitch\": " << static_cast<int>(ev.pitch) << ",\n";
    oss << "      \"pitch_name\": \"" << pitchToNoteName(ev.pitch) << "\",\n";
    if (ev.other_pitch > 0) {
      oss << "      \"other_pitch\": " << static_cast<int>(ev.other_pitch) << ",\n";
      oss << "      \"other_pitch_name\": \"" << pitchToNoteName(ev.other_pitch) << "\",\n";
    }
    oss << "      \"voice_a\": " << static_cast<int>(ev.voice_a) << ",\n";
    if (ev.interval > 0) {
      oss << "      \"interval\": " << ev.interval << ",\n";
      oss << "      \"interval_name\": \"" << intervalToName(ev.interval) << "\",\n";
    }
    oss << "      \"description\": \"" << jsonEscape(ev.description) << "\"\n";
    oss << "    }";
    if (i + 1 < events.size()) oss << ",";
    oss << "\n";
  }

  oss << "  ]\n";
  oss << "}\n";
  return oss.str();
}

}  // namespace bach
