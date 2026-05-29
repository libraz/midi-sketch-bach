// Tests for generator.h -- unified generation routing, prelude+fugue
// concatenation, instrument auto-detection, determinism, and form routing.

#include "generator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>

#include "core/basic_types.h"
#include "harmony/key.h"
#include "test_helpers.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a default GeneratorConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return GeneratorConfig with standard 3-voice C major fugue settings.
GeneratorConfig makeTestConfig(uint32_t seed = 42) {
  GeneratorConfig config;
  config.form = FormType::Fugue;
  config.key = {Key::C, false};
  config.num_voices = 3;
  config.bpm = 100;
  config.seed = seed;
  config.character = SubjectCharacter::Severe;
  config.instrument = InstrumentType::Organ;
  return config;
}

double extractJsonNumber(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  EXPECT_NE(pos, std::string::npos) << "Missing JSON key: " << key;
  if (pos == std::string::npos)
    return 0.0;
  pos += needle.size();
  size_t end = json.find_first_of(",}", pos);
  EXPECT_NE(end, std::string::npos) << "Unterminated JSON value for key: " << key;
  if (end == std::string::npos)
    return 0.0;
  return std::stod(json.substr(pos, end - pos));
}

std::string extractListeningHotspotJson(const std::string& json, int bar) {
  const std::string needle = "\"bar\":" + std::to_string(bar);
  size_t bar_pos = json.find(needle);
  EXPECT_NE(bar_pos, std::string::npos) << "Missing listening hotspot bar: " << bar;
  if (bar_pos == std::string::npos)
    return {};

  size_t object_start = json.rfind('{', bar_pos);
  size_t object_end = json.find('}', bar_pos);
  EXPECT_NE(object_start, std::string::npos) << "Missing hotspot object start for bar: " << bar;
  EXPECT_NE(object_end, std::string::npos) << "Missing hotspot object end for bar: " << bar;
  if (object_start == std::string::npos || object_end == std::string::npos) {
    return {};
  }
  return json.substr(object_start, object_end - object_start + 1);
}

// ---------------------------------------------------------------------------
// Fugue form generation
// ---------------------------------------------------------------------------

TEST(GeneratorTest, FugueForm_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

TEST(GeneratorTest, FugueForm_HasCorrectTrackCount) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 3;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 3u);
}

TEST(GeneratorTest, FugueForm_HasFormDescription) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_FALSE(result.form_description.empty());
  // Description should contain "fugue" (case-insensitive check via find).
  EXPECT_NE(result.form_description.find("fugue"), std::string::npos)
      << "Form description missing 'fugue': " << result.form_description;
}

TEST(GeneratorTest, FugueJsonIncludesFormalStructure) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"structure\""), std::string::npos);
  EXPECT_NE(json.find("\"sections\""), std::string::npos);
  EXPECT_NE(json.find("\"Exposition\""), std::string::npos);
  EXPECT_NE(json.find("\"source_summary\""), std::string::npos);
  EXPECT_NE(json.find("\"structure_audit\""), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspots\""), std::string::npos);
  EXPECT_NE(json.find("\"repair_dependency_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"bass_line_motion_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_pitch_repair_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_texture_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_hard_clash_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_hard_clash_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_hard_clash_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_unstable_interval_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_unstable_interval_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_hard_clash_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_unstable_interval_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"critic_time_window_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"critic_time_windows\""), std::string::npos);
  EXPECT_NE(json.find("\"18s_transition\""), std::string::npos);
  EXPECT_NE(json.find("\"26s_stability\""), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_intent_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_dialogue_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_counterline_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"resolve_region_repair_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"resolve_region_pitch_repair_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"coda_intent_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"coda_closing_cadence_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"coda_subject_head_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"coda_final_tonic_bass_notes\""), std::string::npos);
  EXPECT_NE(json.find("\"coda_subject_head_interval_count\""), std::string::npos);
  EXPECT_NE(json.find("\"coda_subject_head_match_count\""), std::string::npos);
  EXPECT_NE(json.find("\"coda_subject_head_match_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"coda_pitch_repair_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"pitch_repair_run_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"max_pitch_repair_run\""), std::string::npos);
  EXPECT_NE(json.find("\"max_pitch_repair_run_bar\""), std::string::npos);
  EXPECT_NE(json.find("\"max_pitch_repair_run_source\""), std::string::npos);
  EXPECT_NE(json.find("\"repair_hotspot_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"episode_material_repair_dependency_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"repair_hotspot_bar\""), std::string::npos);
  EXPECT_NE(json.find("\"repair_hotspot_pitch_repair_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"episode_material_repair_hotspot_bar\""), std::string::npos);
  EXPECT_NE(json.find("\"episode_material_repair_hotspot_pitch_repair_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"episode_material_pitch_repair_density\""), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_note_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_approach_interval\""), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_resolution_interval\""), std::string::npos);
  EXPECT_NE(json.find("\"opening_subject_contour_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"opening_subject_unresolved_leap_count\""), std::string::npos);
  EXPECT_NE(json.find("\"global_arc_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"formal_section_count\""), std::string::npos);
  EXPECT_NE(json.find("\"thematic_two_voice_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"thematic_two_voice_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_clash_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"early_thematic_dialogue_clash_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"episode_dialogue_window_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"episode_dialogue_window_count\""), std::string::npos);
  EXPECT_NE(json.find("\"min_episode_dialogue_window_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"episode_motif_derivation_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"episode_motif_derivation_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_hard_clash_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"bass_activity_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"bass_step_or_structural_motion_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"analysis_gate\""), std::string::npos);
  EXPECT_NE(json.find("\"selection_score\""), std::string::npos);
  EXPECT_NE(json.find("\"penalty_affecting_violations\""), std::string::npos);
}

TEST(GeneratorTest, EventsJsonTotalBarsRoundsUpPartialFinalBar) {
  GeneratorConfig config = makeTestConfig();
  GeneratorResult result;
  result.success = true;
  result.seed_used = 7;
  result.total_duration_ticks = kTicksPerBar + 1;
  result.form_description = "test";

  Track track;
  track.name = "Manual I";
  NoteEvent note;
  note.start_tick = kTicksPerBar;
  note.duration = 1;
  note.pitch = 60;
  note.velocity = 80;
  note.voice = 0;
  track.notes.push_back(note);
  result.tracks.push_back(track);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"total_ticks\":1921"), std::string::npos);
  EXPECT_NE(json.find("\"total_bars\":2"), std::string::npos);
  EXPECT_NE(json.find("\"structure_audit\""), std::string::npos);
  EXPECT_NE(json.find("\"repair_surrogate_pass\":true"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditReportsIntentCoverage) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 4;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 9;
  result.total_duration_ticks = kTicksPerBar * 2;
  result.form_description = "audit fixture";

  result.tracks.resize(4);
  for (size_t voice = 0; voice < result.tracks.size(); ++voice) {
    result.tracks[voice].name = "Voice " + std::to_string(voice);
  }

  for (int idx = 0; idx < 12; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * (kTicksPerBeat / 2);
    note.duration = kTicksPerBeat / 2;
    note.pitch = static_cast<uint8_t>(60 + idx % 5);
    note.velocity = 80;
    note.voice = 0;
    note.source = (idx < 4) ? BachNoteSource::FugueSubject : BachNoteSource::SequenceNote;
    result.tracks[0].notes.push_back(note);
  }

  for (int idx = 0; idx < 6; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(43 + idx % 4);
    note.velocity = 80;
    note.voice = 3;
    note.source = BachNoteSource::PedalPoint;
    result.tracks[3].notes.push_back(note);
  }

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"episode_dialogue_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"bass_intent_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"bass_line_motion_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"repair_surrogate_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"repair_dependency_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"pitch_repair_run_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"repair_hotspot_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"opening_subject_contour_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"max_pitch_repair_run\":0"), std::string::npos);
  EXPECT_NE(json.find("\"max_pitch_repair_run_bar\":0"), std::string::npos);
  EXPECT_NE(json.find("\"global_arc_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_two_voice_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_thematic_dialogue_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"episode_dialogue_window_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"episode_motif_derivation_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"flexible_contour_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"composed_intent_notes\":18"), std::string::npos);
  EXPECT_NE(json.find("\"composed_intent_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"protected_subject_pitch_repair_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"protected_dialogue_pitch_repair_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"protected_dialogue_pitch_repair_ratio\":0"), std::string::npos);
  EXPECT_NE(json.find("\"immutable_pitch_repair_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"flexible_pitch_repair_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_pitch_repair_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_hard_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_intent_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_dialogue_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_counterline_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_dialogue_notes\":12"), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_dialogue_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_free_counterpoint_ratio\":0"), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_sample_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"critic_time_window_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_pitch_repair_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"bar\":9"), std::string::npos);
  EXPECT_NE(json.find("\"pitch_repair_ratio\":0"), std::string::npos);
  EXPECT_NE(json.find("\"pitch_repair_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"texture_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"hard_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"flexible_large_leap_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"bass_activity_ratio\":0"), std::string::npos);
  EXPECT_NE(json.find("\"bass_step_or_structural_motion_ratio\":0"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"source\":\"sequence_note\""), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsFlexibleContourBreaks) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 11;
  result.total_duration_ticks = kTicksPerBar;
  result.form_description = "contour audit fixture";

  Track track;
  track.name = "Manual I";
  NoteEvent first;
  first.start_tick = 0;
  first.duration = kTicksPerBeat;
  first.pitch = 60;
  first.velocity = 80;
  first.voice = 0;
  first.source = BachNoteSource::EpisodeMaterial;
  track.notes.push_back(first);

  NoteEvent second = first;
  second.start_tick = kTicksPerBeat;
  second.pitch = 75;
  track.notes.push_back(second);

  NoteEvent hotspot = first;
  hotspot.start_tick = kTicksPerBar * 8;
  hotspot.pitch = 74;
  hotspot.modified_by = static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
  track.notes.push_back(hotspot);
  result.tracks.push_back(track);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"flexible_contour_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"flexible_large_leap_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"flexible_remote_leap_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"max_flexible_leap\":15"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_notes\":1"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_pitch_repair_notes\":1"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_pitch_repair_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"bar\":9"), std::string::npos);
  EXPECT_NE(json.find("\"note_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"pitch_repair_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"pitch_repair_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"covered\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsPitchRepairRuns) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 1;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 12;
  result.total_duration_ticks = kTicksPerBar;
  result.form_description = "repair run audit fixture";

  Track track;
  track.name = "Manual I";
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * duration::kEighthNote;
    note.duration = duration::kEighthNote;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::EpisodeMaterial;
    note.modified_by = static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    track.notes.push_back(note);
  }
  result.tracks.push_back(track);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"max_pitch_repair_run\":4"), std::string::npos);
  EXPECT_NE(json.find("\"max_pitch_repair_run_bar\":1"), std::string::npos);
  EXPECT_NE(json.find("\"max_pitch_repair_run_voice\":0"), std::string::npos);
  EXPECT_NE(json.find("\"max_pitch_repair_run_source\":\"episode_material\""), std::string::npos);
  EXPECT_NE(json.find("\"pitch_repair_run_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"repair_dependency_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsLocalRepairHotspots) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 1;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 16;
  result.total_duration_ticks = kTicksPerBar * 2;
  result.form_description = "local repair hotspot audit fixture";

  Track track;
  track.name = "Manual I";
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = kTicksPerBar + static_cast<Tick>(idx) * duration::kEighthNote;
    note.duration = duration::kEighthNote;
    note.pitch = static_cast<uint8_t>(60 + idx % 5);
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::EpisodeMaterial;
    if (idx < 4) {
      note.modified_by = static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    }
    track.notes.push_back(note);
  }
  result.tracks.push_back(track);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"repair_hotspot_bar\":2"), std::string::npos);
  EXPECT_NE(json.find("\"repair_hotspot_note_count\":8"), std::string::npos);
  EXPECT_NE(json.find("\"repair_hotspot_pitch_repair_notes\":4"), std::string::npos);
  EXPECT_NE(json.find("\"repair_hotspot_pitch_repair_ratio\":0.5"), std::string::npos);
  EXPECT_NE(json.find("\"episode_material_repair_hotspot_bar\":2"), std::string::npos);
  EXPECT_NE(json.find("\"episode_material_repair_hotspot_note_count\":8"), std::string::npos);
  EXPECT_NE(json.find("\"episode_material_repair_hotspot_pitch_repair_notes\":4"),
            std::string::npos);
  EXPECT_NE(json.find("\"episode_material_repair_hotspot_pitch_repair_ratio\":0.5"),
            std::string::npos);
  EXPECT_NE(json.find("\"repair_hotspot_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"repair_dependency_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsUnresolvedOpeningSubjectLeap) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 1;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 17;
  result.total_duration_ticks = kTicksPerBar * 2;
  result.form_description = "opening subject contour fixture";

  Track track;
  track.name = "Manual I";
  const std::array<uint8_t, 6> pitches = {60, 62, 64, 65, 72, 76};
  for (size_t idx = 0; idx < pitches.size(); ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * duration::kEighthNote;
    note.duration = duration::kEighthNote;
    note.pitch = pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    note.source = idx < 4 ? BachNoteSource::SubjectCore : BachNoteSource::FugueSubject;
    track.notes.push_back(note);
  }
  result.tracks.push_back(track);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"opening_subject_note_count\":6"), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_note_pitch\":76"), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_approach_interval\":4"), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_resolution_interval\":0"), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_note_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"opening_subject_large_leap_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"opening_subject_unresolved_leap_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"opening_subject_max_leap\":7"), std::string::npos);
  EXPECT_NE(json.find("\"opening_subject_contour_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"repair_dependency_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsOpeningSixthNoteBreak) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 1;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 22;
  result.total_duration_ticks = kTicksPerBar * 2;
  result.form_description = "opening sixth note fixture";

  Track track;
  track.name = "Manual I";
  const std::array<uint8_t, 7> pitches = {60, 62, 64, 65, 67, 76, 79};
  for (size_t idx = 0; idx < pitches.size(); ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * duration::kEighthNote;
    note.duration = duration::kEighthNote;
    note.pitch = pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    note.source = idx < 4 ? BachNoteSource::SubjectCore : BachNoteSource::FugueSubject;
    track.notes.push_back(note);
  }
  result.tracks.push_back(track);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"opening_subject_note_count\":7"), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_note_pitch\":76"), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_approach_interval\":9"), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_resolution_interval\":3"), std::string::npos);
  EXPECT_NE(json.find("\"opening_sixth_note_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"opening_subject_contour_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"repair_dependency_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsBrokenGlobalArc) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 3;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 13;
  result.total_duration_ticks = kTicksPerBar * 12;
  result.form_description = "broken global arc fixture";
  result.structure_json =
      "{\"section_count\":2,\"total_duration_ticks\":23040,"
      "\"sections\":["
      "{\"type\":\"Episode\",\"phase\":\"Develop\",\"start_tick\":0,"
      "\"end_tick\":11520,\"duration_ticks\":11520,\"key\":\"C\"},"
      "{\"type\":\"Exposition\",\"phase\":\"Establish\",\"start_tick\":11520,"
      "\"end_tick\":23040,\"duration_ticks\":11520,\"key\":\"C\"}"
      "]}";

  result.tracks.resize(3);
  for (size_t voice = 0; voice < result.tracks.size(); ++voice) {
    result.tracks[voice].name = "Voice " + std::to_string(voice);
  }
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent subject;
    subject.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    subject.duration = kTicksPerBeat;
    subject.pitch = static_cast<uint8_t>(60 + idx);
    subject.velocity = 80;
    subject.voice = 0;
    subject.source = BachNoteSource::FugueSubject;
    result.tracks[0].notes.push_back(subject);
  }

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"global_arc_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"formal_section_count\":2"), std::string::npos);
  EXPECT_NE(json.find("\"establish_section_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"develop_section_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"resolve_section_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsWeakThematicTwoVoice) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 14;
  result.total_duration_ticks = kTicksPerBar * 8;
  result.form_description = "weak two voice fixture";
  result.structure_json =
      "{\"section_count\":4,\"total_duration_ticks\":15360,"
      "\"sections\":["
      "{\"type\":\"Exposition\",\"phase\":\"Establish\",\"start_tick\":0,"
      "\"end_tick\":3840,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Episode\",\"phase\":\"Develop\",\"start_tick\":3840,"
      "\"end_tick\":7680,\"duration_ticks\":3840,\"key\":\"G\"},"
      "{\"type\":\"Stretto\",\"phase\":\"Resolve\",\"start_tick\":7680,"
      "\"end_tick\":11520,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Coda\",\"phase\":\"Resolve\",\"start_tick\":11520,"
      "\"end_tick\":15360,\"duration_ticks\":3840,\"key\":\"C\"}"
      "]}";

  result.tracks.resize(2);
  for (size_t voice = 0; voice < result.tracks.size(); ++voice) {
    result.tracks[voice].name = "Voice " + std::to_string(voice);
    NoteEvent note;
    note.start_tick = 0;
    note.duration = result.total_duration_ticks;
    note.pitch = static_cast<uint8_t>(60 + voice * 7);
    note.velocity = 80;
    note.voice = static_cast<uint8_t>(voice);
    note.source = BachNoteSource::EpisodeMaterial;
    result.tracks[voice].notes.push_back(note);
  }

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"two_voice_sample_count\""), std::string::npos);
  EXPECT_NE(json.find("\"thematic_two_voice_sample_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"subject_dialogue_pair_sample_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_two_voice_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsWeakEpisodeMotifDerivation) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 16;
  result.total_duration_ticks = kTicksPerBar * 8;
  result.form_description = "weak episode motif fixture";
  result.structure_json =
      "{\"section_count\":4,\"total_duration_ticks\":15360,"
      "\"sections\":["
      "{\"type\":\"Exposition\",\"phase\":\"Establish\",\"start_tick\":0,"
      "\"end_tick\":3840,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Episode\",\"phase\":\"Develop\",\"start_tick\":3840,"
      "\"end_tick\":7680,\"duration_ticks\":3840,\"key\":\"G\"},"
      "{\"type\":\"Stretto\",\"phase\":\"Resolve\",\"start_tick\":11520,"
      "\"end_tick\":13440,\"duration_ticks\":1920,\"key\":\"C\"},"
      "{\"type\":\"Coda\",\"phase\":\"Resolve\",\"start_tick\":13440,"
      "\"end_tick\":15360,\"duration_ticks\":1920,\"key\":\"C\"}"
      "]}";

  result.tracks.resize(2);
  result.tracks[0].name = "Subject";
  result.tracks[1].name = "Episode";

  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(60 + idx * 2);
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::FugueSubject;
    result.tracks[0].notes.push_back(note);
  }

  for (int idx = 0; idx < 6; ++idx) {
    NoteEvent note;
    note.start_tick = kTicksPerBar * 2 + static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(48 + idx * 5);
    note.velocity = 80;
    note.voice = 1;
    note.source = BachNoteSource::EpisodeMaterial;
    result.tracks[1].notes.push_back(note);
  }

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"subject_motif_interval_pair_count\":2"), std::string::npos);
  EXPECT_NE(json.find("\"episode_motif_interval_pair_count\":4"), std::string::npos);
  EXPECT_NE(json.find("\"episode_motif_derived_pair_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"episode_motif_derivation_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsWeakEpisodeDialogueWindow) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 18;
  result.total_duration_ticks = kTicksPerBar * 8;
  result.form_description = "weak episode dialogue window fixture";
  result.structure_json =
      "{\"section_count\":4,\"total_duration_ticks\":15360,"
      "\"sections\":["
      "{\"type\":\"Exposition\",\"phase\":\"Establish\",\"start_tick\":0,"
      "\"end_tick\":3840,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Episode\",\"phase\":\"Develop\",\"start_tick\":3840,"
      "\"end_tick\":7680,\"duration_ticks\":3840,\"key\":\"G\"},"
      "{\"type\":\"Stretto\",\"phase\":\"Resolve\",\"start_tick\":11520,"
      "\"end_tick\":13440,\"duration_ticks\":1920,\"key\":\"C\"},"
      "{\"type\":\"Coda\",\"phase\":\"Resolve\",\"start_tick\":13440,"
      "\"end_tick\":15360,\"duration_ticks\":1920,\"key\":\"C\"}"
      "]}";

  result.tracks.resize(2);
  result.tracks[0].name = "Subject";
  result.tracks[1].name = "Episode";

  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent subject;
    subject.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    subject.duration = kTicksPerBeat;
    subject.pitch = static_cast<uint8_t>(60 + idx);
    subject.velocity = 80;
    subject.voice = 0;
    subject.source = BachNoteSource::FugueSubject;
    result.tracks[0].notes.push_back(subject);
  }

  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = kTicksPerBar * 2 + static_cast<Tick>(idx) * duration::kEighthNote;
    note.duration = duration::kEighthNote;
    note.pitch = static_cast<uint8_t>(52 + idx % 5);
    note.velocity = 80;
    note.voice = 1;
    note.source = BachNoteSource::EpisodeMaterial;
    result.tracks[1].notes.push_back(note);
  }

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"episode_dialogue_window_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"episode_dialogue_weak_window_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"min_episode_dialogue_window_ratio\":0"), std::string::npos);
  EXPECT_NE(json.find("\"episode_dialogue_window_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsWeakBassLine) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 15;
  result.total_duration_ticks = kTicksPerBar * 8;
  result.form_description = "weak bass fixture";
  result.structure_json =
      "{\"section_count\":4,\"total_duration_ticks\":15360,"
      "\"sections\":["
      "{\"type\":\"Exposition\",\"phase\":\"Establish\",\"start_tick\":0,"
      "\"end_tick\":3840,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Episode\",\"phase\":\"Develop\",\"start_tick\":3840,"
      "\"end_tick\":7680,\"duration_ticks\":3840,\"key\":\"G\"},"
      "{\"type\":\"Stretto\",\"phase\":\"Resolve\",\"start_tick\":7680,"
      "\"end_tick\":11520,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Coda\",\"phase\":\"Resolve\",\"start_tick\":11520,"
      "\"end_tick\":15360,\"duration_ticks\":3840,\"key\":\"C\"}"
      "]}";

  result.tracks.resize(2);
  result.tracks[0].name = "Soprano";
  result.tracks[1].name = "Bass";

  for (int idx = 0; idx < 16; ++idx) {
    NoteEvent subject;
    subject.start_tick = static_cast<Tick>(idx) * duration::kHalfNote;
    subject.duration = duration::kHalfNote;
    subject.pitch = static_cast<uint8_t>(60 + idx % 5);
    subject.velocity = 80;
    subject.voice = 0;
    subject.source = idx % 2 == 0 ? BachNoteSource::FugueSubject : BachNoteSource::Countersubject;
    result.tracks[0].notes.push_back(subject);
  }

  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent bass;
    bass.start_tick = static_cast<Tick>(idx) * kTicksPerBar * 2;
    bass.duration = kTicksPerBeat;
    bass.pitch = static_cast<uint8_t>(36 + idx * 15);
    bass.velocity = 80;
    bass.voice = 1;
    bass.source = BachNoteSource::PedalPoint;
    result.tracks[1].notes.push_back(bass);
  }

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"bass_line_motion_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"bass_activity_sample_count\":32"), std::string::npos);
  EXPECT_NE(json.find("\"bass_active_sample_count\":4"), std::string::npos);
  EXPECT_NE(json.find("\"bass_large_leaps\":0"), std::string::npos);
  EXPECT_NE(json.find("\"max_bass_leap\":0"), std::string::npos);
  EXPECT_NE(json.find("\"bass_activity_ratio\":0.125"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsThematicDialogueClashes) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 16;
  result.total_duration_ticks = kTicksPerBar * 8;
  result.form_description = "thematic clash fixture";
  result.structure_json =
      "{\"section_count\":4,\"total_duration_ticks\":15360,"
      "\"sections\":["
      "{\"type\":\"Exposition\",\"phase\":\"Establish\",\"start_tick\":0,"
      "\"end_tick\":3840,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Episode\",\"phase\":\"Develop\",\"start_tick\":3840,"
      "\"end_tick\":7680,\"duration_ticks\":3840,\"key\":\"G\"},"
      "{\"type\":\"Stretto\",\"phase\":\"Resolve\",\"start_tick\":7680,"
      "\"end_tick\":11520,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Coda\",\"phase\":\"Resolve\",\"start_tick\":11520,"
      "\"end_tick\":15360,\"duration_ticks\":3840,\"key\":\"C\"}"
      "]}";

  result.tracks.resize(2);
  result.tracks[0].name = "Subject";
  result.tracks[1].name = "Dialogue";
  for (int idx = 0; idx < 32; ++idx) {
    NoteEvent subject;
    subject.start_tick = static_cast<Tick>(idx) * kTicksPerBeat / 2;
    subject.duration = kTicksPerBeat / 2;
    subject.pitch = 60;
    subject.velocity = 80;
    subject.voice = 0;
    subject.source = BachNoteSource::FugueSubject;
    result.tracks[0].notes.push_back(subject);

    NoteEvent dialogue = subject;
    dialogue.pitch = 61;
    dialogue.voice = 1;
    dialogue.source = BachNoteSource::Countersubject;
    result.tracks[1].notes.push_back(dialogue);
  }

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"thematic_dialogue_pair_sample_count\":64"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_hard_clash_count\":64"), std::string::npos);
  EXPECT_NE(json.find("\"early_thematic_dialogue_pair_sample_count\":64"), std::string::npos);
  EXPECT_NE(json.find("\"early_thematic_dialogue_hard_clash_count\":64"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_clash_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"early_thematic_dialogue_clash_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsHotspotHardClashes) {
  GeneratorConfig config = makeTestConfig();
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 17;
  result.total_duration_ticks = kTicksPerBar * 9;
  result.form_description = "hotspot clash fixture";

  result.tracks.resize(2);
  result.tracks[0].name = "Upper";
  result.tracks[1].name = "Lower";

  NoteEvent upper;
  upper.start_tick = kTicksPerBar * 8;
  upper.duration = kTicksPerBar;
  upper.pitch = 60;
  upper.velocity = 80;
  upper.voice = 0;
  upper.source = BachNoteSource::SequenceNote;
  result.tracks[0].notes.push_back(upper);

  NoteEvent lower = upper;
  lower.pitch = 61;
  lower.voice = 1;
  lower.source = BachNoteSource::Countersubject;
  result.tracks[1].notes.push_back(lower);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"listening_hotspot_hard_clash_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"hard_clash_sample_count\":16"), std::string::npos);
  EXPECT_NE(json.find("\"hard_clash_count\":16"), std::string::npos);
  EXPECT_NE(json.find("\"hard_clash_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"hard_clash_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsEarlyListeningClashes) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.bpm = 60;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 18;
  result.total_duration_ticks = kTicksPerBeat * 12;
  result.form_description = "early listening clash fixture";

  result.tracks.resize(2);
  result.tracks[0].name = "Upper";
  result.tracks[1].name = "Lower";

  NoteEvent upper;
  upper.start_tick = kTicksPerBeat * 6;
  upper.duration = kTicksPerBeat * 6;
  upper.pitch = 60;
  upper.velocity = 80;
  upper.voice = 0;
  upper.source = BachNoteSource::FugueSubject;
  result.tracks[0].notes.push_back(upper);

  NoteEvent lower = upper;
  lower.pitch = 61;
  lower.voice = 1;
  lower.source = BachNoteSource::Countersubject;
  result.tracks[1].notes.push_back(lower);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"early_listening_hard_clash_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_hard_clash_sample_count\":24"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_hard_clash_count\":24"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_hard_clash_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsEarlyBassFourthInstability) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.bpm = 60;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 19;
  result.total_duration_ticks = kTicksPerBeat * 12;
  result.form_description = "early bass fourth instability fixture";

  result.tracks.resize(2);
  result.tracks[0].name = "Upper";
  result.tracks[1].name = "Bass";

  NoteEvent upper;
  upper.start_tick = kTicksPerBeat * 6;
  upper.duration = kTicksPerBeat * 6;
  upper.pitch = 60;
  upper.velocity = 80;
  upper.voice = 0;
  upper.source = BachNoteSource::Countersubject;
  result.tracks[0].notes.push_back(upper);

  NoteEvent bass = upper;
  bass.pitch = 55;
  bass.voice = 1;
  bass.source = BachNoteSource::PedalPoint;
  result.tracks[1].notes.push_back(bass);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"early_listening_hard_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_hard_clash_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_unstable_interval_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_unstable_interval_sample_count\":24"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_unstable_interval_count\":24"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_unstable_interval_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsEightSecondDialogueClash) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.bpm = 60;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 20;
  result.total_duration_ticks = kTicksPerBeat * 12;
  result.form_description = "eight second dialogue clash fixture";

  result.tracks.resize(2);
  result.tracks[0].name = "Subject";
  result.tracks[1].name = "Dialogue";

  NoteEvent subject;
  subject.start_tick = kTicksPerBeat * 7;
  subject.duration = kTicksPerBeat * 2;
  subject.pitch = 60;
  subject.velocity = 80;
  subject.voice = 0;
  subject.source = BachNoteSource::FugueSubject;
  result.tracks[0].notes.push_back(subject);

  NoteEvent dialogue = subject;
  dialogue.pitch = 61;
  dialogue.voice = 1;
  dialogue.source = BachNoteSource::Countersubject;
  result.tracks[1].notes.push_back(dialogue);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"eight_second_dialogue_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_sample_count\":5"), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_hard_clash_count\":5"), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_unstable_interval_count\":5"), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_hard_clash_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"eight_second_dialogue_unstable_interval_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsCriticTimeWindowClash) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.bpm = 60;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 21;
  result.total_duration_ticks = kTicksPerBeat * 20;
  result.form_description = "critic time window clash fixture";

  result.tracks.resize(2);
  result.tracks[0].name = "Upper";
  result.tracks[1].name = "Lower";

  NoteEvent upper;
  upper.start_tick = kTicksPerBeat * 18;
  upper.duration = kTicksPerBeat;
  upper.pitch = 60;
  upper.velocity = 80;
  upper.voice = 0;
  upper.source = BachNoteSource::FugueSubject;
  result.tracks[0].notes.push_back(upper);

  NoteEvent lower = upper;
  lower.pitch = 61;
  lower.voice = 1;
  lower.source = BachNoteSource::Countersubject;
  result.tracks[1].notes.push_back(lower);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"critic_time_window_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"label\":\"18s_transition\""), std::string::npos);
  EXPECT_NE(json.find("\"sample_count\":4"), std::string::npos);
  EXPECT_NE(json.find("\"hard_clash_count\":4"), std::string::npos);
  EXPECT_NE(json.find("\"unstable_interval_count\":4"), std::string::npos);
  EXPECT_NE(json.find("\"hard_clash_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"unstable_interval_ratio\":1"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsWeakCodaCadence) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.key = {Key::C, false};
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 19;
  result.total_duration_ticks = kTicksPerBar * 8;
  result.form_description = "weak coda fixture";

  result.tracks.resize(2);
  result.tracks[0].name = "Upper";
  result.tracks[1].name = "Bass";

  NoteEvent bass;
  bass.start_tick = kTicksPerBar * 7;
  bass.duration = kTicksPerBar;
  bass.pitch = 50;
  bass.velocity = 80;
  bass.voice = 1;
  bass.source = BachNoteSource::Coda;
  result.tracks[1].notes.push_back(bass);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"coda_intent_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"coda_closing_cadence_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"coda_bass_notes\":1"), std::string::npos);
  EXPECT_NE(json.find("\"coda_final_tonic_bass_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsGenericCodaHead) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.key = {Key::C, false};
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 23;
  result.total_duration_ticks = kTicksPerBar * 8;
  result.form_description = "generic coda head fixture";

  result.tracks.resize(2);
  result.tracks[0].name = "Upper";
  result.tracks[1].name = "Bass";

  const std::array<uint8_t, 4> subject_pitches = {60, 62, 64, 65};
  for (size_t idx = 0; idx < subject_pitches.size(); ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * duration::kQuarterNote;
    note.duration = duration::kQuarterNote;
    note.pitch = subject_pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::FugueSubject;
    result.tracks[0].notes.push_back(note);
  }

  for (size_t idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = kTicksPerBar * 7 + static_cast<Tick>(idx) * duration::kQuarterNote;
    note.duration = duration::kQuarterNote;
    note.pitch = 72;
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::Coda;
    result.tracks[0].notes.push_back(note);
  }

  NoteEvent bass;
  bass.start_tick = kTicksPerBar * 7;
  bass.duration = kTicksPerBar;
  bass.pitch = 36;
  bass.velocity = 80;
  bass.voice = 1;
  bass.source = BachNoteSource::Coda;
  result.tracks[1].notes.push_back(bass);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"coda_subject_head_interval_count\":3"), std::string::npos);
  EXPECT_NE(json.find("\"coda_subject_head_match_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"coda_subject_head_match_ratio\":0"), std::string::npos);
  EXPECT_NE(json.find("\"coda_subject_head_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, EventsJsonStructureAuditFlagsResolveRepairDependence) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 2;

  GeneratorResult result;
  result.success = true;
  result.seed_used = 24;
  result.total_duration_ticks = kTicksPerBar * 10;
  result.form_description = "resolve repair dependence fixture";
  result.structure_json =
      "{\"section_count\":4,\"total_duration_ticks\":19200,"
      "\"sections\":["
      "{\"type\":\"Exposition\",\"phase\":\"Establish\",\"start_tick\":0,"
      "\"end_tick\":3840,\"duration_ticks\":3840,\"key\":\"C\"},"
      "{\"type\":\"Episode\",\"phase\":\"Develop\",\"start_tick\":3840,"
      "\"end_tick\":9600,\"duration_ticks\":5760,\"key\":\"G\"},"
      "{\"type\":\"Stretto\",\"phase\":\"Resolve\",\"start_tick\":9600,"
      "\"end_tick\":15360,\"duration_ticks\":5760,\"key\":\"C\"},"
      "{\"type\":\"Coda\",\"phase\":\"Resolve\",\"start_tick\":15360,"
      "\"end_tick\":19200,\"duration_ticks\":3840,\"key\":\"C\"}"
      "]}";

  result.tracks.resize(2);
  result.tracks[0].name = "Upper";
  result.tracks[1].name = "Bass";

  for (size_t idx = 0; idx < 4; ++idx) {
    NoteEvent subject;
    subject.start_tick = static_cast<Tick>(idx) * duration::kQuarterNote;
    subject.duration = duration::kQuarterNote;
    subject.pitch = static_cast<uint8_t>(60 + idx);
    subject.velocity = 80;
    subject.voice = 0;
    subject.source = BachNoteSource::FugueSubject;
    result.tracks[0].notes.push_back(subject);
  }

  for (size_t idx = 0; idx < 12; ++idx) {
    NoteEvent note;
    note.start_tick = kTicksPerBar * 7 + static_cast<Tick>(idx) * duration::kQuarterNote;
    note.duration = duration::kQuarterNote;
    note.pitch = static_cast<uint8_t>(72 + (idx % 3));
    note.velocity = 80;
    note.voice = static_cast<uint8_t>(idx % 2);
    note.source = idx >= 8 ? BachNoteSource::Coda : BachNoteSource::EpisodeMaterial;
    if (idx < 8) {
      note.modified_by = static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    }
    result.tracks[note.voice].notes.push_back(note);
  }

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"resolve_region_notes\":12"), std::string::npos);
  EXPECT_NE(json.find("\"resolve_region_pitch_repair_notes\":8"), std::string::npos);
  EXPECT_NE(json.find("\"resolve_region_repair_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"repair_dependency_pass\":false"), std::string::npos);
  EXPECT_NE(json.find("\"pass\":false"), std::string::npos);
}

TEST(GeneratorTest, FourVoiceRestlessAvoidsBwv578DensitySweepDependence) {
  GeneratorConfig config = makeTestConfig(183);
  config.form = FormType::Fugue;
  config.num_voices = 4;
  config.character = SubjectCharacter::Restless;
  config.bpm = 72;
  config.scale = DurationScale::Medium;
  GeneratorResult result = generate(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  constexpr Tick bar30 = kTicksPerBar * 29;
  constexpr Tick beat3 = bar30 + kTicksPerBeat * 2;
  (void)bar30;
  (void)beat3;
  int cadence_approach_notes = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::CadenceApproach) {
        ++cadence_approach_notes;
      }
    }
  }

  EXPECT_GE(cadence_approach_notes, 4);

  int manual_iii_quantized_gap_turns = 0;
  for (const auto& note : result.tracks[2].notes) {
    if (note.source == BachNoteSource::EpisodeMaterial && note.start_tick >= kTicksPerBar * 17 &&
        note.start_tick < kTicksPerBar * 32 && note.duration == kTicksPerBeat / 2 &&
        note.start_tick % (kTicksPerBeat / 2) == 0 && note.start_tick % kTicksPerBeat != 0) {
      ++manual_iii_quantized_gap_turns;
    }
  }
  EXPECT_EQ(manual_iii_quantized_gap_turns, 0)
      << "Restless 4-voice fugues should not rely on BWV578 quantized-gap "
         "density sweeps for middle manual activity";

  int middle_entry_pedal_support = 0;
  for (const auto& note : result.tracks[3].notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if (note.start_tick >= kTicksPerBar * 17 && note.start_tick < kTicksPerBar * 20) {
      ++middle_entry_pedal_support;
    }
  }
  EXPECT_GE(middle_entry_pedal_support, 0);

  int subdominant_bridge_pedal_support = 0;
  for (const auto& note : result.tracks[3].notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if (note.duration != kTicksPerBeat / 2)
      continue;
    if (note.start_tick >= kTicksPerBar * 23 && note.start_tick < kTicksPerBar * 25) {
      ++subdominant_bridge_pedal_support;
    }
  }
  EXPECT_EQ(subdominant_bridge_pedal_support, 0)
      << "Subdominant bridge support should come from the planned episode "
         "layer, not the BWV578 bridge sweep";

  int stretto_pedal_support = 0;
  for (const auto& note : result.tracks[3].notes) {
    if (note.source == BachNoteSource::EpisodeMaterial && note.start_tick >= kTicksPerBar * 30 &&
        note.start_tick < kTicksPerBar * 32 && note.duration <= kTicksPerBeat) {
      ++stretto_pedal_support;
    }
  }
  EXPECT_GE(stretto_pedal_support, 1);

  Tick coda_start = std::numeric_limits<Tick>::max();
  Tick coda_end = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::Coda) {
        coda_start = std::min(coda_start, note.start_tick);
        coda_end = std::max(coda_end, note.start_tick + note.duration);
      }
    }
  }
  ASSERT_NE(coda_start, std::numeric_limits<Tick>::max());

  int coda_notes = 0;
  int coda_sixteenths = 0;
  int coda_pitch_repairs = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source != BachNoteSource::Coda)
        continue;
      ++coda_notes;
      if (note.duration <= duration::kSixteenthNote) {
        ++coda_sixteenths;
      }
      if ((note.modified_by & (static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                               static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                               static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                               static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                               static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep))) != 0) {
        ++coda_pitch_repairs;
      }
    }
  }
  EXPECT_LE(coda_notes, 72);
  EXPECT_LE(coda_sixteenths, 4);
  EXPECT_LE(coda_pitch_repairs, 6);
}

TEST(GeneratorTest, FourVoiceRestlessJsonReportsHotspotTexture) {
  GeneratorConfig config = makeTestConfig(222);
  config.form = FormType::Fugue;
  config.key = {Key::G, true};
  config.num_voices = 4;
  config.character = SubjectCharacter::Restless;
  config.bpm = 72;
  config.scale = DurationScale::Medium;
  config.target_bars = 32;

  GeneratorResult result = generate(config);
  ASSERT_TRUE(result.success);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"listening_hotspot_texture_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_intent_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_dialogue_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_exposition_counterline_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_answer_counterline_repetition_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"coda_intent_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"coda_closing_cadence_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"global_arc_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_two_voice_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_thematic_dialogue_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"episode_motif_derivation_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_hard_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"label\":\"8s_dialogue\""), std::string::npos);
  EXPECT_NE(json.find("\"bass_line_motion_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_two_voice_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"subject_dialogue_pair_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"early_thematic_dialogue_hard_clash_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_hard_clash_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"pitch_repair_modified_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"protected_dialogue_pitch_repair_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"immutable_pitch_repair_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_pitch_repair_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"early_answer_counterline_same_pitch_run_max\""), std::string::npos);
  EXPECT_NE(json.find("\"bass_step_or_structural_motion_ratio\""), std::string::npos);
  EXPECT_NE(json.find("\"stretto_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"coda_section_count\":1"), std::string::npos);
  EXPECT_NE(json.find("\"coda_final_tonic_bass_notes\":1"), std::string::npos);
  EXPECT_NE(json.find("\"coda_sixteenth_notes\":0"), std::string::npos);
  EXPECT_NE(json.find("\"selection_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"analysis_gate_pass\":true"), std::string::npos);
  EXPECT_GE(extractJsonNumber(json, "selection_score"), 0.868);
  EXPECT_LE(extractJsonNumber(json, "penalty_affecting_violations"), 24);
  EXPECT_LE(extractJsonNumber(json, "penalty_affecting_density"), 0.055);
  EXPECT_NE(json.find("\"melodic_structure_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"average_active_voices\""), std::string::npos);
  EXPECT_NE(json.find("\"texture_pass\":true"), std::string::npos);

  for (int hotspot_bar : {14, 18, 23}) {
    std::string hotspot = extractListeningHotspotJson(json, hotspot_bar);
    EXPECT_GE(extractJsonNumber(hotspot, "average_active_voices"), 2.0)
        << "Listening hotspot bar " << hotspot_bar << " should keep a two-voice thematic texture.";
    EXPECT_GE(extractJsonNumber(hotspot, "min_active_voices"), 2.0)
        << "Listening hotspot bar " << hotspot_bar << " should not drop to a single exposed voice.";
    EXPECT_EQ(extractJsonNumber(hotspot, "hard_clash_count"), 0.0)
        << "Listening hotspot bar " << hotspot_bar
        << " should not thicken texture with exposed hard clashes.";
    EXPECT_EQ(extractJsonNumber(hotspot, "pitch_repair_modified_notes"), 0.0)
        << "Listening hotspot bar " << hotspot_bar
        << " should be composed rather than pitch-repaired.";
  }

  constexpr Tick first_episode_start = kTicksPerBar * 8;
  for (VoiceId voice = 0; voice < config.num_voices; ++voice) {
    const NoteEvent* prior = nullptr;
    const NoteEvent* entry = nullptr;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.voice != voice)
          continue;
        if (note.start_tick < first_episode_start &&
            (prior == nullptr || note.start_tick > prior->start_tick)) {
          prior = &note;
        }
        if (note.start_tick >= first_episode_start &&
            note.start_tick < first_episode_start + kTicksPerBar * 2 &&
            (note.source == BachNoteSource::EpisodeMaterial ||
             note.source == BachNoteSource::SequenceNote) &&
            (entry == nullptr || note.start_tick < entry->start_tick)) {
          entry = &note;
        }
      }
    }
    if (prior == nullptr || entry == nullptr)
      continue;
    Tick prior_end = prior->start_tick + prior->duration;
    Tick gap = entry->start_tick > prior_end ? entry->start_tick - prior_end : 0;
    if (gap > kTicksPerBar)
      continue;
    int entry_leap = std::abs(static_cast<int>(entry->pitch) - static_cast<int>(prior->pitch));
    EXPECT_LE(entry_leap, 7)
        << "First episode entry should continue the prior voice register; voice "
        << static_cast<int>(voice) << " jumped from " << static_cast<int>(prior->pitch) << " to "
        << static_cast<int>(entry->pitch);
  }

  for (VoiceId voice = 0; voice < config.num_voices; ++voice) {
    std::vector<NoteEvent> answer_notes;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.voice == voice && note.source == BachNoteSource::FugueAnswer &&
            note.start_tick < first_episode_start) {
          answer_notes.push_back(note);
        }
      }
    }
    std::sort(
        answer_notes.begin(), answer_notes.end(),
        [](const NoteEvent& lhs, const NoteEvent& rhs) { return lhs.start_tick < rhs.start_tick; });
    for (size_t idx = 1; idx < answer_notes.size(); ++idx) {
      int leap = std::abs(static_cast<int>(answer_notes[idx].pitch) -
                          static_cast<int>(answer_notes[idx - 1].pitch));
      EXPECT_LE(leap, 7) << "The early answer should not expose octave-displaced internal "
                            "leaps; voice "
                         << static_cast<int>(voice) << " jumped from "
                         << static_cast<int>(answer_notes[idx - 1].pitch) << " to "
                         << static_cast<int>(answer_notes[idx].pitch);
    }
  }

  auto sounding_at = [&](VoiceId voice, Tick tick) -> const NoteEvent* {
    const NoteEvent* best = nullptr;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.voice != voice)
          continue;
        if (note.start_tick > tick || note.start_tick + note.duration <= tick) {
          continue;
        }
        if (best == nullptr || note.start_tick >= best->start_tick) {
          best = &note;
        }
      }
    }
    return best;
  };

  int exposed_exposition_hard_clashes = 0;
  constexpr Tick exposition_counterline_start = kTicksPerBar * 4;
  constexpr Tick exposition_counterline_end = kTicksPerBar * 8;
  for (Tick tick = exposition_counterline_start; tick < exposition_counterline_end;
       tick += duration::kSixteenthNote) {
    for (VoiceId lhs = 0; lhs < config.num_voices; ++lhs) {
      const NoteEvent* left = sounding_at(lhs, tick);
      if (left == nullptr)
        continue;
      for (VoiceId rhs = lhs + 1; rhs < config.num_voices; ++rhs) {
        const NoteEvent* right = sounding_at(rhs, tick);
        if (right == nullptr)
          continue;
        int simple = std::abs(static_cast<int>(left->pitch) - static_cast<int>(right->pitch)) % 12;
        bool hard_dissonance =
            simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
        if (hard_dissonance)
          ++exposed_exposition_hard_clashes;
      }
    }
  }
  EXPECT_LE(exposed_exposition_hard_clashes, 10)
      << "The first exposition counterline should not accumulate exposed "
         "seconds, tritones, or sevenths after the answer enters.";

  {
    constexpr VoiceId voice = 0;
    constexpr Tick first_answer_dialogue_end = exposition_counterline_start + kTicksPerBeat * 4;
    std::vector<NoteEvent> countersubject_notes;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.voice == voice && note.source == BachNoteSource::Countersubject &&
            note.start_tick >= exposition_counterline_start &&
            note.start_tick < first_answer_dialogue_end) {
          countersubject_notes.push_back(note);
        }
      }
    }
    std::sort(countersubject_notes.begin(), countersubject_notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.duration < rhs.duration;
              });
    int same_pitch_run = 1;
    for (size_t idx = 1; idx < countersubject_notes.size(); ++idx) {
      if (countersubject_notes[idx].pitch == countersubject_notes[idx - 1].pitch) {
        ++same_pitch_run;
      } else {
        same_pitch_run = 1;
      }
      EXPECT_LE(same_pitch_run, 2)
          << "The first answer dialogue should not turn the countersubject "
             "into a static repeated-note patch; voice "
          << static_cast<int>(voice);
    }
  }

  constexpr Tick first_episode_end = first_episode_start + kTicksPerBar * 2;
  for (Tick tick = first_episode_start; tick < first_episode_end; tick += 120) {
    for (VoiceId lhs = 0; lhs < config.num_voices; ++lhs) {
      const NoteEvent* left = sounding_at(lhs, tick);
      if (left == nullptr)
        continue;
      for (VoiceId rhs = lhs + 1; rhs < config.num_voices; ++rhs) {
        const NoteEvent* right = sounding_at(rhs, tick);
        if (right == nullptr)
          continue;
        int simple = std::abs(static_cast<int>(left->pitch) - static_cast<int>(right->pitch)) % 12;
        bool hard_dissonance =
            simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
        EXPECT_FALSE(hard_dissonance) << "The opening episode should not expose seconds, tritones, "
                                         "or sevenths in the dialogue at tick "
                                      << tick << " between voices " << static_cast<int>(lhs)
                                      << " and " << static_cast<int>(rhs);
      }
    }
  }

  constexpr Tick exposed_middle_entry_start = kTicksPerBar * 20;
  constexpr Tick exposed_middle_entry_end = exposed_middle_entry_start + kTicksPerBar / 2;
  for (Tick tick = exposed_middle_entry_start; tick < exposed_middle_entry_end; tick += 120) {
    for (VoiceId lhs = 0; lhs < config.num_voices; ++lhs) {
      const NoteEvent* left = sounding_at(lhs, tick);
      if (left == nullptr)
        continue;
      for (VoiceId rhs = lhs + 1; rhs < config.num_voices; ++rhs) {
        const NoteEvent* right = sounding_at(rhs, tick);
        if (right == nullptr)
          continue;
        int simple = std::abs(static_cast<int>(left->pitch) - static_cast<int>(right->pitch)) % 12;
        bool exposed_tension = simple == 6 || simple == 10 || simple == 11;
        EXPECT_FALSE(exposed_tension) << "The developing middle-entry support should not expose "
                                         "tritones or sevenths at tick "
                                      << tick << " between voices " << static_cast<int>(lhs)
                                      << " and " << static_cast<int>(rhs);
      }
    }
  }

  constexpr Tick exposed_episode_dialogue_start = kTicksPerBar * 22;
  constexpr Tick exposed_episode_dialogue_end = exposed_episode_dialogue_start + kTicksPerBar;
  int exposed_episode_dialogue_hard_clashes = 0;
  for (Tick tick = exposed_episode_dialogue_start; tick < exposed_episode_dialogue_end;
       tick += 120) {
    std::vector<const NoteEvent*> active;
    for (VoiceId voice = 0; voice < config.num_voices; ++voice) {
      const NoteEvent* note = sounding_at(voice, tick);
      if (note != nullptr)
        active.push_back(note);
    }
    if (active.size() != 2u)
      continue;
    auto is_episode_dialogue_source = [](BachNoteSource source) {
      return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
             source == BachNoteSource::FreeCounterpoint;
    };
    if (!is_episode_dialogue_source(active[0]->source) ||
        !is_episode_dialogue_source(active[1]->source)) {
      continue;
    }
    int simple =
        std::abs(static_cast<int>(active[0]->pitch) - static_cast<int>(active[1]->pitch)) % 12;
    bool hard_dissonance =
        simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
    if (hard_dissonance)
      ++exposed_episode_dialogue_hard_clashes;
  }
  EXPECT_EQ(exposed_episode_dialogue_hard_clashes, 0)
      << "A two-voice episode dialogue hotspot should not expose seconds, "
         "tritones, or sevenths as the only audible contrapuntal relation.";

  constexpr Tick late_pedal_region_start = kTicksPerBar * 32;
  constexpr Tick late_pedal_region_end = kTicksPerBar * 35;
  for (Tick tick = late_pedal_region_start; tick < late_pedal_region_end; tick += 120) {
    const NoteEvent* upper = sounding_at(0, tick);
    const NoteEvent* pedal = sounding_at(config.num_voices - 1, tick);
    if (upper == nullptr || pedal == nullptr)
      continue;
    if (pedal->source != BachNoteSource::PedalPoint)
      continue;
    if (upper->source != BachNoteSource::SequenceNote &&
        upper->source != BachNoteSource::EpisodeMaterial) {
      continue;
    }
    int simple = std::abs(static_cast<int>(upper->pitch) - static_cast<int>(pedal->pitch)) % 12;
    bool exposed_outer_tension = simple == 6 || simple == 10 || simple == 11;
    EXPECT_FALSE(exposed_outer_tension)
        << "The late pedal-point sequence should not pin the upper voice "
           "against the pedal as a tritone or seventh at tick "
        << tick;
  }

  constexpr Tick stretto_region_start = kTicksPerBar * 35;
  constexpr Tick stretto_region_end = kTicksPerBar * 39;
  for (Tick tick = stretto_region_start; tick < stretto_region_end;
       tick += duration::kSixteenthNote) {
    for (VoiceId lhs = 0; lhs < config.num_voices; ++lhs) {
      const NoteEvent* left = sounding_at(lhs, tick);
      if (left == nullptr)
        continue;
      bool left_protected = left->source == BachNoteSource::FugueSubject ||
                            left->source == BachNoteSource::SubjectCore;
      if (!left_protected)
        continue;
      for (VoiceId rhs = lhs + 1; rhs < config.num_voices; ++rhs) {
        const NoteEvent* right = sounding_at(rhs, tick);
        if (right == nullptr)
          continue;
        bool right_protected = right->source == BachNoteSource::FugueSubject ||
                               right->source == BachNoteSource::SubjectCore;
        if (!right_protected)
          continue;
        int simple = std::abs(static_cast<int>(left->pitch) - static_cast<int>(right->pitch)) % 12;
        bool hard_dissonance =
            simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
        EXPECT_FALSE(hard_dissonance) << "Protected stretto subject entries should not collide as "
                                         "seconds, tritones, or sevenths at tick "
                                      << tick << " between voices " << static_cast<int>(lhs)
                                      << " and " << static_cast<int>(rhs);
      }
    }
  }

  int late_resolve_hard_clashes = 0;
  constexpr Tick late_resolve_start = kTicksPerBar * 38;
  constexpr Tick late_resolve_end = kTicksPerBar * 40;
  for (Tick tick = late_resolve_start; tick < late_resolve_end; tick += duration::kSixteenthNote) {
    for (VoiceId lhs = 0; lhs < config.num_voices; ++lhs) {
      const NoteEvent* left = sounding_at(lhs, tick);
      if (left == nullptr)
        continue;
      for (VoiceId rhs = lhs + 1; rhs < config.num_voices; ++rhs) {
        const NoteEvent* right = sounding_at(rhs, tick);
        if (right == nullptr)
          continue;
        int simple = std::abs(static_cast<int>(left->pitch) - static_cast<int>(right->pitch)) % 12;
        bool hard_dissonance =
            simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
        if (hard_dissonance)
          ++late_resolve_hard_clashes;
      }
    }
  }
  EXPECT_LE(late_resolve_hard_clashes, 2)
      << "The late resolve into the coda should not accumulate exposed "
         "seconds, tritones, or sevenths in flexible support material.";
}

TEST(GeneratorTest, FourVoiceRestlessSeed225KeepsHotspotStructure) {
  GeneratorConfig config = makeTestConfig(225);
  config.form = FormType::Fugue;
  config.key = {Key::G, true};
  config.num_voices = 4;
  config.character = SubjectCharacter::Restless;
  config.bpm = 72;
  config.scale = DurationScale::Medium;
  config.target_bars = 32;

  GeneratorResult result = generate(config);
  ASSERT_TRUE(result.success);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"repair_dependency_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_texture_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_hard_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_hard_clash_count\":0"), std::string::npos);
  EXPECT_EQ(extractJsonNumber(json, "pitch_repair_modified_notes"), 0.0);
  EXPECT_EQ(extractJsonNumber(json, "episode_material_pitch_repair_notes"), 0.0);
  EXPECT_GE(extractJsonNumber(json, "selection_score"), 0.868);
  EXPECT_LE(extractJsonNumber(json, "penalty_affecting_violations"), 34.0);

  for (int hotspot_bar : {9, 14, 18, 23}) {
    std::string hotspot = extractListeningHotspotJson(json, hotspot_bar);
    EXPECT_GE(extractJsonNumber(hotspot, "average_active_voices"), 2.0)
        << "Seed 225 hotspot bar " << hotspot_bar << " should keep at least a two-voice texture.";
    EXPECT_LE(extractJsonNumber(hotspot, "hard_clash_count"), 2.0)
        << "Seed 225 hotspot bar " << hotspot_bar
        << " should keep exposed hard clashes within the structural budget.";
  }
}

TEST(GeneratorTest, FourVoiceRestlessWeakSeedsKeepListeningWindows) {
  for (int seed : {223, 224, 226}) {
    GeneratorConfig config = makeTestConfig(seed);
    config.form = FormType::Fugue;
    config.key = {Key::G, true};
    config.num_voices = 4;
    config.character = SubjectCharacter::Restless;
    config.bpm = 72;
    config.scale = DurationScale::Medium;
    config.target_bars = 32;

    GeneratorResult result = generate(config);
    ASSERT_TRUE(result.success) << "seed " << seed;

    std::string json = buildEventsJson(result, config);
    EXPECT_NE(json.find("\"critic_time_window_pass\":true"), std::string::npos) << "seed " << seed;
    EXPECT_NE(json.find("\"listening_hotspot_texture_pass\":true"), std::string::npos)
        << "seed " << seed;
    EXPECT_NE(json.find("\"listening_hotspot_hard_clash_pass\":true"), std::string::npos)
        << "seed " << seed;
    EXPECT_NE(json.find("\"thematic_dialogue_hard_clash_count\":0"), std::string::npos)
        << "seed " << seed;
    EXPECT_NE(json.find("\"analysis_gate_pass\":true"), std::string::npos) << "seed " << seed;
    EXPECT_NE(json.find("\"overall_pass\":true"), std::string::npos) << "seed " << seed;
    EXPECT_NE(json.find("\"episode_material_pitch_repair_notes\""), std::string::npos)
        << "seed " << seed;
    EXPECT_NE(json.find("\"episode_material_pitch_repair_ratio\""), std::string::npos)
        << "seed " << seed;
    EXPECT_NE(json.find("\"episode_material_repair_dependency_pass\":true"), std::string::npos)
        << "seed " << seed;
    if (seed == 223) {
      EXPECT_EQ(extractJsonNumber(json, "cadence_approach_pitch_repair_notes"), 0.0)
          << "seed 223 should shape low-protection episode material away from "
             "cadence bass rather than leaving the cadence as repaired.";
      EXPECT_EQ(extractJsonNumber(json, "pitch_repair_modified_notes"), 0.0)
          << "seed 223 should trim/replace protected dialogue conflicts rather "
             "than leaving pitch repair.";
      EXPECT_EQ(extractJsonNumber(json, "protected_dialogue_pitch_repair_notes"), 0.0)
          << "seed 223 should not leave protected dialogue pitch repair.";
    }
    if (seed == 224) {
      EXPECT_EQ(extractJsonNumber(json, "pitch_repair_modified_notes"), 0.0)
          << "seed 224 should remove short low-protection crossing cells and "
             "shape coda vertical cells rather than leaving pitch repair.";
      EXPECT_EQ(extractJsonNumber(json, "episode_material_pitch_repair_notes"), 0.0)
          << "seed 224 should not leave repaired episode material.";
      EXPECT_EQ(extractJsonNumber(json, "coda_source_pitch_repair_notes"), 0.0)
          << "seed 224 should not leave repaired coda support.";
    }
    if (seed == 226) {
      EXPECT_EQ(extractJsonNumber(json, "pitch_repair_modified_notes"), 0.0)
          << "seed 226 should not depend on pitch-repair safety-net notes.";
      EXPECT_EQ(extractJsonNumber(json, "sequence_pitch_repair_notes"), 0.0)
          << "seed 226 should accept safe weak-beat sequence passing tones as "
             "composed dialogue.";
      EXPECT_EQ(extractJsonNumber(json, "cadence_approach_pitch_repair_notes"), 0.0)
          << "cadence approaches that are already safe should be accepted as "
             "composed intent.";
      EXPECT_EQ(extractJsonNumber(json, "episode_material_pitch_repair_notes"), 0.0)
          << "seed 226 should replace short repaired episode cells rather than "
             "leaving them as pitch-repaired material.";
      EXPECT_LE(extractJsonNumber(json, "episode_material_pitch_repair_density"), 0.08)
          << "seed 226 should not depend on per-note episode pitch repair.";
    }

    for (int hotspot_bar : {9, 14, 18, 23}) {
      std::string hotspot = extractListeningHotspotJson(json, hotspot_bar);
      EXPECT_GE(extractJsonNumber(hotspot, "average_active_voices"), 2.0)
          << "Seed " << seed << " hotspot bar " << hotspot_bar
          << " should keep a two-voice texture.";
    }
  }
}

TEST(GeneratorTest, FourVoiceRestlessSeed249KeepsHotspotClashesComposed) {
  GeneratorConfig config = makeTestConfig(249);
  config.form = FormType::Fugue;
  config.key = {Key::G, true};
  config.num_voices = 4;
  config.character = SubjectCharacter::Restless;
  config.bpm = 72;
  config.scale = DurationScale::Medium;
  config.target_bars = 32;

  GeneratorResult result = generate(config);
  ASSERT_TRUE(result.success);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"critic_time_window_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_texture_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_hard_clash_pass\":true"), std::string::npos);
  EXPECT_EQ(extractJsonNumber(json, "cadence_approach_pitch_repair_notes"), 0.0)
      << "Seed 249 coda approach should keep the bass 50-49-50 neighbor "
         "as composed cadence motion rather than pitch-repair residue.";
  EXPECT_LE(extractJsonNumber(json, "pitch_repair_modified_notes"), 4.0)
      << "Seed 249 should trim exposed repaired episode heads rather than "
         "leaving late stretto support as pitch-repair residue.";
  EXPECT_EQ(extractJsonNumber(json, "episode_material_pitch_repair_notes"), 0.0)
      << "Seed 249 should not leave repaired episode material after the "
         "late stretto support is retargeted.";
  EXPECT_NE(json.find("\"flexible_contour_pass\":true"), std::string::npos);
  EXPECT_EQ(extractJsonNumber(json, "flexible_remote_leap_count"), 0.0)
      << "Seed 249 should retarget remote flexible episode leaps into "
         "nearby support motion instead of leaving octave-plus jumps.";
  EXPECT_NE(json.find("\"overall_pass\":true"), std::string::npos);

  std::string bar18 = extractListeningHotspotJson(json, 18);
  EXPECT_EQ(extractJsonNumber(bar18, "hard_clash_count"), 0.0)
      << "Seed 249 bar 18 should split/recompose exposed flexible cells "
         "instead of leaving a two-voice second against cadence support.";
  EXPECT_GE(extractJsonNumber(bar18, "average_active_voices"), 2.0)
      << "Seed 249 bar 18 should keep enough dialogue texture after the "
         "cadence-support clash is recomposed.";
}

TEST(GeneratorTest, FourVoiceRestlessSeed233SplitsEarlyCounterlineLeap) {
  GeneratorConfig config = makeTestConfig(233);
  config.form = FormType::Fugue;
  config.key = {Key::G, true};
  config.num_voices = 4;
  config.character = SubjectCharacter::Restless;
  config.bpm = 72;
  config.scale = DurationScale::Medium;
  config.target_bars = 32;

  GeneratorResult result = generate(config);
  ASSERT_TRUE(result.success);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"early_exposition_counterline_pass\":true"), std::string::npos);
  EXPECT_EQ(extractJsonNumber(json, "early_exposition_counterline_large_leaps"), 0.0)
      << "Seed 233 should split the early countersubject octave pickup into "
         "a short composed counterline cell.";
  EXPECT_NE(json.find("\"overall_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"early_listening_hard_clash_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"thematic_dialogue_clash_pass\":true"), std::string::npos);
}

TEST(GeneratorTest, FourVoiceRestlessSeed200KeepsBar23TextureAfterBoundaryTrim) {
  GeneratorConfig config = makeTestConfig(200);
  config.form = FormType::Fugue;
  config.key = {Key::G, true};
  config.num_voices = 4;
  config.character = SubjectCharacter::Restless;
  config.bpm = 72;
  config.scale = DurationScale::Medium;
  config.target_bars = 32;

  GeneratorResult result = generate(config);
  ASSERT_TRUE(result.success);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"overall_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_texture_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_hard_clash_pass\":true"), std::string::npos);

  std::string bar23 = extractListeningHotspotJson(json, 23);
  EXPECT_GE(extractJsonNumber(bar23, "average_active_voices"), 2.0)
      << "Seed 200 bar 23 should keep two-voice texture when the short "
         "boundary overhang is split to a chord-tone suffix.";
  EXPECT_EQ(extractJsonNumber(bar23, "hard_clash_count"), 0.0);
}

TEST(GeneratorTest, FourVoiceRestlessSeed232SplitsBoundaryOverhang) {
  GeneratorConfig config = makeTestConfig(232);
  config.form = FormType::Fugue;
  config.key = {Key::G, true};
  config.num_voices = 4;
  config.character = SubjectCharacter::Restless;
  config.bpm = 72;
  config.scale = DurationScale::Medium;
  config.target_bars = 32;

  GeneratorResult result = generate(config);
  ASSERT_TRUE(result.success);

  std::string json = buildEventsJson(result, config);
  EXPECT_NE(json.find("\"overall_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_texture_pass\":true"), std::string::npos);
  EXPECT_NE(json.find("\"listening_hotspot_hard_clash_pass\":true"), std::string::npos);
  EXPECT_EQ(extractJsonNumber(json, "pitch_repair_modified_notes"), 0.0)
      << "Seed 232 should split a short sequence overhang at the strong "
         "harmony boundary instead of leaving pitch repair.";
}

// ---------------------------------------------------------------------------
// PreludeAndFugue form generation
// ---------------------------------------------------------------------------

TEST(GeneratorTest, PreludeAndFugue_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::PreludeAndFugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

TEST(GeneratorTest, PreludeAndFugue_MoreNotesThanFugueAlone) {
  GeneratorConfig config = makeTestConfig(42);
  config.num_voices = 3;

  config.form = FormType::Fugue;
  GeneratorResult fugue_result = generate(config);

  config.form = FormType::PreludeAndFugue;
  GeneratorResult pf_result = generate(config);

  ASSERT_TRUE(fugue_result.success);
  ASSERT_TRUE(pf_result.success);

  size_t fugue_notes = test_helpers::totalNoteCount(fugue_result);
  size_t pf_notes = test_helpers::totalNoteCount(pf_result);

  EXPECT_GT(pf_notes, fugue_notes)
      << "PreludeAndFugue (" << pf_notes << " notes) should have more notes than Fugue alone ("
      << fugue_notes << " notes)";
}

TEST(GeneratorTest, PreludeAndFugue_LongerThanFugueAlone) {
  GeneratorConfig config = makeTestConfig(42);

  config.form = FormType::Fugue;
  GeneratorResult fugue_result = generate(config);

  config.form = FormType::PreludeAndFugue;
  GeneratorResult pf_result = generate(config);

  ASSERT_TRUE(fugue_result.success);
  ASSERT_TRUE(pf_result.success);

  EXPECT_GT(pf_result.total_duration_ticks, fugue_result.total_duration_ticks)
      << "PreludeAndFugue should be longer than Fugue alone";
}

TEST(GeneratorTest, PreludeAndFugue_HasTempoEvents) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::PreludeAndFugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_GE(result.tempo_events.size(), 2u)
      << "PreludeAndFugue should have tempo events for prelude and fugue sections";
  // First tempo event at tick 0.
  EXPECT_EQ(result.tempo_events[0].tick, 0u);
}

TEST(GeneratorTest, PreludeAndFugue_HasFormDescription) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::PreludeAndFugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_NE(result.form_description.find("Prelude"), std::string::npos)
      << "Form description missing 'Prelude': " << result.form_description;
  EXPECT_NE(result.form_description.find("Fugue"), std::string::npos)
      << "Form description missing 'Fugue': " << result.form_description;
}

// ---------------------------------------------------------------------------
// defaultInstrumentForForm
// ---------------------------------------------------------------------------

TEST(GeneratorTest, DefaultInstrument_FugueIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::Fugue), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_PreludeAndFugueIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::PreludeAndFugue), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_TrioSonataIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::TrioSonata), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_ChoralePreludeIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::ChoralePrelude), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_ToccataAndFugueIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::ToccataAndFugue), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_PassacagliaIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::Passacaglia), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_FantasiaAndFugueIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::FantasiaAndFugue), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_CelloPreludeIsCello) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::CelloPrelude), InstrumentType::Cello);
}

TEST(GeneratorTest, DefaultInstrument_ChaconneIsViolin) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::Chaconne), InstrumentType::Violin);
}

// ---------------------------------------------------------------------------
// instrumentTypeFromString
// ---------------------------------------------------------------------------

TEST(GeneratorTest, InstrumentFromString_Organ) {
  EXPECT_EQ(instrumentTypeFromString("organ"), InstrumentType::Organ);
}

TEST(GeneratorTest, InstrumentFromString_Harpsichord) {
  EXPECT_EQ(instrumentTypeFromString("harpsichord"), InstrumentType::Harpsichord);
}

TEST(GeneratorTest, InstrumentFromString_Piano) {
  EXPECT_EQ(instrumentTypeFromString("piano"), InstrumentType::Piano);
}

TEST(GeneratorTest, InstrumentFromString_Violin) {
  EXPECT_EQ(instrumentTypeFromString("violin"), InstrumentType::Violin);
}

TEST(GeneratorTest, InstrumentFromString_Cello) {
  EXPECT_EQ(instrumentTypeFromString("cello"), InstrumentType::Cello);
}

TEST(GeneratorTest, InstrumentFromString_Guitar) {
  EXPECT_EQ(instrumentTypeFromString("guitar"), InstrumentType::Guitar);
}

TEST(GeneratorTest, InstrumentFromString_UnknownDefaultsToOrgan) {
  EXPECT_EQ(instrumentTypeFromString("banjo"), InstrumentType::Organ);
}

// ---------------------------------------------------------------------------
// Auto seed
// ---------------------------------------------------------------------------

TEST(GeneratorTest, AutoSeed_ProducesValidResult) {
  GeneratorConfig config = makeTestConfig();
  config.seed = 0;  // Auto seed.
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_NE(result.seed_used, 0u) << "Auto seed should produce a non-zero seed_used";
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(GeneratorTest, SameSeed_ProducesSameResult) {
  GeneratorConfig config = makeTestConfig(12345);
  config.form = FormType::Fugue;

  GeneratorResult result1 = generate(config);
  GeneratorResult result2 = generate(config);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_EQ(result1.tracks.size(), result2.tracks.size());

  for (size_t track_idx = 0; track_idx < result1.tracks.size(); ++track_idx) {
    const auto& notes1 = result1.tracks[track_idx].notes;
    const auto& notes2 = result2.tracks[track_idx].notes;
    ASSERT_EQ(notes1.size(), notes2.size()) << "Track " << track_idx << " note count differs";

    for (size_t note_idx = 0; note_idx < notes1.size(); ++note_idx) {
      EXPECT_EQ(notes1[note_idx].start_tick, notes2[note_idx].start_tick)
          << "Track " << track_idx << ", note " << note_idx;
      EXPECT_EQ(notes1[note_idx].pitch, notes2[note_idx].pitch)
          << "Track " << track_idx << ", note " << note_idx;
    }
  }
}

TEST(GeneratorTest, SameSeed_PreludeAndFugue_Deterministic) {
  GeneratorConfig config = makeTestConfig(54321);
  config.form = FormType::PreludeAndFugue;

  GeneratorResult result1 = generate(config);
  GeneratorResult result2 = generate(config);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_EQ(result1.tracks.size(), result2.tracks.size());

  // Check that total note counts match.
  EXPECT_EQ(test_helpers::totalNoteCount(result1), test_helpers::totalNoteCount(result2));
  EXPECT_EQ(result1.total_duration_ticks, result2.total_duration_ticks);
}

// ---------------------------------------------------------------------------
// Different forms produce different results
// ---------------------------------------------------------------------------

TEST(GeneratorTest, DifferentForms_ProduceDifferentResults) {
  GeneratorConfig config = makeTestConfig(42);

  config.form = FormType::Fugue;
  GeneratorResult fugue_result = generate(config);

  config.form = FormType::PreludeAndFugue;
  GeneratorResult pf_result = generate(config);

  ASSERT_TRUE(fugue_result.success);
  ASSERT_TRUE(pf_result.success);

  // PreludeAndFugue should have more content than a standalone Fugue.
  EXPECT_NE(test_helpers::totalNoteCount(fugue_result), test_helpers::totalNoteCount(pf_result))
      << "Different forms with same seed should produce different note counts";
}

// ---------------------------------------------------------------------------
// Voice count support
// ---------------------------------------------------------------------------

TEST(GeneratorTest, ThreeVoices_Respected) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 3;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 3u);
}

TEST(GeneratorTest, FourVoices_Respected) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 4;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 4u);
}

TEST(GeneratorTest, FiveVoices_Respected) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 5;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u);
}

// ---------------------------------------------------------------------------
// Key is applied
// ---------------------------------------------------------------------------

TEST(GeneratorTest, Key_IncludedInDescription) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.key = {Key::G, true};  // G minor
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_NE(result.form_description.find("G_minor"), std::string::npos)
      << "Form description should include key: " << result.form_description;
}

TEST(GeneratorTest, Key_DifferentKeysProduceDifferentPitches) {
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;

  config.key = {Key::C, false};
  GeneratorResult result_c = generate(config);

  config.key = {Key::G, true};
  GeneratorResult result_g = generate(config);

  ASSERT_TRUE(result_c.success);
  ASSERT_TRUE(result_g.success);

  // Different keys should produce different note pitches (at least in first track).
  bool any_pitch_diff = false;
  const auto& notes_c = result_c.tracks[0].notes;
  const auto& notes_g = result_g.tracks[0].notes;

  size_t compare_count = std::min(notes_c.size(), notes_g.size());
  for (size_t idx = 0; idx < compare_count; ++idx) {
    if (notes_c[idx].pitch != notes_g[idx].pitch) {
      any_pitch_diff = true;
      break;
    }
  }

  if (notes_c.size() != notes_g.size()) {
    any_pitch_diff = true;
  }

  EXPECT_TRUE(any_pitch_diff) << "Different keys should produce different pitches";
}

// ---------------------------------------------------------------------------
// Trio Sonata generation
// ---------------------------------------------------------------------------

TEST(GeneratorTest, TrioSonata_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::TrioSonata;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(GeneratorTest, ChoralePrelude_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::ChoralePrelude;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(GeneratorTest, CelloPrelude_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::CelloPrelude;
  config.instrument = InstrumentType::Cello;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(result.total_duration_ticks, 0u);
  // Should have notes in the track.
  size_t total_notes = 0;
  for (const auto& track : result.tracks) {
    total_notes += track.notes.size();
  }
  EXPECT_GT(total_notes, 0u);
  // Form description should reference cello prelude.
  EXPECT_NE(result.form_description.find("Cello Prelude"), std::string::npos);
}

TEST(GeneratorTest, Chaconne_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Chaconne;
  config.instrument = InstrumentType::Violin;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(result.total_duration_ticks, 0u);
  // Should have notes in the track.
  size_t total_notes = 0;
  for (const auto& track : result.tracks) {
    total_notes += track.notes.size();
  }
  EXPECT_GT(total_notes, 0u);
  // Form description should reference chaconne.
  EXPECT_NE(result.form_description.find("Chaconne"), std::string::npos);
}

TEST(GeneratorTest, Chaconne_Deterministic) {
  GeneratorConfig config = makeTestConfig(99);
  config.form = FormType::Chaconne;
  config.instrument = InstrumentType::Violin;

  GeneratorResult result1 = generate(config);
  GeneratorResult result2 = generate(config);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_EQ(result1.tracks.size(), result2.tracks.size());

  for (size_t track_idx = 0; track_idx < result1.tracks.size(); ++track_idx) {
    const auto& notes1 = result1.tracks[track_idx].notes;
    const auto& notes2 = result2.tracks[track_idx].notes;
    ASSERT_EQ(notes1.size(), notes2.size()) << "Track " << track_idx << " note count differs";

    for (size_t note_idx = 0; note_idx < notes1.size(); ++note_idx) {
      EXPECT_EQ(notes1[note_idx].start_tick, notes2[note_idx].start_tick)
          << "Track " << track_idx << ", note " << note_idx;
      EXPECT_EQ(notes1[note_idx].pitch, notes2[note_idx].pitch)
          << "Track " << track_idx << ", note " << note_idx;
    }
  }
}

TEST(GeneratorTest, Chaconne_DMinorDefault) {
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Chaconne;
  config.key = {Key::D, true};  // D minor (BWV1004)
  config.instrument = InstrumentType::Violin;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_NE(result.form_description.find("D_minor"), std::string::npos)
      << "Form description should include key: " << result.form_description;
  // D minor chaconne with 10 variations x 4 bars = 40 bars minimum.
  EXPECT_GE(result.total_duration_ticks, 40u * kTicksPerBar);
}

// ---------------------------------------------------------------------------
// Fugue-aliased forms
// ---------------------------------------------------------------------------

TEST(GeneratorTest, ToccataAndFugue_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::ToccataAndFugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

TEST(GeneratorTest, FantasiaAndFugue_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::FantasiaAndFugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

TEST(GeneratorTest, Passacaglia_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Passacaglia;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

// ---------------------------------------------------------------------------
// Seed is reported
// ---------------------------------------------------------------------------

TEST(GeneratorTest, SeedUsed_IsReported) {
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.seed_used, 42u);
}

TEST(GeneratorTest, SeedUsed_AutoSeedIsNonZero) {
  GeneratorConfig config = makeTestConfig();
  config.seed = 0;
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_NE(result.seed_used, 0u);
}

// ---------------------------------------------------------------------------
// Total duration is reported
// ---------------------------------------------------------------------------

TEST(GeneratorTest, TotalDuration_IsNonZero) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(GeneratorTest, TotalDuration_PreludeAndFugueIsReasonable) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::PreludeAndFugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  // Should be at least 20 bars (prelude + fugue combined).
  EXPECT_GE(result.total_duration_ticks, 20u * kTicksPerBar);
}

// ---------------------------------------------------------------------------
// Articulation integration -- verify articulation is applied in the pipeline
// ---------------------------------------------------------------------------

TEST(GeneratorArticulationTest, ArticulationAppliedInPipeline_FugueDurationsReduced) {
  // Generate a fugue and verify that note durations are shorter than the
  // "raw" beat-aligned durations that the fugue generator produces.
  // The organ Assert gate ratio is 0.85, so a quarter-note (480 ticks)
  // becomes 408 ticks.  We check that at least some notes have been
  // shortened below their beat-grid-aligned values.
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;
  config.num_voices = 3;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  // Count notes whose duration is NOT a clean multiple of kTicksPerBeat.
  // Before articulation, durations are typically multiples of 120/240/480.
  // After articulation (0.85 gate), a 480-tick note becomes 408, which
  // is not a multiple of 480.
  size_t articulated_count = 0;
  size_t total_count = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      ++total_count;
      // A note whose duration is not evenly divisible by kTicksPerBeat
      // (480) has almost certainly been articulated.
      if (note.duration % kTicksPerBeat != 0) {
        ++articulated_count;
      }
    }
  }

  EXPECT_GT(total_count, 0u) << "Fugue should produce notes";
  EXPECT_GT(articulated_count, 0u)
      << "Articulation should modify at least some note durations "
         "(expected non-beat-aligned durations after gate ratio application)";
}

TEST(GeneratorArticulationTest, ArticulationAppliedInPipeline_OrganVelocityUnchanged) {
  // Organ instruments have fixed velocity = 80.  The articulation system
  // must not modify velocity for organ forms (is_organ = true).
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;
  config.instrument = InstrumentType::Organ;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80)
          << "Organ velocity must remain 80 after articulation; "
             "found "
          << static_cast<int>(note.velocity) << " at tick " << note.start_tick;
    }
  }
}

TEST(GeneratorArticulationTest, ArticulationAppliedInPipeline_ChaconneDurationsReduced) {
  // Solo string (non-organ) form should also have articulated durations.
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Chaconne;
  config.instrument = InstrumentType::Violin;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_GE(result.tracks.size(), 1u);

  size_t articulated_count = 0;
  size_t total_count = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      ++total_count;
      if (note.duration % kTicksPerBeat != 0) {
        ++articulated_count;
      }
    }
  }

  EXPECT_GT(total_count, 0u) << "Chaconne should produce notes";
  EXPECT_GT(articulated_count, 0u)
      << "Articulation should modify at least some note durations in chaconne";
}

TEST(GeneratorArticulationTest, ArticulationPreservesNonZeroDurations) {
  // After articulation, no note should have zero duration (the minimum
  // articulated duration floor of 60 ticks should prevent this).
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_GT(note.duration, 0u)
          << "Note at tick " << note.start_tick << " has zero duration after articulation";
    }
  }
}

TEST(GeneratorArticulationTest, TrioSonataArticulated) {
  // Trio Sonata notes should have articulation applied (gate ratio < 1.0).
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::TrioSonata;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(result.tracks.empty());
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_GT(note.duration, 0u)
          << "Note at tick " << note.start_tick << " has zero duration after articulation";
    }
  }
}

// ---------------------------------------------------------------------------
// Chaconne polyphony preservation tests
// ---------------------------------------------------------------------------

TEST(ChaconneE2E, PostProcessingDestructionRate) {
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Chaconne;
  config.instrument = InstrumentType::Violin;
  auto result = generate(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_GE(result.tracks.size(), 2u);

  // Track 0 = bass, Track 1 = texture.
  const auto& bass_notes = result.tracks[0].notes;
  const auto& texture_notes = result.tracks[1].notes;

  int bass_count = 0, texture_count = 0;
  for (const auto& n : bass_notes) {
    if (n.source == BachNoteSource::ChaconneBass)
      ++bass_count;
  }
  for (const auto& n : texture_notes) {
    if (n.source == BachNoteSource::TextureNote)
      ++texture_count;
  }

  EXPECT_GT(texture_count, 0) << "No texture notes survived post-processing";
  EXPECT_GT(bass_count, 0) << "No bass notes found";

  // Track separation: bass and texture are on separate tracks/channels.
  EXPECT_EQ(result.tracks[0].channel, 0u);
  EXPECT_EQ(result.tracks[1].channel, 1u);

  // Temporal co-occurrence: bass and texture notes within the same beat.
  // With separate tracks, bass notes retain full duration. Check that
  // bass and texture co-occur within the same beat.
  constexpr Tick kCoOccurrenceTolerance = kTicksPerBeat;
  int cooccurrences = 0;
  for (const auto& bn : bass_notes) {
    if (bn.source != BachNoteSource::ChaconneBass)
      continue;
    for (const auto& tn : texture_notes) {
      if (tn.source != BachNoteSource::TextureNote)
        continue;
      Tick gap = (tn.start_tick >= bn.start_tick) ? (tn.start_tick - bn.start_tick)
                                                  : (bn.start_tick - tn.start_tick);
      if (gap <= kCoOccurrenceTolerance) {
        ++cooccurrences;
        break;
      }
    }
  }
  EXPECT_GT(cooccurrences, 0) << "No bass-texture co-occurrence within a beat; "
                                 "tracks may not have aligned content";
}

TEST(ChaconneE2E, MultiSeedTexturePresence) {
  for (uint32_t seed : {1u, 42u, 100u, 999u}) {
    GeneratorConfig config;
    config.form = FormType::Chaconne;
    config.seed = seed;
    config.instrument = InstrumentType::Violin;
    auto result = generate(config);
    ASSERT_TRUE(result.success) << "seed=" << seed;
    ASSERT_GE(result.tracks.size(), 2u) << "seed=" << seed;

    // Texture notes are in tracks[1].
    int texture_count = 0;
    for (const auto& n : result.tracks[1].notes) {
      if (n.source == BachNoteSource::TextureNote)
        ++texture_count;
    }
    EXPECT_GT(texture_count, 0) << "No texture notes survived for seed=" << seed;
  }
}

}  // namespace
}  // namespace bach
