// The figuration line's bar-head chord-tone rule, and the one escape from it.
//
// figuration_harmonic_consistency binds a single voice: the figuration must
// open every bar on a chord tone, while the theme entries sounding against it
// walk freely through non-chord tones -- which is what a melodic line does, not
// a defect in it. Over the shipped fugue surface a majority of figuration bar
// heads have a concurrent theme tone outside the bar's triad, and at a few of
// them the two constraints close on each other: every triad tone in the voice
// band is at once dissonant against a sounding theme tone, outside the
// voice-order window, or tied into a perfect parallel. The builder then leaves
// the chord for the nearest free diatonic tone and stamps FigurationAnchorRelaxed
// on it, which is what exempts the note from the rule.
//
// The bit is an escape hatch into a rule this project otherwise enforces
// absolutely, so what has to be tested is not that it works but that it stays
// shut: it must not fire while a chord tone is playable, and where it does fire
// the tone it admits must be at least as good as the parallel it replaced.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "composer/composer.h"
#include "composer/figuration_palette.h"
#include "composer/form_director.h"
#include "composer/harness_fixture.h"
#include "composer/provenance.h"
#include "composer/texture_helpers.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr std::array<SubjectCharacter, 4> kCharacters = {{
    SubjectCharacter::Severe,
    SubjectCharacter::Playful,
    SubjectCharacter::Noble,
    SubjectCharacter::Restless,
}};

constexpr std::uint32_t kFirstSeed = 1;
constexpr std::uint32_t kSeedCount = 8;

// The two forms whose figuration runs against theme entries. The other eight
// either carry no figuration section or carry it over a settled accompaniment,
// where the triad never closes.
constexpr std::array<FormType, 2> kFiguratedForms = {{
    FormType::Fugue,
    FormType::PreludeAndFugue,
}};

bool soundsAt(const NoteEvent& note, Tick tick) {
  return note.start_tick <= tick && tick < note.start_tick + note.duration;
}

bool carriesBit(const NoteProvenance& provenance, RuleBit bit) {
  return (provenance.satisfied_rules & (ruleBitMask(bit))).any();
}

// --- The escape stays shut while the chord is playable ----------------------

TEST(FigurationAnchorRelaxation, StaysUnusedWhileATriadToneIsFree) {
  // No other voice in the registry, so nothing can block a chord tone and
  // nothing can be in parallel with anything.
  const detail::ChordSpec chord{0, false};
  ThemeToneRegistry registry;
  FigurationSection section;
  int prev_anchor = 0;
  for (int bar = 0; bar < 4; ++bar) {
    appendFigurationWaveBar(registry, section, bar, /*voice=*/1, chord, detail::Mode::Major,
                            /*notes_per_beat=*/2, /*offset=*/1, prev_anchor, /*band_lo=*/51,
                            /*band_hi=*/66, /*num_voices=*/3);
  }
  ASSERT_FALSE(section.notes.empty());
  EXPECT_TRUE(section.relaxed_anchor_ticks.empty())
      << "the anchor left the chord although no voice was there to block it";
}

TEST(FigurationAnchorRelaxation, StaysUnusedUnderAVoiceThatOnlyThreatensParallels) {
  // An ascending scale in V0 is the shape most likely to drag a same-direction
  // line into parallels, but it leaves the triad reachable: the displacement
  // that only offers chord tones is enough, so the escape must not open.
  ThemeToneRegistry registry;
  const int kScale[8] = {72, 74, 76, 77, 79, 81, 83, 84};
  for (int bar = 0; bar < 4; ++bar) {
    for (int beat = 0; beat < 4; ++beat) {
      const Tick tick = static_cast<Tick>(bar) * kTicksPerBar + beat * kTicksPerBeat;
      registry.record(tick, /*voice=*/0, kScale[(bar * 4 + beat) % 8], kTicksPerBeat);
    }
  }
  const detail::ChordSpec chord{0, false};
  FigurationSection section;
  int prev_anchor = 0;
  for (int bar = 0; bar < 4; ++bar) {
    appendFigurationWaveBar(registry, section, bar, /*voice=*/1, chord, detail::Mode::Major,
                            /*notes_per_beat=*/2, /*offset=*/0, prev_anchor, /*band_lo=*/51,
                            /*band_hi=*/66, /*num_voices=*/3);
  }
  EXPECT_TRUE(section.relaxed_anchor_ticks.empty());
}

// --- The escape opens when the chord really is spent ------------------------

TEST(FigurationAnchorRelaxation, OpensWhenEveryChordToneInBandIsBlocked) {
  // Reachability of the exemption, proved on the mechanism rather than on the
  // shipped surface, where the sweep below no longer reaches it. A bar head is
  // built so that each C major tone inside the band fails for one of the three
  // reasons the escape exists for, and no two fail the same way:
  //   60  hidden fifth against V0's rising fifth (a perfect-motion fault),
  //   64  tritone against V2's Bb (a clash the anchor may not sound),
  //   67  parallel octave with V0 (the cardinal prohibition).
  // Nothing is left, so the line takes the nearest free diatonic tone and
  // records the exemption.
  ThemeToneRegistry registry;
  registry.record(/*tick=*/1800, /*voice=*/0, /*pitch=*/65, /*duration=*/120);
  registry.record(/*tick=*/1920, /*voice=*/0, /*pitch=*/79, /*duration=*/480);
  registry.record(/*tick=*/1800, /*voice=*/2, /*pitch=*/58, /*duration=*/120);
  registry.record(/*tick=*/1920, /*voice=*/2, /*pitch=*/58, /*duration=*/480);

  const detail::ChordSpec chord{0, false};
  FigurationSection section;
  int prev_anchor = 53;
  appendFigurationWaveBar(registry, section, /*bar=*/1, /*voice=*/1, chord, detail::Mode::Major,
                          /*notes_per_beat=*/1, /*offset=*/0, prev_anchor, /*band_lo=*/60,
                          /*band_hi=*/67, /*num_voices=*/3);

  ASSERT_FALSE(section.notes.empty());
  EXPECT_FALSE(section.relaxed_anchor_ticks.empty())
      << "no chord tone was playable and the line still did not take the escape";
}

// --- What the escape is allowed to ship -------------------------------------

TEST(FigurationAnchorRelaxation, RelaxedAnchorsShipConsonantAgainstTheWholeTexture) {
  // A note exempted from the chord-tone rule still has to be a note the ear
  // accepts. It must open a bar (the only onset the rule constrains) and it must
  // be consonant with every other voice sounding at that onset -- otherwise the
  // exemption traded a parallel for a clash, which is not a trade worth making.
  std::size_t relaxed_notes = 0;
  for (FormType form : kFiguratedForms) {
    for (SubjectCharacter character : kCharacters) {
      for (std::uint32_t offset = 0; offset < kSeedCount; ++offset) {
        ComposeRequest request;
        request.form = form;
        request.character = character;
        request.seed = kFirstSeed + offset;
        request.is_minor = (offset % 2) == 1;

        HarnessFixture fixture;
        if (buildFormFixture(request, &fixture) != FormDirectorStatus::Ok)
          continue;
        const ComposeResult result =
            Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
        ASSERT_EQ(result.notes.size(), result.provenance.size());

        for (std::size_t index = 0; index < result.notes.size(); ++index) {
          if (!carriesBit(result.provenance[index], RuleBit::FigurationAnchorRelaxed))
            continue;
          ++relaxed_notes;
          const NoteEvent& note = result.notes[index];
          EXPECT_EQ(note.start_tick % kTicksPerBar, 0)
              << "a relaxed anchor landed off the bar downbeat, where the rule it "
              << "is exempt from does not even apply";
          for (const NoteEvent& other : result.notes) {
            if (other.voice == note.voice || !soundsAt(other, note.start_tick))
              continue;
            EXPECT_TRUE(isConsonantPair(note.pitch, other.pitch))
                << "relaxed anchor " << static_cast<int>(note.pitch) << " at tick "
                << note.start_tick << " is dissonant against " << static_cast<int>(other.pitch);
          }
        }
      }
    }
  }
  // Emitted on every run so the current measurement is visible when the ceiling
  // below is tightened.
  std::printf("[figuration] relaxed anchors shipped: %zu\n", relaxed_notes);
  // The escape has to stay rare, or it has become a licence to leave the chord.
  // RATCHET: this ceiling may only ever be LOWERED -- a rise means the builder
  // is escaping where it should be finding a chord tone.
  //
  // It reached zero, and the reason is that the chord grew a fourth tone. The
  // escape opens only where every chord tone in the band is at once blocked, so
  // a dominant that may offer its seventh has one more way not to be exhausted;
  // the bar heads that used to escape now find that tone. Widening the sweep to
  // 64 seeds per form and character does not reach it either, so the reachability
  // of the exemption is proved on the mechanism instead --
  // OpensWhenEveryChordToneInBandIsBlocked constructs the exhaustion directly.
  EXPECT_EQ(relaxed_notes, 0u) << "the escape fired on the shipped surface, where the anchor "
                                  "selector should be finding a chord tone";
}

}  // namespace
}  // namespace bach::composer
