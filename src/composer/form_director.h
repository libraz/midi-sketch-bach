#ifndef BACH_COMPOSER_FORM_DIRECTOR_H
#define BACH_COMPOSER_FORM_DIRECTOR_H

#include <cstdint>

#include "composer/harness_fixture.h"
#include "core/basic_types.h"

namespace bach::composer {

// Resolved request after snap/clamp -- exactly what buildFormFixture executes.
// A request is built by callers (CLI, harness) from raw user intent; the
// director never mutates it in place, it consumes it.
struct ComposeRequest {
  FormType form = FormType::Fugue;
  bool is_minor = false;
  SubjectCharacter character = SubjectCharacter::Severe;
  std::uint16_t target_bars = 0;  // 0 = use the form's natural length
  std::uint32_t seed = 0;
  // Opt-in: route accompanimental secondary-voice spans (TrioVoiceCarrier)
  // through the scored free-counterpoint candidate search instead of replaying
  // their designed material verbatim. Off by default so every form's output
  // stays the deterministic carrier-assembly result; on, the inner voice is
  // generated per-span by CandidateSearch (corpus-Gaussian + chord-tone
  // scoring). A measurement knob, not a quality default -- the carrier path
  // wins on score unless explicitly shown otherwise.
  bool enable_free_counterpoint = false;
};

// Static per-form layout limits and meter. One immutable record per FormType,
// returned by reference from formSpec(). num_voices is fixed per form; bar
// counts feed resolveBars(); ts_* drive the HarnessFixture meter field.
struct FormSpec {
  std::uint8_t num_voices;      // fixed voice count for the form
  std::uint16_t natural_bars;   // length used when target_bars == 0
  std::uint16_t min_bars;       // lower clamp bound
  std::uint16_t max_bars;       // upper clamp bound (global cap 128)
  std::uint16_t snap_bars;      // length granularity (ground period etc.)
  std::uint8_t ts_numerator;    // 3 for chaconne/passacaglia, else 4
  std::uint8_t ts_denominator;  // always 4 for now
};

// Outcome of buildFormFixture. The composer reports failures via return codes,
// not exceptions, so the director mirrors that: a non-Ok status means the
// HarnessFixture out-parameter was left untouched.
enum class FormDirectorStatus : std::uint8_t {
  Ok = 0,
  // The requested SubjectCharacter is forbidden for the requested FormType
  // (see isFormCharacterCompatible). A CONFIG_FAIL in project taxonomy terms.
  IncompatibleCharacter = 1,
  // The requested FormType is not a recognised enumerator.
  UnknownForm = 2,
};

/**
 * @brief Look up the immutable layout spec for a form.
 * @param form The form whose spec is requested.
 * @return Reference to the static FormSpec for the form.
 */
const FormSpec& formSpec(FormType form);

/**
 * @brief Test whether a character is admissible for a form.
 * @param form The requested form.
 * @param character The requested subject character.
 * @return False for the forbidden pairs (Playful/Restless x ChoralePrelude,
 *         Noble x ToccataAndFugue); true otherwise.
 */
bool isFormCharacterCompatible(FormType form, SubjectCharacter character);

/**
 * @brief Resolve a final bar count for a form from a length request.
 * @param form The requested form (selects natural/min/max/snap).
 * @param scale Length scale applied when target_bars == 0 (Short = natural,
 *              Medium ~2x, Long ~3x, Full ~4x).
 * @param target_bars Explicit length override; when > 0 it replaces the scale
 *                    entirely. Snapping and clamping still apply.
 * @return The resolved bar count: scaled (or overridden), rounded to the
 *         nearest snap_bars multiple, then clamped to [min_bars, max_bars].
 */
std::uint16_t resolveBars(FormType form, DurationScale scale, std::uint16_t target_bars);

/**
 * @brief Build the harness fixture for a resolved compose request.
 * @param req The resolved request to execute.
 * @param out Receives the built fixture on success; left untouched otherwise.
 * @return FormDirectorStatus::Ok on success, or a diagnostic status on a
 *         configuration violation (incompatible character / unknown form).
 * @note Per-form layout is currently delegated to the existing phase builders
 *       via internal stubs; dedicated builders replace those over time. The
 *       fixture's meter field is set from the form's FormSpec.
 */
FormDirectorStatus buildFormFixture(const ComposeRequest& req, HarnessFixture* out);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_FORM_DIRECTOR_H
