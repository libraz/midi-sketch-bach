"""Single-file validation orchestrator."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Set, Type, Union

from .form_profile import FormProfile, get_form_profile
from .model import Score
from .loaders import load_json, load_midi
from .rules.base import Category, Rule, RuleResult, Severity
from .rules.counterpoint import ALL_COUNTERPOINT_RULES
from .rules.melodic import ALL_MELODIC_RULES
from .rules.overlap import ALL_OVERLAP_RULES
from .rules.dissonance import ALL_DISSONANCE_RULES
from .rules.independence import ALL_INDEPENDENCE_RULES
from .rules.structure import ALL_STRUCTURE_RULES
from .rules.cadence import ALL_CADENCE_RULES
from .rules.arpeggio import ALL_ARPEGGIO_RULES

ALL_RULE_CLASSES: List[Type] = (
    ALL_COUNTERPOINT_RULES
    + ALL_MELODIC_RULES
    + ALL_OVERLAP_RULES
    + ALL_DISSONANCE_RULES
    + ALL_INDEPENDENCE_RULES
    + ALL_STRUCTURE_RULES
    + ALL_CADENCE_RULES
    + ALL_ARPEGGIO_RULES
)

CATEGORY_MAP: Dict[str, List[Type]] = {
    "counterpoint": ALL_COUNTERPOINT_RULES,
    "melodic": ALL_MELODIC_RULES,
    "overlap": ALL_OVERLAP_RULES,
    "dissonance": ALL_DISSONANCE_RULES,
    "independence": ALL_INDEPENDENCE_RULES,
    "structure": ALL_STRUCTURE_RULES,
    "cadence": ALL_CADENCE_RULES,
    "arpeggio": ALL_ARPEGGIO_RULES,
}


def load_score(path: Union[str, Path]) -> Score:
    """Auto-detect format and load a Score."""
    p = Path(path)
    if p.suffix.lower() in (".mid", ".midi"):
        return load_midi(p)
    return load_json(p)


def get_rules(
    categories: Optional[Set[str]] = None,
) -> List[Rule]:
    """Instantiate rule objects, optionally filtered by category names."""
    classes = []
    if categories:
        for cat in categories:
            classes.extend(CATEGORY_MAP.get(cat, []))
    else:
        classes = list(ALL_RULE_CLASSES)
    return [cls() for cls in classes]


def validate(
    score: Score,
    categories: Optional[Set[str]] = None,
    bar_range: Optional[tuple] = None,
) -> List[RuleResult]:
    """Run all selected rules on a score.

    Args:
        score: The Score to validate.
        categories: Optional set of category names to run (None = all).
        bar_range: Optional (start_bar, end_bar) to filter violations post-check.

    Returns:
        List of RuleResult from each rule.
    """
    profile = get_form_profile(score.form)
    rules = get_rules(categories)
    results = []
    for rule in rules:
        if hasattr(rule, 'applies_to') and not rule.applies_to(profile):
            results.append(RuleResult(
                rule_name=rule.name,
                category=rule.category,
                passed=True,
                info=f"skipped for {profile.form_name}",
            ))
            continue
        if hasattr(rule, 'configure'):
            rule.configure(profile)
        result = rule.check(score)
        if bar_range:
            start_bar, end_bar = bar_range
            result.violations = [
                v for v in result.violations if start_bar <= v.bar <= end_bar
            ]
            result.passed = len(result.violations) == 0
        # Downgrade severity for violations from relaxed sources.
        if profile.relaxed_sources and result.violations:
            for v in result.violations:
                if v.source and v.source.name.lower() in profile.relaxed_sources:
                    v.severity = _downgrade_severity(v.severity)
            # Recompute passed after downgrade.
            result.passed = all(
                v.severity not in (Severity.CRITICAL, Severity.ERROR)
                for v in result.violations
            ) if result.violations else True
        results.append(result)
    return results


def _downgrade_severity(sev: Severity) -> Severity:
    """Downgrade severity by one level."""
    if sev == Severity.CRITICAL:
        return Severity.ERROR
    if sev == Severity.ERROR:
        return Severity.WARNING
    if sev == Severity.WARNING:
        return Severity.INFO
    return Severity.INFO


def overall_passed(results: List[RuleResult]) -> bool:
    """True if all rules passed (no CRITICAL/ERROR violations)."""
    for r in results:
        if r.critical_count > 0 or r.error_count > 0:
            return False
    return True
