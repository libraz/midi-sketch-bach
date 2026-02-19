// Implementation of the organ toccata free section generator (BWV 565 style).

#include "forms/toccata.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <random>
#include <vector>

#include "core/bach_vocabulary.h"
#include "core/figure_match.h"
#include "core/gm_program.h"
#include "core/melodic_state.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "forms/form_constraint_setup.h"
#include "forms/form_utils.h"
#include "forms/gesture_template.h"
#include "forms/toccata_figuration.h"
#include "forms/toccata_internal.h"
#include "harmony/chord_types.h"
#include "harmony/key.h"
#include "harmony/harmonic_event.h"
#include "ornament/ornament_engine.h"
#include "organ/organ_techniques.h"
#include "organ/registration.h"

namespace bach {

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

using namespace duration;

constexpr uint8_t kGreatChannel = 0;
constexpr uint8_t kSwellChannel = 1;
constexpr uint8_t kPedalChannel = 3;

constexpr uint8_t kMinVoices = 2;
constexpr uint8_t kMaxVoices = 5;

// ---------------------------------------------------------------------------
// Dramaticus 8-phase design values
// ---------------------------------------------------------------------------

// Phase IDs for the 8-phase Dramaticus design.
constexpr ToccataSectionId kDramaticusPhaseIds[8] = {
    ToccataSectionId::Gesture,
    ToccataSectionId::EchoCollapse,
    ToccataSectionId::RecitExpansion,
    ToccataSectionId::SequenceClimb1,
    ToccataSectionId::HarmonicBreak,
    ToccataSectionId::SequenceClimb2,
    ToccataSectionId::DomObsession,
    ToccataSectionId::FinalExplosion,
};

// Energy curve (two-peak arch): A=0.90 B=0.30 C=0.50 D=0.75 E=0.35 F=0.85 G=0.95 H=1.00
constexpr float kDramaticusEnergy[8] = {
    0.90f, 0.30f, 0.50f, 0.75f, 0.35f, 0.85f, 0.95f, 1.00f};

// Range ceiling for voice 0 (Great) per phase -- staircase release.
constexpr uint8_t kDramaticusRangeCeiling[8] = {
    84, 84, 86, 89, 84, 93, 96, 96};

// Registration velocity hints per phase (non-monotonic).
constexpr uint8_t kDramaticusVelocity[8] = {
    100, 45, 60, 85, 50, 95, 105, 115};

// Ornament density per phase.
constexpr float kDramaticusOrnament[8] = {
    0.12f, 0.04f, 0.10f, 0.08f, 0.03f, 0.08f, 0.06f, 0.10f};

// Discrete bars tables for standard total_bars values.
const std::map<int, std::vector<Tick>> kDramaticusBarsTables = {
    {18, {2, 1, 3, 2, 1, 3, 4, 2}},
    {24, {2, 2, 4, 3, 2, 4, 4, 3}},
    {30, {3, 2, 5, 4, 2, 5, 5, 4}},
    {36, {3, 3, 6, 5, 3, 6, 6, 4}},
};

// Fallback proportions for non-table total_bars.
constexpr float kDramaticusFallbackProportions[8] = {
    0.083f, 0.083f, 0.167f, 0.125f, 0.083f, 0.167f, 0.167f, 0.125f};

// Minimum bars per phase (weighted guarantee).
constexpr Tick kDramaticusMinBars[8] = {1, 1, 2, 2, 1, 2, 2, 2};

// ---------------------------------------------------------------------------
// Figuration engine wrapper
// ---------------------------------------------------------------------------

// Helper to create figuration context and generate a figured passage for a voice.
std::vector<NoteEvent> generateFigured(
    const HarmonicTimeline& timeline,
    const KeySignature& key_sig,
    int phase_idx,          // 0-7 for phases A-H
    uint8_t voice,          // 0 or 1
    Tick start_tick, Tick end_tick,
    std::mt19937& rng) {
  using namespace toccata_figuration;
  const auto& profile = getDramaticusPhaseProfile(phase_idx);
  ScaleType sc_type = key_sig.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  uint8_t ceiling = (voice < 2) ? profile.voice_ceiling[voice]
                                : toccata_internal::getToccataHighPitch(voice);
  ToccataFigurationContext ctx;
  ctx.profile = &profile;
  ctx.markov_model = &kToccataUpperMarkov;
  ctx.key = key_sig.tonic;
  ctx.scale = sc_type;
  ctx.low_pitch = toccata_internal::getToccataLowPitch(voice);
  ctx.high_pitch = std::min(toccata_internal::getToccataHighPitch(voice), ceiling);
  ctx.energy = kDramaticusEnergy[phase_idx];
  ctx.mel_state = MelodicState{};
  ctx.mel_state.contour.shape = profile.contour;
  ctx.prev_degree_step = 0;

  auto result = generateFigurationSpan(ctx, start_tick, end_tick, voice,
                                        timeline, rng);
  return std::move(result.notes);
}

// ---------------------------------------------------------------------------
// Track creation
// ---------------------------------------------------------------------------

std::vector<Track> createToccataTracks(uint8_t num_voices) {
  std::vector<Track> tracks;
  tracks.reserve(num_voices);

  struct TrackSpec {
    uint8_t channel;
    uint8_t program;
    const char* name;
  };

  static constexpr TrackSpec kSpecs[] = {
      {kGreatChannel, GmProgram::kChurchOrgan, "Manual I (Great)"},
      {kSwellChannel, GmProgram::kReedOrgan, "Manual II (Swell)"},
      {kPedalChannel, GmProgram::kChurchOrgan, "Pedal"},
      {2, GmProgram::kChurchOrgan, "Manual III (Positiv)"},
      {4, GmProgram::kChurchOrgan, "Manual IV"},
  };

  for (uint8_t idx = 0; idx < num_voices && idx < 5; ++idx) {
    Track track;
    track.channel = kSpecs[idx].channel;
    track.program = kSpecs[idx].program;
    track.name = kSpecs[idx].name;
    tracks.push_back(track);
  }

  return tracks;
}

// ---------------------------------------------------------------------------
// Pitch range helpers
// ---------------------------------------------------------------------------

uint8_t getToccataLowPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return 60;                          // C4 (Great)
    case 1: return 52;                          // E3 (Swell)
    case 2: return organ_range::kPedalLow;      // 24 (Pedal unchanged)
    case 3: return 43;                          // G2 (Positiv)
    default: return 52;
  }
}

uint8_t getToccataHighPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return 88;                          // E6 (Great)
    case 1: return 76;                          // E5 (Swell)
    case 2: return organ_range::kPedalHigh;     // 50 (Pedal unchanged)
    case 3: return 67;                          // G4 (Positiv)
    default: return 76;
  }
}

// ---------------------------------------------------------------------------
// NoteEvent creation helper
// ---------------------------------------------------------------------------

NoteEvent makeNote(Tick tick, Tick dur, uint8_t pitch, uint8_t voice,
                   BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  assert(source != BachNoteSource::Unknown && "makeNote: source must be explicitly specified");
  NoteEvent n;
  n.start_tick = tick;
  n.duration = dur;
  n.pitch = pitch;
  n.velocity = kOrganVelocity;
  n.voice = voice;
  n.source = source;
  return n;
}

// ---------------------------------------------------------------------------
// Toccata harmonic plan -- baroque-faithful chord progression
// ---------------------------------------------------------------------------

// Dramaticus 8-phase harmonic plan
// ---------------------------------------------------------------------------

/// @brief Build 8-phase harmonic plan for Dramaticus toccata.
HarmonicTimeline buildDramaticusHarmonicPlan(
    const KeySignature& key_sig,
    const std::vector<ToccataSectionBoundary>& phases) {
  HarmonicTimeline timeline;
  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  bool minor = key_sig.is_minor;

  auto addChord = [&](Tick tick, Tick end_tick, ChordDegree degree,
                      ChordQuality quality, int root_offset,
                      float weight = 1.0f, float chord_bias = 0.6f) {
    HarmonicEvent evt;
    evt.tick = tick;
    evt.end_tick = end_tick;
    evt.key = key_sig.tonic;
    evt.is_minor = minor;
    evt.chord.degree = degree;
    evt.chord.quality = quality;
    evt.chord.root_pitch = static_cast<uint8_t>((tpc + root_offset) % 12);
    evt.bass_pitch = clampPitch(36 + (tpc + root_offset) % 12,
                                organ_range::kPedalLow, organ_range::kPedalHigh);
    evt.weight = weight;
    evt.chord_tone_bias = chord_bias;
    timeline.addEvent(evt);
  };

  if (phases.size() < 8) return timeline;

  // A: Gesture -- V -> i (BWV565-style V opening)
  {
    Tick seg_start = phases[0].start, seg_end = phases[0].end;
    Tick mid = seg_start + (seg_end - seg_start) / 2;
    addChord(seg_start, mid, ChordDegree::V, ChordQuality::Major, 7);
    addChord(mid, seg_end, ChordDegree::I,
             minor ? ChordQuality::Minor : ChordQuality::Major, 0);
  }

  // B: EchoCollapse -- i sustained
  addChord(phases[1].start, phases[1].end,
           ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0, 0.5f);

  // C: RecitExpansion -- i -> III -> viio7 -> V -> VI (mediant before diminished)
  {
    Tick seg_start = phases[2].start, seg_end = phases[2].end;
    Tick dur = seg_end - seg_start;
    Tick t1 = seg_start + dur * 20 / 100;
    Tick t2 = seg_start + dur * 40 / 100;
    Tick t3 = seg_start + dur * 60 / 100;
    Tick t4 = seg_start + dur * 80 / 100;
    addChord(seg_start, t1, ChordDegree::I,
             minor ? ChordQuality::Minor : ChordQuality::Major, 0);
    addChord(t1, t2, ChordDegree::iii,
             minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 3 : 4);  // III in minor = relative major
    addChord(t2, t3, ChordDegree::viiDim, ChordQuality::Diminished7, 11, 1.0f, 0.9f);
    addChord(t3, t4, ChordDegree::V, ChordQuality::Major, 7);
    addChord(t4, seg_end, ChordDegree::vi,
             minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 8 : 9);  // VI (deceptive)
  }

  // D: SequenceClimb1 -- iv -> vi -> V/V -> V (vi adds modal color)
  {
    Tick seg_start = phases[3].start, seg_end = phases[3].end;
    Tick dur = seg_end - seg_start;
    Tick t1 = seg_start + dur / 4;
    Tick t2 = seg_start + dur * 2 / 4;
    Tick t3 = seg_start + dur * 3 / 4;
    addChord(seg_start, t1, ChordDegree::IV,
             minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(t1, t2, ChordDegree::vi,
             minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 8 : 9);  // vi/VI adds modal color before dominant prep
    addChord(t2, t3, ChordDegree::V_of_V, ChordQuality::Major, 2);
    addChord(t3, seg_end, ChordDegree::V, ChordQuality::Major, 7);
  }

  // E: HarmonicBreak -- viio7 -> V (high chord_tone_bias)
  {
    Tick seg_start = phases[4].start, seg_end = phases[4].end;
    Tick mid = seg_start + (seg_end - seg_start) / 2;
    addChord(seg_start, mid, ChordDegree::viiDim, ChordQuality::Diminished7, 11, 1.0f, 0.9f);
    addChord(mid, seg_end, ChordDegree::V, ChordQuality::Major, 7);
  }

  // F: SequenceClimb2 -- III -> iv -> V/V -> V7 (III opens with mediant color)
  {
    Tick seg_start = phases[5].start, seg_end = phases[5].end;
    Tick dur = seg_end - seg_start;
    Tick t1 = seg_start + dur / 4;
    Tick t2 = seg_start + dur * 2 / 4;
    Tick t3 = seg_start + dur * 3 / 4;
    addChord(seg_start, t1, ChordDegree::iii,
             minor ? ChordQuality::Major : ChordQuality::Minor,
             minor ? 3 : 4);  // III in minor = relative major
    addChord(t1, t2, ChordDegree::IV,
             minor ? ChordQuality::Minor : ChordQuality::Major, 5);
    addChord(t2, t3, ChordDegree::V_of_V, ChordQuality::Major, 2);
    addChord(t3, seg_end, ChordDegree::V, ChordQuality::Dominant7, 7);
  }

  // G: DomObsession -- V -> viio7/V -> V -> V7 -> V sustained
  {
    Tick seg_start = phases[6].start, seg_end = phases[6].end;
    Tick dur = seg_end - seg_start;
    Tick t1 = seg_start + dur * 20 / 100;
    Tick t2 = seg_start + dur * 40 / 100;
    Tick t3 = seg_start + dur * 60 / 100;
    Tick t4 = seg_start + dur * 80 / 100;
    addChord(seg_start, t1, ChordDegree::V, ChordQuality::Major, 7);
    addChord(t1, t2, ChordDegree::viiDim, ChordQuality::Diminished7, 6,
             1.0f, 0.9f);  // viio7/V
    addChord(t2, t3, ChordDegree::V, ChordQuality::Major, 7);
    addChord(t3, t4, ChordDegree::V, ChordQuality::Dominant7, 7, 1.2f);
    addChord(t4, seg_end, ChordDegree::V, ChordQuality::Major, 7, 1.5f);  // sustained
  }

  // H: FinalExplosion -- V7 -> I Major (Picardy)
  {
    Tick seg_start = phases[7].start, seg_end = phases[7].end;
    Tick mid = seg_start + (seg_end - seg_start) * 80 / 100;
    addChord(seg_start, mid, ChordDegree::V, ChordQuality::Dominant7, 7);
    addChord(mid, seg_end, ChordDegree::I, ChordQuality::Major, 0, 1.5f);  // Picardy
  }

  return timeline;
}

// ---------------------------------------------------------------------------
// Legacy 3-section mapping from 8 phases
// ---------------------------------------------------------------------------

/// @brief Build legacy 3-section boundaries from 8 phases.
/// sections[0] = Opening    = phases[0].start -> phases[1].end
/// sections[1] = Recitative = phases[2].start -> phases[4].end
/// sections[2] = Drive      = phases[5].start -> phases[7].end
std::vector<ToccataSectionBoundary> buildLegacySectionsFromPhases(
    const std::vector<ToccataSectionBoundary>& phases) {
  if (phases.size() < 8) return {};
  return {
      {ToccataSectionId::Opening, phases[0].start, phases[1].end},
      {ToccataSectionId::Recitative, phases[2].start, phases[4].end},
      {ToccataSectionId::Drive, phases[5].start, phases[7].end},
  };
}

// ---------------------------------------------------------------------------
// Opening helpers
// ---------------------------------------------------------------------------

/// @brief Generate a mordent wave pattern: held note + mordent + descending scale.
/// @return Tick position after the wave ends.
Tick generateMordentWave(uint8_t start_pitch,
                         const std::vector<uint8_t>& scale_tones,
                         Tick start_tick, Tick section_end,
                         std::vector<NoteEvent>& notes,
                         uint8_t low0, uint8_t high0,
                         uint8_t low1, uint8_t high1) {
  if (scale_tones.empty() || start_tick >= section_end) return start_tick;

  Tick tick = start_tick;
  uint8_t p0 = clampPitch(start_pitch, low0, high0);
  uint8_t p1 = clampPitch(static_cast<int>(start_pitch) - 12, low1, high1);

  // 1. Held note (dotted quarter) -- parallel octaves on voice 0 + voice 1
  Tick held_dur = std::min(kDottedQuarter, section_end - tick);
  if (held_dur == 0) return tick;
  notes.push_back(makeNote(tick, held_dur, p0, 0, BachNoteSource::ToccataGesture));
  notes.push_back(makeNote(tick, held_dur, p1, 1, BachNoteSource::ToccataGesture));
  tick += held_dur;
  if (tick >= section_end) return tick;

  // 2. Mordent: main -> diatonic lower neighbor -> main (3 x 32nd)
  // Find the scale tone just below the starting pitch for each voice.
  uint8_t lower0 = clampPitch(static_cast<int>(p0) - 1, low0, high0);
  for (size_t si = 0; si < scale_tones.size(); ++si) {
    if (scale_tones[si] >= p0 && si > 0) {
      lower0 = scale_tones[si - 1];
      break;
    }
  }
  uint8_t lower1 = clampPitch(static_cast<int>(lower0) - 12, low1, high1);
  uint8_t mordent_pitches0[] = {p0, lower0, p0};
  uint8_t mordent_pitches1[] = {p1, lower1, p1};

  for (int i = 0; i < 3 && tick < section_end; ++i) {
    Tick dur = std::min(kThirtySecondNote, section_end - tick);
    if (dur == 0) break;
    notes.push_back(makeNote(tick, dur, mordent_pitches0[i], 0,
                             BachNoteSource::ToccataGesture));
    notes.push_back(makeNote(tick, dur, mordent_pitches1[i], 1,
                             BachNoteSource::ToccataGesture));
    tick += dur;
  }
  if (tick >= section_end) return tick;

  // 3. Descending scale run from start_pitch down ~one octave (32nd notes)
  // Find scale tone index nearest to p0
  size_t top_idx = 0;
  for (size_t i = 0; i < scale_tones.size(); ++i) {
    if (scale_tones[i] <= p0) top_idx = i;
  }

  uint8_t target = clampPitch(static_cast<int>(p0) - 12, low0, high0);
  for (size_t i = top_idx; i > 0 && tick < section_end; --i) {
    if (scale_tones[i] < target) break;
    Tick dur = std::min(kThirtySecondNote, section_end - tick);
    if (dur == 0) break;
    uint8_t sp0 = clampPitch(scale_tones[i], low0, high0);
    uint8_t sp1 = clampPitch(static_cast<int>(scale_tones[i]) - 12, low1, high1);
    notes.push_back(makeNote(tick, dur, sp0, 0, BachNoteSource::ToccataGesture));
    notes.push_back(makeNote(tick, dur, sp1, 1, BachNoteSource::ToccataGesture));
    tick += dur;
  }

  return tick;
}

/// @brief Generate a block chord across voices 0, 1, and optionally 2.
void generateBlockChordNotes(const KeySignature& key_sig, Tick tick, Tick end_tick,
                             std::vector<NoteEvent>& notes,
                             uint8_t num_voices) {
  Tick dur = std::min(kWholeNote, end_tick - tick);
  if (dur == 0) return;

  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  uint8_t third = clampPitch(60 + tpc + (key_sig.is_minor ? 3 : 4),
                             getToccataLowPitch(1), getToccataHighPitch(1));
  uint8_t fifth = clampPitch(60 + tpc + 7,
                             getToccataLowPitch(0), getToccataHighPitch(0));

  notes.push_back(makeNote(tick, dur, fifth, 0, BachNoteSource::ToccataGesture));
  notes.push_back(makeNote(tick, dur, third, 1, BachNoteSource::ToccataGesture));
  if (num_voices >= 3) {
    uint8_t bass = clampPitch(36 + tpc,
                              getToccataLowPitch(2), getToccataHighPitch(2));
    notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
  }
}

/// @brief Generate arpeggio flourish on voice 0 using chord tones.
/// @note Currently unused by Dramaticus (replaced by figuration engine).
///       Retained for other archetypes (Perpetuus, Concertato, Sectionalis).
[[maybe_unused]]
void generateArpeggioFlourish(const HarmonicTimeline& timeline,
                              Tick start_tick, Tick end_tick,
                              std::vector<NoteEvent>& notes,
                              std::mt19937& rng) {
  uint8_t low = getToccataLowPitch(0);
  uint8_t high = getToccataHighPitch(0);

  const HarmonicEvent& event = timeline.getAt(start_tick);
  auto chord_tones = collectChordTonesInRange(event.chord, low, high);
  if (chord_tones.empty()) return;

  Tick tick = start_tick;
  bool ascending = true;
  int same_dir_count = 0;
  size_t idx = chord_tones.size() / 2;
  MelodicState mel_state;
  uint8_t prev_flourish_pitch = chord_tones[idx];

  while (tick < end_tick) {
    // Refresh chord tones at bar boundaries
    if (tick > start_tick && positionInBar(tick) == 0) {
      const HarmonicEvent& ev = timeline.getAt(tick);
      auto new_tones = collectChordTonesInRange(ev.chord, low, high);
      if (!new_tones.empty()) {
        chord_tones = new_tones;
        idx = std::min(idx, chord_tones.size() - 1);
      }
    }

    Tick dur = std::min(kSixteenthNote, end_tick - tick);
    if (dur == 0) break;

    notes.push_back(makeNote(tick, dur, chord_tones[idx], 0,
                             BachNoteSource::ToccataGesture));
    updateMelodicState(mel_state, prev_flourish_pitch, chord_tones[idx]);
    prev_flourish_pitch = chord_tones[idx];
    tick += dur;

    // Direction: boundary-aware coin flip with anti-sawtooth prevention.
    {
      bool prev_ascending = ascending;
      if (idx + 1 >= chord_tones.size()) {
        ascending = false;
      } else if (idx == 0) {
        ascending = true;
      } else {
        // After 2 consecutive same-direction, 60% reverse (anti-sawtooth).
        float flip_prob = (same_dir_count >= 2) ? 0.60f : 0.50f;
        ascending = !rng::rollProbability(rng, flip_prob);
      }
      same_dir_count = (ascending == prev_ascending) ? same_dir_count + 1 : 0;
    }

    if (ascending) {
      if (idx + 1 < chord_tones.size()) ++idx;
      else { ascending = false; if (idx > 0) --idx; }
    } else {
      if (idx > 0) --idx;
      else { ascending = true; if (idx + 1 < chord_tones.size()) ++idx; }
    }
  }
}

// ---------------------------------------------------------------------------
// Opening gesture (BWV 565 style)
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generateOpeningGesture(const HarmonicTimeline& timeline,
                                              const KeySignature& key_sig,
                                              Tick start_tick, Tick end_tick,
                                              std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = getToccataHighPitch(0);
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = getToccataHighPitch(1);

  auto scale_tones0 = getScaleTones(key_sig.tonic, key_sig.is_minor, low0, high0);
  if (scale_tones0.empty()) return notes;

  Tick total_opening = end_tick - start_tick;
  Tick opening_bars = total_opening / kTicksPerBar;

  // Dominant in octave 5 for dramatic opening (BWV 565: A5)
  uint8_t dominant_pc = static_cast<uint8_t>(getDominant(key_sig).tonic);
  uint8_t start_pitch1 = clampPitch(72 + dominant_pc, low0, high0);

  // One scale step below dominant for second wave
  size_t dom_idx = 0;
  for (size_t i = 0; i < scale_tones0.size(); ++i) {
    if (scale_tones0[i] <= start_pitch1) dom_idx = i;
  }
  uint8_t start_pitch2 = (dom_idx > 0) ? scale_tones0[dom_idx - 1]
                                        : scale_tones0[0];

  Tick tick = start_tick;

  if (opening_bars >= 4) {
    // Full pattern: wave1 + pause + wave2 + pause + block chord + arpeggio
    tick = generateMordentWave(start_pitch1, scale_tones0, tick, end_tick,
                               notes, low0, high0, low1, high1);
    tick = std::min(tick + kGrandPauseDuration, end_tick);

    if (tick < end_tick - kTicksPerBar * 2) {
      tick = generateMordentWave(start_pitch2, scale_tones0, tick, end_tick,
                                 notes, low0, high0, low1, high1);
      tick = std::min(tick + kGrandPauseDuration, end_tick);
    }

    // Block chord (tonic, 1 bar)
    if (tick < end_tick - kTicksPerBar) {
      generateBlockChordNotes(key_sig, tick, end_tick, notes, 3);
      tick += std::min(kWholeNote, end_tick - tick);
    }

    // Arpeggio flourish through remaining opening (voice 0 coexists with
    // pedal solo on voice 2 in the final bars).
    if (tick < end_tick) {
      auto fig = generateFigured(timeline, key_sig, 0, 0, tick, end_tick, rng);
      notes.insert(notes.end(), fig.begin(), fig.end());
    }
  } else if (opening_bars >= 2) {
    // Simplified: single wave + arpeggio
    tick = generateMordentWave(start_pitch1, scale_tones0, tick, end_tick,
                               notes, low0, high0, low1, high1);
    tick = std::min(tick + kGrandPauseDuration, end_tick);

    if (tick < end_tick) {
      auto fig = generateFigured(timeline, key_sig, 0, 0, tick, end_tick, rng);
      notes.insert(notes.end(), fig.begin(), fig.end());
    }
  } else {
    // Very short: just arpeggio figuration
    auto fig = generateFigured(timeline, key_sig, 0, 0, start_tick, end_tick, rng);
    notes.insert(notes.end(), fig.begin(), fig.end());
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Recitative -- Stylus Phantasticus (free rhetorical style)
// ---------------------------------------------------------------------------

/// @brief Select a duration from the recitative weighted table.
/// @note Currently unused by Dramaticus (replaced by figuration engine).
[[maybe_unused]]
Tick selectRecitDuration(std::mt19937& rng) {
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  if (roll < 0.10f) return kThirtySecondNote;
  if (roll < 0.30f) return kSixteenthNote;
  if (roll < 0.60f) return kEighthNote;
  if (roll < 0.80f) return kQuarterNote;
  if (roll < 0.90f) return kDottedQuarter;
  return kHalfNote;
}

/// @brief Select a signed scale-step interval for recitative melody.
/// @return Number of scale steps (positive = ascending, negative = descending).
/// @note Currently unused by Dramaticus (replaced by figuration engine).
[[maybe_unused]]
int selectRecitScaleStep(std::mt19937& rng, bool ascending) {
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  int magnitude;
  if (roll < 0.50f) magnitude = 1;                                // Step (2nd)
  else if (roll < 0.80f) magnitude = rng::rollRange(rng, 2, 3);  // Skip (3rd/4th)
  else magnitude = 4;                                              // Max leap (5th)
  return ascending ? magnitude : -magnitude;
}

// ---------------------------------------------------------------------------
// Dramaticus 8-phase generation functions
// ---------------------------------------------------------------------------

// ClimbingMotif: short melodic cell for sequential transposition.
// Intervals are relative semitone offsets from the motif's base pitch
// (first note = 0). Used by generateSequenceClimb().
struct ClimbingMotif {
  std::vector<int8_t> intervals;   // Semitone intervals from base (first = 0).
  std::vector<Tick> durations;     // Duration for each note.
  uint8_t num_notes = 0;
};

// Extract a climbing motif (4-6 notes) from opening gesture material.
// Falls back to a default descending scale fragment if too few notes found.
ClimbingMotif extractClimbingMotif(const std::vector<NoteEvent>& opening_notes,
                                   uint8_t voice) {
  ClimbingMotif motif;
  constexpr uint8_t kMinMotifNotes = 3;
  constexpr uint8_t kMaxMotifNotes = 6;

  uint8_t base_pitch = 0;
  for (const auto& note : opening_notes) {
    if (note.voice != voice) continue;
    if (motif.num_notes == 0) {
      base_pitch = note.pitch;
    }
    int8_t rel = static_cast<int8_t>(
        static_cast<int>(note.pitch) - static_cast<int>(base_pitch));
    motif.intervals.push_back(rel);
    motif.durations.push_back(note.duration);
    ++motif.num_notes;
    if (motif.num_notes >= kMaxMotifNotes) break;
  }

  if (motif.num_notes < kMinMotifNotes) {
    motif.intervals = {0, -2, -4, -5};
    motif.durations = {kEighthNote, kEighthNote, kEighthNote, kEighthNote};
    motif.num_notes = 4;
  }

  return motif;
}

/// @brief Check if a figure's contour is descending (net negative direction).
static bool isFigureDescending(const MelodicFigure& fig) {
  if (!fig.degree_intervals || fig.note_count < 2) return false;
  int sum = 0;
  for (uint8_t idx = 0; idx < fig.note_count - 1; ++idx) {
    sum += fig.degree_intervals[idx].degree_diff;
  }
  return sum < 0;
}

/// @brief Check if a figure's contour is ascending (net positive direction).
static bool isFigureAscending(const MelodicFigure& fig) {
  if (!fig.degree_intervals || fig.note_count < 2) return false;
  int sum = 0;
  for (uint8_t idx = 0; idx < fig.note_count - 1; ++idx) {
    sum += fig.degree_intervals[idx].degree_diff;
  }
  return sum > 0;
}

/// @brief Try to apply vocabulary figure to a climbing motif segment.
/// Register-linked: descending figures only during descending phases,
/// ascending figures only during ascending phases.
static ClimbingMotif tryVocabularyGesture(
    const ClimbingMotif& motif, ToccataArchetype archetype,
    int current_base_pitch, int next_base_pitch,
    Key key, ScaleType scale, std::mt19937& rng_engine) {
  auto hint = getArchetypeFigures(archetype);
  if (!rng::rollProbability(rng_engine, hint.activation_prob)) return motif;

  bool register_descending = (next_base_pitch < current_base_pitch);

  // Try each figure in the hint.
  for (int fig_idx = 0; fig_idx < hint.count; ++fig_idx) {
    if (!hint.figures[fig_idx]) continue;
    const MelodicFigure& fig = *hint.figures[fig_idx];

    // Register-linked direction check.
    if (isFigureDescending(fig) && !register_descending) continue;
    if (isFigureAscending(fig) && register_descending) continue;

    // Only apply if note counts are compatible.
    if (fig.note_count != motif.num_notes) continue;
    if (!fig.degree_intervals) continue;

    // Reconstruct intervals from figure's degree diffs.
    ClimbingMotif result;
    result.num_notes = motif.num_notes;
    result.durations = motif.durations;
    result.intervals.resize(motif.num_notes);
    result.intervals[0] = 0;  // Base pitch.

    int abs_deg = scale_util::pitchToAbsoluteDegree(
        clampPitch(current_base_pitch, 0, 127), key, scale);
    uint8_t base_p = scale_util::absoluteDegreeToPitch(abs_deg, key, scale);

    for (uint8_t note_idx = 1; note_idx < fig.note_count; ++note_idx) {
      abs_deg += fig.degree_intervals[note_idx - 1].degree_diff;
      uint8_t pitch = scale_util::absoluteDegreeToPitch(abs_deg, key, scale);
      result.intervals[note_idx] = static_cast<int8_t>(
          static_cast<int>(pitch) - static_cast<int>(base_p));
    }
    return result;
  }

  return motif;
}

// Emit one motif statement transposed to base_pitch, clamped to [low, high].
std::vector<NoteEvent> generateClimbFromMotif(const ClimbingMotif& motif,
                                               uint8_t base_pitch,
                                               uint8_t voice,
                                               Tick start_tick, Tick max_end,
                                               uint8_t low, uint8_t high) {
  std::vector<NoteEvent> notes;
  notes.reserve(motif.num_notes);
  Tick tick = start_tick;

  for (uint8_t idx = 0; idx < motif.num_notes && tick < max_end; ++idx) {
    int raw_pitch = static_cast<int>(base_pitch) + static_cast<int>(motif.intervals[idx]);
    uint8_t clamped = clampPitch(raw_pitch, low, high);

    Tick dur = motif.durations[idx];
    if (tick + dur > max_end) {
      dur = max_end - tick;
    }
    if (dur == 0) break;

    notes.push_back(makeNote(tick, dur, clamped, voice,
                             BachNoteSource::ToccataFigure));
    tick += dur;
  }

  return notes;
}

// Ascending sequential motif climb for Phases D (energy 0.75) and F (energy 0.85).
// Generates 3-4 motif statements, each transposed up by 2-3 scale steps.
// Voice 0 carries the motif; voice 1 provides chord-tone support.
// Connectors between motif statements use figuration engine for musical continuity.
std::vector<NoteEvent> generateSequenceClimb(const HarmonicTimeline& timeline,
                                              const KeySignature& key_sig,
                                              const ClimbingMotif& motif,
                                              Tick start_tick, Tick end_tick,
                                              uint8_t range_ceiling,
                                              int phase_idx,
                                              std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  if (start_tick >= end_tick) return notes;

  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = std::min(getToccataHighPitch(0), range_ceiling);
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = getToccataHighPitch(1);

  auto scale_tones = getScaleTones(key_sig.tonic, key_sig.is_minor, low0, high0);
  if (scale_tones.size() < 4) return notes;

  auto scale_tones1 = getScaleTones(key_sig.tonic, key_sig.is_minor, low1, high1);
  if (scale_tones1.empty()) return notes;

  // Find starting pitch: chord root in lower third of range (room to climb).
  const HarmonicEvent& initial_event = timeline.getAt(start_tick);
  uint8_t chord_root_pc = static_cast<uint8_t>(getPitchClass(initial_event.chord.root_pitch));

  uint8_t target_start = low0 + (high0 - low0) / 3;
  size_t best_idx = 0;
  int best_dist = 127;
  for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
    int dist = std::abs(static_cast<int>(scale_tones[idx]) -
                        static_cast<int>(target_start));
    bool is_root = (getPitchClass(scale_tones[idx]) == chord_root_pc);
    int effective_dist = is_root ? dist : dist + 4;
    if (effective_dist < best_dist) {
      best_dist = effective_dist;
      best_idx = idx;
    }
  }

  Tick section_dur = end_tick - start_tick;
  int num_statements = (section_dur >= kTicksPerBar * 4) ? 4 : 3;
  Tick available_per_statement = section_dur / static_cast<Tick>(num_statements);

  Tick tick = start_tick;
  size_t current_scale_idx = best_idx;

  for (int stmt = 0; stmt < num_statements && tick < end_tick; ++stmt) {
    // Voice 0: motif statement.
    uint8_t base_pitch = scale_tones[current_scale_idx];
    Tick stmt_end = std::min(tick + available_per_statement, end_tick);

    auto motif_notes = generateClimbFromMotif(
        motif, base_pitch, 0, tick, stmt_end, low0, high0);
    notes.insert(notes.end(), motif_notes.begin(), motif_notes.end());

    Tick motif_end_tick = tick;
    for (const auto& mote : motif_notes) {
      Tick note_end = mote.start_tick + mote.duration;
      if (note_end > motif_end_tick) motif_end_tick = note_end;
    }

    // Voice 1: chord-tone support on downbeats only (dotted-half + rest gap).
    // Sparse texture: one note per bar at beat 0, creating breathing room.
    {
      const HarmonicEvent& evt = timeline.getAt(tick);
      auto chord_tones = collectChordTonesInRange(evt.chord, low1, high1);

      uint8_t support_target = clampPitch(
          static_cast<int>(base_pitch) - 7, low1, high1);
      uint8_t support_pitch = support_target;
      if (!chord_tones.empty()) {
        support_pitch = chord_tones[findClosestToneIndex(chord_tones, support_target)];
      } else {
        support_pitch = scale_tones1[findClosestToneIndex(scale_tones1, support_target)];
      }

      constexpr Tick kDottedHalf = kHalfNote + kQuarterNote;  // 1200 ticks
      Tick v1_tick = tick;
      while (v1_tick < motif_end_tick) {
        // Only generate on downbeats (beat 0 of each bar).
        if (beatInBar(v1_tick) != 0) {
          v1_tick += kQuarterNote;
          continue;
        }

        // Refresh chord tone at each bar boundary.
        const HarmonicEvent& bar_evt = timeline.getAt(v1_tick);
        auto bar_ct = collectChordTonesInRange(bar_evt.chord, low1, high1);
        if (!bar_ct.empty()) {
          support_pitch = bar_ct[findClosestToneIndex(bar_ct, support_pitch)];
        }

        Tick dur = std::min(kDottedHalf, motif_end_tick - v1_tick);
        if (dur == 0) break;

        notes.push_back(makeNote(v1_tick, dur, support_pitch, 1,
                                 BachNoteSource::FreeCounterpoint));
        // Advance past this bar to the next downbeat (rest gap on beat 4).
        v1_tick += kTicksPerBar;
      }
    }

    // Connector between motif statements: figuration-driven transition.
    tick = motif_end_tick;
    if (stmt < num_statements - 1 && tick < end_tick) {
      // Connector runs from motif_end_tick to the statement boundary.
      Tick connector_end = std::min(stmt_end, end_tick);
      Tick connector_dur = (connector_end > tick) ? connector_end - tick : 0;

      // Determine the motif's ending pitch for figuration proximity.
      uint8_t motif_end_pitch = base_pitch;  // Fallback to base pitch.
      if (!motif_notes.empty()) {
        motif_end_pitch = motif_notes.back().pitch;
      }

      if (connector_dur >= kTicksPerBeat) {
        // Use figuration engine for connector passage.
        using namespace toccata_figuration;
        const auto& profile = getDramaticusPhaseProfile(phase_idx);
        ScaleType sc_type = key_sig.is_minor ? ScaleType::HarmonicMinor
                                             : ScaleType::Major;
        ToccataFigurationContext fig_ctx;
        fig_ctx.profile = &profile;
        fig_ctx.markov_model = &kToccataUpperMarkov;
        fig_ctx.key = key_sig.tonic;
        fig_ctx.scale = sc_type;
        fig_ctx.low_pitch = low0;
        fig_ctx.high_pitch = high0;
        fig_ctx.energy = kDramaticusEnergy[phase_idx];
        fig_ctx.mel_state = MelodicState{};
        fig_ctx.mel_state.contour.shape = profile.contour;
        fig_ctx.prev_degree_step = 0;
        fig_ctx.initial_pitch = motif_end_pitch;

        auto fig_result = generateFigurationSpan(
            fig_ctx, tick, connector_end, 0, timeline, rng);

        if (!fig_result.notes.empty()) {
          notes.insert(notes.end(), fig_result.notes.begin(),
                       fig_result.notes.end());
          tick = fig_result.end_tick;
        } else {
          // Minimum 1 note guarantee: single bridge note near motif ending pitch.
          Tick bridge_dur = std::min(kEighthNote, connector_end - tick);
          if (bridge_dur > 0) {
            uint8_t bridge_pitch = clampPitch(
                static_cast<int>(motif_end_pitch) + 1, low0, high0);
            // Strong beat fallback: ensure chord tone on beat 1 or beat 3.
            Tick pos_in_bar = positionInBar(tick);
            if (pos_in_bar == 0 || pos_in_bar == kTicksPerBeat * 2) {
              const HarmonicEvent& conn_harm = timeline.getAt(tick);
              auto conn_ct = collectChordTonesInRange(conn_harm.chord, low0, high0);
              if (!conn_ct.empty()) {
                bridge_pitch = conn_ct[findClosestToneIndex(conn_ct, motif_end_pitch)];
              }
            }
            notes.push_back(makeNote(tick, bridge_dur, bridge_pitch, 0,
                                     BachNoteSource::ToccataGesture));
            tick += bridge_dur;
          }
        }
      } else if (connector_dur > 0) {
        // Short connector (< 1 beat): keep simple ascending 8th-note behavior.
        size_t conn_idx = current_scale_idx;
        int num_conn = rng::rollProbability(rng, 0.5f) ? 2 : 1;
        for (int conn = 0; conn < num_conn && tick < connector_end; ++conn) {
          if (conn_idx + 1 < scale_tones.size()) ++conn_idx;
          Tick dur = std::min(kEighthNote, connector_end - tick);
          if (dur == 0) break;
          notes.push_back(makeNote(tick, dur, scale_tones[conn_idx], 0,
                                   BachNoteSource::ToccataGesture));
          tick += dur;
        }
      }

      // Advance scale index for next motif statement (2-3 steps up).
      int step_up = rng::rollRange(rng, 2, 3);
      size_t next_idx = current_scale_idx + static_cast<size_t>(step_up);
      if (next_idx >= scale_tones.size()) {
        next_idx = scale_tones.size() - 1;
      }
      current_scale_idx = next_idx;
    }
  }

  return notes;
}

// Phase B: whispered echo fragments dying away after the opening gesture.
// Sparse texture: short motifs on voice 1, sustained tones, then fade.
std::vector<NoteEvent> generateEchoCollapse(const HarmonicTimeline& timeline,
                                            const KeySignature& key_sig,
                                            Tick start_tick, Tick end_tick,
                                            std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  if (start_tick >= end_tick) return notes;

  Tick phase_dur = end_tick - start_tick;
  Tick sparse_end = start_tick + phase_dur * 40 / 100;
  Tick sustain_end = start_tick + phase_dur * 70 / 100;

  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = std::min(getToccataHighPitch(0), static_cast<uint8_t>(84));
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = std::min(getToccataHighPitch(1), static_cast<uint8_t>(84));

  const HarmonicEvent& harm = timeline.getAt(start_tick);
  auto chord_tones_v1 = collectChordTonesInRange(harm.chord, low1, high1);
  auto chord_tones_v0 = collectChordTonesInRange(harm.chord, low0, high0);
  if (chord_tones_v1.empty() || chord_tones_v0.empty()) return notes;

  // Sub-section 1 (~40%): sparse figuration on voice 1.
  {
    auto fig = generateFigured(timeline, key_sig, 1, 1, start_tick, sparse_end, rng);
    notes.insert(notes.end(), fig.begin(), fig.end());
  }

  // Sub-section 2 (~30%): sustained chord tones on voice 1 only (sparse echo).
  // Single-voice texture for the echo collapse -- dying away to near-silence.
  {
    Tick tick = sparse_end;

    while (tick < sustain_end) {
      Tick dur = kWholeNote;  // Whole notes only for maximum sparseness.
      dur = std::min(dur, sustain_end - tick);
      if (dur == 0) break;

      size_t idx = rng::rollRange(rng, 0,
                                  static_cast<int>(chord_tones_v1.size()) - 1);
      notes.push_back(makeNote(tick, dur, chord_tones_v1[idx], 1,
                               BachNoteSource::ToccataGesture));

      tick += dur;
    }
  }

  // Sub-section 3 (~30%): fading to near-silence, 1-2 soft whole notes.
  {
    Tick tick = sustain_end;
    int fade_count = rng::rollRange(rng, 1, 2);

    for (int fi = 0; fi < fade_count && tick < end_tick; ++fi) {
      Tick dur = std::min(kWholeNote, end_tick - tick);
      if (dur == 0) break;

      uint8_t fade_pitch = chord_tones_v1[0];
      if (chord_tones_v1.size() >= 2 && fi > 0) {
        fade_pitch = chord_tones_v1[1];
      }

      notes.push_back(makeNote(tick, dur, fade_pitch, 1,
                               BachNoteSource::ToccataGesture));
      tick += dur;

      if (fi == 0 && fade_count > 1) {
        tick += std::min(kHalfNote, end_tick - tick);
      }
    }
  }

  return notes;
}

// Phase C: free recitative expansion (i -> viio7 -> V -> VI deceptive).
// Alternates melody between voices every 2 bars; grand pause at ~50%.
std::vector<NoteEvent> generateRecitativeExpansion(const HarmonicTimeline& timeline,
                                                   const KeySignature& key_sig,
                                                   Tick start_tick, Tick end_tick,
                                                   std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  if (start_tick >= end_tick) return notes;

  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = std::min(getToccataHighPitch(0), static_cast<uint8_t>(86));
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = std::min(getToccataHighPitch(1), static_cast<uint8_t>(86));

  Tick phase_dur = end_tick - start_tick;
  Tick phase_bars = phase_dur / kTicksPerBar;
  if (phase_bars == 0) return notes;

  Tick gp_bar = phase_bars / 2;

  auto scale0 = getScaleTones(key_sig.tonic, key_sig.is_minor, low0, high0);
  auto scale1 = getScaleTones(key_sig.tonic, key_sig.is_minor, low1, high1);
  if (scale0.empty() || scale1.empty()) return notes;

  ScaleType sc_type = key_sig.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  for (Tick bar = 0; bar < phase_bars; ++bar) {
    Tick bar_start = start_tick + bar * kTicksPerBar;
    Tick bar_end = bar_start + kTicksPerBar;

    // Grand pause at midpoint.
    if (bar == gp_bar) {
      Tick half_start = bar_start + kGrandPauseDuration;
      if (half_start >= bar_end) continue;

      const HarmonicEvent& event = timeline.getAt(half_start);
      auto chord_tones = getChordTones(event.chord, 4);
      if (chord_tones.size() >= 2) {
        size_t ct_idx = (chord_tones.size() >= 3) ? 2 : 1;
        uint8_t chosen = clampPitch(static_cast<int>(chord_tones[ct_idx]), low0, high0);
        notes.push_back(makeNote(half_start, bar_end - half_start, chosen, 0,
                                 BachNoteSource::FreeCounterpoint));
      }
      continue;
    }

    // Swap melody voice every 2 bars for manual contrast.
    uint8_t melody_voice = ((bar / 2) % 2 == 0) ? 0 : 1;
    uint8_t chord_voice = 1 - melody_voice;
    uint8_t ch_low = (chord_voice == 0) ? low0 : low1;
    uint8_t ch_high = (chord_voice == 0) ? high0 : high1;

    // Melody line: figuration with free rhythm (recitative).
    {
      auto fig = generateFigured(timeline, key_sig, 2, melody_voice,
                                  bar_start, bar_end, rng);
      notes.insert(notes.end(), fig.begin(), fig.end());
    }

    // Chord layer: sustained diatonic tones.
    const auto& ch_scale = (chord_voice == 0) ? scale0 : scale1;
    Tick tick = bar_start;
    while (tick < bar_end) {
      const HarmonicEvent& event = timeline.getAt(tick);
      auto chord_tones = getChordTones(event.chord, 3);

      std::vector<uint8_t> valid;
      for (auto tone : chord_tones) {
        if (tone >= ch_low && tone <= ch_high &&
            scale_util::isScaleTone(tone, key_sig.tonic, sc_type)) {
          valid.push_back(tone);
        }
      }
      if (valid.empty()) {
        chord_tones = getChordTones(event.chord, 4);
        for (auto tone : chord_tones) {
          if (tone >= ch_low && tone <= ch_high &&
              scale_util::isScaleTone(tone, key_sig.tonic, sc_type)) {
            valid.push_back(tone);
          }
        }
      }
      if (valid.empty()) {
        uint8_t root = clampPitch(static_cast<int>(event.bass_pitch), ch_low, ch_high);
        for (const auto& st : ch_scale) {
          if (st >= root) { valid.push_back(st); break; }
        }
        if (valid.empty() && !ch_scale.empty()) {
          valid.push_back(ch_scale.back());
        }
      }

      // Whole notes only -- sparse chord pad for recitative texture.
      Tick dur = kWholeNote;
      if (tick + dur > bar_end) dur = bar_end - tick;
      if (dur == 0) break;

      notes.push_back(makeNote(tick, dur, rng::selectRandom(rng, valid),
                               chord_voice, BachNoteSource::FreeCounterpoint));
      tick += dur;
    }
  }

  return notes;
}

// Phase E: dramatic harmonic pause/valley (viio7 -> V).
// Sustained dark chords, grand pause, then sparse dominant arpeggiation.
std::vector<NoteEvent> generateHarmonicBreak(const HarmonicTimeline& timeline,
                                             const KeySignature& key_sig,
                                             Tick start_tick, Tick end_tick,
                                             std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  if (start_tick >= end_tick) return notes;

  Tick phase_dur = end_tick - start_tick;
  Tick dim_end = start_tick + phase_dur / 2;
  Tick pause_end = dim_end + kGrandPauseDuration;
  if (pause_end >= end_tick) {
    pause_end = dim_end;
  }

  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = std::min(getToccataHighPitch(0), static_cast<uint8_t>(84));
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = std::min(getToccataHighPitch(1), static_cast<uint8_t>(84));

  // First half: sustained viio7 chord in close position.
  if (dim_end > start_tick) {
    const HarmonicEvent& dim_event = timeline.getAt(start_tick);
    auto tones_v0 = collectChordTonesInRange(dim_event.chord, low0, high0);
    auto tones_v1 = collectChordTonesInRange(dim_event.chord, low1, high1);

    Tick tick = start_tick;
    while (tick < dim_end) {
      Tick note_dur = std::min(kWholeNote, dim_end - tick);
      if (note_dur == 0) break;

      if (!tones_v0.empty()) {
        size_t idx = tones_v0.size() / 2;
        if (tones_v0.size() > 1) {
          idx = rng::rollRange(rng, static_cast<int>(tones_v0.size() / 2),
                               static_cast<int>(tones_v0.size()) - 1);
        }
        notes.push_back(makeNote(tick, note_dur, tones_v0[idx], 0,
                                 BachNoteSource::ToccataGesture));
      }

      if (!tones_v1.empty()) {
        size_t idx = 0;
        if (tones_v1.size() > 1) {
          idx = rng::rollRange(rng, 0,
                               static_cast<int>(tones_v1.size() / 2));
        }
        notes.push_back(makeNote(tick, note_dur, tones_v1[idx], 1,
                                 BachNoteSource::ToccataGesture));
      }

      tick += note_dur;
    }
  }

  // Grand pause: silence between dim_end and pause_end.

  // Second half: V chord quarter-note arpeggiation on voice 0.
  if (pause_end < end_tick) {
    const HarmonicEvent& dom_event = timeline.getAt(pause_end);
    auto dom_tones_v0 = collectChordTonesInRange(dom_event.chord, low0, high0);
    if (dom_tones_v0.empty()) return notes;

    auto fig = generateFigured(timeline, key_sig, 4, 0, pause_end, end_tick, rng);
    notes.insert(notes.end(), fig.begin(), fig.end());

    // Voice 1 sustained chord tone in the final bar.
    Tick final_bar_start = end_tick - kTicksPerBar;
    if (final_bar_start >= pause_end && final_bar_start < end_tick) {
      auto dom_tones_v1 = collectChordTonesInRange(dom_event.chord, low1, high1);
      if (!dom_tones_v1.empty()) {
        Tick sustain_dur = std::min(kWholeNote, end_tick - final_bar_start);
        if (sustain_dur > 0) {
          notes.push_back(makeNote(final_bar_start, sustain_dur,
                                   dom_tones_v1[0], 1,
                                   BachNoteSource::ToccataGesture));
        }
      }
    }
  }

  return notes;
}

// Phase G: obsessive dominant prolongation building maximum tension.
// 5 sub-phases: V assertion -> viio7/V -> V return -> V7 intensification -> V sustained.
std::vector<NoteEvent> generateDominantObsession(const HarmonicTimeline& timeline,
                                                 const KeySignature& key_sig,
                                                 uint8_t num_voices,
                                                 Tick start_tick, Tick end_tick,
                                                 std::mt19937& rng) {
  (void)num_voices;  // Reserved for future multi-voice sub-phases.
  std::vector<NoteEvent> notes;
  Tick phase_dur = end_tick - start_tick;
  if (phase_dur == 0) return notes;

  constexpr uint8_t kCeiling = 96;

  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = std::min(getToccataHighPitch(0), kCeiling);
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = std::min(getToccataHighPitch(1), kCeiling);

  Tick sp1_end = start_tick + phase_dur * 20 / 100;
  Tick sp2_end = start_tick + phase_dur * 40 / 100;
  Tick sp3_end = start_tick + phase_dur * 60 / 100;
  Tick sp4_end = start_tick + phase_dur * 80 / 100;

  auto scale0 = getScaleTones(key_sig.tonic, key_sig.is_minor, low0, high0);
  auto scale1 = getScaleTones(key_sig.tonic, key_sig.is_minor, low1, high1);
  if (scale0.empty() || scale1.empty()) return notes;

  // Sub-phase 1: V assertion -- figuration with chord attraction.
  {
    auto fig0 = generateFigured(timeline, key_sig, 6, 0, start_tick, sp1_end, rng);
    auto fig1 = generateFigured(timeline, key_sig, 6, 1, start_tick, sp1_end, rng);
    notes.insert(notes.end(), fig0.begin(), fig0.end());
    notes.insert(notes.end(), fig1.begin(), fig1.end());
  }

  // Sub-phase 2: viio7/V color -- figuration with high tension.
  {
    auto fig0 = generateFigured(timeline, key_sig, 6, 0, sp1_end, sp2_end, rng);
    auto fig1 = generateFigured(timeline, key_sig, 6, 1, sp1_end, sp2_end, rng);
    notes.insert(notes.end(), fig0.begin(), fig0.end());
    notes.insert(notes.end(), fig1.begin(), fig1.end());
  }

  // Sub-phase 3: V return -- ascending figuration toward ceiling.
  {
    auto fig0 = generateFigured(timeline, key_sig, 6, 0, sp2_end, sp3_end, rng);
    auto fig1 = generateFigured(timeline, key_sig, 6, 1, sp2_end, sp3_end, rng);
    notes.insert(notes.end(), fig0.begin(), fig0.end());
    notes.insert(notes.end(), fig1.begin(), fig1.end());
  }

  // Sub-phase 4: V7 intensification -- voice 0 only (single-voice density).
  // Voice 1 drops out in the last 30% of the phase for texture reduction.
  {
    auto fig0 = generateFigured(timeline, key_sig, 6, 0, sp3_end, sp4_end, rng);
    notes.insert(notes.end(), fig0.begin(), fig0.end());
  }

  // Sub-phase 5: V sustained -- voice 0 held dominant chord tones (tension plateau).
  // Single voice sustain narrows texture toward the cadence.
  {
    const HarmonicEvent& evt = timeline.getAt(sp4_end);
    auto ct0 = collectChordTonesInRange(evt.chord, low0, high0);

    uint8_t sustained0 = !ct0.empty() ? ct0.back() : high0;

    Tick tick = sp4_end;
    while (tick < end_tick) {
      Tick dur = std::min(kHalfNote, end_tick - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur, sustained0, 0,
                               BachNoteSource::ToccataGesture));
      tick += dur;
    }
  }

  return notes;
}

// Phase H: final cadential explosion -- V7 arpeggio sweep -> transition -> I Major chord.
std::vector<NoteEvent> generateFinalExplosion(const HarmonicTimeline& timeline,
                                              const KeySignature& key_sig,
                                              uint8_t num_voices,
                                              Tick start_tick, Tick end_tick,
                                              std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  Tick phase_dur = end_tick - start_tick;
  if (phase_dur == 0) return notes;

  constexpr uint8_t kCeiling = 96;
  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);

  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = std::min(getToccataHighPitch(0), kCeiling);
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = std::min(getToccataHighPitch(1), kCeiling);

  Tick sweep_end = start_tick + phase_dur * 70 / 100;
  Tick trans_end = start_tick + phase_dur * 80 / 100;

  // Sub-phase 1: V7 arpeggio sweep -- figuration at maximum density.
  {
    auto fig0 = generateFigured(timeline, key_sig, 7, 0, start_tick, sweep_end, rng);
    auto fig1 = generateFigured(timeline, key_sig, 7, 1, start_tick, sweep_end, rng);
    notes.insert(notes.end(), fig0.begin(), fig0.end());
    notes.insert(notes.end(), fig1.begin(), fig1.end());
  }

  // Sub-phase 2: descending figuration transition.
  {
    auto fig0 = generateFigured(timeline, key_sig, 7, 0, sweep_end, trans_end, rng);
    auto fig1 = generateFigured(timeline, key_sig, 7, 1, sweep_end, trans_end, rng);
    notes.insert(notes.end(), fig0.begin(), fig0.end());
    notes.insert(notes.end(), fig1.begin(), fig1.end());
  }

  // Sub-phase 3: final I Major block chord (Picardy third).
  {
    Tick chord_dur = end_tick - trans_end;
    if (chord_dur == 0) return notes;

    uint8_t fifth = clampPitch(60 + (tpc + 7) % 12, low0, high0);
    uint8_t major_third = clampPitch(60 + (tpc + 4) % 12, low1, high1);

    Tick tick = trans_end;
    while (tick < end_tick) {
      Tick dur = std::min(kWholeNote, end_tick - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur, fifth, 0,
                               BachNoteSource::ToccataGesture));
      notes.push_back(makeNote(tick, dur, major_third, 1,
                               BachNoteSource::ToccataGesture));
      tick += dur;
    }

    if (num_voices >= 3) {
      uint8_t bass_root = clampPitch(36 + tpc,
                                     organ_range::kPedalLow,
                                     organ_range::kPedalHigh);
      Tick pedal_tick = trans_end;
      while (pedal_tick < end_tick) {
        Tick dur = std::min(kWholeNote, end_tick - pedal_tick);
        if (dur == 0) break;
        notes.push_back(makeNote(pedal_tick, dur, bass_root, 2,
                                 BachNoteSource::PedalPoint));
        pedal_tick += dur;
      }
    }
  }

  return notes;
}

// Energy-aware pedal (voice 2) for all 8 Dramaticus phases.
// Behavior varies by kDramaticusEnergy[phase_idx]:
//   < 0.4  -> sustained whole notes (B, E)
//   0.4-0.7 -> T-D alternation in whole notes (C)
//   0.7-0.85 -> T-D-SD quarter note pattern (D)
//   >= 0.85 -> 8th-note rhythmic T-D (F, G, H)
//   Phase A (idx=0) -> special: harmonic timeline bass in whole notes
std::vector<NoteEvent> generatePhasePedal(const HarmonicTimeline& timeline,
                                          const KeySignature& key_sig,
                                          int phase_idx,
                                          Tick start_tick, Tick end_tick,
                                          std::mt19937& /*rng*/) {
  std::vector<NoteEvent> notes;
  if (start_tick >= end_tick) return notes;
  if (phase_idx < 0 || phase_idx > 7) return notes;

  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  uint8_t tonic = clampPitch(36 + tpc,
                             organ_range::kPedalLow, organ_range::kPedalHigh);
  uint8_t dominant = clampPitch(
      36 + static_cast<int>(getDominant(key_sig).tonic),
      organ_range::kPedalLow, organ_range::kPedalHigh);
  uint8_t subdominant = clampPitch(
      36 + static_cast<int>(getSubdominant(key_sig).tonic),
      organ_range::kPedalLow, organ_range::kPedalHigh);

  float energy = kDramaticusEnergy[phase_idx];

  // Phase A (Gesture): follow harmonic timeline bass in whole notes.
  if (phase_idx == 0) {
    Tick tick = start_tick;
    while (tick < end_tick) {
      const HarmonicEvent& evt = timeline.getAt(tick);
      uint8_t bass = clampPitch(static_cast<int>(evt.bass_pitch),
                                organ_range::kPedalLow, organ_range::kPedalHigh);
      Tick dur = std::min(kWholeNote, end_tick - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
      tick += dur;
    }
    return notes;
  }

  // Low energy (< 0.4): Phases B and E -- sustained whole notes.
  if (energy < 0.4f) {
    Tick tick = start_tick;
    while (tick < end_tick) {
      const HarmonicEvent& evt = timeline.getAt(tick);
      uint8_t bass = clampPitch(static_cast<int>(evt.bass_pitch),
                                organ_range::kPedalLow, organ_range::kPedalHigh);
      Tick dur = std::min(kWholeNote, end_tick - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
      tick += dur;
    }
    return notes;
  }

  // Medium energy (0.4-0.7): Phase C -- T-D alternation in whole notes.
  if (energy < 0.7f) {
    Tick tick = start_tick;
    bool use_tonic = true;
    while (tick < end_tick) {
      Tick dur = std::min(kWholeNote, end_tick - tick);
      if (dur == 0) break;

      const HarmonicEvent& evt = timeline.getAt(tick);
      uint8_t bass = clampPitch(static_cast<int>(evt.bass_pitch),
                                organ_range::kPedalLow, organ_range::kPedalHigh);
      uint8_t pitch = use_tonic ? tonic : dominant;
      if (std::abs(static_cast<int>(bass) - static_cast<int>(pitch)) > 2) {
        pitch = bass;
      }

      notes.push_back(makeNote(tick, dur, pitch, 2, BachNoteSource::PedalPoint));
      tick += dur;
      use_tonic = !use_tonic;
    }
    return notes;
  }

  // High energy (>= 0.85): Phases F, G, H -- 8th-note rhythmic pedal.
  if (energy >= 0.85f) {
    Tick tick = start_tick;
    while (tick < end_tick) {
      Tick dur = std::min(kEighthNote, end_tick - tick);
      if (dur == 0) break;

      const HarmonicEvent& evt = timeline.getAt(tick);
      uint8_t timeline_bass = clampPitch(static_cast<int>(evt.bass_pitch),
                                         organ_range::kPedalLow,
                                         organ_range::kPedalHigh);

      bool beat_even = ((tick / kEighthNote) % 2 == 0);
      uint8_t pitch = beat_even ? tonic : dominant;
      if (std::abs(static_cast<int>(timeline_bass) - static_cast<int>(tonic)) > 2 &&
          std::abs(static_cast<int>(timeline_bass) - static_cast<int>(dominant)) > 2) {
        pitch = timeline_bass;
      }

      notes.push_back(makeNote(tick, dur, pitch, 2, BachNoteSource::PedalPoint));
      tick += dur;
    }
    return notes;
  }

  // Medium-high energy (0.7-0.85): Phase D -- T-D-SD quarter note pattern.
  {
    struct PedalStep { uint8_t pitch; };
    PedalStep pattern[] = {{tonic}, {dominant}, {subdominant}, {tonic}};
    constexpr size_t kPatternLen = 4;

    Tick tick = start_tick;
    size_t pat_idx = 0;
    while (tick < end_tick) {
      Tick dur = std::min(kQuarterNote, end_tick - tick);
      if (dur == 0) break;

      const HarmonicEvent& evt = timeline.getAt(tick);
      uint8_t timeline_bass = clampPitch(static_cast<int>(evt.bass_pitch),
                                         organ_range::kPedalLow,
                                         organ_range::kPedalHigh);

      uint8_t pitch = pattern[pat_idx % kPatternLen].pitch;
      if (std::abs(static_cast<int>(timeline_bass) - static_cast<int>(tonic)) > 2 &&
          std::abs(static_cast<int>(timeline_bass) - static_cast<int>(dominant)) > 2 &&
          std::abs(static_cast<int>(timeline_bass) - static_cast<int>(subdominant)) > 2) {
        pitch = timeline_bass;
      }

      notes.push_back(makeNote(tick, dur, pitch, 2, BachNoteSource::PedalPoint));
      tick += dur;
      ++pat_idx;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

uint8_t clampToccataVoiceCount(uint8_t num_voices) {
  if (num_voices < kMinVoices) return kMinVoices;
  if (num_voices > kMaxVoices) return kMaxVoices;
  return num_voices;
}

}  // namespace

// ---------------------------------------------------------------------------
// Dramaticus archetype (BWV 565 style)
// ---------------------------------------------------------------------------

ToccataResult generateDramaticusToccata(const ToccataConfig& config) {
  ToccataResult result;
  result.success = false;

  uint8_t num_voices = clampToccataVoiceCount(config.num_voices);

  if (config.total_bars <= 0) {
    result.error_message = "total_bars must be positive";
    return result;
  }

  std::mt19937 rng(config.seed);

  // --- 1. Bars allocation (8-phase discrete) ---
  int effective_bars = config.total_bars;
  std::vector<float> fallback_props(
      std::begin(kDramaticusFallbackProportions),
      std::end(kDramaticusFallbackProportions));
  std::vector<Tick> min_bars(
      std::begin(kDramaticusMinBars), std::end(kDramaticusMinBars));

  auto phase_bars = toccata_internal::allocateBarsDiscrete(
      effective_bars, kDramaticusBarsTables, fallback_props, min_bars);

  // --- 2. Phase boundaries ---
  std::vector<ToccataSectionId> phase_ids(
      std::begin(kDramaticusPhaseIds), std::end(kDramaticusPhaseIds));
  auto phases = toccata_internal::buildSectionBoundaries(phase_bars, phase_ids);

  Tick total_duration = static_cast<Tick>(effective_bars) * kTicksPerBar;

  // --- 3. Harmonic plan ---
  HarmonicTimeline timeline =
      buildDramaticusHarmonicPlan(config.key, phases);

  // --- 4. Phase generation (rng consumed in deterministic A-H order) ---
  // A: Gesture
  auto a_notes = generateOpeningGesture(
      timeline, config.key, phases[0].start, phases[0].end, rng);

  // B: EchoCollapse
  auto b_notes = generateEchoCollapse(
      timeline, config.key, phases[1].start, phases[1].end, rng);

  // C: RecitExpansion
  auto c_notes = generateRecitativeExpansion(
      timeline, config.key, phases[2].start, phases[2].end, rng);

  // D: SequenceClimb1 (motif extracted from A, then vocabulary-enriched)
  ClimbingMotif motif = extractClimbingMotif(a_notes, 0);
  ScaleType climb_scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  // Apply vocabulary gesture: ascending direction (climb from low register toward ceiling).
  motif = tryVocabularyGesture(
      motif, config.archetype,
      static_cast<int>(getToccataLowPitch(0)),
      static_cast<int>(kDramaticusRangeCeiling[3]),
      config.key.tonic, climb_scale, rng);
  auto d_notes = generateSequenceClimb(
      timeline, config.key, motif,
      phases[3].start, phases[3].end,
      kDramaticusRangeCeiling[3], 3, rng);

  // E: HarmonicBreak
  auto e_notes = generateHarmonicBreak(
      timeline, config.key, phases[4].start, phases[4].end, rng);

  // F: SequenceClimb2
  auto f_notes = generateSequenceClimb(
      timeline, config.key, motif,
      phases[5].start, phases[5].end,
      kDramaticusRangeCeiling[5], 5, rng);

  // G: DomObsession
  auto g_notes = generateDominantObsession(
      timeline, config.key, num_voices,
      phases[6].start, phases[6].end, rng);

  // H: FinalExplosion
  auto h_notes = generateFinalExplosion(
      timeline, config.key, num_voices,
      phases[7].start, phases[7].end, rng);

  // --- 5. Ornaments per phase ---
  struct PhaseRef {
    std::vector<NoteEvent>* notes;
    VoiceRole role;
  };
  PhaseRef phase_refs[8] = {
      {&a_notes, VoiceRole::Propel},   {&b_notes, VoiceRole::Respond},
      {&c_notes, VoiceRole::Respond},  {&d_notes, VoiceRole::Propel},
      {&e_notes, VoiceRole::Respond},  {&f_notes, VoiceRole::Propel},
      {&g_notes, VoiceRole::Propel},   {&h_notes, VoiceRole::Propel},
  };

  for (int i = 0; i < 8; ++i) {
    OrnamentContext ctx;
    ctx.config.ornament_density = kDramaticusOrnament[i];
    ctx.role = phase_refs[i].role;
    ctx.seed = config.seed;
    ctx.timeline = &timeline;
    ctx.cadence_ticks = {phases[i].end};
    *phase_refs[i].notes = applyOrnaments(*phase_refs[i].notes, ctx);
  }

  // --- 5b. Tag gesture notes (after ornaments so metadata survives) ---
  constexpr uint16_t kOpeningGestureId = 1;
  tagGestureNotes(a_notes, kOpeningGestureId);
  result.core_intervals = extractGestureCoreIntervals(a_notes, kOpeningGestureId);

  // --- 6. Merge all phase notes ---
  std::vector<NoteEvent> all_notes;
  auto append = [&](const std::vector<NoteEvent>& src) {
    all_notes.insert(all_notes.end(), src.begin(), src.end());
  };
  append(a_notes);
  append(b_notes);
  append(c_notes);
  append(d_notes);
  append(e_notes);
  append(f_notes);
  append(g_notes);
  append(h_notes);

  // --- 7. Pedal (voice 2) per phase ---
  if (num_voices >= 3) {
    for (int i = 0; i < 8; ++i) {
      auto pedal = generatePhasePedal(
          timeline, config.key, i,
          phases[i].start, phases[i].end, rng);
      all_notes.insert(all_notes.end(), pedal.begin(), pedal.end());
    }
  }

  // --- 8. Tag untagged notes ---
  assert(countUnknownSource(all_notes) == 0 &&
         "All notes should have source set by generators");

  // --- 9. Constraint-driven finalize ---
  {
    auto voice_range = [](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      return {getToccataLowPitch(v), getToccataHighPitch(v)};
    };
    ScaleType toc_scale = config.key.is_minor ? ScaleType::HarmonicMinor
                                              : ScaleType::Major;
    finalizeFormNotes(all_notes, num_voices, voice_range, config.key.tonic,
                      toc_scale, /*max_consecutive=*/2);
  }

  // --- 10. Tracks ---
  std::vector<Track> tracks = createToccataTracks(num_voices);
  toccata_internal::assignNotesToTracks(all_notes, tracks);

  // --- 11. Registration (phase-level velocity) ---
  ExtendedRegistrationPlan reg_plan;
  for (int i = 0; i < 8 && i < static_cast<int>(phases.size()); ++i) {
    Registration reg;
    reg.velocity_hint = kDramaticusVelocity[i];
    std::string label = "phase_" + std::string(1, 'A' + static_cast<char>(i));
    reg_plan.addPoint(phases[i].start, reg, label);
  }
  applyExtendedRegistrationPlan(tracks, reg_plan);

  // --- 12. Sort + overlap cleanup ---
  form_utils::sortTrackNotes(tracks);
  toccata_internal::cleanupToccataOverlaps(tracks);

  // --- 13. Picardy third ---
  if (config.enable_picardy && config.key.is_minor) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               total_duration - kTicksPerBar);
    }
  }

  // --- 14. Result ---
  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.archetype = ToccataArchetype::Dramaticus;
  result.phases = phases;
  result.sections = buildLegacySectionsFromPhases(phases);
  toccata_internal::populateLegacyFields(result);
  result.success = true;

  return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ToccataResult generateToccata(const ToccataConfig& config) {
  switch (config.archetype) {
    case ToccataArchetype::Dramaticus:
      return generateDramaticusToccata(config);
    case ToccataArchetype::Perpetuus:
      return generatePerpetuusToccata(config);
    case ToccataArchetype::Concertato:
      return generateConcertatoToccata(config);
    case ToccataArchetype::Sectionalis:
      return generateSectionalisToccata(config);
  }
  // Unreachable, but satisfy compiler.
  ToccataResult result;
  result.error_message = "Unknown toccata archetype";
  return result;
}

}  // namespace bach
