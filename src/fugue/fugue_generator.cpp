// Implementation of the fugue generator: full pipeline from subject
// generation through stretto to MIDI track output.

#include "fugue/fugue_generator.h"

#include <algorithm>
#include <cstdint>

#include "core/gm_program.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "fugue/answer.h"
#include "fugue/countersubject.h"
#include "fugue/episode.h"
#include "fugue/exposition.h"
#include "fugue/middle_entry.h"
#include "fugue/stretto.h"
#include "fugue/subject.h"
#include "fugue/subject_validator.h"
#include "fugue/tonal_plan.h"
#include "organ/manual.h"
#include "organ/registration.h"

namespace bach {

namespace {

/// @brief Minimum number of voices for a fugue.
constexpr uint8_t kMinVoices = 2;

/// @brief Maximum number of voices for a fugue.
constexpr uint8_t kMaxVoices = 5;

/// @brief Minimum total fugue length in bars.
constexpr Tick kMinFugueBars = 12;

/// @brief Duration of episodes in bars.
constexpr Tick kEpisodeBars = 2;

/// @brief Duration of coda in bars.
constexpr Tick kCodaBars = 2;

/// @brief Organ velocity (pipe organs have no velocity sensitivity).
constexpr uint8_t kOrganVelocity = 80;

/// @brief Clamp voice count to valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampVoiceCount(uint8_t num_voices) {
  if (num_voices < kMinVoices) return kMinVoices;
  if (num_voices > kMaxVoices) return kMaxVoices;
  return num_voices;
}

/// @brief Human-readable name for an organ manual.
/// @param manual OrganManual enum value.
/// @return Descriptive string for the manual.
const char* manualTrackName(OrganManual manual) {
  switch (manual) {
    case OrganManual::Great:   return "Manual I (Great)";
    case OrganManual::Swell:   return "Manual II (Swell)";
    case OrganManual::Positiv: return "Manual III (Positiv)";
    case OrganManual::Pedal:   return "Pedal";
  }
  return "Unknown Manual";
}

/// @brief Create MIDI tracks for an organ fugue using assignManuals().
///
/// Delegates to the organ manual assignment system (organ/manual.h) which
/// handles voice-to-manual routing for all voice counts (2-5):
///   2 voices: Great + Swell
///   3 voices: Great + Swell + Positiv
///   4 voices: Great + Swell + Positiv + Pedal
///   5 voices: Great (x2) + Swell + Positiv + Pedal
///
/// @param num_voices Number of voices (2-5).
/// @return Vector of Track objects with channel/program/name configured.
std::vector<Track> createOrganTracks(uint8_t num_voices) {
  auto assignments = assignManuals(num_voices, FormType::Fugue);

  std::vector<Track> tracks;
  tracks.reserve(assignments.size());

  for (const auto& assignment : assignments) {
    Track track;
    track.channel = channelForAssignment(assignment);
    track.program = programForAssignment(assignment);
    track.name = manualTrackName(assignment.manual);
    tracks.push_back(track);
  }

  return tracks;
}

/// @brief Distribute notes into tracks by voice_id.
///
/// Each note's voice field determines which track it belongs to.
/// Notes with voice_id >= tracks.size() are silently discarded.
///
/// @param notes All collected notes from all fugue sections.
/// @param tracks Output tracks (notes appended to matching track).
void assignNotesToTracks(const std::vector<NoteEvent>& notes,
                         std::vector<Track>& tracks) {
  for (const auto& note : notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }
}

/// @brief Sort notes in each track by start_tick for MIDI output.
/// @param tracks Tracks whose notes will be sorted in place.
void sortTrackNotes(std::vector<Track>& tracks) {
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

/// @brief Generate a valid subject with retry logic.
///
/// Attempts to generate a subject that passes the SubjectValidator's
/// quality threshold (composite >= 0.7). Each attempt uses a different
/// seed derived from the base seed. If all attempts fail, returns the
/// last generated subject as a best-effort fallback.
///
/// @param config Fugue configuration.
/// @param attempts_out Output: number of attempts used.
/// @return Generated Subject (may not be acceptable if all retries failed).
Subject generateValidSubject(const FugueConfig& config, int& attempts_out) {
  SubjectGenerator gen;
  SubjectValidator validator;

  Subject best_subject;
  float best_composite = -1.0f;

  for (int attempt = 0; attempt < config.max_subject_retries; ++attempt) {
    // Use a large prime multiplier to spread seeds apart, avoiding correlation.
    uint32_t attempt_seed = config.seed + static_cast<uint32_t>(attempt) * 1000003u;
    Subject subject = gen.generate(config, attempt_seed);
    SubjectScore score = validator.evaluate(subject);
    float composite = score.composite();

    attempts_out = attempt + 1;

    if (score.isAcceptable()) {
      return subject;
    }

    // Track best attempt for fallback.
    if (composite > best_composite) {
      best_composite = composite;
      best_subject = subject;
    }
  }

  // Fallback: return the best attempt even if below threshold.
  return best_subject;
}

/// @brief Duration of the dominant pedal in bars (placed before stretto).
constexpr Tick kDominantPedalBars = 4;

/// @brief Generate pedal point notes for a given pitch and duration.
///
/// The pedal is split into bar-length tied notes for better MIDI compatibility
/// (many MIDI renderers handle shorter notes more reliably than very long ones).
///
/// @param pitch MIDI pitch of the pedal note.
/// @param start_tick Start position in ticks.
/// @param duration Total duration of the pedal in ticks.
/// @param voice_id Voice for the pedal (lowest voice).
/// @return Vector of pedal point NoteEvents, each one bar long (or shorter
///         for the final segment).
std::vector<NoteEvent> generatePedalPoint(uint8_t pitch, Tick start_tick,
                                          Tick duration, VoiceId voice_id) {
  std::vector<NoteEvent> notes;
  Tick remaining = duration;
  Tick tick = start_tick;
  while (remaining > 0) {
    Tick note_dur = std::min(remaining, static_cast<Tick>(kTicksPerBar));
    NoteEvent evt;
    evt.pitch = pitch;
    evt.start_tick = tick;
    evt.duration = note_dur;
    evt.velocity = kOrganVelocity;
    evt.voice = voice_id;
    evt.source = BachNoteSource::PedalPoint;
    notes.push_back(evt);
    tick += note_dur;
    remaining -= note_dur;
  }
  return notes;
}

/// @brief Remove notes from the lowest voice that overlap with a pedal region.
///
/// Any existing note in the specified voice whose start_tick falls within
/// [region_start, region_end) is removed, making room for the pedal point.
///
/// @param all_notes The collection of all notes (modified in place).
/// @param voice_id Voice whose notes should be removed.
/// @param region_start Start tick of the pedal region (inclusive).
/// @param region_end End tick of the pedal region (exclusive).
void removeLowestVoiceNotes(std::vector<NoteEvent>& all_notes,
                            VoiceId voice_id,
                            Tick region_start, Tick region_end) {
  all_notes.erase(
      std::remove_if(all_notes.begin(), all_notes.end(),
                     [voice_id, region_start, region_end](const NoteEvent& evt) {
                       return evt.voice == voice_id &&
                              evt.start_tick >= region_start &&
                              evt.start_tick < region_end;
                     }),
      all_notes.end());
}

/// @brief Compute the tonic pitch in bass register for a given key.
///
/// Returns the tonic in octave 2 (e.g., C2 = MIDI 36 for Key::C).
/// This is the standard register for organ pedal notes.
///
/// @param key Musical key.
/// @return MIDI pitch number for the tonic in octave 2.
uint8_t tonicBassPitch(Key key) {
  // Octave 2 starts at MIDI 36 (C2). Add the key's pitch class offset.
  int pitch = 36 + static_cast<int>(key);
  return clampPitch(pitch, organ_range::kPedalLow, organ_range::kPedalHigh);
}

/// @brief Create coda notes: sustained tonic chord across all voices.
///
/// Each voice receives a whole note forming a tonic chord:
///   Voice 0: tonic (root)
///   Voice 1: perfect 5th above
///   Voice 2: major 3rd above
///   Voice 3: octave below (bass register)
///   Voice 4: root doubled at octave above
///
/// @param start_tick When the coda begins.
/// @param duration Coda duration in ticks.
/// @param key Musical key for the tonic chord.
/// @param num_voices Number of voices.
/// @return Vector of coda notes.
std::vector<NoteEvent> createCodaNotes(Tick start_tick, Tick duration,
                                       Key key, uint8_t num_voices) {
  std::vector<NoteEvent> notes;
  notes.reserve(num_voices);

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(key);

  // Pitch offsets for each voice to form a tonic chord.
  static constexpr int kChordOffsets[] = {
      0,    // Root
      7,    // Perfect 5th
      4,    // Major 3rd
      -12,  // Octave below (pedal register)
      12    // Octave above
  };

  for (uint8_t voice_idx = 0; voice_idx < num_voices && voice_idx < 5; ++voice_idx) {
    NoteEvent note;
    note.start_tick = start_tick;
    note.duration = duration;

    int pitch = tonic_pitch + kChordOffsets[voice_idx];
    // Clamp to valid organ range [24, 96].
    note.pitch = clampPitch(pitch, organ_range::kPedalLow, organ_range::kManual1High);
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    notes.push_back(note);
  }

  return notes;
}

}  // namespace

FugueResult generateFugue(const FugueConfig& config) {
  FugueResult result;
  result.success = false;

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // =========================================================================
  // Step 1: Generate valid subject
  // =========================================================================
  int subject_attempts = 0;
  Subject subject = generateValidSubject(config, subject_attempts);
  result.attempts = subject_attempts;

  if (subject.notes.empty()) {
    result.error_message = "Failed to generate a valid subject";
    return result;
  }

  // =========================================================================
  // Step 2: Generate answer (Real/Tonal auto-detection)
  // =========================================================================
  Answer answer = generateAnswer(subject, config.answer_type);

  // =========================================================================
  // Step 3: Generate countersubject(s)
  // =========================================================================
  Countersubject counter_subject =
      generateCountersubject(subject, config.seed + 1000);

  // For 4+ voices, generate a second countersubject that contrasts with both
  // the subject and the first countersubject.
  Countersubject counter_subject_2;
  if (num_voices >= 4) {
    counter_subject_2 = generateSecondCountersubject(
        subject, counter_subject, config.seed + 5000);
  }

  // =========================================================================
  // Step 4: Build exposition (FuguePhase::Establish)
  // =========================================================================
  Exposition expo = buildExposition(subject, answer, counter_subject,
                                    config, config.seed);

  // =========================================================================
  // Step 5: Generate tonal plan
  // =========================================================================
  // Estimate total duration: exposition + 2 episodes + middle entry + stretto + coda.
  Tick estimated_duration = expo.total_ticks * 3;
  Tick min_duration = kTicksPerBar * kMinFugueBars;
  if (estimated_duration < min_duration) {
    estimated_duration = min_duration;
  }

  TonalPlan tonal_plan = generateTonalPlan(config, /*is_minor=*/false,
                                           estimated_duration);

  // =========================================================================
  // Collect all notes and build structure
  // =========================================================================
  FugueStructure& structure = result.structure;
  std::vector<NoteEvent> all_notes;
  Tick current_tick = 0;

  // --- Section 1: Exposition (Establish) ---
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, expo.total_ticks, config.key);
  auto expo_notes = expo.allNotes();
  all_notes.insert(all_notes.end(), expo_notes.begin(), expo_notes.end());
  current_tick = expo.total_ticks;

  // --- Develop phase: Episode+MiddleEntry pairs ---
  Tick episode_duration = kTicksPerBar * static_cast<Tick>(config.episode_bars);

  // Build key cycle from near-related keys.
  std::vector<Key> near_keys = getNearRelatedKeys(config.key, /*is_minor=*/false);
  if (near_keys.empty()) {
    near_keys.push_back(getDominantKey(config.key));
  }

  int develop_pairs = config.develop_pairs;
  Key prev_key = config.key;

  for (int pair_idx = 0; pair_idx < develop_pairs; ++pair_idx) {
    Key target_key = near_keys[pair_idx % near_keys.size()];
    uint32_t pair_seed_base = config.seed + static_cast<uint32_t>(pair_idx) * 2000u + 2000u;

    // Compute energy level for this episode from the energy curve.
    float episode_energy = FugueEnergyCurve::getLevel(current_tick, estimated_duration);

    // Episode: transition from prev_key to target_key.
    Episode episode = generateEpisode(subject, current_tick, episode_duration,
                                      prev_key, target_key,
                                      num_voices, pair_seed_base,
                                      pair_idx, episode_energy);
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         current_tick, current_tick + episode_duration, target_key);
    all_notes.insert(all_notes.end(), episode.notes.begin(), episode.notes.end());
    current_tick += episode_duration;

    // MiddleEntry: voice rotates by pair index.
    uint8_t entry_voice = static_cast<uint8_t>(pair_idx % num_voices);
    MiddleEntry middle_entry = generateMiddleEntry(subject, target_key,
                                                   current_tick, entry_voice);
    Tick middle_end = middle_entry.end_tick;
    if (middle_end <= current_tick) {
      middle_end = current_tick + subject.length_ticks;
      if (middle_end <= current_tick) {
        middle_end = current_tick + kTicksPerBar * 2;
      }
    }
    structure.addSection(SectionType::MiddleEntry, FuguePhase::Develop,
                         current_tick, middle_end, target_key);
    all_notes.insert(all_notes.end(), middle_entry.notes.begin(),
                     middle_entry.notes.end());
    current_tick = middle_end;
    prev_key = target_key;
  }

  // --- Return episode: transition back to home key ---
  {
    float return_energy = FugueEnergyCurve::getLevel(current_tick, estimated_duration);
    Episode return_episode = generateEpisode(
        subject, current_tick, episode_duration,
        prev_key, config.key, num_voices,
        config.seed + static_cast<uint32_t>(develop_pairs) * 2000u + 2000u,
        develop_pairs, return_energy);
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         current_tick, current_tick + episode_duration, config.key);
    all_notes.insert(all_notes.end(), return_episode.notes.begin(),
                     return_episode.notes.end());
    current_tick += episode_duration;
  }

  // --- Dominant pedal: 4 bars before stretto (Develop -> Resolve transition) ---
  VoiceId lowest_voice = num_voices - 1;
  {
    Tick pedal_duration = kTicksPerBar * kDominantPedalBars;
    uint8_t dominant_pitch =
        static_cast<uint8_t>(tonicBassPitch(config.key) + interval::kPerfect5th);
    // Clamp to pedal range.
    dominant_pitch = clampPitch(static_cast<int>(dominant_pitch),
                                organ_range::kPedalLow, organ_range::kPedalHigh);

    // Remove existing lowest-voice notes in the pedal region.
    removeLowestVoiceNotes(all_notes, lowest_voice,
                           current_tick, current_tick + pedal_duration);

    auto dominant_pedal = generatePedalPoint(dominant_pitch, current_tick,
                                             pedal_duration, lowest_voice);
    all_notes.insert(all_notes.end(), dominant_pedal.begin(), dominant_pedal.end());
    current_tick += pedal_duration;
  }

  // --- Stretto (Resolve) ---
  Stretto stretto = generateStretto(subject, config.key, current_tick,
                                    num_voices, config.seed + 4000,
                                    config.character);
  Tick stretto_end = stretto.end_tick;
  // Ensure stretto has non-zero duration.
  if (stretto_end <= current_tick) {
    stretto_end = current_tick + kTicksPerBar * 2;
  }
  structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                       current_tick, stretto_end, config.key);
  auto stretto_notes = stretto.allNotes();
  all_notes.insert(all_notes.end(), stretto_notes.begin(), stretto_notes.end());
  current_tick = stretto_end;

  // --- Section 6: Coda (Resolve) with tonic pedal ---
  Tick coda_duration = kTicksPerBar * kCodaBars;
  structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                       current_tick, current_tick + coda_duration, config.key);
  auto coda_notes = createCodaNotes(current_tick, coda_duration,
                                    config.key, num_voices);
  all_notes.insert(all_notes.end(), coda_notes.begin(), coda_notes.end());

  // Tonic pedal in coda: lowest voice sustains the tonic.
  {
    uint8_t tonic_pitch = tonicBassPitch(config.key);

    // Remove the lowest voice's coda chord note -- replaced by pedal point.
    removeLowestVoiceNotes(all_notes, lowest_voice,
                           current_tick, current_tick + coda_duration);

    auto tonic_pedal = generatePedalPoint(tonic_pitch, current_tick,
                                          coda_duration, lowest_voice);
    all_notes.insert(all_notes.end(), tonic_pedal.begin(), tonic_pedal.end());
  }

  // =========================================================================
  // Create tracks and assign notes
  // =========================================================================
  result.tracks = createOrganTracks(num_voices);
  assignNotesToTracks(all_notes, result.tracks);
  sortTrackNotes(result.tracks);

  // Build timeline from the tonal plan (bar-resolution I-chord approximation).
  Tick total_ticks = current_tick + kTicksPerBar * kCodaBars;
  result.timeline = tonal_plan.toHarmonicTimeline(total_ticks);

  // --- Apply energy-based dynamic registration ---
  // Sample the energy curve at section boundaries for CC#7 volume automation.
  {
    std::vector<std::pair<Tick, float>> energy_samples;
    const auto& sections = structure.sections;
    energy_samples.reserve(sections.size());
    for (const auto& section : sections) {
      float energy = FugueEnergyCurve::getLevel(section.start_tick, total_ticks);
      energy_samples.push_back({section.start_tick, energy});
    }
    auto energy_events = generateEnergyRegistrationEvents(
        energy_samples, static_cast<uint8_t>(result.tracks.size()));
    // Insert energy events into matching tracks by channel.
    for (const auto& evt : energy_events) {
      uint8_t event_channel = evt.status & 0x0F;
      for (auto& track : result.tracks) {
        if (track.channel == event_channel) {
          track.events.push_back(evt);
          break;
        }
      }
    }
  }

  result.success = true;
  return result;
}

}  // namespace bach
