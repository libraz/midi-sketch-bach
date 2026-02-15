// Implementation of role-based bass line realization from harmonic scheme.

#include "solo_string/arch/bass_realizer.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "harmony/chord_types.h"
#include "harmony/key.h"
#include "solo_string/arch/chaconne_scheme.h"

namespace bach {

static constexpr BachNoteSource kBassSource = BachNoteSource::ChaconneBass;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Standard bass velocity for bowed instrument realization.
static constexpr uint8_t kBassVelocity = 80;

/// Bass voice index (single-line bass).
static constexpr VoiceId kBassVoice = 0;

/// Maximum leap in semitones for Walking bass (excludes octave).
static constexpr int kMaxWalkingLeap = 4;

/// Fraction of beats eligible for weak-beat syncopation in Syncopated style.
static constexpr float kSyncopationRate = 0.4f;

// ---------------------------------------------------------------------------
// Chord tone computation from SchemeEntry + key
// ---------------------------------------------------------------------------

namespace {

/// @brief Compute the root MIDI pitch for a SchemeEntry in a given key and octave.
/// @param entry The scheme entry with chord degree and quality.
/// @param key The key signature for pitch realization.
/// @param bass_octave The octave for root placement (MIDI octave numbering).
/// @return Root MIDI pitch clamped to [0, 127].
int computeRootPitch(const SchemeEntry& entry, const KeySignature& key, int bass_octave) {
  uint8_t semitone_offset = key.is_minor ? degreeMinorSemitones(entry.degree)
                                         : degreeSemitones(entry.degree);
  return (bass_octave + 1) * 12 + static_cast<int>(key.tonic) + semitone_offset;
}

/// @brief Get the third interval above root based on chord quality.
/// @param quality Chord quality determining the third size.
/// @return Interval in semitones (3 for minor/dim, 4 for major/aug/dom7).
int thirdInterval(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Minor:
    case ChordQuality::Diminished:
    case ChordQuality::Minor7:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
      return 3;
    default:
      return 4;
  }
}

/// @brief Get the fifth interval above root based on chord quality.
/// @param quality Chord quality determining the fifth size.
/// @return Interval in semitones (6 for dim, 8 for aug, 7 for others).
int fifthInterval(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Diminished:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
      return 6;
    case ChordQuality::Augmented:
      return 8;
    default:
      return 7;
  }
}

/// @brief Collect root, 3rd, 5th as MIDI pitches for a chord at a given root pitch.
/// @param root_pitch Root MIDI pitch of the chord.
/// @param quality Chord quality for third/fifth computation.
/// @return Vector of 3 pitches: {root, third, fifth}.
std::vector<int> getChordPitches(int root_pitch, ChordQuality quality) {
  return {root_pitch, root_pitch + thirdInterval(quality), root_pitch + fifthInterval(quality)};
}

/// @brief Determine the bass octave from a register profile.
/// @param profile Register bounds.
/// @return Octave number suitable for root placement.
int bassOctaveFromProfile(const BassRegisterProfile& profile) {
  // Place the bass in the octave that centers within the register.
  int mid = (static_cast<int>(profile.effective_low) + static_cast<int>(profile.effective_high)) / 2;
  return (mid / 12) - 1;
}

/// @brief Clamp a pitch to the register profile and MIDI range [0, 127].
/// @param pitch Raw pitch value.
/// @param profile Register bounds.
/// @return Clamped pitch.
uint8_t clampToProfile(int pitch, const BassRegisterProfile& profile) {
  return clampPitch(pitch, profile.effective_low, profile.effective_high);
}

/// @brief Create a NoteEvent with common fields pre-filled.
/// @param start Start tick.
/// @param dur Duration in ticks.
/// @param pitch MIDI pitch.
/// @return NoteEvent with source, velocity, and voice set.
NoteEvent makeBassNote(Tick start, Tick dur, uint8_t pitch) {
  NoteEvent note{};
  note.start_tick = start;
  note.duration = dur;
  note.pitch = pitch;
  note.velocity = kBassVelocity;
  note.voice = kBassVoice;
  note.source = kBassSource;
  return note;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public: getRealizationStyle
// ---------------------------------------------------------------------------

BassRealizationStyle getRealizationStyle(VariationRole role) {
  switch (role) {
    case VariationRole::Establish:
      return BassRealizationStyle::Simple;
    case VariationRole::Develop:
      return BassRealizationStyle::Walking;
    case VariationRole::Destabilize:
      return BassRealizationStyle::Syncopated;
    case VariationRole::Illuminate:
      return BassRealizationStyle::Lyrical;
    case VariationRole::Accumulate:
      return BassRealizationStyle::Elaborate;
    case VariationRole::Resolve:
      return BassRealizationStyle::Simple;
  }
  return BassRealizationStyle::Simple;  // Unreachable but satisfies compiler.
}

// ---------------------------------------------------------------------------
// Public: getBassRegisterProfile
// ---------------------------------------------------------------------------

BassRegisterProfile getBassRegisterProfile(
    VariationRole role, uint8_t instrument_low, uint8_t instrument_high,
    int accumulate_index) {
  // Base register: inner portion of the instrument range.
  // Bass sits in the lower third of the instrument range.
  int range = static_cast<int>(instrument_high) - static_cast<int>(instrument_low);
  int base_low = static_cast<int>(instrument_low);
  int base_high = base_low + range / 3;

  // Ensure at least one octave of range.
  if (base_high - base_low < 12) {
    base_high = base_low + 12;
  }

  // Clamp to instrument bounds.
  base_high = std::min(base_high, static_cast<int>(instrument_high));

  switch (role) {
    case VariationRole::Establish:
    case VariationRole::Resolve:
    case VariationRole::Develop:
    case VariationRole::Destabilize:
      // Standard bass register: lower third of instrument.
      break;

    case VariationRole::Illuminate:
      // Slightly wider for lyrical expression.
      base_high = std::min(base_high + 2, static_cast<int>(instrument_high));
      break;

    case VariationRole::Accumulate: {
      // Staged expansion: +3, +5, +7 semitones per index.
      int expansion_table[] = {3, 5, 7};
      int clamped_index = std::min(std::max(accumulate_index, 0), 2);
      int expansion = expansion_table[clamped_index];
      base_high = std::min(base_high + expansion, static_cast<int>(instrument_high));
      break;
    }
  }

  return BassRegisterProfile{
      static_cast<uint8_t>(base_low),
      static_cast<uint8_t>(base_high)};
}

// ---------------------------------------------------------------------------
// Style-specific generators (internal)
// ---------------------------------------------------------------------------

namespace {

/// @brief Generate Simple bass: one note per SchemeEntry at chord root.
std::vector<NoteEvent> realizeSimple(
    const ChaconneScheme& scheme, const KeySignature& key,
    const BassRegisterProfile& profile) {
  std::vector<NoteEvent> notes;
  notes.reserve(scheme.size());

  int bass_octave = bassOctaveFromProfile(profile);

  for (const auto& entry : scheme.entries()) {
    int root = computeRootPitch(entry, key, bass_octave);
    uint8_t pitch = clampToProfile(root, profile);
    Tick start = static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
    Tick dur = static_cast<Tick>(entry.duration_beats) * kTicksPerBeat;
    notes.push_back(makeBassNote(start, dur, pitch));
  }

  return notes;
}

/// @brief Generate Walking bass: chord tones on strong beats, stepwise passing on weak beats.
std::vector<NoteEvent> realizeWalking(
    const ChaconneScheme& scheme, const KeySignature& key,
    const BassRegisterProfile& profile, uint32_t seed) {
  std::vector<NoteEvent> notes;
  notes.reserve(scheme.size() * 4);  // Rough upper bound.

  int bass_octave = bassOctaveFromProfile(profile);
  const auto& entries = scheme.entries();
  uint32_t rng_index = 0;

  for (size_t eidx = 0; eidx < entries.size(); ++eidx) {
    const auto& entry = entries[eidx];
    int root = computeRootPitch(entry, key, bass_octave);
    uint8_t root_clamped = clampToProfile(root, profile);

    Tick entry_start = static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
    int num_beats = static_cast<int>(entry.duration_beats);

    // Determine the target pitch for the next entry (for voice leading).
    uint8_t next_target = root_clamped;
    if (eidx + 1 < entries.size()) {
      int next_root = computeRootPitch(entries[eidx + 1], key, bass_octave);
      next_target = clampToProfile(next_root, profile);
    }

    uint8_t current_pitch = root_clamped;

    for (int beat = 0; beat < num_beats; ++beat) {
      Tick beat_start = entry_start + static_cast<Tick>(beat) * kTicksPerBeat;

      if (beat == 0) {
        // Strong beat: chord tone (root).
        notes.push_back(makeBassNote(beat_start, kTicksPerBeat, root_clamped));
        current_pitch = root_clamped;
      } else {
        // Weak beat: stepwise passing tone.
        // Move toward the next chord head by 1-2 semitones.
        int direction = (static_cast<int>(next_target) > static_cast<int>(current_pitch)) ? 1 : -1;
        if (next_target == current_pitch) {
          // Same pitch: alternate direction randomly.
          direction = (rng::splitmix32(seed, rng_index++) & 1) ? 1 : -1;
        }

        int step_size = 1 + static_cast<int>(rng::splitmix32(seed, rng_index++) % 2);  // 1 or 2
        int candidate = static_cast<int>(current_pitch) + direction * step_size;

        // Enforce max walking leap.
        if (std::abs(candidate - static_cast<int>(current_pitch)) > kMaxWalkingLeap) {
          candidate = static_cast<int>(current_pitch) + direction * 2;
        }

        uint8_t pitch = clampToProfile(candidate, profile);
        notes.push_back(makeBassNote(beat_start, kTicksPerBeat, pitch));
        current_pitch = pitch;
      }
    }
  }

  return notes;
}

/// @brief Generate Syncopated bass: chord tones only, some weak-beat emphasis.
std::vector<NoteEvent> realizeSyncopated(
    const ChaconneScheme& scheme, const KeySignature& key,
    const BassRegisterProfile& profile, uint32_t seed) {
  std::vector<NoteEvent> notes;
  notes.reserve(scheme.size() * 3);

  int bass_octave = bassOctaveFromProfile(profile);
  uint32_t rng_index = 0;

  for (const auto& entry : scheme.entries()) {
    int root = computeRootPitch(entry, key, bass_octave);
    auto chord_pitches = getChordPitches(root, entry.quality);

    Tick entry_start = static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
    int num_beats = static_cast<int>(entry.duration_beats);

    bool prev_strong_absent = false;  // Track consecutive strong-beat absence.

    for (int beat = 0; beat < num_beats; ++beat) {
      Tick beat_start = entry_start + static_cast<Tick>(beat) * kTicksPerBeat;

      // Decide syncopation: shift to weak beat (second half of beat).
      float roll = static_cast<float>(rng::splitmix32(seed, rng_index++) & 0xFFFF) / 65535.0f;
      bool syncopate = (roll < kSyncopationRate) && !prev_strong_absent;

      if (syncopate) {
        // Weak-beat emphasis: start at the offbeat, hold through to next beat.
        Tick half_beat = kTicksPerBeat / 2;
        Tick synco_start = beat_start + half_beat;

        // Duration extends to fill the remaining space in this beat plus half the next.
        Tick synco_dur = half_beat;
        if (beat + 1 < num_beats) {
          synco_dur += half_beat;  // Tie into next beat's first half.
        }

        // Select a chord tone (root or third, favoring root).
        uint32_t tone_pick = rng::splitmix32(seed, rng_index++) % 3;
        int tone_idx = (tone_pick == 0) ? 1 : 0;  // 33% third, 67% root.
        int picked = chord_pitches[static_cast<size_t>(tone_idx)];
        uint8_t pitch = clampToProfile(picked, profile);

        notes.push_back(makeBassNote(synco_start, synco_dur, pitch));
        prev_strong_absent = true;
      } else {
        // Normal strong-beat placement.
        // Pick a chord tone: root for first beat, random chord tone otherwise.
        int picked = root;
        if (beat > 0) {
          uint32_t tone_pick = rng::splitmix32(seed, rng_index++) % 3;
          picked = chord_pitches[tone_pick];
        }
        uint8_t pitch = clampToProfile(picked, profile);

        // Vary duration: some notes are short (staccato feel), some held.
        Tick dur = kTicksPerBeat;
        float dur_roll = static_cast<float>(rng::splitmix32(seed, rng_index++) & 0xFFFF) / 65535.0f;
        if (dur_roll < 0.3f && beat > 0) {
          dur = kTicksPerBeat / 2;  // Short note for rhythmic variety.
        }

        notes.push_back(makeBassNote(beat_start, dur, pitch));
        prev_strong_absent = false;
      }
    }
  }

  return notes;
}

/// @brief Generate Lyrical bass: half-note base with occasional 3rd/5th leaps.
std::vector<NoteEvent> realizeLyrical(
    const ChaconneScheme& scheme, const KeySignature& key,
    const BassRegisterProfile& profile, uint32_t seed) {
  std::vector<NoteEvent> notes;
  notes.reserve(scheme.size() * 2);

  int bass_octave = bassOctaveFromProfile(profile);
  uint32_t rng_index = 0;

  for (const auto& entry : scheme.entries()) {
    int root = computeRootPitch(entry, key, bass_octave);
    auto chord_pitches = getChordPitches(root, entry.quality);

    Tick entry_start = static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
    Tick total_dur = static_cast<Tick>(entry.duration_beats) * kTicksPerBeat;
    constexpr Tick kHalfNote = kTicksPerBeat * 2;

    if (total_dur <= kHalfNote) {
      // Short entry: single note at root.
      uint8_t pitch = clampToProfile(root, profile);
      notes.push_back(makeBassNote(entry_start, total_dur, pitch));
    } else {
      // First half: root as half note.
      uint8_t root_pitch = clampToProfile(root, profile);
      notes.push_back(makeBassNote(entry_start, kHalfNote, root_pitch));

      // Second half: leap to 3rd or 5th for lyrical character.
      float leap_roll =
          static_cast<float>(rng::splitmix32(seed, rng_index++) & 0xFFFF) / 65535.0f;
      int leap_target;
      if (leap_roll < 0.5f) {
        leap_target = chord_pitches[1];  // Third.
      } else {
        leap_target = chord_pitches[2];  // Fifth.
      }

      uint8_t leap_pitch = clampToProfile(leap_target, profile);
      Tick remaining = total_dur - kHalfNote;
      notes.push_back(makeBassNote(entry_start + kHalfNote, remaining, leap_pitch));
    }
  }

  return notes;
}

/// @brief Generate Elaborate bass: eighth-note arpeggios through chord tones.
std::vector<NoteEvent> realizeElaborate(
    const ChaconneScheme& scheme, const KeySignature& key,
    const BassRegisterProfile& profile, uint32_t seed) {
  std::vector<NoteEvent> notes;
  notes.reserve(scheme.size() * 8);  // Up to 8 eighth notes per entry.

  int bass_octave = bassOctaveFromProfile(profile);
  uint32_t rng_index = 0;

  for (const auto& entry : scheme.entries()) {
    int root = computeRootPitch(entry, key, bass_octave);
    auto chord_pitches = getChordPitches(root, entry.quality);

    // Build arpeggio pitch pool: root, 3rd, 5th, octave (ascending then descending).
    std::vector<int> arp_pool;
    arp_pool.reserve(7);
    for (int cp : chord_pitches) {
      arp_pool.push_back(cp);
    }
    arp_pool.push_back(root + 12);  // Octave above root (Elaborate allows octave leaps).

    // Add descending mirror: 5th, 3rd back down.
    arp_pool.push_back(chord_pitches[2]);
    arp_pool.push_back(chord_pitches[1]);
    arp_pool.push_back(root);

    Tick entry_start = static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
    int num_beats = static_cast<int>(entry.duration_beats);
    int eighth_notes_total = num_beats * 2;

    for (int eighth = 0; eighth < eighth_notes_total; ++eighth) {
      Tick note_start = entry_start + static_cast<Tick>(eighth) * duration::kEighthNote;

      // Cycle through arpeggio pool with slight randomization.
      size_t pool_idx = static_cast<size_t>(eighth) % arp_pool.size();

      // Occasionally vary the pool selection for musical interest.
      float vary_roll =
          static_cast<float>(rng::splitmix32(seed, rng_index++) & 0xFFFF) / 65535.0f;
      if (vary_roll < 0.2f && arp_pool.size() > 1) {
        pool_idx = rng::splitmix32(seed, rng_index++) % arp_pool.size();
      }

      uint8_t pitch = clampToProfile(arp_pool[pool_idx], profile);
      notes.push_back(makeBassNote(note_start, duration::kEighthNote, pitch));
    }
  }

  return notes;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public: realizeBass
// ---------------------------------------------------------------------------

std::vector<NoteEvent> realizeBass(
    const ChaconneScheme& scheme,
    const KeySignature& key,
    VariationRole role,
    uint8_t register_low, uint8_t register_high,
    uint32_t seed,
    int accumulate_index) {
  if (scheme.size() == 0) {
    return {};
  }

  BassRegisterProfile profile =
      getBassRegisterProfile(role, register_low, register_high, accumulate_index);
  BassRealizationStyle style = getRealizationStyle(role);

  switch (style) {
    case BassRealizationStyle::Simple:
      return realizeSimple(scheme, key, profile);

    case BassRealizationStyle::Walking:
      return realizeWalking(scheme, key, profile, seed);

    case BassRealizationStyle::Syncopated:
      return realizeSyncopated(scheme, key, profile, seed);

    case BassRealizationStyle::Lyrical:
      return realizeLyrical(scheme, key, profile, seed);

    case BassRealizationStyle::Elaborate:
      return realizeElaborate(scheme, key, profile, seed);
  }

  return {};  // Unreachable but satisfies compiler.
}

}  // namespace bach
