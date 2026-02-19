// Perpetuus archetype toccata generator (BWV 538 style).
// Ascending energy. Continuous 16th-note moto perpetuo.

#include "forms/toccata.h"
#include "forms/toccata_internal.h"

#include <cassert>
#include <cmath>

#include "core/melodic_state.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "ornament/ornament_engine.h"

namespace bach {

using namespace toccata_internal;

namespace {

// ---------------------------------------------------------------------------
// Perpetuus harmonic plan
// ---------------------------------------------------------------------------

HarmonicTimeline buildPerpetuusHarmonicPlan(
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

  // Ascent: i -> III -> iv -> V -> VI -> i (mediant color in middle)
  if (sections.size() >= 1) {
    Tick s = sections[0].start;
    Tick dur = sections[0].end - s;
    Tick seg = dur / 6;
    if (seg == 0) seg = dur;

    addChord(s, s + seg,
             ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0);
    addChord(s + seg, s + seg * 2,
             ChordDegree::iii, minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 3 : 4);  // III in minor = relative major
    addChord(s + seg * 2, s + seg * 3,
             ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(s + seg * 3, s + seg * 4,
             ChordDegree::V, ChordQuality::Major, 7);
    addChord(s + seg * 4, s + seg * 5,
             ChordDegree::vi, minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 8 : 9);  // VI in minor = deceptive color
    addChord(s + seg * 5, sections[0].end,
             ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0);
  }

  // Plateau: i -> VI -> iv -> III -> V/V -> V -> viio7 -> i (mediant enrichment)
  if (sections.size() >= 2) {
    Tick s = sections[1].start;
    Tick dur = sections[1].end - s;
    Tick seg = dur / 8;
    if (seg == 0) seg = dur;

    addChord(s, s + seg,
             ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0);
    addChord(s + seg, s + seg * 2,
             ChordDegree::vi, minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 8 : 9);  // VI in minor = deceptive relation
    addChord(s + seg * 2, s + seg * 3,
             ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(s + seg * 3, s + seg * 4,
             ChordDegree::iii, minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 3 : 4);  // III in minor = relative major
    addChord(s + seg * 4, s + seg * 5,
             ChordDegree::V_of_V, ChordQuality::Major, 2);
    addChord(s + seg * 5, s + seg * 6,
             ChordDegree::V, ChordQuality::Major, 7);
    addChord(s + seg * 6, s + seg * 7,
             ChordDegree::viiDim, ChordQuality::Diminished7, 11);
    addChord(s + seg * 7, sections[1].end,
             ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0);
  }

  // Climax: iv -> V/V -> V7 -> I (1 chord per half bar, compressed)
  if (sections.size() >= 3) {
    Tick s = sections[2].start;
    Tick dur = sections[2].end - s;
    Tick seg = dur / 4;
    if (seg == 0) seg = dur;

    addChord(s, s + seg,
             ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(s + seg, s + seg * 2,
             ChordDegree::V_of_V, ChordQuality::Major, 2);
    addChord(s + seg * 2, s + seg * 3,
             ChordDegree::V, ChordQuality::Dominant7, 7);
    addChord(s + seg * 3, sections[2].end,
             ChordDegree::I, ChordQuality::Major, 0, 1.5f);
  }

  return timeline;
}

// ---------------------------------------------------------------------------
// Moto perpetuo generator
// ---------------------------------------------------------------------------

/// @brief Generate continuous 16th-note passage targeting harmonic tones.
/// No rests. Strong beats (1, 3) target nearest chord tone; weak beats use
/// arpeggio-driven motion: 60% directional chord tone, 25% skip (3rd),
/// 15% NCT stepwise (passing/neighbor).
std::vector<NoteEvent> generateMotoPerpetuo(
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
  MelodicState mel_state;
  uint8_t prev_moto_pitch = scale[idx];

  // Constrain effective range to ~2 octaves around starting position.
  // This prevents the moto perpetuo from traversing the full 5-octave range.
  constexpr int kMaxRangeFromCenter = 19;  // ~1.5 octave each side for Brechung range.

  Tick tick = start_tick;
  while (tick < end_tick) {
    Tick dur = std::min(kSixteenthNote, end_tick - tick);
    if (dur == 0) break;

    uint8_t beat = beatInBar(tick);
    bool strong_beat = (beat == 0 || beat == 2);

    if (strong_beat) {
      // Target nearest chord tone within ±octave of current position.
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
          if (d < best_dist) {
            best = ct;
            best_dist = d;
          }
        }
        // Find scale index nearest to chosen chord tone.
        for (size_t i = 0; i < scale.size(); ++i) {
          if (scale[i] >= best) {
            idx = i;
            break;
          }
        }
      }
    } else {
      // Weak beat: arpeggio-driven with NCT stepwise.
      // 60% directional chord tone, 25% skip (3rd), 15% NCT stepwise.
      float roll = rng::rollFloat(rng, 0.0f, 1.0f);
      if (roll < 0.60f) {
        // Directional chord-tone target: find nearest chord tone in current direction.
        const HarmonicEvent& evt = timeline.getAt(tick);
        auto chord_tones = collectChordTonesInRange(evt.chord, low, high);
        uint8_t current = scale[idx];
        uint8_t best = current;
        int best_dist = 127;
        for (auto ctn : chord_tones) {
          int dist = static_cast<int>(ctn) - static_cast<int>(current);
          bool correct_dir = ascending ? (dist > 0) : (dist < 0);
          int abs_dist = std::abs(dist);
          if (correct_dir && abs_dist >= 2 && abs_dist < best_dist) {
            best = ctn;
            best_dist = abs_dist;
          }
        }
        if (best != current) {
          // Find the nearest scale index for this pitch.
          for (size_t sci = 0; sci < scale.size(); ++sci) {
            if (scale[sci] >= best) { idx = sci; break; }
          }
        } else {
          // Fallback: skip by 2-3 scale steps in current direction.
          int step = rng::rollRange(rng, 2, 3);
          if (ascending) {
            if (idx + step < scale.size()) idx += step;
            else { ascending = false; if (idx >= static_cast<size_t>(step)) idx -= step; }
          } else {
            if (idx >= static_cast<size_t>(step)) idx -= step;
            else { ascending = true; if (idx + step < scale.size()) idx += step; }
          }
        }
      } else if (roll < 0.85f) {
        // Skip: 2-3 scale steps (3rd interval).
        int step = rng::rollRange(rng, 2, 3);
        if (ascending) {
          if (idx + step < scale.size()) idx += step;
          else { ascending = false; if (idx >= static_cast<size_t>(step)) idx -= step; }
        } else {
          if (idx >= static_cast<size_t>(step)) idx -= step;
          else { ascending = true; if (idx + step < scale.size()) idx += step; }
        }
      } else {
        // NCT stepwise: ±1 step (passing tone / neighbor tone role).
        int step = 1;
        if (ascending) {
          if (idx + step < scale.size()) idx += step;
          else { ascending = false; if (idx >= 1) idx -= step; }
        } else {
          if (idx >= 1) idx -= step;
          else { ascending = true; if (idx + step < scale.size()) idx += step; }
        }
      }
    }

    // Soft range boundary: reverse direction if approaching the edges.
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

// ---------------------------------------------------------------------------
// Sustained chord pad
// ---------------------------------------------------------------------------

/// @brief Generate sustained chord tones (half/whole notes) on a given voice.
std::vector<NoteEvent> generateChordPad(
    const HarmonicTimeline& timeline,
    const KeySignature& /* key_sig */,
    uint8_t voice, Tick start_tick, Tick end_tick,
    std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low = getToccataLowPitch(voice);
  uint8_t high = getToccataHighPitch(voice);

  // Start in the middle of the range.
  uint8_t prev_pitch = (low + high) / 2;

  Tick tick = start_tick;
  while (tick < end_tick) {
    const HarmonicEvent& ev = timeline.getAt(tick);
    // Limit chord tone search to ±octave from previous pitch.
    uint8_t search_low = (prev_pitch > low + 12) ? prev_pitch - 12 : low;
    uint8_t search_high = (prev_pitch + 12 < high) ? prev_pitch + 12 : high;
    auto chord_tones = collectChordTonesInRange(ev.chord, search_low, search_high);

    if (chord_tones.empty()) {
      // Fallback: wider search.
      chord_tones = collectChordTonesInRange(ev.chord, low, high);
    }
    if (chord_tones.empty()) {
      tick += kHalfNote;
      continue;
    }

    Tick dur = rng::rollProbability(rng, 0.4f) ? kWholeNote : kHalfNote;
    if (tick + dur > end_tick) dur = end_tick - tick;
    if (dur == 0) break;

    // Pick nearest chord tone to previous pitch.
    uint8_t best = chord_tones[0];
    int best_dist = absoluteInterval(prev_pitch, best);
    for (auto ct : chord_tones) {
      int d = absoluteInterval(prev_pitch, ct);
      if (d < best_dist) { best = ct; best_dist = d; }
    }

    notes.push_back(makeNote(tick, dur, best, voice));
    prev_pitch = best;
    tick += dur;
  }

  return notes;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public: Perpetuus archetype
// ---------------------------------------------------------------------------

ToccataResult generatePerpetuusToccata(const ToccataConfig& config) {
  ToccataResult result;
  result.success = false;

  uint8_t num_voices = clampToccataVoiceCount(config.num_voices);

  if (config.total_bars <= 0) {
    result.error_message = "total_bars must be positive";
    return result;
  }

  std::mt19937 rng(config.seed);

  // Section allocation: Ascent 35%, Plateau 40%, Climax 25%.
  auto bars = allocateBars(config.total_bars, {0.35f, 0.40f, 0.25f});
  auto sections = buildSectionBoundaries(
      bars, {ToccataSectionId::Ascent, ToccataSectionId::Plateau,
             ToccataSectionId::Climax});
  Tick total_duration = static_cast<Tick>(config.total_bars) * kTicksPerBar;

  // Harmonic plan.
  HarmonicTimeline timeline = buildPerpetuusHarmonicPlan(config.key, sections);

  std::vector<NoteEvent> all_notes;

  // --- Ascent: Voice 0 moto perpetuo, Voice 1 chord pad ---
  // Voice 0 carries the running 16th notes from the start.
  auto moto_ascent = generateMotoPerpetuo(
      timeline, config.key, 0, sections[0].start, sections[0].end, rng);
  all_notes.insert(all_notes.end(), moto_ascent.begin(), moto_ascent.end());

  // Voice 1 enters at 40% of ascent (delayed entry for texture thickening).
  Tick v1_entry = sections[0].start +
                  (sections[0].end - sections[0].start) * 40 / 100;
  auto pad_ascent = generateChordPad(
      timeline, config.key, 1, v1_entry, sections[0].end, rng);
  all_notes.insert(all_notes.end(), pad_ascent.begin(), pad_ascent.end());

  // --- Plateau: Voice 0 moto perpetuo, Voice 1 moto perpetuo (canon-like) ---
  auto moto_plat0 = generateMotoPerpetuo(
      timeline, config.key, 0, sections[1].start, sections[1].end, rng);
  all_notes.insert(all_notes.end(), moto_plat0.begin(), moto_plat0.end());

  auto moto_plat1 = generateMotoPerpetuo(
      timeline, config.key, 1, sections[1].start, sections[1].end, rng);
  all_notes.insert(all_notes.end(), moto_plat1.begin(), moto_plat1.end());

  // --- Climax: All manual voices moto perpetuo + pedal ---
  auto moto_clim0 = generateMotoPerpetuo(
      timeline, config.key, 0, sections[2].start, sections[2].end, rng);
  all_notes.insert(all_notes.end(), moto_clim0.begin(), moto_clim0.end());

  auto moto_clim1 = generateMotoPerpetuo(
      timeline, config.key, 1, sections[2].start, sections[2].end, rng);
  all_notes.insert(all_notes.end(), moto_clim1.begin(), moto_clim1.end());

  // --- Pedal ---
  if (num_voices >= 3) {
    // Pedal enters at Plateau (delayed entry, ascending energy).
    auto pedal_plat = generateChordPad(
        timeline, config.key, 2, sections[1].start, sections[1].end, rng);
    // Remap to pedal source.
    for (auto& n : pedal_plat) n.source = BachNoteSource::PedalPoint;
    all_notes.insert(all_notes.end(), pedal_plat.begin(), pedal_plat.end());

    auto pedal_clim = generateChordPad(
        timeline, config.key, 2, sections[2].start, sections[2].end, rng);
    for (auto& n : pedal_clim) n.source = BachNoteSource::PedalPoint;
    all_notes.insert(all_notes.end(), pedal_clim.begin(), pedal_clim.end());
  }

  // --- Additional voices (3+): chord support in climax ---
  for (uint8_t vi = 3; vi < num_voices && vi < 5; ++vi) {
    auto extra = generateChordPad(
        timeline, config.key, vi, sections[2].start, sections[2].end, rng);
    all_notes.insert(all_notes.end(), extra.begin(), extra.end());
  }

  // Apply ornaments (lighter density for perpetuus -- ornaments would
  // disrupt the continuous texture).
  auto applyOrnamentsToSection = [&](std::vector<NoteEvent>& section_notes,
                                     float density, VoiceRole role,
                                     const std::vector<Tick>& cadence_ticks) {
    OrnamentContext ctx;
    ctx.config.ornament_density = density;
    ctx.role = role;
    ctx.seed = config.seed;
    ctx.timeline = &timeline;
    ctx.cadence_ticks = cadence_ticks;
    section_notes = applyOrnaments(section_notes, ctx);
  };

  // Very light ornamentation (moto perpetuo should stay clean).
  applyOrnamentsToSection(all_notes, 0.03f, VoiceRole::Propel,
                          {sections.back().end - kTicksPerBar});

  // Constraint-driven finalize: overlap dedup + range clamping + repeat mitigation.
  assert(countUnknownSource(all_notes) == 0 &&
         "All notes should have source set by generators");
  {
    auto voice_range = [](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      return {getToccataLowPitch(v), getToccataHighPitch(v)};
    };
    ScaleType perp_scale = config.key.is_minor ? ScaleType::HarmonicMinor
                                                : ScaleType::Major;
    finalizeFormNotes(all_notes, num_voices, voice_range, config.key.tonic,
                      perp_scale, /*max_consecutive=*/2);
  }

  // Build tracks.
  std::vector<Track> tracks = createToccataTracks(num_voices);
  assignNotesToTracks(all_notes, tracks);

  // Registration: ascending dynamic (piano -> mezzo -> forte -> pleno -> tutti).
  ExtendedRegistrationPlan reg_plan;
  Registration piano;
  piano.velocity_hint = 55;
  reg_plan.addPoint(sections[0].start, piano, "piano");

  Registration mezzo;
  mezzo.velocity_hint = 70;
  reg_plan.addPoint(v1_entry, mezzo, "mezzo");

  Registration forte;
  forte.velocity_hint = 85;
  reg_plan.addPoint(sections[1].start, forte, "forte");

  Registration pleno;
  pleno.velocity_hint = 100;
  Tick plat_mid = sections[1].start +
                  (sections[1].end - sections[1].start) / 2;
  reg_plan.addPoint(plat_mid, pleno, "pleno");

  Registration tutti;
  tutti.velocity_hint = 110;
  reg_plan.addPoint(sections[2].start, tutti, "tutti");

  applyExtendedRegistrationPlan(tracks, reg_plan);

  // Sort and cleanup.
  sortToccataTrackNotes(tracks);
  cleanupToccataOverlaps(tracks);

  // Picardy third.
  if (config.enable_picardy && config.key.is_minor) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               total_duration - kTicksPerBar);
    }
  }

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.archetype = ToccataArchetype::Perpetuus;
  result.sections = sections;
  populateLegacyFields(result);
  result.success = true;

  return result;
}

}  // namespace bach
