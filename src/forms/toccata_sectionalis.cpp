// Sectionalis archetype toccata generator (BWV 566 style).
// Wave energy. Free->QuasiFugal->Free->Cadenza->Coda.

#include "forms/toccata.h"
#include "forms/toccata_internal.h"

#include <cmath>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "harmony/key.h"
#include "ornament/ornament_engine.h"

namespace bach {

using namespace toccata_internal;

namespace {

// ---------------------------------------------------------------------------
// Sectionalis harmonic plan
// ---------------------------------------------------------------------------

HarmonicTimeline buildSectionalisHarmonicPlan(
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

  auto segmentChords = [&](Tick s, Tick e, int count,
                           const std::vector<std::tuple<ChordDegree, ChordQuality, int>>& chords) {
    Tick seg = (e - s) / static_cast<Tick>(count);
    if (seg == 0) seg = e - s;
    for (int i = 0; i < count && i < static_cast<int>(chords.size()); ++i) {
      Tick cs = s + seg * static_cast<Tick>(i);
      Tick ce = (i + 1 < count) ? s + seg * static_cast<Tick>(i + 1) : e;
      addChord(cs, ce, std::get<0>(chords[i]), std::get<1>(chords[i]),
               std::get<2>(chords[i]));
    }
  };

  ChordQuality tonic_q = minor ? ChordQuality::Minor : ChordQuality::Major;
  ChordQuality sub_q = minor ? ChordQuality::Minor : ChordQuality::Major;

  // Free1: I -> V -> I -> IV -> V
  if (sections.size() >= 1) {
    segmentChords(sections[0].start, sections[0].end, 5, {
        {ChordDegree::I, tonic_q, 0},
        {ChordDegree::V, ChordQuality::Major, 7},
        {ChordDegree::I, tonic_q, 0},
        {ChordDegree::IV, sub_q, 5},
        {ChordDegree::V, ChordQuality::Major, 7},
    });
  }

  // QuasiFugal: I -> V -> vi -> IV -> V -> I
  if (sections.size() >= 2) {
    segmentChords(sections[1].start, sections[1].end, 6, {
        {ChordDegree::I, tonic_q, 0},
        {ChordDegree::V, ChordQuality::Major, 7},
        {ChordDegree::vi, minor ? ChordQuality::Major : ChordQuality::Minor,
         minor ? 8 : 9},
        {ChordDegree::IV, sub_q, 5},
        {ChordDegree::V, ChordQuality::Major, 7},
        {ChordDegree::I, tonic_q, 0},
    });
  }

  // Free2: bVI -> iv -> V/V -> V (chromatic color)
  if (sections.size() >= 3) {
    segmentChords(sections[2].start, sections[2].end, 4, {
        {ChordDegree::vi, ChordQuality::Major, 8},  // bVI
        {ChordDegree::IV, ChordQuality::Minor, 5},   // iv
        {ChordDegree::V_of_V, ChordQuality::Major, 2},
        {ChordDegree::V, ChordQuality::Major, 7},
    });
  }

  // Cadenza: V pedal
  if (sections.size() >= 4) {
    addChord(sections[3].start, sections[3].end,
             ChordDegree::V, ChordQuality::Major, 7, 0.8f);
  }

  // Coda: IV -> V/V -> V7 -> I
  if (sections.size() >= 5) {
    segmentChords(sections[4].start, sections[4].end, 4, {
        {ChordDegree::IV, sub_q, 5},
        {ChordDegree::V_of_V, ChordQuality::Major, 2},
        {ChordDegree::V, ChordQuality::Dominant7, 7},
        {ChordDegree::I, ChordQuality::Major, 0},
    });
  }

  return timeline;
}

// ---------------------------------------------------------------------------
// Quasi-fugal section: 2-bar motif imitated across voices
// ---------------------------------------------------------------------------

/// @brief Generate a quasi-fugal section with a 2-bar head motif imitated
/// in each voice, followed by free counterpoint.
std::vector<NoteEvent> generateQuasiFugalSection(
    const HarmonicTimeline& /* timeline */,
    const KeySignature& key_sig,
    uint8_t num_voices, Tick start_tick, Tick end_tick,
    std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t voices = std::min(num_voices, static_cast<uint8_t>(3));

  // Generate a 2-bar motif using voice 0's range.
  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = getToccataHighPitch(0);
  auto scale0 = getScaleTones(key_sig.tonic, key_sig.is_minor, low0, high0);
  if (scale0.empty()) return notes;

  constexpr Tick kMotifDuration = 2 * kTicksPerBar;
  constexpr int kMotifNotes = 8;  // 8 notes in 2 bars (quarter notes).

  // Create the head motif as scale-index offsets from a starting position.
  std::vector<int> motif_offsets(kMotifNotes);
  motif_offsets[0] = 0;
  for (int i = 1; i < kMotifNotes; ++i) {
    int step = rng::rollRange(rng, -2, 2);
    motif_offsets[i] = motif_offsets[i - 1] + step;
  }

  // Voice entries staggered by 2 bars.
  Tick motif_note_dur = kMotifDuration / kMotifNotes;

  for (uint8_t v = 0; v < voices; ++v) {
    Tick entry_tick = start_tick + static_cast<Tick>(v) * kMotifDuration;
    if (entry_tick >= end_tick) break;

    uint8_t low = getToccataLowPitch(v);
    uint8_t high = getToccataHighPitch(v);
    auto scale = getScaleTones(key_sig.tonic, key_sig.is_minor, low, high);
    if (scale.empty()) continue;

    size_t base_idx = scale.size() / 2;

    // Head motif.
    for (int i = 0; i < kMotifNotes; ++i) {
      Tick tick = entry_tick + static_cast<Tick>(i) * motif_note_dur;
      if (tick >= end_tick) break;
      Tick dur = std::min(motif_note_dur, end_tick - tick);

      int new_idx = static_cast<int>(base_idx) + motif_offsets[i];
      new_idx = std::max(0, std::min(new_idx, static_cast<int>(scale.size()) - 1));

      notes.push_back(makeNote(tick, dur, scale[new_idx], v));
    }

    // Free counterpoint after motif.
    Tick free_start = entry_tick + kMotifDuration;
    if (free_start >= end_tick) continue;

    size_t idx = base_idx;
    bool ascending = rng::rollProbability(rng, 0.5f);
    Tick tick = free_start;
    while (tick < end_tick) {
      Tick dur = rng::rollProbability(rng, 0.5f) ? kEighthNote : kQuarterNote;
      if (tick + dur > end_tick) dur = end_tick - tick;
      if (dur == 0) break;

      int step = rng::rollProbability(rng, 0.15f) ? 2 : 1;
      if (ascending) {
        if (idx + step < scale.size()) idx += step;
        else { ascending = false; if (idx >= static_cast<size_t>(step)) idx -= step; }
      } else {
        if (idx >= static_cast<size_t>(step)) idx -= step;
        else { ascending = true; if (idx + step < scale.size()) idx += step; }
      }
      if (rng::rollProbability(rng, 0.12f)) ascending = !ascending;

      notes.push_back(makeNote(tick, dur, scale[idx], v));
      tick += dur;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Pedal cadenza: solo pedal passage with scale runs and arpeggios
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generatePedalCadenza(
    const HarmonicTimeline& /* timeline */,
    const KeySignature& key_sig,
    Tick start_tick, Tick end_tick,
    std::mt19937& /* rng */) {
  std::vector<NoteEvent> notes;
  constexpr uint8_t kPedalVoice = 2;

  uint8_t low = organ_range::kPedalLow;
  uint8_t high = organ_range::kPedalHigh;
  auto scale = getScaleTones(key_sig.tonic, key_sig.is_minor, low, high);
  if (scale.empty()) return notes;

  Tick dur_unit = kSixteenthNote;  // Minimum note value.

  // Dominant pitch for trill target.
  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  uint8_t dominant_pc = static_cast<uint8_t>(getDominant(key_sig).tonic);

  Tick tick = start_tick;
  Tick cadenza_mid = start_tick + (end_tick - start_tick) / 2;

  // Phase 1: descending scale run.
  {
    size_t idx = scale.size() > 1 ? scale.size() - 1 : 0;
    while (tick < cadenza_mid && tick < end_tick) {
      Tick dur = std::min(dur_unit, cadenza_mid - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur, scale[idx], kPedalVoice,
                               BachNoteSource::PedalPoint));
      tick += dur;
      if (idx > 0) --idx;
      else idx = scale.size() > 1 ? scale.size() - 1 : 0;
    }
  }

  // Phase 2: dominant arpeggio + trill.
  {
    // V7 arpeggio: root, 3rd, 5th, 7th.
    std::vector<uint8_t> v7_pcs = {
        static_cast<uint8_t>(getDominant(key_sig).tonic),  // root of V
        static_cast<uint8_t>((tpc + 11) % 12),  // maj 3rd of V
        static_cast<uint8_t>((tpc + 2) % 12),   // 5th of V
        static_cast<uint8_t>((tpc + 5) % 12),   // min 7th of V
    };

    // Ascending arpeggio.
    Tick arp_end = tick + (end_tick - tick) * 60 / 100;
    for (size_t ai = 0; tick < arp_end && tick < end_tick; ++ai) {
      uint8_t pc = v7_pcs[ai % v7_pcs.size()];
      uint8_t p = clampPitch(36 + pc, low, high);
      Tick dur = std::min(dur_unit, arp_end - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur, p, kPedalVoice,
                               BachNoteSource::PedalPoint));
      tick += dur;
    }

    // Dominant trill.
    uint8_t dom_pitch = clampPitch(36 + dominant_pc, low, high);
    uint8_t upper_pitch = dom_pitch;
    for (const auto& s : scale) {
      if (s > dom_pitch) { upper_pitch = s; break; }
    }

    bool use_main = true;
    while (tick < end_tick) {
      Tick dur = std::min(dur_unit, end_tick - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur,
                               use_main ? dom_pitch : upper_pitch,
                               kPedalVoice, BachNoteSource::PedalPoint));
      tick += dur;
      use_main = !use_main;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Free section: scale-based arpeggio passage (reused for Free1/Free2/Coda)
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generateFreeSection(
    const HarmonicTimeline& timeline,
    const KeySignature& key_sig,
    uint8_t num_voices, Tick start_tick, Tick end_tick,
    std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  constexpr int kMaxRangeFromCenter = 14;

  for (uint8_t v = 0; v < std::min(num_voices, static_cast<uint8_t>(2)); ++v) {
    uint8_t low = getToccataLowPitch(v);
    uint8_t high = getToccataHighPitch(v);
    auto scale = getScaleTones(key_sig.tonic, key_sig.is_minor, low, high);
    if (scale.empty()) continue;

    size_t idx = scale.size() / 2;
    bool ascending = (v == 0);

    Tick tick = start_tick;
    while (tick < end_tick) {
      // Mix of 8th and 16th notes.
      Tick dur = rng::rollProbability(rng, 0.6f) ? kEighthNote : kSixteenthNote;
      if (tick + dur > end_tick) dur = end_tick - tick;
      if (dur == 0) break;

      // Target chord tones on strong beats (within ±octave).
      uint8_t beat = beatInBar(tick);
      if (beat == 0 || beat == 2) {
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

      if (rng::rollProbability(rng, 0.10f)) ascending = !ascending;

      notes.push_back(makeNote(tick, dur, scale[idx], v));
      tick += dur;
    }
  }

  return notes;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public: Sectionalis archetype
// ---------------------------------------------------------------------------

ToccataResult generateSectionalisToccata(const ToccataConfig& config) {
  ToccataResult result;
  result.success = false;

  uint8_t num_voices = clampToccataVoiceCount(config.num_voices);

  if (config.total_bars <= 0) {
    result.error_message = "total_bars must be positive";
    return result;
  }

  std::mt19937 rng(config.seed);

  // Section allocation: Free1 20%, QuasiFugal 25%, Free2 20%, Cadenza 15%, Coda 20%.
  auto bars = allocateBars(config.total_bars,
                           {0.20f, 0.25f, 0.20f, 0.15f, 0.20f});
  auto sections = buildSectionBoundaries(
      bars, {ToccataSectionId::Free1, ToccataSectionId::QuasiFugal,
             ToccataSectionId::Free2, ToccataSectionId::Cadenza,
             ToccataSectionId::Coda});
  Tick total_duration = static_cast<Tick>(config.total_bars) * kTicksPerBar;

  HarmonicTimeline timeline = buildSectionalisHarmonicPlan(config.key, sections);

  std::vector<NoteEvent> all_notes;

  // --- Free1: Free arpeggio passages ---
  auto free1 = generateFreeSection(
      timeline, config.key, num_voices, sections[0].start, sections[0].end, rng);
  all_notes.insert(all_notes.end(), free1.begin(), free1.end());

  // --- QuasiFugal: Imitative texture ---
  auto quasi = generateQuasiFugalSection(
      timeline, config.key, num_voices, sections[1].start, sections[1].end, rng);
  all_notes.insert(all_notes.end(), quasi.begin(), quasi.end());

  // --- Free2: Another free section with chromatic color ---
  auto free2 = generateFreeSection(
      timeline, config.key, num_voices, sections[2].start, sections[2].end, rng);
  all_notes.insert(all_notes.end(), free2.begin(), free2.end());

  // --- Cadenza: Pedal solo ---
  auto cadenza = generatePedalCadenza(
      timeline, config.key, sections[3].start, sections[3].end, rng);
  all_notes.insert(all_notes.end(), cadenza.begin(), cadenza.end());

  // --- Coda: All voices, driving to final cadence ---
  auto coda = generateFreeSection(
      timeline, config.key, num_voices, sections[4].start, sections[4].end, rng);
  all_notes.insert(all_notes.end(), coda.begin(), coda.end());

  // Pedal for non-cadenza sections.
  if (num_voices >= 3) {
    uint8_t low = getToccataLowPitch(2);
    uint8_t high = getToccataHighPitch(2);

    // Free1 + QuasiFugal + Free2: sustained pedal tones.
    for (size_t si = 0; si < 3 && si < sections.size(); ++si) {
      Tick tick = sections[si].start;
      while (tick < sections[si].end) {
        const HarmonicEvent& ev = timeline.getAt(tick);
        uint8_t bass = clampPitch(static_cast<int>(ev.bass_pitch), low, high);
        Tick dur = std::min(kWholeNote, sections[si].end - tick);
        if (dur == 0) break;
        all_notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
        tick += dur;
      }
    }

    // Coda: quarter-note bass (energetic).
    {
      Tick tick = sections[4].start;
      while (tick < sections[4].end) {
        const HarmonicEvent& ev = timeline.getAt(tick);
        uint8_t bass = clampPitch(static_cast<int>(ev.bass_pitch), low, high);
        Tick dur = std::min(kQuarterNote, sections[4].end - tick);
        if (dur == 0) break;
        all_notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
        tick += dur;
      }
    }
  }

  // Apply ornaments (moderate density, more in free sections).
  {
    OrnamentContext ctx;
    ctx.config.ornament_density = 0.08f;
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
    all_notes = postValidateNotes(
        std::move(all_notes), num_voices, config.key, voice_ranges);
  }

  // Build tracks.
  std::vector<Track> tracks = createToccataTracks(num_voices);
  assignNotesToTracks(all_notes, tracks);

  // Registration: forte -> mezzo -> piano -> pleno -> tutti (wave-like).
  ExtendedRegistrationPlan reg_plan;
  Registration forte;
  forte.velocity_hint = 85;
  reg_plan.addPoint(sections[0].start, forte, "forte");

  Registration mezzo;
  mezzo.velocity_hint = 70;
  reg_plan.addPoint(sections[1].start, mezzo, "mezzo");

  Registration piano;
  piano.velocity_hint = 55;
  reg_plan.addPoint(sections[2].start, piano, "piano");

  Registration pleno;
  pleno.velocity_hint = 100;
  reg_plan.addPoint(sections[3].start, pleno, "pleno");

  Registration tutti;
  tutti.velocity_hint = 110;
  reg_plan.addPoint(sections[4].start, tutti, "tutti");

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
  result.archetype = ToccataArchetype::Sectionalis;
  result.sections = sections;
  populateLegacyFields(result);
  result.success = true;

  return result;
}

}  // namespace bach
