"""Form-specific validation profiles.

Each musical form has different rules and thresholds for validation.
For example, counterpoint rules are irrelevant for solo string pieces,
and passacaglia has a ground bass repetition expectation.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Optional, Tuple

from .rules.base import Severity


@dataclass(frozen=True)
class FormProfile:
    """Validation profile for a specific musical form."""

    form_name: str
    style_family: str  # "organ" | "keyboard" | "solo_string_flow" | "solo_string_arch"

    # Rule application flags
    counterpoint_enabled: bool = True
    independence_enabled: bool = True
    exposition_required: bool = False
    cadence_validation: bool = True

    # Threshold overrides
    hidden_perfect_severity: Severity = Severity.WARNING
    voice_crossing_base_severity: Severity = Severity.ERROR
    min_stepwise_ratio: float = 0.4
    max_leap_semitones: int = 13
    voice_spacing_max: int = 12

    # Instrument-specific
    instrument_range: Optional[Tuple[int, int]] = None  # MIDI pitch (min, max)
    expected_voices: Optional[Tuple[int, int]] = None  # (min, max) range
    ground_bass_expected: bool = False
    ground_bass_period: Optional[int] = None  # bar-count repetition period

    # Keyboard-style parallel policy: relax parallel perfect severity for inner voices
    # and only flag consecutive outer-voice parallels in same direction as ERROR.
    keyboard_parallel_policy: bool = False

    # Sources where violations should be severity-downgraded (e.g., toccata sections).
    relaxed_sources: frozenset = frozenset()

    # Leap resolution: minimum leap interval (semitones) that triggers the
    # "leap must resolve by step" rule.  Default 5 (perfect 4th).
    # Higher values relax the rule for arpeggio-heavy forms.
    leap_resolution_threshold: int = 5


# ---------------------------------------------------------------------------
# Profile registry
# ---------------------------------------------------------------------------

_PROFILES: Dict[str, FormProfile] = {
    "fugue": FormProfile(
        form_name="fugue",
        style_family="organ",
        counterpoint_enabled=True,
        independence_enabled=True,
        exposition_required=True,
        cadence_validation=True,
        expected_voices=(2, 5),
    ),
    "prelude_and_fugue": FormProfile(
        form_name="prelude_and_fugue",
        style_family="organ",
        counterpoint_enabled=True,
        independence_enabled=True,
        exposition_required=True,
        cadence_validation=True,
        expected_voices=(2, 5),
        leap_resolution_threshold=7,
    ),
    "toccata_and_fugue": FormProfile(
        form_name="toccata_and_fugue",
        style_family="organ",
        counterpoint_enabled=True,
        independence_enabled=True,
        exposition_required=True,
        cadence_validation=True,
        expected_voices=(2, 5),
        relaxed_sources=frozenset({"free_counterpoint", "post_process"}),
    ),
    "fantasia_and_fugue": FormProfile(
        form_name="fantasia_and_fugue",
        style_family="organ",
        counterpoint_enabled=True,
        independence_enabled=True,
        exposition_required=True,
        cadence_validation=True,
        expected_voices=(2, 5),
    ),
    "trio_sonata": FormProfile(
        form_name="trio_sonata",
        style_family="organ",
        counterpoint_enabled=True,
        independence_enabled=True,
        exposition_required=False,
        cadence_validation=True,
        hidden_perfect_severity=Severity.INFO,
        expected_voices=(3, 3),
    ),
    "chorale_prelude": FormProfile(
        form_name="chorale_prelude",
        style_family="organ",
        counterpoint_enabled=True,
        independence_enabled=True,
        exposition_required=False,
        cadence_validation=True,
        expected_voices=(2, 4),
    ),
    "goldberg_variations": FormProfile(
        form_name="goldberg_variations",
        style_family="keyboard",
        counterpoint_enabled=True,
        independence_enabled=True,
        exposition_required=False,
        cadence_validation=True,
        hidden_perfect_severity=Severity.INFO,
        voice_crossing_base_severity=Severity.WARNING,
        max_leap_semitones=48,
        voice_spacing_max=24,
        keyboard_parallel_policy=True,
        instrument_range=(29, 89),
        expected_voices=(1, 5),
    ),
    "passacaglia": FormProfile(
        form_name="passacaglia",
        style_family="organ",
        counterpoint_enabled=True,
        independence_enabled=True,
        exposition_required=False,
        cadence_validation=True,
        min_stepwise_ratio=0.2,
        expected_voices=(2, 5),
        ground_bass_expected=True,
        ground_bass_period=8,
    ),
    "cello_prelude": FormProfile(
        form_name="cello_prelude",
        style_family="solo_string_flow",
        counterpoint_enabled=False,
        independence_enabled=False,
        exposition_required=False,
        cadence_validation=False,
        instrument_range=(36, 81),
        expected_voices=(1, 1),
    ),
    "chaconne": FormProfile(
        form_name="chaconne",
        style_family="solo_string_arch",
        counterpoint_enabled=False,
        independence_enabled=False,
        exposition_required=False,
        cadence_validation=False,
        instrument_range=(55, 96),
        expected_voices=(1, 1),
        ground_bass_expected=True,
        ground_bass_period=4,
    ),
}

# Default profile for unknown forms (conservative: all rules enabled).
_DEFAULT_PROFILE = FormProfile(
    form_name="unknown",
    style_family="organ",
    counterpoint_enabled=True,
    independence_enabled=True,
    exposition_required=False,
    cadence_validation=True,
)


def get_form_profile(form_name: Optional[str]) -> FormProfile:
    """Look up a FormProfile by form name. Returns default for unknown forms."""
    if not form_name:
        return _DEFAULT_PROFILE
    return _PROFILES.get(form_name, _DEFAULT_PROFILE)


def all_form_names() -> list[str]:
    """Return all registered form names."""
    return list(_PROFILES.keys())
