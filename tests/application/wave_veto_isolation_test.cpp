#include <gtest/gtest.h>

#include <cstdint>
#include <thread>

#include "application/composition_service.h"
#include "composer/validation.h"

namespace bach::application {
namespace {

CompositionRequest fugueRequest(std::uint32_t seed) {
  CompositionRequest request;
  request.form = FormType::Fugue;
  request.key = {Key::G, true};
  request.character = SubjectCharacter::Severe;
  request.instrument = InstrumentType::Organ;
  request.instrument_specified = true;
  request.scale = DurationScale::Short;
  request.bpm = 100;
  request.seed = seed;
  return request;
}

bool sameCounts(const composer::WaveVetoStats& lhs, const composer::WaveVetoStats& rhs) {
  return lhs.anchor_parallel_displaced == rhs.anchor_parallel_displaced &&
         lhs.wobble_breaker_fired == rhs.wobble_breaker_fired &&
         lhs.step_parallel_adjusted == rhs.step_parallel_adjusted &&
         lhs.step_harsh_adjusted == rhs.step_harsh_adjusted &&
         lhs.order_clamp_changed == rhs.order_clamp_changed &&
         lhs.window_expanded == rhs.window_expanded &&
         lhs.anchor_fault_held == rhs.anchor_fault_held;
}

}  // namespace

TEST(WaveVetoIsolationTest, ConcurrentCompositionsReportOnlyTheirOwnCounts) {
  constexpr std::uint32_t kSeedA = 405;
  constexpr std::uint32_t kSeedB = 406;
  CompositionProduct solo_a;
  CompositionProduct solo_b;
  ASSERT_EQ(compose(fugueRequest(kSeedA), &solo_a), CompositionStatus::Ok);
  ASSERT_EQ(compose(fugueRequest(kSeedB), &solo_b), CompositionStatus::Ok);

  constexpr int kRounds = 3;
  for (int round = 0; round < kRounds; ++round) {
    CompositionProduct par_a;
    CompositionProduct par_b;
    CompositionStatus status_a = CompositionStatus::InvalidArgument;
    CompositionStatus status_b = CompositionStatus::InvalidArgument;
    std::thread thread_a([&] { status_a = compose(fugueRequest(kSeedA), &par_a); });
    std::thread thread_b([&] { status_b = compose(fugueRequest(kSeedB), &par_b); });
    thread_a.join();
    thread_b.join();
    ASSERT_EQ(status_a, CompositionStatus::Ok);
    ASSERT_EQ(status_b, CompositionStatus::Ok);
    EXPECT_TRUE(sameCounts(par_a.composition.validation.wave_veto,
                           solo_a.composition.validation.wave_veto));
    EXPECT_TRUE(sameCounts(par_b.composition.validation.wave_veto,
                           solo_b.composition.validation.wave_veto));
    EXPECT_TRUE(sameCounts(par_a.final_validation.wave_veto, solo_a.final_validation.wave_veto));
    EXPECT_TRUE(sameCounts(par_b.final_validation.wave_veto, solo_b.final_validation.wave_veto));
  }
}

}  // namespace bach::application
