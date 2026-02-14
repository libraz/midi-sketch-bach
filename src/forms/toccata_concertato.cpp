// Concertato archetype toccata generator (BWV 564 style).
// Arch energy. Allegro->Adagio->Vivace.

#include "forms/toccata.h"
#include "forms/toccata_internal.h"

#include <cmath>

#include "core/melodic_state.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/parallel_repair.h"
#include "counterpoint/vertical_safe.h"
#include "ornament/ornament_engine.h"

namespace bach {

using namespace toccata_internal;

namespace {

// ---------------------------------------------------------------------------
// Concertato harmonic plan
// ---------------------------------------------------------------------------

HarmonicTimeline buildConcertatoHarmonicPlan(
    const KeySignature& key_sig,
    const std::vector<ToccataSectionBoundary>& sections) {
  HarmonicTimeline timeline;
  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  bool minor = key_sig.is_minor;

  auto addChord = [&](Tick tick, Tick end_tick, ChordDegree degree,
                      ChordQuality quality, int root_offset,
                      float weight = 1.0f) {
    HarmonicEvent e;
    e.tick = tick;
    e.end_tick = end_tick;
    e.key = key_sig.tonic;
    e.is_minor = minor;
    e.chord.degree = degree;
    e.chord.quality = quality;
    e.chord.root_pitch = static_cast<uint8_t>((tpc + root_offset) % 12);
    e.bass_pitch = clampPitch(36 + (tpc + root_offset) % 12,
                              organ_range::kPedalLow, organ_range::kPedalHigh);
    e.weight = weight;
    timeline.addEvent(e);
  };

  // Allegro: I -> V -> I -> IV -> V/V -> V -> I
  if (sections.size() >= 1) {
    Tick s = sections[0].start;
    Tick dur = sections[0].end - s;
    Tick seg = dur / 7;
    if (seg == 0) seg = dur;

    ChordQuality tonic_q = minor ? ChordQuality::Minor : ChordQuality::Major;
    addChord(s, s + seg, ChordDegree::I, tonic_q, 0);
    addChord(s + seg, s + seg * 2, ChordDegree::V, ChordQuality::Major, 7);
    addChord(s + seg * 2, s + seg * 3, ChordDegree::I, tonic_q, 0);
    addChord(s + seg * 3, s + seg * 4,
             ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(s + seg * 4, s + seg * 5, ChordDegree::V_of_V, ChordQuality::Major, 2);
    addChord(s + seg * 5, s + seg * 6, ChordDegree::V, ChordQuality::Major, 7);
    addChord(s + seg * 6, sections[0].end, ChordDegree::I, tonic_q, 0);
  }

  // Adagio: vi -> IV -> ii -> V/vi -> vi -> IV -> V -> I
  if (sections.size() >= 2) {
    Tick s = sections[1].start;
    Tick dur = sections[1].end - s;
    Tick seg = dur / 8;
    if (seg == 0) seg = dur;

    addChord(s, s + seg,
             ChordDegree::vi, minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 8 : 9);
    addChord(s + seg, s + seg * 2,
             ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(s + seg * 2, s + seg * 3,
             ChordDegree::ii, ChordQuality::Minor, 2);
    addChord(s + seg * 3, s + seg * 4,
             ChordDegree::V, ChordQuality::Major, minor ? 8 : 9);  // V/vi
    addChord(s + seg * 4, s + seg * 5,
             ChordDegree::vi, minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 8 : 9);
    addChord(s + seg * 5, s + seg * 6,
             ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(s + seg * 6, s + seg * 7,
             ChordDegree::V, ChordQuality::Major, 7);
    addChord(s + seg * 7, sections[1].end,
             ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0);
  }

  // Vivace: I -> V/IV -> IV -> V -> V/V -> V7 -> I
  if (sections.size() >= 3) {
    Tick s = sections[2].start;
    Tick dur = sections[2].end - s;
    Tick seg = dur / 7;
    if (seg == 0) seg = dur;

    ChordQuality tonic_q = minor ? ChordQuality::Minor : ChordQuality::Major;
    addChord(s, s + seg, ChordDegree::I, tonic_q, 0);
    addChord(s + seg, s + seg * 2, ChordDegree::V_of_IV, ChordQuality::Major, 0);
    addChord(s + seg * 2, s + seg * 3,
             ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(s + seg * 3, s + seg * 4, ChordDegree::V, ChordQuality::Major, 7);
    addChord(s + seg * 4, s + seg * 5, ChordDegree::V_of_V, ChordQuality::Major, 2);
    addChord(s + seg * 5, s + seg * 6, ChordDegree::V, ChordQuality::Dominant7, 7);
    addChord(s + seg * 6, sections[2].end, ChordDegree::I, ChordQuality::Major, 0, 1.5f);
  }

  return timeline;
}

// ---------------------------------------------------------------------------
// Cantabile melody generator
// ---------------------------------------------------------------------------

/// @brief Generate a cantabile melody with long note values and stepwise motion.
/// Strong beats use chord tones or suspensions only.
std::vector<NoteEvent> generateCantabileMelody(
    const HarmonicTimeline& timeline,
    const KeySignature& key_sig,
    uint8_t voice, Tick start_tick, Tick end_tick,
    std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  uint8_t low = getToccataLowPitch(voice);
  uint8_t high = getToccataHighPitch(voice);
  auto scale = getScaleTones(key_sig.tonic, key_sig.is_minor, low, high);
  if (scale.empty()) return notes;

  size_t idx = scale.size() / 2;
  // Cantabile melody should stay in a narrow range (~1.5 octaves).
  constexpr int kCantabileRange = 10;

  Tick tick = start_tick;
  while (tick < end_tick) {
    // Duration: long notes (dotted quarter, half, dotted half).
    float roll = rng::rollFloat(rng, 0.0f, 1.0f);
    Tick dur;
    if (roll < 0.25f) dur = kDottedQuarter;
    else if (roll < 0.60f) dur = kHalfNote;
    else if (roll < 0.85f) dur = kDottedQuarter + kQuarterNote;  // dotted half
    else dur = kWholeNote;

    if (tick + dur > end_tick) dur = end_tick - tick;
    if (dur == 0) break;

    // Strong beat constraint: use chord tone within ±octave.
    uint8_t beat = beatInBar(tick);
    if (beat == 0 || beat == 2) {
      uint8_t current = scale[idx];
      uint8_t search_low = (current > low + kCantabileRange)
                               ? current - kCantabileRange : low;
      uint8_t search_high = (current + kCantabileRange < high)
                                ? current + kCantabileRange : high;

      const HarmonicEvent& ev = timeline.getAt(tick);
      auto chord_tones = collectChordTonesInRange(ev.chord, search_low, search_high);
      if (!chord_tones.empty()) {
        uint8_t best = chord_tones[0];
        int best_dist = absoluteInterval(current, best);
        for (auto ct : chord_tones) {
          int d = absoluteInterval(current, ct);
          if (d < best_dist) {
            best = ct;
            best_dist = d;
          }
        }
        for (size_t i = 0; i < scale.size(); ++i) {
          if (scale[i] >= best) { idx = i; break; }
        }
      }
    } else {
      // Stepwise motion.
      int step = rng::rollProbability(rng, 0.10f) ? 2 : 1;
      bool ascending = rng::rollProbability(rng, 0.5f);
      if (ascending) {
        if (idx + step < scale.size()) idx += step;
        else if (idx >= static_cast<size_t>(step)) idx -= step;
      } else {
        if (idx >= static_cast<size_t>(step)) idx -= step;
        else if (idx + step < scale.size()) idx += step;
      }
    }

    // Keep within cantabile range.
    uint8_t center = scale[scale.size() / 2];
    if (scale[idx] > center + kCantabileRange && idx > 0) --idx;
    else if (scale[idx] < center - kCantabileRange && idx + 1 < scale.size()) ++idx;

    notes.push_back(makeNote(tick, dur, scale[idx], voice));
    tick += dur;
  }

  return notes;
}

/// @brief Generate moto perpetuo (reuse from perpetuus). Forward declaration.
std::vector<NoteEvent> generateMotoPerpetuo_Concertato(
    const HarmonicTimeline& timeline,
    const KeySignature& key_sig,
    uint8_t voice, Tick start_tick, Tick end_tick,
    std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low = getToccataLowPitch(voice);
  uint8_t high = getToccataHighPitch(voice);
  auto scale = getScaleTones(key_sig.tonic, key_sig.is_minor, low, high);
  if (scale.empty()) return notes;

  size_t idx = scale.size() / 2;
  bool ascending = true;
  constexpr int kMaxRangeFromCenter = 14;
  MelodicState mel_state;
  uint8_t prev_moto_pitch = scale[idx];

  Tick tick = start_tick;
  while (tick < end_tick) {
    Tick dur = std::min(kSixteenthNote, end_tick - tick);
    if (dur == 0) break;

    uint8_t beat = beatInBar(tick);
    bool strong_beat = (beat == 0 || beat == 2);

    if (strong_beat) {
      uint8_t current = scale[idx];
      uint8_t search_low = (current > low + kMaxRangeFromCenter)
                               ? current - kMaxRangeFromCenter : low;
      uint8_t search_high = (current + kMaxRangeFromCenter < high)
                                ? current + kMaxRangeFromCenter : high;

      const HarmonicEvent& ev = timeline.getAt(tick);
      auto chord_tones = collectChordTonesInRange(ev.chord, search_low, search_high);
      if (!chord_tones.empty()) {
        uint8_t best = chord_tones[0];
        int best_dist = absoluteInterval(current, best);
        for (auto ct : chord_tones) {
          int d = absoluteInterval(current, ct);
          if (d < best_dist) { best = ct; best_dist = d; }
        }
        for (size_t i = 0; i < scale.size(); ++i) {
          if (scale[i] >= best) { idx = i; break; }
        }
      }
    } else {
      int step = rng::rollProbability(rng, 0.15f) ? 2 : 1;
      if (ascending) {
        if (idx + step < scale.size()) idx += step;
        else { ascending = false; if (idx >= static_cast<size_t>(step)) idx -= step; }
      } else {
        if (idx >= static_cast<size_t>(step)) idx -= step;
        else { ascending = true; if (idx + step < scale.size()) idx += step; }
      }
    }

    // Soft range boundary.
    uint8_t center = scale[scale.size() / 2];
    if (scale[idx] > center + kMaxRangeFromCenter) ascending = false;
    else if (scale[idx] < center - kMaxRangeFromCenter) ascending = true;

    notes.push_back(makeNote(tick, dur, scale[idx], voice));
    updateMelodicState(mel_state, prev_moto_pitch, scale[idx]);
    prev_moto_pitch = scale[idx];
    tick += dur;

    // Direction via MelodicState persistence model.
    ascending = (chooseMelodicDirection(mel_state, rng) > 0);
  }

  return notes;
}

/// @brief Generate sustained bass line for Adagio section.
std::vector<NoteEvent> generateSustainedBass(
    const HarmonicTimeline& timeline,
    const KeySignature& /* key_sig */,
    uint8_t voice, Tick start_tick, Tick end_tick,
    std::mt19937& /* rng */) {
  std::vector<NoteEvent> notes;
  uint8_t low = getToccataLowPitch(voice);
  uint8_t high = getToccataHighPitch(voice);

  Tick tick = start_tick;
  while (tick < end_tick) {
    const HarmonicEvent& ev = timeline.getAt(tick);
    uint8_t bass = clampPitch(static_cast<int>(ev.bass_pitch), low, high);

    Tick dur = kWholeNote;
    if (tick + dur > end_tick) dur = end_tick - tick;
    if (dur == 0) break;

    notes.push_back(makeNote(tick, dur, bass, voice, BachNoteSource::PedalPoint));
    tick += dur;
  }

  return notes;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public: Concertato archetype
// ---------------------------------------------------------------------------

ToccataResult generateConcertatoToccata(const ToccataConfig& config) {
  ToccataResult result;
  result.success = false;

  uint8_t num_voices = clampToccataVoiceCount(config.num_voices);

  if (config.total_bars <= 0) {
    result.error_message = "total_bars must be positive";
    return result;
  }

  std::mt19937 rng(config.seed);

  // Section allocation: Allegro 35%, Adagio 30%, Vivace 35%.
  auto bars = allocateBars(config.total_bars, {0.35f, 0.30f, 0.35f});
  auto sections = buildSectionBoundaries(
      bars, {ToccataSectionId::Allegro, ToccataSectionId::Adagio,
             ToccataSectionId::Vivace});
  Tick total_duration = static_cast<Tick>(config.total_bars) * kTicksPerBar;

  HarmonicTimeline timeline = buildConcertatoHarmonicPlan(config.key, sections);

  std::vector<NoteEvent> all_notes;

  // --- Allegro: Voice 0 moto perpetuo, Voice 1 chord support ---
  auto allegro_v0 = generateMotoPerpetuo_Concertato(
      timeline, config.key, 0, sections[0].start, sections[0].end, rng);
  all_notes.insert(all_notes.end(), allegro_v0.begin(), allegro_v0.end());

  // Voice 1: eighth-note arpeggios in allegro.
  {
    uint8_t low1 = getToccataLowPitch(1);
    uint8_t high1 = getToccataHighPitch(1);
    auto scale1 = getScaleTones(config.key.tonic, config.key.is_minor, low1, high1);
    size_t idx1 = scale1.size() / 2;
    bool asc1 = true;

    Tick tick = sections[0].start;
    while (tick < sections[0].end) {
      Tick dur = std::min(kEighthNote, sections[0].end - tick);
      if (dur == 0) break;

      if (!scale1.empty()) {
        all_notes.push_back(makeNote(tick, dur, scale1[idx1], 1));
        int step = rng::rollProbability(rng, 0.20f) ? 2 : 1;
        if (asc1) {
          if (idx1 + step < scale1.size()) idx1 += step;
          else { asc1 = false; if (idx1 >= static_cast<size_t>(step)) idx1 -= step; }
        } else {
          if (idx1 >= static_cast<size_t>(step)) idx1 -= step;
          else { asc1 = true; if (idx1 + step < scale1.size()) idx1 += step; }
        }
      }
      tick += dur;
    }
  }

  // --- Adagio: Voice 1 cantabile melody, Voice 0 sustained chords ---
  auto adagio_melody = generateCantabileMelody(
      timeline, config.key, 1, sections[1].start, sections[1].end, rng);
  all_notes.insert(all_notes.end(), adagio_melody.begin(), adagio_melody.end());

  // Voice 0: sustained chord tones during adagio.
  {
    uint8_t low0 = getToccataLowPitch(0);
    uint8_t high0 = getToccataHighPitch(0);
    Tick tick = sections[1].start;
    while (tick < sections[1].end) {
      const HarmonicEvent& ev = timeline.getAt(tick);
      auto chord_tones = collectChordTonesInRange(ev.chord, low0, high0);
      Tick dur = kWholeNote;
      if (tick + dur > sections[1].end) dur = sections[1].end - tick;
      if (dur == 0) break;
      if (!chord_tones.empty()) {
        all_notes.push_back(makeNote(tick, dur,
                                     rng::selectRandom(rng, chord_tones), 0));
      }
      tick += dur;
    }
  }

  // --- Vivace: Voice 0 moto perpetuo, Voice 1 eighth-note counterpoint ---
  auto vivace_v0 = generateMotoPerpetuo_Concertato(
      timeline, config.key, 0, sections[2].start, sections[2].end, rng);
  all_notes.insert(all_notes.end(), vivace_v0.begin(), vivace_v0.end());

  auto vivace_v1 = generateMotoPerpetuo_Concertato(
      timeline, config.key, 1, sections[2].start, sections[2].end, rng);
  all_notes.insert(all_notes.end(), vivace_v1.begin(), vivace_v1.end());

  // --- Pedal ---
  if (num_voices >= 3) {
    // Allegro: quarter-note bass.
    {
      uint8_t low = getToccataLowPitch(2);
      uint8_t high = getToccataHighPitch(2);
      Tick tick = sections[0].start;
      while (tick < sections[0].end) {
        const HarmonicEvent& ev = timeline.getAt(tick);
        uint8_t bass = clampPitch(static_cast<int>(ev.bass_pitch), low, high);
        Tick dur = std::min(kQuarterNote, sections[0].end - tick);
        if (dur == 0) break;
        all_notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
        tick += dur;
      }
    }
    // Adagio: sustained bass.
    auto adagio_bass = generateSustainedBass(
        timeline, config.key, 2, sections[1].start, sections[1].end, rng);
    all_notes.insert(all_notes.end(), adagio_bass.begin(), adagio_bass.end());

    // Vivace: eighth-note bass.
    {
      uint8_t low = getToccataLowPitch(2);
      uint8_t high = getToccataHighPitch(2);
      Tick tick = sections[2].start;
      while (tick < sections[2].end) {
        const HarmonicEvent& ev = timeline.getAt(tick);
        uint8_t bass = clampPitch(static_cast<int>(ev.bass_pitch), low, high);
        Tick dur = std::min(kEighthNote, sections[2].end - tick);
        if (dur == 0) break;
        all_notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
        tick += dur;
      }
    }
  }

  // --- Additional voices ---
  for (uint8_t vi = 3; vi < num_voices && vi < 5; ++vi) {
    uint8_t lo = getToccataLowPitch(vi);
    uint8_t hi = getToccataHighPitch(vi);
    Tick tick = sections[2].start;  // Extra voices join in Vivace.
    while (tick < sections[2].end) {
      const HarmonicEvent& ev = timeline.getAt(tick);
      auto chord_tones = collectChordTonesInRange(ev.chord, lo, hi);
      Tick dur = std::min(kQuarterNote, sections[2].end - tick);
      if (dur == 0) break;
      if (!chord_tones.empty()) {
        all_notes.push_back(makeNote(tick, dur,
                                     rng::selectRandom(rng, chord_tones), vi));
      }
      tick += dur;
    }
  }

  // Apply ornaments.
  {
    OrnamentContext ctx;
    ctx.config.ornament_density = 0.10f;
    ctx.role = VoiceRole::Propel;
    ctx.seed = config.seed;
    ctx.timeline = &timeline;
    ctx.cadence_ticks = {sections.back().end - kTicksPerBar};
    all_notes = applyOrnaments(all_notes, ctx);
  }

  // Post-validate: counterpoint repair (parallel perfects, range clamping).
  {
    std::vector<std::pair<uint8_t, uint8_t>> voice_ranges;
    for (uint8_t v = 0; v < num_voices; ++v) {
      voice_ranges.emplace_back(getToccataLowPitch(v), getToccataHighPitch(v));
    }
    for (auto& n : all_notes) {
      if (n.source == BachNoteSource::Unknown) {
        n.source = isPedalVoice(n.voice, num_voices) ? BachNoteSource::PedalPoint
                                  : BachNoteSource::FreeCounterpoint;
      }
    }
    all_notes = coordinateVoices(
        std::move(all_notes), num_voices, config.key.tonic, &timeline);

    all_notes = postValidateNotes(
        std::move(all_notes), num_voices, config.key, voice_ranges);

    // Leap resolution: fix unresolved melodic leaps.
    {
      LeapResolutionParams lr_params;
      lr_params.num_voices = num_voices;
      lr_params.key_at_tick = [&](Tick) { return config.key.tonic; };
      lr_params.scale_at_tick = [&](Tick t) {
        const auto& ev = timeline.getAt(t);
        return ev.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      };
      lr_params.voice_range_static = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        if (v < voice_ranges.size()) return voice_ranges[v];
        return {0, 127};
      };
      lr_params.is_chord_tone = [&](Tick t, uint8_t p) {
        return isChordTone(p, timeline.getAt(t));
      };
      lr_params.vertical_safe =
          makeVerticalSafeCallback(timeline, all_notes, num_voices);
      resolveLeaps(all_notes, lr_params);

      // Second parallel-perfect repair pass after leap resolution.
      {
        ParallelRepairParams pp_params;
        pp_params.num_voices = num_voices;
        pp_params.scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
        pp_params.key_at_tick = lr_params.key_at_tick;
        pp_params.voice_range_static = lr_params.voice_range_static;
        pp_params.max_iterations = 3;
        repairParallelPerfect(all_notes, pp_params);
      }
    }
  }

  // Build tracks.
  std::vector<Track> tracks = createToccataTracks(num_voices);
  assignNotesToTracks(all_notes, tracks);

  // Registration: forte -> piano -> forte -> tutti (inter-movement contrast).
  ExtendedRegistrationPlan reg_plan;
  Registration forte;
  forte.velocity_hint = 85;
  reg_plan.addPoint(sections[0].start, forte, "forte");

  Registration piano;
  piano.velocity_hint = 55;
  reg_plan.addPoint(sections[1].start, piano, "piano");

  Registration mezzo;
  mezzo.velocity_hint = 70;
  Tick adagio_mid = sections[1].start + (sections[1].end - sections[1].start) / 2;
  reg_plan.addPoint(adagio_mid, mezzo, "mezzo");

  Registration forte2;
  forte2.velocity_hint = 85;
  reg_plan.addPoint(sections[2].start, forte2, "forte");

  Registration tutti;
  tutti.velocity_hint = 110;
  Tick vivace_last_2 = sections[2].end > 2 * kTicksPerBar
                           ? sections[2].end - 2 * kTicksPerBar
                           : sections[2].start;
  reg_plan.addPoint(vivace_last_2, tutti, "tutti");

  applyExtendedRegistrationPlan(tracks, reg_plan);

  sortToccataTrackNotes(tracks);
  cleanupToccataOverlaps(tracks);

  if (config.enable_picardy && config.key.is_minor) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               total_duration - kTicksPerBar);
    }
  }

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.archetype = ToccataArchetype::Concertato;
  result.sections = sections;
  populateLegacyFields(result);
  result.success = true;

  return result;
}

}  // namespace bach
