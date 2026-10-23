// Minor-mode spelling of scale degrees 6 and 7.
//
// spellScale (composer/figuration.h) is the one function that decides whether
// a minor key's 6th and 7th are raised. The unit tests below pin its contract;
// the sweep then reads every shipped form in minor, on the composed array and
// on the ornamented one, against the harmonic plan the form declared, and
// holds the spelling faults a single owner is meant to rule out.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "application/composition_service.h"
#include "composer/composer.h"
#include "composer/figuration.h"
#include "composer/form_director.h"
#include "composer/harness_fixture.h"
#include "composer/ornament_pass.h"
#include "composer/rule_helpers.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

using detail::chordPcMask;
using detail::ChordSpec;
using detail::Mode;
using detail::spelledScaleStep;
using detail::spellScale;

constexpr ChordSpec kMinorDominant{7, false};
constexpr ChordSpec kMinorTonic{0, true};

// --- spellScale contract -----------------------------------------------------

TEST(SpellScale, MinorWithoutHarmonyIsTheNaturalMinor) {
  EXPECT_EQ(spellScale(0, Mode::Minor, 0), (detail::ScaleSpelling{0, 2, 3, 5, 7, 8, 10}));
  EXPECT_EQ(spellScale(0, Mode::Minor, chordPcMask(kMinorTonic)),
            (detail::ScaleSpelling{0, 2, 3, 5, 7, 8, 10}));
}

TEST(SpellScale, MinorDominantRaisesTheSeventhAndTheSixthWithIt) {
  EXPECT_EQ(spellScale(0, Mode::Minor, chordPcMask(kMinorDominant)),
            (detail::ScaleSpelling{0, 2, 3, 5, 7, 9, 11}));
  // The same chord a tone higher in a local D minor.
  EXPECT_EQ(spellScale(2, Mode::Minor, chordPcMask(ChordSpec{9, false})),
            (detail::ScaleSpelling{0, 2, 3, 5, 7, 9, 11}));
}

TEST(SpellScale, MajorIgnoresTheHarmony) {
  EXPECT_EQ(spellScale(0, Mode::Major, chordPcMask(ChordSpec{4, false})),
            (detail::ScaleSpelling{0, 2, 4, 5, 7, 9, 11}));
}

TEST(SpellScale, ChordToneOnTheSixthKeepsTheAugmentedSecondItOwns) {
  // B-D-F-Ab: both ends of the augmented second are chord tones.
  const detail::PcMask diminished_seventh = static_cast<detail::PcMask>(
      detail::pcBit(11) | detail::pcBit(2) | detail::pcBit(5) | detail::pcBit(8));
  EXPECT_EQ(spellScale(0, Mode::Minor, diminished_seventh),
            (detail::ScaleSpelling{0, 2, 3, 5, 7, 8, 11}));
}

TEST(SpelledScaleStep, NeverReturnsTheSameLetterInflected) {
  const detail::PcMask dominant = chordPcMask(kMinorDominant);
  EXPECT_EQ(spelledScaleStep(71, -1, 0, Mode::Minor, dominant), 69);  // B -> A, not B-flat
  EXPECT_EQ(spelledScaleStep(72, -1, 0, Mode::Minor, dominant), 71);  // C -> B
  EXPECT_EQ(spelledScaleStep(67, 1, 0, Mode::Minor, dominant), 69);   // G -> A
  EXPECT_EQ(spelledScaleStep(68, 1, 0, Mode::Minor, dominant), 71);   // A-flat -> B, not A
  EXPECT_EQ(spelledScaleStep(70, 1, 0, Mode::Minor, dominant), 72);   // B-flat -> C, not B
  EXPECT_EQ(spelledScaleStep(69, -1, 0, Mode::Minor, 0), 67);         // A -> G, not A-flat
  // Wrapping the octave in both directions.
  EXPECT_EQ(spelledScaleStep(60, -1, 0, Mode::Minor, dominant), 59);
  EXPECT_EQ(spelledScaleStep(59, 1, 0, Mode::Minor, dominant), 60);
  EXPECT_EQ(spelledScaleStep(2, -1, 2, Mode::Minor, 0), 0);
}

TEST(SpelledScaleStep, ChromaticMinorPitchIsTheRaisedLetter) {
  EXPECT_EQ(spelledScaleStep(71, -1, 0, Mode::Minor, 0), 69);  // B -> A: B implies the raised 6th
  EXPECT_EQ(spelledScaleStep(71, 1, 0, Mode::Minor, 0), 72);
  EXPECT_EQ(spelledScaleStep(69, 1, 0, Mode::Minor, 0), 70);
  EXPECT_EQ(spelledScaleStep(70, -1, 0, Mode::Minor, chordPcMask(kMinorDominant)), 69);
}

TEST(SpelledScaleStep, WithoutHarmonyMatchesThePlainScaleWalk) {
  for (Mode mode : {Mode::Major, Mode::Minor}) {
    for (int midi = 24; midi < 100; ++midi) {
      // A chromatic minor pitch takes the letter step pinned above instead.
      if (mode == Mode::Minor && !detail::inScale(midi, mode))
        continue;
      for (int dir : {-1, 1}) {
        int expected = midi;
        for (int distance = 1; distance <= 12; ++distance) {
          if (detail::inScale(midi + dir * distance, mode)) {
            expected = midi + dir * distance;
            break;
          }
        }
        EXPECT_EQ(detail::melodicScaleStep(midi, dir, mode), expected) << midi << " " << dir;
      }
    }
  }
}

TEST(SpellScale, MinorTwoAsAMinorTriadReadsAsTheDiminishedTriad) {
  // ChordSpec cannot say "diminished": D-F-A in C minor is ii, D-F-A-flat.
  const detail::PcMask held = detail::chordPcMaskInKey(ChordSpec{2, true}, 0, Mode::Minor);
  EXPECT_NE(held & detail::pcBit(8), 0);
  EXPECT_EQ(held & detail::pcBit(9), 0);
  EXPECT_EQ(spellScale(0, Mode::Minor, held), (detail::ScaleSpelling{0, 2, 3, 5, 7, 8, 10}));
  // The same triad is its own tones in major.
  EXPECT_EQ(detail::chordPcMaskInKey(ChordSpec{2, true}, 0, Mode::Major),
            chordPcMask(ChordSpec{2, true}));
}

TEST(SpellScale, SoundingToneOutranksTheDeclaredChordForItsLetter) {
  // A line sounding B-flat under a declared G major chord keeps B-flat.
  const detail::PcMask context =
      detail::withSoundingTone(chordPcMask(kMinorDominant), 70, 0, Mode::Minor);
  EXPECT_TRUE(detail::inSpelledScale(70, 0, Mode::Minor, context));
  EXPECT_FALSE(detail::inSpelledScale(71, 0, Mode::Minor, context));
}

TEST(SpellScale, RespellMovesOnlyTheInflectionOfTheSameLetter) {
  const detail::PcMask dominant = chordPcMask(kMinorDominant);
  EXPECT_EQ(detail::respellInScale(70, 0, Mode::Minor, dominant), 71);  // B-flat -> B
  EXPECT_EQ(detail::respellInScale(68, 0, Mode::Minor, dominant), 69);  // A-flat -> A
  EXPECT_EQ(detail::respellInScale(67, 0, Mode::Minor, dominant), 67);
  EXPECT_EQ(detail::respellInScale(71, 0, Mode::Minor, 0), 70);  // no dominant: natural
  EXPECT_EQ(detail::respellInScale(66, 0, Mode::Major, 0), 66);  // major is left alone
}

TEST(SpellScale, AugmentedSecondIsReadByLetterInEitherOctave) {
  EXPECT_TRUE(detail::isAugmentedSecond(68, 71, 0, Mode::Minor));       // A-flat up to B
  EXPECT_FALSE(detail::isAugmentedSecond(67, 70, 0, Mode::Minor));      // G up to B-flat: a third
  EXPECT_TRUE(detail::isAugmentedSecondClass(71, 80, 0, Mode::Minor));  // B up to A-flat
  EXPECT_FALSE(detail::isAugmentedSecondClass(71, 72, 0, Mode::Minor));
}

// --- Shipped-form sweep ------------------------------------------------------

constexpr std::array<SubjectCharacter, 4> kCharacters = {{
    SubjectCharacter::Severe,
    SubjectCharacter::Playful,
    SubjectCharacter::Noble,
    SubjectCharacter::Restless,
}};

constexpr std::uint32_t kSeedCount = 6;

struct SpellingCounts {
  // A note sounding the lowered 7th of a minor local key at an onset whose
  // declared chord is that key's major dominant.
  std::size_t lowered_seventh_over_dominant = 0;
  // Two consecutive notes of one voice a semitone apart on the same letter
  // (B to B-flat, A-flat to A), both inside one dominant span.
  std::size_t same_letter_step_over_dominant = 0;
  // Consecutive notes of one voice forming an augmented second, either of
  // them inside a dominant span.
  std::size_t augmented_second_over_dominant = 0;
  // Two voices sounding the same letter with different inflections at one
  // onset inside a dominant span.
  std::size_t false_relation_over_dominant = 0;
  // Adjacent tones of one ornament figure on the same letter (an auxiliary
  // that is a chromatic alteration of its neighbour in the figure).
  std::size_t ornament_same_letter = 0;

  void add(const SpellingCounts& other) {
    lowered_seventh_over_dominant += other.lowered_seventh_over_dominant;
    same_letter_step_over_dominant += other.same_letter_step_over_dominant;
    augmented_second_over_dominant += other.augmented_second_over_dominant;
    false_relation_over_dominant += other.false_relation_over_dominant;
    ornament_same_letter += other.ornament_same_letter;
  }
};

// Letter of a pitch in a key: the plain-scale degree at or below it, so a
// chromatic pitch reads as its lower neighbour raised.
int letterOf(int pitch, const rule_helpers::KeyContext& key) {
  const int offset = (((pitch - key.tonic_pc) % 12) + 12) % 12;
  const detail::ScaleSpelling plain =
      spellScale(key.tonic_pc, key.is_minor ? Mode::Minor : Mode::Major, 0);
  int letter = 6;
  while (letter > 0 && plain[static_cast<std::size_t>(letter)] > offset)
    --letter;
  return letter;
}

bool sameLetterInflected(int left, int right, const rule_helpers::KeyContext& key) {
  return left % 12 != right % 12 && letterOf(left, key) == letterOf(right, key);
}

// True when `tick` lies under a minor local key's major dominant.
bool overMinorDominant(const HarmonicPlan& plan, Tick tick, rule_helpers::KeyContext* key_out) {
  if (plan.chords.empty())
    return false;
  const rule_helpers::KeyContext key = rule_helpers::keyAt(plan, tick);
  if (!key.is_minor)
    return false;
  const ChordEvent& chord = rule_helpers::activeChord(plan, tick);
  if (tick < chord.start_tick)
    return false;
  const bool major_quality =
      chord.quality == ChordQuality::Major || chord.quality == ChordQuality::Dominant7;
  if (!major_quality || (chord.root_pc + 12 - key.tonic_pc) % 12 != 7)
    return false;
  *key_out = key;
  return true;
}

SpellingCounts countSpelling(const std::vector<NoteEvent>& notes, const HarmonicPlan& plan) {
  SpellingCounts counts;
  std::vector<std::vector<const NoteEvent*>> by_voice;
  for (const NoteEvent& note : notes) {
    if (note.voice >= by_voice.size())
      by_voice.resize(note.voice + 1u);
    by_voice[note.voice].push_back(&note);
  }
  for (auto& line : by_voice) {
    std::stable_sort(line.begin(), line.end(), [](const NoteEvent* left, const NoteEvent* right) {
      return left->start_tick < right->start_tick;
    });
  }
  rule_helpers::KeyContext key;
  for (const NoteEvent& note : notes) {
    if (overMinorDominant(plan, note.start_tick, &key) &&
        note.pitch % 12 == (key.tonic_pc + 10) % 12) {
      ++counts.lowered_seventh_over_dominant;
    }
  }
  for (const auto& line : by_voice) {
    for (std::size_t idx = 1; idx < line.size(); ++idx) {
      const NoteEvent& prev = *line[idx - 1];
      const NoteEvent& curr = *line[idx];
      const int interval = std::abs(static_cast<int>(curr.pitch) - static_cast<int>(prev.pitch));
      if (prev.source == BachNoteSource::Ornament && curr.source == BachNoteSource::Ornament &&
          prev.start_tick + prev.duration == curr.start_tick && interval == 1) {
        const rule_helpers::KeyContext local = rule_helpers::keyAt(plan, curr.start_tick);
        if (sameLetterInflected(prev.pitch, curr.pitch, local)) {
          ++counts.ornament_same_letter;
        }
      }
      // The augmented second is a melodic fault wherever it touches the
      // dominant; the chromatic step only inside one dominant span, since a
      // step across the bar line into V is ordinary chromatic voice leading.
      rule_helpers::KeyContext prev_key;
      const bool curr_over = overMinorDominant(plan, curr.start_tick, &key);
      const bool prev_over = overMinorDominant(plan, prev.start_tick, &prev_key);
      if (!curr_over && !prev_over)
        continue;
      if (!curr_over)
        key = prev_key;
      const bool same_span = curr_over && prev_over &&
                             rule_helpers::activeChord(plan, prev.start_tick).start_tick ==
                                 rule_helpers::activeChord(plan, curr.start_tick).start_tick;
      if (same_span && interval == 1 && sameLetterInflected(prev.pitch, curr.pitch, key)) {
        ++counts.same_letter_step_over_dominant;
      }
      if (detail::isAugmentedSecond(prev.pitch, curr.pitch, key.tonic_pc, Mode::Minor)) {
        ++counts.augmented_second_over_dominant;
      }
    }
  }
  std::vector<Tick> onsets;
  for (const NoteEvent& note : notes)
    onsets.push_back(note.start_tick);
  std::sort(onsets.begin(), onsets.end());
  onsets.erase(std::unique(onsets.begin(), onsets.end()), onsets.end());
  for (Tick tick : onsets) {
    if (!overMinorDominant(plan, tick, &key))
      continue;
    std::vector<const NoteEvent*> sounding;
    for (const NoteEvent& note : notes) {
      if (note.start_tick <= tick && tick < note.start_tick + note.duration)
        sounding.push_back(&note);
    }
    bool clash = false;
    for (std::size_t left = 0; left < sounding.size() && !clash; ++left) {
      for (std::size_t right = left + 1; right < sounding.size() && !clash; ++right) {
        clash = sounding[left]->voice != sounding[right]->voice &&
                sameLetterInflected(sounding[left]->pitch, sounding[right]->pitch, key);
      }
    }
    if (clash)
      ++counts.false_relation_over_dominant;
  }
  return counts;
}

std::uint32_t lastTick(const std::vector<NoteEvent>& notes) {
  std::uint32_t last = 0;
  for (const NoteEvent& note : notes)
    last = std::max(last, note.start_tick + note.duration);
  return last;
}

// Per-form ceilings over every character x seeds 1-6 in minor at the natural
// length: the four spelling faults on the composed array, and the ornament
// fault on the decorated one. RATCHET: measured counts, only ever lowered.
// What remains is verbatim material under a dominant the plan declares (the
// fugue family's minor-station entries, strettos and episode sequences), the
// goldberg canon's exact imitation, and augmented seconds across a bar line
// into or out of V.
struct SpellingCeiling {
  FormType form;
  std::size_t lowered_seventh;
  std::size_t same_letter_step;
  std::size_t augmented_second;
  std::size_t false_relation;
  std::size_t ornament_same_letter;
};

constexpr std::array<SpellingCeiling, 10> kSpellingCeilings = {{
    {FormType::Fugue, 114, 12, 0, 5, 22},
    {FormType::PreludeAndFugue, 15, 0, 0, 1, 0},
    {FormType::TrioSonata, 8, 0, 5, 10, 0},
    {FormType::ChoralePrelude, 9, 6, 0, 0, 0},
    {FormType::ToccataAndFugue, 16, 0, 7, 0, 0},
    // The compass-bound counter-line concedes an augmented second only where
    // every other tone keeps a perfect-motion fault: 3 -> 5.
    {FormType::Passacaglia, 5, 0, 5, 0, 0},
    {FormType::FantasiaAndFugue, 17, 0, 5, 0, 0},
    {FormType::CelloPrelude, 0, 0, 0, 0, 0},
    {FormType::Chaconne, 2, 0, 6, 2, 0},
    {FormType::GoldbergVariations, 32, 8, 42, 64, 4},
}};

TEST(MinorSpellingSweep, DominantSpansKeepTheLeadingTone) {
  for (const SpellingCeiling& ceiling : kSpellingCeilings) {
    SpellingCounts structural_total;
    SpellingCounts shipped_total;
    for (SubjectCharacter character : kCharacters) {
      for (std::uint32_t seed = 1; seed <= kSeedCount; ++seed) {
        ComposeRequest request;
        request.form = ceiling.form;
        request.character = character;
        request.seed = seed;
        request.is_minor = true;
        HarnessFixture fixture;
        if (buildFormFixture(request, &fixture) != FormDirectorStatus::Ok)
          continue;
        ComposeResult result =
            Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
        structural_total.add(countSpelling(result.notes, fixture.harmony));
        OrnamentParams ornament;
        ornament.character = character;
        ornament.instrument = defaultInstrumentForForm(ceiling.form);
        ornament.mode = Mode::Minor;
        ornament.seed = seed;
        ornament.bpm = application::kDefaultBpm;
        resolveFixtureOrnamentContext(fixture, ceiling.form, lastTick(result.notes), &ornament);
        applyOrnamentPass(result, ornament);
        shipped_total.add(countSpelling(result.notes, fixture.harmony));
      }
    }
    std::printf(
        "[minor-spelling] %-20s b7/V=%zu step=%zu aug2=%zu xrel=%zu | shipped b7/V=%zu "
        "orn=%zu\n",
        formTypeToString(ceiling.form), structural_total.lowered_seventh_over_dominant,
        structural_total.same_letter_step_over_dominant,
        structural_total.augmented_second_over_dominant,
        structural_total.false_relation_over_dominant, shipped_total.lowered_seventh_over_dominant,
        shipped_total.ornament_same_letter);
    const char* form = formTypeToString(ceiling.form);
    EXPECT_LE(structural_total.lowered_seventh_over_dominant, ceiling.lowered_seventh) << form;
    EXPECT_LE(structural_total.same_letter_step_over_dominant, ceiling.same_letter_step) << form;
    EXPECT_LE(structural_total.augmented_second_over_dominant, ceiling.augmented_second) << form;
    EXPECT_LE(structural_total.false_relation_over_dominant, ceiling.false_relation) << form;
    EXPECT_LE(shipped_total.ornament_same_letter, ceiling.ornament_same_letter) << form;
  }
}

// The passacaglia's closing dominant: whatever the suspension and the seam
// branch choose, an upper voice sounds the leading tone over the last dominant
// before the final bar, in both modes.
TEST(MinorSpellingSweep, PassacagliaClosingDominantSoundsTheLeadingTone) {
  for (bool is_minor : {false, true}) {
    for (SubjectCharacter character : kCharacters) {
      for (std::uint32_t seed = 1; seed <= kSeedCount; ++seed) {
        ComposeRequest request;
        request.form = FormType::Passacaglia;
        request.character = character;
        request.seed = seed;
        request.is_minor = is_minor;
        HarnessFixture fixture;
        if (buildFormFixture(request, &fixture) != FormDirectorStatus::Ok)
          continue;
        const ComposeResult result =
            Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
        const HarmonicPlan& plan = fixture.harmony;
        ASSERT_FALSE(plan.chords.empty());
        // The last dominant span that ends before the final chord.
        Tick span_start = 0;
        Tick span_end = 0;
        for (std::size_t idx = 0; idx + 1 < plan.chords.size(); ++idx) {
          const ChordEvent& chord = plan.chords[idx];
          const bool dominant =
              chord.root_pc % 12 == 7 &&
              (chord.quality == ChordQuality::Major || chord.quality == ChordQuality::Dominant7);
          if (dominant) {
            span_start = chord.start_tick;
            span_end = plan.chords[idx + 1].start_tick;
          }
        }
        ASSERT_LT(span_start, span_end) << "seed " << seed;
        VoiceId lowest = 0;
        for (const NoteEvent& note : result.notes)
          lowest = std::max(lowest, note.voice);
        bool leading_tone = false;
        for (const NoteEvent& note : result.notes) {
          leading_tone = leading_tone || (note.voice != lowest && note.pitch % 12 == 11 &&
                                          note.start_tick < span_end &&
                                          note.start_tick + note.duration > span_start);
        }
        EXPECT_TRUE(leading_tone) << (is_minor ? "minor" : "major") << " "
                                  << subjectCharacterToString(character) << " seed " << seed;
      }
    }
  }
}

}  // namespace
}  // namespace bach::composer
