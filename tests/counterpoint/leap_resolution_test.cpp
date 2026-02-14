// Tests for counterpoint/leap_resolution.h -- unresolved leap detection
// and scale-aware resolution.

#include "counterpoint/leap_resolution.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/scale.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

LeapResolutionParams makeDefaultParams(uint8_t num_voices) {
  LeapResolutionParams params;
  params.num_voices = num_voices;
  params.key_at_tick = [](Tick) { return Key::C; };
  params.scale_at_tick = [](Tick) { return ScaleType::Major; };
  return params;
}

NoteEvent makeNote(Tick tick, uint8_t pitch, uint8_t voice,
                   BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  NoteEvent n;
  n.start_tick = tick;
  n.duration = kTicksPerBeat;
  n.pitch = pitch;
  n.voice = voice;
  n.source = source;
  return n;
}

bool hasLeapFlag(const NoteEvent& note) {
  return (note.modified_by & static_cast<uint8_t>(NoteModifiedBy::LeapResolution)) != 0;
}

// Convenience: place notes on off-beat positions by default so strong-beat
// protection doesn't interfere. beat=0 is strong, use beat 1 (= tick 480).
NoteEvent offBeat(int index, uint8_t pitch, uint8_t voice = 0,
                  BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  // Offset from beat 0 by using eighth-note-like ticks starting at tick 240.
  Tick tick = static_cast<Tick>(index) * kTicksPerBeat + kTicksPerBeat / 2;
  return makeNote(tick, pitch, voice, source);
}

// Sort notes by (voice, start_tick) to find a particular note.
const NoteEvent& findNote(const std::vector<NoteEvent>& notes,
                          uint8_t voice, int index) {
  std::vector<const NoteEvent*> voice_notes;
  for (const auto& n : notes) {
    if (n.voice == voice) voice_notes.push_back(&n);
  }
  std::sort(voice_notes.begin(), voice_notes.end(),
            [](const NoteEvent* a, const NoteEvent* b) {
              return a->start_tick < b->start_tick;
            });
  return *voice_notes[static_cast<size_t>(index)];
}

// ---------------------------------------------------------------------------
// Basic tests
// ---------------------------------------------------------------------------

TEST(LeapResolution, EmptyNotes) {
  std::vector<NoteEvent> notes;
  auto params = makeDefaultParams(1);
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

TEST(LeapResolution, TwoNotes) {
  // Only 2 notes -> can't form a triplet -> no resolution needed.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 72)};
  auto params = makeDefaultParams(1);
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

TEST(LeapResolution, NoLeap) {
  // Stepwise motion -> no leap to resolve.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 62), offBeat(2, 64)};
  auto params = makeDefaultParams(1);
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

TEST(LeapResolution, AlreadyResolvedChordTone) {
  // C4->G4 (P5 up), then F4 (step down, contrary) -- resolved.
  // With is_chord_tone saying F is a chord tone.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 65)};
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t p) { return p == 65; };  // F is chord tone.
  EXPECT_EQ(resolveLeaps(notes, params), 0);
  EXPECT_EQ(notes[2].pitch, 65);  // Unchanged.
}

TEST(LeapResolution, UnresolvedLeapFixed) {
  // C4->G4 (P5 up), then A4 (same direction, not resolved).
  // Should fix A4 to F4 or E4 (step below G4, contrary).
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 69)};
  auto params = makeDefaultParams(1);
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 1);
  // resolve_dir = -1 (leap was up), try offset 1: 67-1=66 (not scale tone),
  // offset 2: 67-2=65 (F, scale tone).
  EXPECT_EQ(findNote(notes, 0, 2).pitch, 65);  // F4.
  EXPECT_TRUE(hasLeapFlag(findNote(notes, 0, 2)));
}

TEST(LeapResolution, UpwardLeapResolvesDown) {
  // C4->A4 (M6 up = 9st), then B4 (same dir). Should resolve down.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 69), offBeat(2, 71)};
  auto params = makeDefaultParams(1);
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 1);
  // resolve_dir = -1, offset 1: 69-1=68 (Ab, not C major),
  // offset 2: 69-2=67 (G, yes).
  EXPECT_EQ(findNote(notes, 0, 2).pitch, 67);  // G4.
}

TEST(LeapResolution, DownwardLeapResolvesUp) {
  // G4->C4 (P5 down = 7st), then Bb3 (same dir, down). Should resolve up.
  std::vector<NoteEvent> notes = {offBeat(0, 67), offBeat(1, 60), offBeat(2, 58)};
  auto params = makeDefaultParams(1);
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 1);
  // resolve_dir = +1, offset 1: 60+1=61 (not C major),
  // offset 2: 60+2=62 (D, yes).
  EXPECT_EQ(findNote(notes, 0, 2).pitch, 62);  // D4.
}

// ---------------------------------------------------------------------------
// Theory review FB tests
// ---------------------------------------------------------------------------

TEST(LeapResolution, FB1_FalseResolutionDetected) {
  // C4->G4 (P5 up), then F#4(66) -- contrary step (down by 1) but F# is NOT
  // in C major scale, NOT chord tone, NOT tendency. The algorithm should
  // find the proper diatonic resolution (F4=65) and apply it.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 66)};
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t) { return false; };  // Nothing is chord tone.
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 1);
  EXPECT_EQ(findNote(notes, 0, 2).pitch, 65);  // F4: correct diatonic resolution.
}

TEST(LeapResolution, FB1_TendencyResolutionPreserved) {
  // A4(69)->B3(59), M7 down (10st). notes[k+2] = C4(60).
  // Motion = 60-59 = +1 (up). Contrary to down. Step (|1|<=2).
  // isTendencyResolution(59, 60, C, Major): B->C = leading tone -> tonic.
  // -> should be preserved (not modified).
  std::vector<NoteEvent> notes = {offBeat(0, 69), offBeat(1, 59), offBeat(2, 60)};
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t) { return false; };
  EXPECT_EQ(resolveLeaps(notes, params), 0);
  EXPECT_EQ(notes[2].pitch, 60);  // Preserved: B->C is leading tone -> tonic.
}

TEST(LeapResolution, FB1_FaMiResolutionPreserved) {
  // B3(59)->F4(65), tritone up (6st >= 5).
  // notes[k+2] = E4(64). Motion = 64-65 = -1 (down). Contrary to UP leap.
  // Step? |1| <= 2.
  // isTendencyResolution(65, 64, C, Major): F->E = fa->mi.
  // -> preserved.
  std::vector<NoteEvent> notes = {offBeat(0, 59), offBeat(1, 65), offBeat(2, 64)};
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t) { return false; };
  EXPECT_EQ(resolveLeaps(notes, params), 0);
  EXPECT_EQ(notes[2].pitch, 64);  // Preserved: F->E is fa->mi.
}

TEST(LeapResolution, FB1_NoChordToneFallback) {
  // When is_chord_tone is null, contrary step alone = resolved.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 65)};
  auto params = makeDefaultParams(1);
  // is_chord_tone is null (default).
  EXPECT_EQ(resolveLeaps(notes, params), 0);
  EXPECT_EQ(notes[2].pitch, 65);  // Preserved: contrary step without harmonic check.
}

TEST(LeapResolution, FB2_ZigzagNotProtected) {
  // Zigzag: notes go up-down-up. The step chain protection (P4) only protects
  // 2+ consecutive SAME-DIRECTION steps. Zigzag is not protected.
  // C4->G4 (P5 up), then A4 (up, same dir, not resolved).
  std::vector<NoteEvent> notes = {
      offBeat(0, 60), offBeat(1, 67), offBeat(2, 69),  // leap + same dir
      offBeat(3, 67), offBeat(4, 69)  // zigzag after
  };
  auto params = makeDefaultParams(1);
  // dir_a = sign(69-67) = +1, dir_b = sign(67-69) = -1. Not same -> P4 doesn't protect.
  int modified = resolveLeaps(notes, params);
  EXPECT_GE(modified, 1);  // notes[2] should be modified.
}

TEST(LeapResolution, FB3_StrongBeatProtected) {
  // notes[k+2] on a strong beat (tick % kTicksPerBeat == 0) + non-leading-tone
  // -> protected.
  std::vector<NoteEvent> notes = {
      makeNote(240, 60, 0),    // off-beat
      makeNote(480, 67, 0),    // beat 1 (on-beat but this is k+1)
      makeNote(960, 69, 0)     // beat 2 (strong beat, this is k+2, non-leading-tone A)
  };
  auto params = makeDefaultParams(1);
  EXPECT_EQ(resolveLeaps(notes, params), 0);  // Protected: strong beat + non-leading-tone.
}

TEST(LeapResolution, FB3_LeadingToneOnStrongBeatNotProtected) {
  // notes[k+2] on strong beat, but it's a leading tone AND leap >= 7st.
  // C4->G4 (P5=7st up), then B4 on strong beat.
  // B (pitch class 11) is leading tone in C major. abs_leap = 7 >= 7. -> NOT protected.
  std::vector<NoteEvent> notes = {
      makeNote(240, 60, 0),    // off-beat
      makeNote(720, 67, 0),    // off-beat (this is k+1)
      makeNote(960, 71, 0)     // strong beat, B4=leading tone, k+2
  };
  auto params = makeDefaultParams(1);
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 1);  // Modified: strong-beat leading tone after large leap.
}

// ---------------------------------------------------------------------------
// Stabilization patch tests
// ---------------------------------------------------------------------------

TEST(LeapResolution, SPA_BarlineCrossTonicProtected) {
  // notes[k+1] in bar 0, notes[k+2] in bar 1. Next bar is tonic -> protect.
  std::vector<NoteEvent> notes = {
      offBeat(0, 60),      // bar 0
      offBeat(2, 67),      // bar 0 (tick ~1200)
      makeNote(1920 + 240, 69, 0)  // bar 1, off-beat (tick 2160)
  };
  auto params = makeDefaultParams(1);
  // is_chord_tone: tonic of C major at bar 1 tick -> pitch C (60) is chord tone.
  // The PA check tests: is_chord_tone(notes[i2].start_tick, static_cast<uint8_t>(next_key))
  // next_key = Key::C = 0, so checks is_chord_tone(2160, 0).
  // We say pitch 0 (C in octave -1) is chord tone -> tonic cadence.
  params.is_chord_tone = [](Tick, uint8_t p) { return p == 0 || p == 60 || p == 72; };
  EXPECT_EQ(resolveLeaps(notes, params), 0);  // Protected: tonic cadence bar crossing.
}

TEST(LeapResolution, SPA_BarlineCrossNonTonicAllowed) {
  // Same bar crossing but next bar is NOT tonic -> modification allowed.
  std::vector<NoteEvent> notes = {
      offBeat(0, 60),
      offBeat(2, 67),
      makeNote(1920 + 240, 69, 0)  // bar 1, off-beat
  };
  auto params = makeDefaultParams(1);
  // is_chord_tone: returns false for tonic pitch -> non-tonic bar.
  params.is_chord_tone = [](Tick, uint8_t p) { return p == 67; };  // G is CT, but not C.
  int modified = resolveLeaps(notes, params);
  EXPECT_GE(modified, 1);
}

TEST(LeapResolution, SPB_PreviousHarmonyResolution) {
  // Contrary step but notes[k+2] is NOT chord tone at its own tick,
  // but IS chord tone at notes[k+1]'s tick -> resolved via previous harmony.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 65)};
  auto params = makeDefaultParams(1);
  // notes[i1].start_tick = offBeat(1) tick, notes[i2].start_tick = offBeat(2) tick.
  Tick tick1 = notes[1].start_tick;
  Tick tick2 = notes[2].start_tick;
  params.is_chord_tone = [tick1, tick2](Tick t, uint8_t p) {
    if (t == tick2) return false;  // Not chord tone at current tick.
    if (t == tick1 && p == 65) return true;  // F is chord tone at previous tick.
    return false;
  };
  EXPECT_EQ(resolveLeaps(notes, params), 0);  // Preserved: previous harmony resolution.
}

TEST(LeapResolution, SPC_TwoConsecutiveStepChainProtected) {
  // After a leap, k+2 moves by step, and k+3, k+4 continue in same direction
  // -> scalar run.
  // C4->G4 (P5 up), then F4(65), E4(64), D4(62) -- all descending steps.
  // dir_a = sign(65-67) = -1, dir_b = sign(64-65) = -1, dir_c = sign(62-64) = -1.
  // All same. step_a = |65-67| = 2 <= 2, step_b = |64-65| = 1 <= 2. -> Protected.
  std::vector<NoteEvent> notes = {
      offBeat(0, 60), offBeat(1, 67), offBeat(2, 65), offBeat(3, 64), offBeat(4, 62)
  };
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t) { return false; };
  // Contrary step from 67->65 (down 2) but not chord tone, not tendency.
  // But P4 (step chain) should protect because 65->64->62 is 3 same-direction steps.
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

TEST(LeapResolution, SPC_SingleStepNotProtected) {
  // After leap, k+2 steps, but k+3 changes direction -> not 2 consecutive same-dir.
  std::vector<NoteEvent> notes = {
      offBeat(0, 60), offBeat(1, 67), offBeat(2, 69),  // same dir (not resolved)
      offBeat(3, 67), offBeat(4, 69)  // direction changes
  };
  auto params = makeDefaultParams(1);
  // dir_a = +1 (69>67), dir_b = -1 (67<69). Not same -> P4 doesn't protect.
  int modified = resolveLeaps(notes, params);
  EXPECT_GE(modified, 1);
}

TEST(LeapResolution, SPD_LeadingToneSmallLeapProtected) {
  // Leap < 7st (P4 = 5st), leading tone on strong beat -> protected.
  // D4(62)->G4(67), P4 up (5st). notes[k+2] = B4(71) on strong beat.
  // B is leading tone. abs_leap = 5 < 7. -> Protected by P2.
  std::vector<NoteEvent> notes = {
      makeNote(240, 62, 0),    // off-beat
      makeNote(480, 67, 0),    // beat 1
      makeNote(960, 71, 0)     // strong beat, B = leading tone
  };
  auto params = makeDefaultParams(1);
  EXPECT_EQ(resolveLeaps(notes, params), 0);  // Protected: small leap + strong-beat LT.
}

TEST(LeapResolution, SPD_LeadingToneLargeLeapModified) {
  // Leap >= 7st (P5 = 7st), leading tone on strong beat -> NOT protected.
  // C4(60)->G4(67), P5 up (7st). notes[k+2] = B4(71) on strong beat.
  std::vector<NoteEvent> notes = {
      makeNote(240, 60, 0),
      makeNote(720, 67, 0),
      makeNote(960, 71, 0)     // strong beat, B = leading tone, but large leap
  };
  auto params = makeDefaultParams(1);
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 1);
}

// ---------------------------------------------------------------------------
// Final stabilization tests
// ---------------------------------------------------------------------------

TEST(LeapResolution, FA_SequencePatternProtected) {
  // 5-note sequence: intervals +9, -3, +9, -3 (repeating M6 pattern).
  // C4(60)->A4(69)->F#4(66)->Eb5(75)->C5(72).
  // k=0: leap 60->69 (M6=9st). notes[k+2]=66 (F#4, not in C major).
  // Without PF, the algorithm would replace 66 with G4(67).
  // PF detects the repeating interval pattern and protects notes[k+2].
  std::vector<NoteEvent> notes = {
      offBeat(0, 60), offBeat(1, 69), offBeat(2, 66), offBeat(3, 75), offBeat(4, 72)
  };
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t) { return false; };
  uint8_t original_k2 = notes[2].pitch;  // 66
  resolveLeaps(notes, params);
  // k=0 triplet: PF protects notes[2] (sequence pattern detected).
  // k=2 may modify notes[4] (PF can't check -- not enough trailing notes).
  EXPECT_EQ(findNote(notes, 0, 2).pitch, original_k2);  // 66 preserved by PF.
}

TEST(LeapResolution, FB_AlreadyModifiedSkipped) {
  // notes[k+2] has LeapResolution flag already set -> skip (vibration prevention).
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 69)};
  notes[2].modified_by = static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
  auto params = makeDefaultParams(1);
  EXPECT_EQ(resolveLeaps(notes, params), 0);  // Skipped: already modified.
  EXPECT_EQ(notes[2].pitch, 69);  // Unchanged.
}

TEST(LeapResolution, FC_SeventhToSixthResolution) {
  // E4(64)->B4(71), P5 up (7st). notes[k+2] = A4(69).
  // Motion = 69-71 = -2 (down). Contrary to up.
  // isTendencyResolution(71, 69, C, Major):
  //   prev_pc=11 (B), 7th degree. curr_pc=9 (A), 6th degree. Descending.
  // -> tendency resolution preserved.
  std::vector<NoteEvent> notes = {offBeat(0, 64), offBeat(1, 71), offBeat(2, 69)};
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t) { return false; };
  EXPECT_EQ(resolveLeaps(notes, params), 0);
  EXPECT_EQ(notes[2].pitch, 69);  // Preserved: 7th->6th tendency resolution.
}

// ---------------------------------------------------------------------------
// Protection condition tests
// ---------------------------------------------------------------------------

TEST(LeapResolution, ImmutableProtected) {
  // FugueSubject -> Immutable -> skip.
  std::vector<NoteEvent> notes = {
      offBeat(0, 60),
      offBeat(1, 67),
      offBeat(2, 69, 0, BachNoteSource::FugueSubject)  // Immutable source.
  };
  auto params = makeDefaultParams(1);
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

TEST(LeapResolution, StructuralProtected) {
  // FugueAnswer -> Structural -> skip.
  std::vector<NoteEvent> notes = {
      offBeat(0, 60),
      offBeat(1, 67),
      offBeat(2, 69, 0, BachNoteSource::FugueAnswer)
  };
  auto params = makeDefaultParams(1);
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

TEST(LeapResolution, ChordToneLandingProtected) {
  // notes[k+2] is a chord tone -> P5 protection.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 69)};
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t p) { return p == 69; };  // A is chord tone.
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

// ---------------------------------------------------------------------------
// Search / safety tests
// ---------------------------------------------------------------------------

TEST(LeapResolution, ChordTonePreferred) {
  // C4->A4 (M6 up = 9st). resolve_dir = -1.
  // offset 1: 68 (not scale). offset 2: 67 (G, scale).
  // Make G a chord tone -> should be selected.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 69), offBeat(2, 71)};
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t p) { return p == 67; };
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 1);
  EXPECT_EQ(findNote(notes, 0, 2).pitch, 67);  // G selected (only valid candidate, also CT).
}

TEST(LeapResolution, DiatonicFallback) {
  // When is_chord_tone is null, still picks diatonic step.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 69)};
  auto params = makeDefaultParams(1);
  // No is_chord_tone callback.
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 1);
  // offset 1: 66 (not scale), offset 2: 65 (F, scale) -> F.
  EXPECT_EQ(findNote(notes, 0, 2).pitch, 65);
}

TEST(LeapResolution, NoCandidateSkips) {
  // All candidates rejected -> no modification.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 69)};
  auto params = makeDefaultParams(1);
  params.voice_range_static = [](uint8_t) -> std::pair<uint8_t, uint8_t> {
    return {68, 127};  // Very high range, excludes candidates 65-66.
  };
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 0);
  EXPECT_EQ(notes[2].pitch, 69);  // Unchanged.
}

TEST(LeapResolution, VerticalSafeRejects) {
  // vertical_safe rejects first candidate, picks second.
  // C4->A4 (9st up). resolve_dir = -1. offset 1: 68 (not scale). offset 2: 67 (G).
  // But vertical_safe rejects 67. No more candidates -> skip.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 69), offBeat(2, 71)};
  auto params = makeDefaultParams(1);
  params.vertical_safe = [](Tick, uint8_t, uint8_t p) {
    return p != 67;  // Reject G.
  };
  int modified = resolveLeaps(notes, params);
  // offset 1: 68 (not scale). offset 2: 67 (scale but rejected by vertical_safe).
  EXPECT_EQ(modified, 0);  // No valid candidate.
}

TEST(LeapResolution, VoiceRangeRespected) {
  // Candidate is outside voice range -> rejected.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 69)};
  auto params = makeDefaultParams(1);
  params.voice_range_static = [](uint8_t) -> std::pair<uint8_t, uint8_t> {
    return {67, 80};  // Low bound is 67, candidates at 65-66 are out of range.
  };
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

TEST(LeapResolution, MultiVoiceIndependent) {
  // Two voices, each with a leap. Both should be independently resolved.
  std::vector<NoteEvent> notes = {
      offBeat(0, 60, 0), offBeat(1, 67, 0), offBeat(2, 69, 0),  // Voice 0: leap up
      offBeat(0, 72, 1), offBeat(1, 65, 1), offBeat(2, 63, 1)   // Voice 1: leap down
  };
  auto params = makeDefaultParams(2);
  int modified = resolveLeaps(notes, params);
  EXPECT_EQ(modified, 2);
  // Voice 0: resolve down from 67. offset 1: 66 (no), offset 2: 65 (F).
  EXPECT_EQ(findNote(notes, 0, 2).pitch, 65);
  // Voice 1: resolve up from 65. offset 1: 66 (no), offset 2: 67 (G).
  EXPECT_EQ(findNote(notes, 1, 2).pitch, 67);
}

TEST(LeapResolution, ThresholdConfigurable) {
  // With threshold=7, a P4 (5st) leap should NOT trigger resolution.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 65), offBeat(2, 67)};
  auto params = makeDefaultParams(1);
  params.leap_threshold = 7;
  EXPECT_EQ(resolveLeaps(notes, params), 0);  // 5st < 7, no resolution.
}

TEST(LeapResolution, ModifiedByFlag) {
  // Verify the LeapResolution flag is set on modified notes.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 67), offBeat(2, 69)};
  auto params = makeDefaultParams(1);
  resolveLeaps(notes, params);
  EXPECT_TRUE(hasLeapFlag(findNote(notes, 0, 2)));
}

TEST(LeapResolution, LeapAtEnd) {
  // Leap at the last two notes -> no k+2 to check -> 0 modifications.
  std::vector<NoteEvent> notes = {offBeat(0, 60), offBeat(1, 62), offBeat(2, 67)};
  auto params = makeDefaultParams(1);
  // Leap is at k=1 (62->67), but there's no k+2=notes[3]. Only k=0 forms a triplet:
  // (60, 62, 67): leap = 62-60 = 2st < 5. No leap.
  EXPECT_EQ(resolveLeaps(notes, params), 0);
}

TEST(LeapResolution, LeadingToneResolutionPreserved) {
  // After a downward leap, B->C resolution (leading tone -> tonic) is preserved.
  // G4(67)->B3(59), octave down (8st). notes[k+2] = C4(60).
  // Motion = 60-59 = +1 (up). Contrary to down. Step.
  // isTendencyResolution(59, 60, C, Major): B->C = leading tone->tonic.
  std::vector<NoteEvent> notes = {offBeat(0, 67), offBeat(1, 59), offBeat(2, 60)};
  auto params = makeDefaultParams(1);
  params.is_chord_tone = [](Tick, uint8_t) { return false; };
  EXPECT_EQ(resolveLeaps(notes, params), 0);
  EXPECT_EQ(notes[2].pitch, 60);  // Preserved.
}

// ---------------------------------------------------------------------------
// Helper function unit tests
// ---------------------------------------------------------------------------

TEST(LeapDetail, IsLeadingTone_Major) {
  // B (pc=11) is leading tone in C major.
  EXPECT_TRUE(leap_detail::isLeadingTone(71, Key::C, ScaleType::Major));
  EXPECT_TRUE(leap_detail::isLeadingTone(59, Key::C, ScaleType::Major));  // B3
  EXPECT_FALSE(leap_detail::isLeadingTone(69, Key::C, ScaleType::Major));  // A, not leading.
}

TEST(LeapDetail, IsLeadingTone_NaturalMinor) {
  // NaturalMinor has subtonic, NOT leading tone.
  EXPECT_FALSE(leap_detail::isLeadingTone(71, Key::C, ScaleType::NaturalMinor));
}

TEST(LeapDetail, IsLeadingTone_HarmonicMinor) {
  // HarmonicMinor has raised 7th = leading tone.
  EXPECT_TRUE(leap_detail::isLeadingTone(71, Key::C, ScaleType::HarmonicMinor));
}

TEST(LeapDetail, IsTendencyResolution_TiToDo) {
  // B->C in C major (leading tone -> tonic).
  EXPECT_TRUE(leap_detail::isTendencyResolution(71, 72, Key::C, ScaleType::Major));
  EXPECT_TRUE(leap_detail::isTendencyResolution(59, 60, Key::C, ScaleType::Major));
}

TEST(LeapDetail, IsTendencyResolution_FaToMi) {
  // F->E in C major (4th -> 3rd, descending).
  EXPECT_TRUE(leap_detail::isTendencyResolution(65, 64, Key::C, ScaleType::Major));
}

TEST(LeapDetail, IsTendencyResolution_7thTo6th) {
  // B->A in C major (7th -> 6th, descending step).
  EXPECT_TRUE(leap_detail::isTendencyResolution(71, 69, Key::C, ScaleType::Major));
}

TEST(LeapDetail, IsSequencePattern) {
  // intervals: +7, -2, +7, -2.
  uint8_t pitches1[] = {60, 67, 65, 72, 70};
  EXPECT_TRUE(leap_detail::isSequencePattern(pitches1, 5));

  // Non-repeating intervals.
  uint8_t pitches2[] = {60, 67, 65, 70, 68};
  EXPECT_FALSE(leap_detail::isSequencePattern(pitches2, 5));

  // Too few pitches.
  uint8_t pitches3[] = {60, 67, 65, 72};
  EXPECT_FALSE(leap_detail::isSequencePattern(pitches3, 4));
}

}  // namespace
}  // namespace bach
