// Form-director foundation tests.
//
// Covers the form-driven entry layer for the composer:
//   1. FormSpec table sanity for all 10 forms (voices, bar ordering, meter).
//   2. resolveBars: scale multipliers, snap rounding, clamp, target_bars
//      override.
//   3. isFormCharacterCompatible: the three forbidden pairs.
//   4. arcPoint: monotone rise to a single climax, exactly-one-climax for
//      representative cycle counts, determinism.
//   5. buildFormFixture end-to-end for all 10 forms x seed 1 (the placeholder
//      builders replay proven phase fixtures, so the full Composer must pass).

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "composer/arc.h"
#include "composer/composer.h"
#include "composer/form_director.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr std::array<FormType, 10> kAllForms = {{
    FormType::Fugue,
    FormType::PreludeAndFugue,
    FormType::TrioSonata,
    FormType::ChoralePrelude,
    FormType::ToccataAndFugue,
    FormType::Passacaglia,
    FormType::FantasiaAndFugue,
    FormType::CelloPrelude,
    FormType::Chaconne,
    FormType::GoldbergVariations,
}};

// --- 1. FormSpec table sanity ----------------------------------------------

TEST(FormDirectorSpec, EveryFormHasSaneLayoutLimits) {
  for (FormType form : kAllForms) {
    const FormSpec& spec = formSpec(form);
    EXPECT_GE(spec.num_voices, 1) << "form " << static_cast<int>(form);
    EXPECT_LE(spec.num_voices, 3) << "form " << static_cast<int>(form);
    EXPECT_GT(spec.snap_bars, 0) << "form " << static_cast<int>(form);
    EXPECT_LE(spec.min_bars, spec.natural_bars) << "form " << static_cast<int>(form);
    EXPECT_LE(spec.natural_bars, spec.max_bars) << "form " << static_cast<int>(form);
    EXPECT_LE(spec.max_bars, 128) << "form " << static_cast<int>(form);
    EXPECT_EQ(spec.ts_denominator, 4) << "form " << static_cast<int>(form);
    EXPECT_TRUE(spec.ts_numerator == 3 || spec.ts_numerator == 4)
        << "form " << static_cast<int>(form);
  }
}

TEST(FormDirectorSpec, TripleMeterFormsAreChaconneAndPassacaglia) {
  EXPECT_EQ(formSpec(FormType::Passacaglia).ts_numerator, 3);
  EXPECT_EQ(formSpec(FormType::Chaconne).ts_numerator, 3);
  EXPECT_EQ(formSpec(FormType::Fugue).ts_numerator, 4);
  EXPECT_EQ(formSpec(FormType::TrioSonata).ts_numerator, 4);
}

TEST(FormDirectorSpec, VoiceCountsMatchDesign) {
  EXPECT_EQ(formSpec(FormType::CelloPrelude).num_voices, 1);
  EXPECT_EQ(formSpec(FormType::ChoralePrelude).num_voices, 2);
  EXPECT_EQ(formSpec(FormType::Passacaglia).num_voices, 2);
  EXPECT_EQ(formSpec(FormType::Fugue).num_voices, 3);
  EXPECT_EQ(formSpec(FormType::ToccataAndFugue).num_voices, 3);
  EXPECT_EQ(formSpec(FormType::Passacaglia).snap_bars, 8);
}

// --- 2. resolveBars --------------------------------------------------------

TEST(FormDirectorResolveBars, ShortScaleYieldsSnappedNaturalLength) {
  for (FormType form : kAllForms) {
    const FormSpec& spec = formSpec(form);
    const std::uint16_t resolved = resolveBars(form, DurationScale::Short, 0);
    // Short resolves to the natural length after the form's snap/clamp pass.
    // It must be a snap multiple, within [min, max], and adjacent to natural.
    EXPECT_EQ(resolved % spec.snap_bars, 0) << "form " << static_cast<int>(form);
    EXPECT_GE(resolved, spec.min_bars) << "form " << static_cast<int>(form);
    EXPECT_LE(resolved, spec.max_bars) << "form " << static_cast<int>(form);
    const int diff = static_cast<int>(resolved) - static_cast<int>(spec.natural_bars);
    EXPECT_LT(diff < 0 ? -diff : diff, spec.snap_bars) << "form " << static_cast<int>(form);
  }
}

TEST(FormDirectorResolveBars, ScaleMultipliersApplyThenSnapAndClamp) {
  // Fugue: natural 42, snap 4, max 128.
  // Short -> 42 -> snap 44 (nearest multiple of 4: 42 -> 44? 40 vs 44, 42 is
  // mid; round-half-up gives 44).
  EXPECT_EQ(resolveBars(FormType::Fugue, DurationScale::Short, 0), 44);
  // Medium ~2x -> 84 -> already a multiple of 4.
  EXPECT_EQ(resolveBars(FormType::Fugue, DurationScale::Medium, 0), 84);
  // Long ~3x -> 126 -> snap to 128 -> within cap.
  EXPECT_EQ(resolveBars(FormType::Fugue, DurationScale::Long, 0), 128);
  // Full ~4x -> 168 -> clamps to 128.
  EXPECT_EQ(resolveBars(FormType::Fugue, DurationScale::Full, 0), 128);
}

TEST(FormDirectorResolveBars, SnapRoundsToNearestMultiple) {
  // Passacaglia snap is 8. target_bars 21 -> nearest multiple of 8 = 24.
  EXPECT_EQ(resolveBars(FormType::Passacaglia, DurationScale::Short, 21), 24);
  // target_bars 19 -> nearest multiple of 8 = 16, but min is 16 so 16.
  EXPECT_EQ(resolveBars(FormType::Passacaglia, DurationScale::Short, 19), 16);
  // target_bars 20 -> midpoint between 16 and 24, round-half-up -> 24.
  EXPECT_EQ(resolveBars(FormType::Passacaglia, DurationScale::Short, 20), 24);
}

TEST(FormDirectorResolveBars, TargetBarsOverridesScaleButStillSnapsAndClamps) {
  // target_bars > 0 ignores the scale entirely.
  EXPECT_EQ(resolveBars(FormType::Fugue, DurationScale::Full, 32), 32);
  // Override below min clamps up to min.
  EXPECT_EQ(resolveBars(FormType::Fugue, DurationScale::Short, 4), 16);
  // Override above max clamps to max.
  EXPECT_EQ(resolveBars(FormType::Fugue, DurationScale::Short, 999), 128);
  // Override snaps: Fugue snap 4, target 30 -> 32.
  EXPECT_EQ(resolveBars(FormType::Fugue, DurationScale::Short, 30), 32);
}

// --- 3. isFormCharacterCompatible ------------------------------------------

TEST(FormDirectorCompat, ForbiddenPairs) {
  EXPECT_FALSE(isFormCharacterCompatible(FormType::ChoralePrelude, SubjectCharacter::Playful));
  EXPECT_FALSE(isFormCharacterCompatible(FormType::ChoralePrelude, SubjectCharacter::Restless));
  EXPECT_FALSE(isFormCharacterCompatible(FormType::ToccataAndFugue, SubjectCharacter::Noble));
}

TEST(FormDirectorCompat, AdmissiblePairs) {
  EXPECT_TRUE(isFormCharacterCompatible(FormType::ChoralePrelude, SubjectCharacter::Severe));
  EXPECT_TRUE(isFormCharacterCompatible(FormType::ChoralePrelude, SubjectCharacter::Noble));
  EXPECT_TRUE(isFormCharacterCompatible(FormType::ToccataAndFugue, SubjectCharacter::Severe));
  EXPECT_TRUE(isFormCharacterCompatible(FormType::ToccataAndFugue, SubjectCharacter::Playful));
  EXPECT_TRUE(isFormCharacterCompatible(FormType::Fugue, SubjectCharacter::Noble));
  EXPECT_TRUE(isFormCharacterCompatible(FormType::Fugue, SubjectCharacter::Restless));
}

// --- 4. arcPoint -----------------------------------------------------------

// For a representative cycle count, exactly one cycle is the climax and the
// density tier rises monotonically up to it.
void checkArc(std::size_t cycle_count) {
  std::size_t climax_count = 0;
  std::size_t climax_idx = 0;
  std::uint8_t prev_density = 0;
  bool first = true;
  for (std::size_t idx = 0; idx < cycle_count; ++idx) {
    const ArcPoint pt = arcPoint(idx, cycle_count);
    if (pt.is_climax) {
      ++climax_count;
      climax_idx = idx;
      EXPECT_EQ(pt.stage, ArcStage::Climax) << "cc=" << cycle_count << " idx=" << idx;
      EXPECT_EQ(pt.density_tier, 3) << "cc=" << cycle_count << " idx=" << idx;
      EXPECT_EQ(pt.velocity_tier, 3) << "cc=" << cycle_count << " idx=" << idx;
    }
    // Density never exceeds the peak tier.
    EXPECT_LE(pt.density_tier, 3);
    (void)first;
    (void)prev_density;
  }
  EXPECT_EQ(climax_count, 1u) << "cc=" << cycle_count;

  // Density must be non-decreasing on the rising limb [0, climax_idx].
  std::uint8_t running = 0;
  for (std::size_t idx = 0; idx <= climax_idx; ++idx) {
    const ArcPoint pt = arcPoint(idx, cycle_count);
    EXPECT_GE(pt.density_tier, running) << "cc=" << cycle_count << " idx=" << idx;
    running = pt.density_tier;
  }

  // Climax sits in the back portion of the span (>= ~60%).
  if (cycle_count > 1) {
    EXPECT_GE(climax_idx * 100, cycle_count * 60) << "cc=" << cycle_count;
    EXPECT_LE(climax_idx, cycle_count - 1) << "cc=" << cycle_count;
  }
}

TEST(FormDirectorArc, MonotoneRiseAndSingleClimax) {
  checkArc(4);
  checkArc(8);
  checkArc(30);
}

TEST(FormDirectorArc, SingleCycleIsItsOwnClimax) {
  const ArcPoint pt = arcPoint(0, 1);
  EXPECT_TRUE(pt.is_climax);
  EXPECT_EQ(pt.stage, ArcStage::Climax);
  EXPECT_EQ(pt.density_tier, 3);
}

TEST(FormDirectorArc, Deterministic) {
  for (std::size_t cc : {1u, 2u, 5u, 16u, 32u}) {
    for (std::size_t idx = 0; idx < cc; ++idx) {
      const ArcPoint a = arcPoint(idx, cc);
      const ArcPoint b = arcPoint(idx, cc);
      EXPECT_EQ(a.stage, b.stage);
      EXPECT_EQ(a.density_tier, b.density_tier);
      EXPECT_EQ(a.register_shift, b.register_shift);
      EXPECT_EQ(a.velocity_tier, b.velocity_tier);
      EXPECT_EQ(a.is_climax, b.is_climax);
    }
  }
}

TEST(FormDirectorArc, RegisterReturnsToBaselineAtEnd) {
  // Cycle counts whose climax sits strictly before the final cycle, so a
  // resolve limb exists and register falls back to the baseline. (For very
  // short spans the climax can coincide with the final cycle, which keeps the
  // peak register on purpose.)
  for (std::size_t cc : {8u, 16u, 30u}) {
    const ArcPoint last = arcPoint(cc - 1, cc);
    EXPECT_EQ(last.register_shift, 0) << "cc=" << cc;
  }
}

// --- 5. buildFormFixture end-to-end ----------------------------------------

TEST(FormDirectorBuild, AllFormsRunThroughComposerCleanly) {
  for (FormType form : kAllForms) {
    ComposeRequest req;
    req.form = form;
    req.seed = 1;
    HarnessFixture fixture;
    const FormDirectorStatus status = buildFormFixture(req, &fixture);
    ASSERT_EQ(status, FormDirectorStatus::Ok) << "form " << static_cast<int>(form);

    const ComposeResult result =
        Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
    EXPECT_EQ(result.validation.status, ValidationStatus::Ok)
        << "form " << static_cast<int>(form);
    EXPECT_TRUE(result.validation.failures.empty()) << "form " << static_cast<int>(form);
    EXPECT_FALSE(result.notes.empty()) << "form " << static_cast<int>(form);
  }
}

TEST(FormDirectorBuild, MeterStampedFromFormSpec) {
  ComposeRequest req;
  req.form = FormType::Passacaglia;
  req.seed = 1;
  HarnessFixture fixture;
  ASSERT_EQ(buildFormFixture(req, &fixture), FormDirectorStatus::Ok);
  EXPECT_EQ(fixture.ts_numerator, 3);
  EXPECT_EQ(fixture.ts_denominator, 4);

  ComposeRequest req4;
  req4.form = FormType::Fugue;
  req4.seed = 1;
  HarnessFixture fixture4;
  ASSERT_EQ(buildFormFixture(req4, &fixture4), FormDirectorStatus::Ok);
  EXPECT_EQ(fixture4.ts_numerator, 4);
  EXPECT_EQ(fixture4.ts_denominator, 4);
}

TEST(FormDirectorBuild, IncompatibleCharacterIsRejectedWithoutTouchingOut) {
  ComposeRequest req;
  req.form = FormType::ChoralePrelude;
  req.character = SubjectCharacter::Playful;
  req.seed = 1;

  HarnessFixture fixture;
  fixture.ts_numerator = 7;  // sentinel: must stay untouched on failure
  const FormDirectorStatus status = buildFormFixture(req, &fixture);
  EXPECT_EQ(status, FormDirectorStatus::IncompatibleCharacter);
  EXPECT_EQ(fixture.ts_numerator, 7);
}

TEST(FormDirectorBuild, NullOutIsRejected) {
  ComposeRequest req;
  req.form = FormType::Fugue;
  EXPECT_EQ(buildFormFixture(req, nullptr), FormDirectorStatus::UnknownForm);
}

}  // namespace
}  // namespace bach::composer
