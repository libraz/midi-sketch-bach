#include "composer/figuration.h"

#include "composer/span.h"
#include "composer/voice_intent.h"
#include "composer/voice_plan.h"
#include "core/basic_types.h"

namespace bach::composer::detail {

void pushCounterlineBar(VoicePlan& vp, SpanId& next_id, std::uint8_t voice, int bar,
                        Subdivision subdivision, std::uint8_t voice_center) {
  Span s;
  s.id = next_id++;
  s.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
  s.end_tick = s.start_tick + kTicksPerBar;
  s.voice = voice;
  s.intent = VoiceIntent::SequentialCounterline;
  s.subdivision = subdivision;
  s.voice_center = voice_center;
  vp.spans.push_back(s);
}

}  // namespace bach::composer::detail
